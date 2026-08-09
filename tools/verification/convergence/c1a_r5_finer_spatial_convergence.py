#!/usr/bin/env python3
"""Execute and analyse C1A-R5 finer frozen-terrain Regional2D convergence."""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import c1a_convergence as c1a
import c1a_r4_execute_frozen_terrain as r4


STUDY_ID = "regional-spatial-frozen-terrain-v5"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional-spatial-frozen-terrain-v5")
DEFAULT_R4_ROOT = r4.DEFAULT_EXTERNAL_ROOT
DEFAULT_G6_ROOT = r4.DEFAULT_G6_ROOT
DEFAULT_R2D_BINARY = r4.DEFAULT_R2D_BINARY
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
NEW_TARGETS = (500.0, 450.0, 400.0)
FULL_TIME_S = 600.0
LEVEL_COLORS = {"h600": "#4c78a8", "h500": "#f58518", "h450": "#54a24b", "h400": "#9467bd"}
AXIS = "#24292f"
GRID = "#d0d7de"
SERIAL_H300_PRIOR_H = 19.12


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, fieldnames: Sequence[str], rows: Sequence[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def file_sha256(path: Path) -> str:
    return c1a.file_sha256(path)


def command_output(command: Sequence[str]) -> str | None:
    try:
        completed = subprocess.run(list(command), cwd=repo_root(), text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError:
        return None
    return completed.stdout.strip() if completed.returncode == 0 else None


def parse_cpu_slots() -> list[dict[str, Any]]:
    text = command_output(["lscpu", "-p=CPU,CORE,SOCKET,ONLINE,MAXMHZ"]) or ""
    slots: list[dict[str, Any]] = []
    seen: set[tuple[int | None, int]] = set()
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        if len(parts) < 5 or parts[3].lower() == "no":
            continue
        cpu = int(parts[0])
        core = int(parts[1])
        socket = int(parts[2]) if parts[2] else None
        max_mhz = float(parts[4]) if parts[4] else None
        key = (socket, core)
        if key in seen:
            continue
        seen.add(key)
        slots.append({"cpu": cpu, "core": core, "socket": socket, "max_mhz": max_mhz})
    return sorted(slots, key=lambda slot: (-(slot["max_mhz"] or 0.0), slot["socket"] or 0, slot["core"], slot["cpu"]))


def read_proc_metrics(pid: int) -> dict[str, Any] | None:
    proc = Path("/proc") / str(pid)
    try:
        stat = (proc / "stat").read_text(encoding="utf-8")
        status = (proc / "status").read_text(encoding="utf-8")
    except OSError:
        return None
    close = stat.rfind(")")
    fields = stat[close + 2 :].split()
    ticks = os.sysconf(os.sysconf_names["SC_CLK_TCK"])
    cpu_time_s = (int(fields[11]) + int(fields[12])) / ticks
    rss_kb = 0
    for line in status.splitlines():
        if line.startswith("VmRSS:"):
            rss_kb = int(line.split()[1])
            break
    return {"cpu_time_s": cpu_time_s, "rss_kb": rss_kb}


def read_frequency_khz() -> dict[str, float] | None:
    values: list[float] = []
    for path in Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/scaling_cur_freq"):
        try:
            values.append(float(path.read_text(encoding="utf-8").strip()))
        except (OSError, ValueError):
            pass
    if not values:
        return None
    return {"min_khz": min(values), "mean_khz": statistics.fmean(values), "max_khz": max(values)}


def read_temperature_c() -> dict[str, float] | None:
    values: list[float] = []
    for path in Path("/sys/class/thermal").glob("thermal_zone*/temp"):
        try:
            value = float(path.read_text(encoding="utf-8").strip())
        except (OSError, ValueError):
            continue
        if value > 1000.0:
            value /= 1000.0
        if 0.0 < value < 130.0:
            values.append(value)
    if not values:
        return None
    return {"min_c": min(values), "mean_c": statistics.fmean(values), "max_c": max(values)}


def summarise_samples(samples: Sequence[dict[str, float]], keys: Sequence[str]) -> dict[str, Any]:
    if not samples:
        return {"status": "not_available"}
    out: dict[str, Any] = {"sample_count": len(samples)}
    for key in keys:
        values = [float(sample[key]) for sample in samples if key in sample]
        if values:
            out[key] = {"minimum": min(values), "median": statistics.median(values), "maximum": max(values)}
    return out


def parse_runner_stdout(stdout: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for line in stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            parsed[key.strip()] = value.strip()
    return parsed


def solver_args(case_root: Path, mesh_path: Path, run_id: str) -> list[str]:
    rel_mesh = mesh_path.relative_to(case_root)
    return [
        "--case-root",
        str(case_root),
        "--terrain-record",
        "manifests/terrain/conditioned-terrain.json",
        "--mesh",
        rel_mesh.as_posix(),
        "--run-id",
        run_id,
        "--coupling-section",
        r4.SECTION_ID,
        "--coupling-patch",
        r4.COUPLING_PATCH,
        "--pre-event-free-surface-elevation-m",
        "0.0",
        "--dry-depth-m",
        "1e-6",
        "--depth-tolerance-m",
        "1e-8",
        "--normal-tolerance",
        "1e-8",
        "--zero-momentum-tolerance",
        "1e-8",
        "--transfer-absolute-area-tolerance-m2",
        "1e-3",
        "--transfer-relative-area-tolerance",
        "1e-8",
        "--transfer-maximum-contributors",
        "64",
        "--overwrite",
    ]


def prepare_shared_case(external_root: Path, g6_root: Path, r4_root: Path) -> tuple[Path, dict[str, Any]]:
    case_root = external_root / "case"
    if not (case_root / "case.json").is_file():
        r4.copy_required_g6_case_inputs(g6_root / "case", case_root)
        (case_root / "meshes").mkdir(parents=True, exist_ok=True)
        shutil.copy2(r4_root / "case/meshes/kamaishi-regional.geo", case_root / "meshes/kamaishi-regional.geo")
    r4.set_case_final_time(case_root, FULL_TIME_S)
    case_record = r4.fail_closed_invariance(case_root)
    return case_root, case_record


def import_h600_reuse(external_root: Path, case_root: Path, r4_root: Path, case_record: dict[str, Any]) -> dict[str, Any]:
    r4_metrics = read_json(r4_root / "frozen_terrain_v4_metrics.json")
    r4_run_summary = read_json(r4_root / "spatial_run_summary.json")
    r4_h600 = next(run for run in r4_run_summary["runs"] if run.get("status") == "passed" and float(run["requested_solver_target_m"]) == 600.0)
    old_run = Path(r4_h600["output_dir"]).parents[1]
    new_run = case_root / "runs" / r4_h600["run_id"]
    if not new_run.exists():
        shutil.copytree(old_run, new_run)
    old_mesh = Path(r4_h600["mesh"]["mesh_path"])
    new_mesh = case_root / "meshes/r4-h600.msh"
    if not new_mesh.exists():
        shutil.copy2(old_mesh, new_mesh)
    mesh = r4.parse_msh_triangles(new_mesh)
    proof = {
        "status": "passed",
        "source_study": str(r4_root),
        "existing_run_id": r4_h600["run_id"],
        "reused_run_id": r4_h600["run_id"],
        "old_output_dir": r4_h600["output_dir"],
        "new_output_dir": str(new_run / "outputs/regional2d"),
        "old_mesh_sha256": r4_metrics["levels"]["h600"]["mesh_sha256"],
        "new_mesh_sha256": mesh["mesh_sha256"],
        "terrain_sha256": case_record["terrain"]["sha256"],
        "source_sha256": case_record["source"]["sha256"],
        "physical_configuration_sha256": case_record["physical_configuration_sha256"],
        "domain_sha256": case_record["domain_sha256"],
        "coupling_section_sha256": case_record["coupling_section_sha256"],
        "compatible": mesh["mesh_sha256"] == r4_metrics["levels"]["h600"]["mesh_sha256"]
        and case_record["terrain"]["sha256"] == r4.EXPECTED_TERRAIN_SHA256
        and case_record["source"]["sha256"] == r4.EXPECTED_SOURCE_SHA256,
    }
    if not proof["compatible"]:
        raise RuntimeError("h600 reuse compatibility check failed")
    run = {
        "status": "passed",
        "run_id": r4_h600["run_id"],
        "output_dir": str(new_run / "outputs/regional2d"),
        "requested_solver_target_m": 600.0,
        "mesh": mesh,
        "resource_usage": r4_h600["resource_usage"],
        "timestep": r4_h600["timestep"],
        "case_invariance": {
            "terrain_sha256": case_record["terrain"]["sha256"],
            "source_sha256": case_record["source"]["sha256"],
            "physical_configuration_sha256": case_record["physical_configuration_sha256"],
            "domain_sha256": case_record["domain_sha256"],
            "coupling_section_sha256": case_record["coupling_section_sha256"],
        },
        "reuse": proof,
    }
    write_json(external_root / "h600_reuse_proof.json", proof)
    write_json(external_root / "spatial/h600/run.json", run)
    return run


def preflight(args: argparse.Namespace) -> dict[str, Any]:
    external_root = args.external_root
    case_root, case_record = prepare_shared_case(external_root, args.g6_root, args.r4_root)
    h600_run = import_h600_reuse(external_root, case_root, args.r4_root, case_record)
    rows = []
    records = []
    for target in (600.0, *NEW_TARGETS):
        mesh_path = case_root / f"meshes/r4-h{target:g}.msh"
        if not mesh_path.exists():
            mesh_path = r4.generate_mesh(case_root, target, external_root / "logs")
        mesh = r4.parse_msh_triangles(mesh_path)
        record = r4.adapter_level_record(case_record, target, mesh_path)
        record["mesh"] = mesh
        record["actual_characteristic_mesh_size"]["value_m"] = mesh["actual_characteristic_h_m"]
        records.append(record)
        rows.append(
            {
                "level_id": f"h{target:g}",
                "requested_solver_target_m": target,
                "actual_characteristic_h_m": mesh["actual_characteristic_h_m"],
                "active_cells": mesh["active_cells"],
                "total_cells": mesh["total_cells"],
                "domain_area_m2": mesh["domain_area_m2"],
                "minimum_cell_area_m2": mesh["minimum_cell_area_m2"],
                "mean_cell_area_m2": mesh["mean_cell_area_m2"],
                "maximum_cell_area_m2": mesh["maximum_cell_area_m2"],
                "mesh_sha256": mesh["mesh_sha256"],
                "terrain_sha256": case_record["terrain"]["sha256"],
                "source_sha256": case_record["source"]["sha256"],
                "run_status": "reused" if target == 600.0 else "pending",
            }
        )
    c1a.assert_regional_frozen_family_invariance(records)
    summary = {
        "schema": {"name": "tsunami.c1a_r5_mesh_preflight", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "case_root": str(case_root),
        "case_record": case_record,
        "h600_reuse_run": h600_run["run_id"],
        "frozen_family_invariance": "passed",
        "candidate_meshes": rows,
    }
    write_json(external_root / "mesh_preflight.json", summary)
    write_csv(external_root / "mesh_preflight.csv", list(rows[0]), rows)
    return summary


def completed_run_valid(path: Path) -> bool:
    if not path.is_file():
        return False
    run = read_json(path)
    if run.get("status") != "passed":
        return False
    output = Path(run["output_dir"])
    diagnostics = output / "diagnostics.csv"
    coupling = output / "coupling" / r4.SECTION_ID / "samples.csv"
    if not diagnostics.is_file() or not coupling.is_file():
        return False
    try:
        stats = r4.timestep_stats_from_diagnostics(output)
    except Exception:
        return False
    return abs(float(stats.get("final_diagnostic_time_s") or 0.0) - FULL_TIME_S) < 1.0e-9


def run_full(args: argparse.Namespace) -> dict[str, Any]:
    summary = preflight(args)
    case_root = Path(summary["case_root"])
    slots = parse_cpu_slots()
    targets = list(args.targets)
    if len(slots) < len(targets):
        raise RuntimeError(f"not enough physical CPU slots: have {len(slots)}, need {len(targets)}")
    pending = [target for target in targets if not completed_run_valid(args.external_root / "spatial" / f"h{target:g}" / "run.json")]
    first_launch = utc_now()
    processes: list[dict[str, Any]] = []
    for index, target in enumerate(pending):
        level_id = f"h{target:g}"
        run_id = f"r5-full-{level_id}"
        mesh_path = case_root / f"meshes/r4-{level_id}.msh"
        command = [str(args.r2d_binary), *solver_args(case_root, mesh_path, run_id)]
        assigned_cpu = slots[index]["cpu"]
        command = ["taskset", "-c", str(assigned_cpu), *command]
        env = {**os.environ, "OMP_NUM_THREADS": "1"}
        log_path = args.external_root / "logs" / f"{run_id}.log"
        log_path.parent.mkdir(parents=True, exist_ok=True)
        launched_mono = time.monotonic()
        proc = subprocess.Popen(command, cwd=repo_root(), env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        processes.append(
            {
                "target": target,
                "level_id": level_id,
                "run_id": run_id,
                "process": proc,
                "command": command,
                "assigned_cpu": assigned_cpu,
                "launch_timestamp_utc": utc_now(),
                "launch_monotonic_s": launched_mono,
                "completion_timestamp_utc": None,
                "completion_monotonic_s": None,
                "log_path": log_path,
                "peak_rss_kb": 0,
                "first_metrics": None,
                "last_metrics": None,
            }
        )
    freq_samples: list[dict[str, float]] = []
    temp_samples: list[dict[str, float]] = []
    monitor_rows: list[dict[str, Any]] = []
    while any(item["process"].poll() is None for item in processes):
        aggregate_rss = 0
        alive = []
        for item in processes:
            if item["process"].poll() is not None:
                if item["completion_monotonic_s"] is None:
                    item["completion_monotonic_s"] = time.monotonic()
                    item["completion_timestamp_utc"] = utc_now()
                continue
            metrics = read_proc_metrics(item["process"].pid)
            if metrics:
                item["first_metrics"] = item["first_metrics"] or metrics
                item["last_metrics"] = metrics
                item["peak_rss_kb"] = max(int(item["peak_rss_kb"]), int(metrics["rss_kb"]))
                aggregate_rss += int(metrics["rss_kb"])
            alive.append(item["level_id"])
        freq = read_frequency_khz()
        temp = read_temperature_c()
        if freq:
            freq_samples.append(freq)
        if temp:
            temp_samples.append(temp)
        monitor_rows.append(
            {
                "timestamp_utc": utc_now(),
                "alive_levels": "+".join(alive),
                "aggregate_rss_kb": aggregate_rss,
                "mean_frequency_khz": freq.get("mean_khz") if freq else "",
                "max_temperature_c": temp.get("max_c") if temp else "",
            }
        )
        time.sleep(args.monitor_interval_s)
    write_csv(args.external_root / "resource_monitor.csv", ["timestamp_utc", "alive_levels", "aggregate_rss_kb", "mean_frequency_khz", "max_temperature_c"], monitor_rows)
    new_runs = []
    for item in processes:
        stdout, _ = item["process"].communicate()
        if item["completion_monotonic_s"] is None:
            item["completion_monotonic_s"] = time.monotonic()
            item["completion_timestamp_utc"] = utc_now()
        item["log_path"].write_text("\n".join(["command=" + " ".join(item["command"]), stdout]), encoding="utf-8")
        first = item["first_metrics"] or {"cpu_time_s": 0.0}
        last = item["last_metrics"] or first
        output_dir = case_root / "runs" / item["run_id"] / "outputs/regional2d"
        run = {
            "status": "passed" if item["process"].returncode == 0 else "failed",
            "run_id": item["run_id"],
            "returncode": item["process"].returncode,
            "requested_solver_target_m": item["target"],
            "output_dir": str(output_dir),
            "mesh": r4.parse_msh_triangles(case_root / f"meshes/r4-{item['level_id']}.msh"),
            "launch_timestamp_utc": item["launch_timestamp_utc"],
            "completion_timestamp_utc": item["completion_timestamp_utc"],
            "cpu_affinity": {"taskset_cpu": item["assigned_cpu"], "policy": "P2 fast-core-first distinct physical cores"},
            "resource_usage": {
                "wall_clock_s": float(item["completion_monotonic_s"]) - float(item["launch_monotonic_s"]),
                "cpu_time_s": max(0.0, float(last["cpu_time_s"]) - float(first["cpu_time_s"])),
                "peak_memory_kb": item["peak_rss_kb"],
            },
            "stdout": stdout.strip(),
        }
        if run["status"] == "passed":
            run["timestep"] = r4.timestep_stats_from_diagnostics(output_dir)
        else:
            run["log_tail"] = stdout[-4000:]
        run["case_invariance"] = {
            "terrain_sha256": summary["case_record"]["terrain"]["sha256"],
            "source_sha256": summary["case_record"]["source"]["sha256"],
            "physical_configuration_sha256": summary["case_record"]["physical_configuration_sha256"],
            "domain_sha256": summary["case_record"]["domain_sha256"],
            "coupling_section_sha256": summary["case_record"]["coupling_section_sha256"],
        }
        write_json(args.external_root / "spatial" / item["level_id"] / "run.json", run)
        new_runs.append(run)
    runs = [read_json(args.external_root / "spatial/h600/run.json")]
    for target in NEW_TARGETS:
        runs.append(read_json(args.external_root / "spatial" / f"h{target:g}" / "run.json"))
    first_launch_all = min((run.get("launch_timestamp_utc") or first_launch for run in runs), default=first_launch)
    last_completion_all = max((run.get("completion_timestamp_utc") or utc_now() for run in runs), default=utc_now())
    concurrent_makespan_s = max((run["resource_usage"]["wall_clock_s"] for run in runs if run["requested_solver_target_m"] in targets), default=0.0)
    sequential_compute_s = sum(float(run["resource_usage"]["wall_clock_s"]) for run in runs if run["requested_solver_target_m"] in targets)
    spatial_summary = {
        "schema": {"name": "tsunami.c1a_r5_frozen_spatial_runs", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "first_launch_timestamp_utc": first_launch_all,
        "last_completion_timestamp_utc": last_completion_all,
        "concurrent_makespan_s": concurrent_makespan_s,
        "sequential_equivalent_compute_s": sequential_compute_s,
        "effective_ensemble_speedup": sequential_compute_s / concurrent_makespan_s if concurrent_makespan_s > 0.0 else None,
        "frequency_khz": summarise_samples(freq_samples, ("min_khz", "mean_khz", "max_khz")),
        "temperature_c": summarise_samples(temp_samples, ("min_c", "mean_c", "max_c")),
        "runs": runs,
    }
    write_json(args.external_root / "spatial_run_summary.json", spatial_summary)
    return spatial_summary


def load_snapshot_by_time(level: r4.LevelData) -> dict[float, list[dict[str, float]]]:
    by_time: dict[float, list[dict[str, float]]] = defaultdict(list)
    for row in read_csv(level.output_dir / "snapshots.csv"):
        converted = {
            "cell": float(row["cell"]),
            "depth": float(row["depth"]),
            "momentum_x": float(row["momentum_x"]),
            "momentum_y": float(row["momentum_y"]),
            "bed_elevation": float(row["bed_elevation"]),
            "free_surface_elevation": float(row["free_surface_elevation"]),
        }
        by_time[float(row["time"])].append(converted)
    return {time_s: sorted(rows, key=lambda item: item["cell"]) for time_s, rows in by_time.items()}


def nearest_values(points: Sequence[tuple[float, float]], cells: Sequence[dict[str, float]], values: Sequence[float]) -> list[float]:
    out = []
    for x, y in points:
        best = min(range(len(cells)), key=lambda i: (cells[i]["centroid_x_m"] - x) ** 2 + (cells[i]["centroid_y_m"] - y) ** 2)
        out.append(float(values[best]))
    return out


def metric_values(a: Sequence[float], b: Sequence[float]) -> dict[str, Any]:
    return {
        "rmse": math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)) / len(a)),
        "nrmse": c1a.nrmse(a, b),
        "correlation": c1a._pearson(a, b),
        "mean_bias": statistics.fmean([x - y for x, y in zip(a, b)]),
        "max_abs_difference": max(abs(x - y) for x, y in zip(a, b)),
    }


def corridor_points(corridor: dict[str, Any], count: int, *, start_fraction: float = 0.0, end_fraction: float = 1.0) -> list[tuple[float, float]]:
    origin = corridor["configured_origin"]
    tangent = corridor["local_basis"]["tangent"]
    start = float(corridor["stations"]["offshore_xi_m"])
    end = float(corridor["stations"]["target_xi_m"])
    a = start + (end - start) * start_fraction
    b = start + (end - start) * end_fraction
    xis = [(a + b) * 0.5] if count == 1 else [a + (b - a) * i / (count - 1) for i in range(count)]
    return [(float(origin["x"]) + xi * float(tangent["x"]), float(origin["y"]) + xi * float(tangent["y"])) for xi in xis]


def regular_domain_points(levels: dict[str, r4.LevelData], count_x: int = 48, count_y: int = 96) -> list[tuple[float, float]]:
    cells_by_level = {level_id: r4.parse_msh_surface_cells(level.mesh_path) for level_id, level in levels.items()}
    xmin = max(min(cell["centroid_x_m"] for cell in cells) for cells in cells_by_level.values())
    xmax = min(max(cell["centroid_x_m"] for cell in cells) for cells in cells_by_level.values())
    ymin = max(min(cell["centroid_y_m"] for cell in cells) for cells in cells_by_level.values())
    ymax = min(max(cell["centroid_y_m"] for cell in cells) for cells in cells_by_level.values())
    return [(xmin + (xmax - xmin) * ix / (count_x - 1), ymin + (ymax - ymin) * iy / (count_y - 1)) for iy in range(count_y) for ix in range(count_x)]


def bed_source_diagnostics(metrics: dict[str, Any], levels: dict[str, r4.LevelData], pair_ids: Sequence[str]) -> dict[str, Any]:
    case_root = Path(metrics["case_root"])
    corridor = read_json(case_root / "manifests/corridors/kamaishi-delivery-corridor.json")
    cells = {level_id: r4.parse_msh_surface_cells(level.mesh_path) for level_id, level in levels.items()}
    snapshots = {level_id: load_snapshot_by_time(level)[0.0] for level_id, level in levels.items()}
    bed = {level_id: [row["bed_elevation"] for row in snapshots[level_id]] for level_id in levels}
    source = {level_id: [row["free_surface_elevation"] for row in snapshots[level_id]] for level_id in levels}
    supports = {
        "whole_domain": regular_domain_points(levels),
        "corridor_centreline": corridor_points(corridor, 401),
    }
    result: dict[str, Any] = {"supports": {name: {"point_count": len(points)} for name, points in supports.items()}}
    for support_name, points in supports.items():
        result[support_name] = {}
        bed_on_support = {level_id: nearest_values(points, cells[level_id], bed[level_id]) for level_id in levels}
        source_on_support = {level_id: nearest_values(points, cells[level_id], source[level_id]) for level_id in levels}
        for pair_id in pair_ids:
            fine_id, coarse_id = pair_id.split("_vs_")
            result[support_name][pair_id] = {
                "bed": metric_values(bed_on_support[fine_id], bed_on_support[coarse_id]),
                "source_surface_perturbation": metric_values(source_on_support[fine_id], source_on_support[coarse_id]),
            }
    result["coupling_section"] = {}
    support = r4.common_support()
    for pair_id in pair_ids:
        fine_id, coarse_id = pair_id.split("_vs_")
        fine = levels[fine_id]
        coarse = levels[coarse_id]
        result["coupling_section"][pair_id] = {
            "bed": metric_values(
                r4.interpolate_profile(r4.profile_at_time(fine, 0.0, "bed"), support),
                r4.interpolate_profile(r4.profile_at_time(coarse, 0.0, "bed"), support),
            ),
            "source_surface_perturbation": metric_values(
                r4.interpolate_profile(r4.profile_at_time(fine, 0.0, "eta"), support),
                r4.interpolate_profile(r4.profile_at_time(coarse, 0.0, "eta"), support),
            ),
        }
    return result


def pair_metrics(levels: dict[str, r4.LevelData], level_metrics: dict[str, Any], fine_id: str, coarse_id: str) -> dict[str, Any]:
    fine = levels[fine_id]
    coarse = levels[coarse_id]
    fine_source = level_metrics[fine_id]["source_projection"]
    coarse_source = level_metrics[coarse_id]["source_projection"]
    return {
        "coarse_level": coarse_id,
        "fine_level": fine_id,
        "refinement_ratio_actual_h": level_metrics[coarse_id]["actual_characteristic_mesh_size_m"] / level_metrics[fine_id]["actual_characteristic_mesh_size_m"],
        "eta_waveform": r4.waveform_metric(fine.series, coarse.series, "eta_m"),
        "qn_waveform": r4.waveform_metric(fine.series, coarse.series, "qn_m2_per_s"),
        "Qn_waveform": r4.waveform_metric(fine.series, coarse.series, "Qn_m3_per_s"),
        "qbar_waveform": r4.waveform_metric(fine.series, coarse.series, "qbar_m2_per_s"),
        "eta_distributed_common_support": r4.distributed_metric(fine, coarse, "eta"),
        "qn_distributed_common_support": r4.distributed_metric(fine, coarse, "qn"),
        "bed_projection_common_support": r4.bed_projection_metric(fine, coarse),
        "source_projection": r4.source_comparison(fine_source, coarse_source),
        "arrival_time_proxy_difference_s": level_metrics[fine_id]["forcing_window_qoi"]["arrival_time_proxy_s"] - level_metrics[coarse_id]["forcing_window_qoi"]["arrival_time_proxy_s"],
    }


def sequence_class(values: Sequence[float]) -> str:
    if len(values) < 3:
        return "insufficient"
    decreasing = all(b <= a for a, b in zip(values, values[1:]))
    increasing = all(b >= a for a, b in zip(values, values[1:]))
    if decreasing:
        return "monotonic_decreasing"
    if increasing:
        return "monotonic_increasing"
    return "non_monotonic"


def richardson_for_triples(level_ids: Sequence[str], level_metrics: dict[str, Any]) -> dict[str, Any]:
    triples = [("h600", "h500", "h400"), ("h600", "h450", "h400"), ("h500", "h450", "h400")]
    result = {}
    for triple in triples:
        if not all(level in level_ids for level in triple):
            continue
        coarse, medium, fine = triple
        h_values = [level_metrics[fine]["actual_characteristic_mesh_size_m"], level_metrics[medium]["actual_characteristic_mesh_size_m"], level_metrics[coarse]["actual_characteristic_mesh_size_m"]]
        result["_".join(triple)] = {"levels_coarse_to_fine": list(triple), "h_fine_to_coarse_m": h_values, "quantities": {}}
        for key in ("peak_eta_abs_m", "peak_qn_abs_m2_per_s", "peak_Qn_abs_m3_per_s", "peak_qbar_abs_m2_per_s"):
            values = [level_metrics[fine]["forcing_window_qoi"][key], level_metrics[medium]["forcing_window_qoi"][key], level_metrics[coarse]["forcing_window_qoi"][key]]
            monotonic = sequence_class(values) in {"monotonic_decreasing", "monotonic_increasing"}
            result["_".join(triple)]["quantities"][key] = {
                "values_fine_to_coarse": values,
                "status": "computed" if monotonic else "not_decision_grade_non_monotonic",
                "result": c1a.richardson_gci(values, h_values) if monotonic else None,
            }
    return result


def classify(metrics: dict[str, Any]) -> dict[str, Any]:
    fine_pair = metrics["comparisons"].get("h400_vs_h450") or {}
    formal = {
        "eta_waveform": fine_pair.get("eta_waveform", {}).get("nrmse"),
        "qn_waveform": fine_pair.get("qn_waveform", {}).get("nrmse"),
        "Qn_waveform": fine_pair.get("Qn_waveform", {}).get("nrmse"),
        "qbar_waveform": fine_pair.get("qbar_waveform", {}).get("nrmse"),
        "eta_distributed": fine_pair.get("eta_distributed_common_support", {}).get("nrmse"),
        "qn_distributed": fine_pair.get("qn_distributed_common_support", {}).get("nrmse"),
    }
    failing = [key for key, value in formal.items() if value is None or float(value) > r4.QUALIFICATION_THRESHOLD]
    adjacent_order = ["h500_vs_h600", "h450_vs_h500", "h400_vs_h450"]
    trends = {}
    for name in ("Qn_waveform", "qn_distributed_common_support", "eta_distributed_common_support", "eta_waveform", "qn_waveform", "qbar_waveform"):
        values = [metrics["comparisons"][pair][name]["nrmse"] for pair in adjacent_order if pair in metrics["comparisons"]]
        trends[name] = {"adjacent_nrmse": values, "classification": sequence_class(values)}
    monotonic_core = trends["Qn_waveform"]["classification"] == "monotonic_decreasing" and trends["qn_distributed_common_support"]["classification"] == "monotonic_decreasing"
    if not failing:
        outcome = "qualified"
    elif monotonic_core and max(float(value) for value in formal.values() if value is not None) < 0.25:
        outcome = "approaching_asymptotic_regime"
    elif monotonic_core:
        outcome = "not_qualified"
    else:
        outcome = "non_asymptotic"
    return {
        "outcome_classification": outcome,
        "formal_2_percent_status": "passed" if not failing else "failed",
        "failing_formal_metrics": failing,
        "qoi_trends": trends,
        "temporal_convergence_may_begin": outcome == "qualified",
        "selected_production_mesh": "h400" if outcome == "qualified" else None,
        "h300_recommended": outcome == "approaching_asymptotic_regime",
    }


def build_metrics(external_root: Path) -> tuple[dict[str, Any], dict[str, r4.LevelData]]:
    pre = read_json(external_root / "mesh_preflight.json")
    runs_summary = read_json(external_root / "spatial_run_summary.json")
    case_root = Path(pre["case_root"])
    passed = [run for run in runs_summary["runs"] if run.get("status") == "passed"]
    if len(passed) < 4:
        raise RuntimeError("R5 requires h600 plus three new passed runs")
    by_level = {f"h{float(run['requested_solver_target_m']):g}": run for run in passed}
    levels = {level_id: r4.derive_level_data(level_id, float(level_id[1:]), case_root, by_level[level_id]) for level_id in sorted(by_level, key=lambda item: float(item[1:]), reverse=True)}
    level_metrics = {}
    for level_id, level in levels.items():
        run = by_level[level_id]
        mesh = run["mesh"]
        timestep = r4.timestep_stats_from_diagnostics(level.output_dir)
        level_metrics[level_id] = {
            "requested_solver_target_m": level.target_m,
            "active_cells": mesh["active_cells"],
            "total_cells": mesh["total_cells"],
            "actual_characteristic_mesh_size_m": mesh["actual_characteristic_h_m"],
            "mesh_sha256": mesh["mesh_sha256"],
            "minimum_cell_area_m2": mesh["minimum_cell_area_m2"],
            "mean_cell_area_m2": mesh["mean_cell_area_m2"],
            "maximum_cell_area_m2": mesh["maximum_cell_area_m2"],
            "domain_area_m2": mesh["domain_area_m2"],
            "runtime_wall_clock_s": run["resource_usage"]["wall_clock_s"],
            "runtime_cpu_time_s": run["resource_usage"]["cpu_time_s"],
            "peak_memory_kb": run["resource_usage"]["peak_memory_kb"],
            "launch_timestamp_utc": run.get("launch_timestamp_utc"),
            "completion_timestamp_utc": run.get("completion_timestamp_utc"),
            "cpu_affinity": run.get("cpu_affinity"),
            "timestep": timestep,
            "forcing_window_qoi": r4.qoi_summary(level.series),
            "source_projection": r4.source_projection(level),
            "coupling_sample_count": int(level.metadata["sample_count"]),
            "reused": bool(run.get("reuse")),
        }
    ordered_ids = sorted(levels, key=lambda level_id: level_metrics[level_id]["actual_characteristic_mesh_size_m"], reverse=True)
    comparisons = {}
    for i, coarse_id in enumerate(ordered_ids):
        for fine_id in ordered_ids[i + 1 :]:
            comparisons[f"{fine_id}_vs_{coarse_id}"] = pair_metrics(levels, level_metrics, fine_id, coarse_id)
    requested_pairs = ["h500_vs_h600", "h450_vs_h500", "h400_vs_h450", "h400_vs_h500", "h400_vs_h600"]
    bed_source = bed_source_diagnostics({"case_root": str(case_root)}, levels, requested_pairs)
    metrics = {
        "schema": {"name": "tsunami.c1a_r5_finer_frozen_spatial_convergence", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "external_root": str(external_root),
        "case_root": str(case_root),
        "selected_ladder": ordered_ids,
        "forcing_window_s": list(r4.FORCING_WINDOW_S),
        "h600_reuse_proof": read_json(external_root / "h600_reuse_proof.json"),
        "frozen_family_invariance": {
            "terrain_path": pre["case_record"]["terrain"]["path"],
            "terrain_sha256": pre["case_record"]["terrain"]["sha256"],
            "source_path": pre["case_record"]["source"]["path"],
            "source_sha256": pre["case_record"]["source"]["sha256"],
            "physical_configuration_sha256": pre["case_record"]["physical_configuration_sha256"],
            "domain_sha256": pre["case_record"]["domain_sha256"],
            "coupling_section_sha256": pre["case_record"]["coupling_section_sha256"],
            "status": "passed",
        },
        "runtime": {
            "first_launch_timestamp_utc": runs_summary.get("first_launch_timestamp_utc"),
            "last_completion_timestamp_utc": runs_summary.get("last_completion_timestamp_utc"),
            "concurrent_makespan_s": runs_summary.get("concurrent_makespan_s"),
            "sequential_equivalent_compute_s": runs_summary.get("sequential_equivalent_compute_s"),
            "effective_ensemble_speedup": runs_summary.get("effective_ensemble_speedup"),
            "frequency_khz": runs_summary.get("frequency_khz"),
            "temperature_c": runs_summary.get("temperature_c"),
        },
        "levels": level_metrics,
        "refinement_ratios": {
            f"{coarse}_over_{fine}": level_metrics[coarse]["actual_characteristic_mesh_size_m"] / level_metrics[fine]["actual_characteristic_mesh_size_m"]
            for i, coarse in enumerate(ordered_ids)
            for fine in ordered_ids[i + 1 :]
        },
        "comparisons": comparisons,
        "bed_source_projection_diagnostics": bed_source,
        "richardson_gci": richardson_for_triples(ordered_ids, level_metrics),
        "morphology": morphology(comparisons),
        "along_corridor_error_trend": {
            pair: bed_source["corridor_centreline"].get(pair, {}).get("bed", {}) for pair in requested_pairs
        },
        "source_volume_variation": {
            pair: comparisons[pair]["source_projection"] for pair in requested_pairs if pair in comparisons
        },
        "dominant_residual_error_mechanism": "mixed_spatial_error: finite-volume diffusion/dispersion remains primary; mesh-dependent bathymetry projection remains secondary unless R5 trends overturn this",
        "no_observations_used": True,
        "no_calibration_performed": True,
        "local3d_not_started": True,
        "temporal_convergence_started": False,
        "h300_not_run": True,
    }
    metrics["qualification"] = classify(metrics)
    if metrics["qualification"]["h300_recommended"]:
        speedup = float(metrics["runtime"]["effective_ensemble_speedup"] or 1.0)
        metrics["h300_projection"] = {"serial_prior_h": SERIAL_H300_PRIOR_H, "estimated_runtime_h": SERIAL_H300_PRIOR_H / max(speedup, 1.0e-12)}
    else:
        metrics["h300_projection"] = {"status": "not_recommended"}
    return metrics, levels


def morphology(comparisons: dict[str, Any]) -> dict[str, Any]:
    result = {}
    for quantity in ("eta_waveform", "qn_waveform", "Qn_waveform", "qbar_waveform"):
        values = [comparisons[pair][quantity]["nrmse"] for pair in ("h500_vs_h600", "h450_vs_h500", "h400_vs_h450") if pair in comparisons]
        lags = [comparisons[pair][quantity]["phase_alignment"]["optimal_lag_s"] for pair in ("h500_vs_h600", "h450_vs_h500", "h400_vs_h450") if pair in comparisons]
        result[quantity] = {
            "adjacent_nrmse": values,
            "adjacent_optimal_lag_s": lags,
            "finding": "approaching similar morphology" if sequence_class(values) == "monotonic_decreasing" and max(abs(float(lag)) for lag in lags) <= 10.0 else "material morphology or phase differences remain",
        }
    return result


def svg_line(path: Path, title: str, xlabel: str, ylabel: str, series: dict[str, list[dict[str, float]]], y_key: str, *, x_key: str = "time_s") -> None:
    width, height = 760, 420
    left, top, right, bottom = 76, 42, 30, 58
    all_rows = [row for rows in series.values() for row in rows]
    xs = [float(row[x_key]) for row in all_rows]
    ys = [float(row[y_key]) for row in all_rows]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    if abs(ymax - ymin) < 1.0e-30:
        ymin -= 1.0
        ymax += 1.0
    pad = 0.08 * (ymax - ymin)
    ymin -= pad
    ymax += pad

    def xmap(x: float) -> float:
        return left + (x - xmin) / max(xmax - xmin, 1.0e-30) * (width - left - right)

    def ymap(y: float) -> float:
        return height - bottom - (y - ymin) / (ymax - ymin) * (height - top - bottom)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="70" y="24" font-size="16" font-family="sans-serif" fill="{AXIS}">{html.escape(title)}</text>',
    ]
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        yv = ymin + (ymax - ymin) * frac
        y = ymap(yv)
        parts.append(f'<line x1="{left}" y1="{y:.2f}" x2="{width-right}" y2="{y:.2f}" stroke="{GRID}"/>')
        parts.append(f'<text x="{left-8}" y="{y+4:.2f}" text-anchor="end" font-size="12">{yv:.3g}</text>')
    for level_id, rows in series.items():
        points = " ".join(f'{xmap(float(row[x_key])):.2f},{ymap(float(row[y_key])):.2f}' for row in rows)
        color = LEVEL_COLORS.get(level_id, "#555555")
        parts.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2.2"/>')
        parts.append(f'<text x="{width-right-80}" y="{top+18*len(parts)%240}" font-size="12" fill="{color}">{level_id}</text>')
    parts.append(f'<line x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}" stroke="{AXIS}"/>')
    parts.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}" stroke="{AXIS}"/>')
    parts.append(f'<text x="{width/2}" y="{height-8}" text-anchor="middle" font-size="13">{html.escape(xlabel)}</text>')
    parts.append(f'<text x="18" y="{height/2}" transform="rotate(-90 18 {height/2})" text-anchor="middle" font-size="13">{html.escape(ylabel)}</text>')
    parts.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def generate_figures(metrics: dict[str, Any], levels: dict[str, r4.LevelData], figure_root: Path, metrics_path: Path) -> dict[str, Any]:
    outputs = []
    for filename, title, y_key, ylabel in [
        ("c1a_r5_eta_convergence.svg", "C1A-R5 eta Convergence", "eta_m", "eta perturbation (m)"),
        ("c1a_r5_qn_convergence.svg", "C1A-R5 qn Convergence", "qn_m2_per_s", "qn perturbation (m^2/s)"),
        ("c1a_r5_Qn_convergence.svg", "C1A-R5 Qn Convergence", "Qn_m3_per_s", "Qn (m^3/s)"),
    ]:
        path = figure_root / filename
        svg_line(path, title, "time since event start (s)", ylabel, {level_id: r4.rows_in_window(level.series) for level_id, level in levels.items()}, y_key)
        outputs.append({"figure": str(path), "source_metrics": str(metrics_path)})
    error_rows = {}
    for quantity in ("eta_waveform", "qn_waveform", "Qn_waveform"):
        error_rows[quantity] = [
            {"h_m": metrics["levels"][comp["fine_level"]]["actual_characteristic_mesh_size_m"], "nrmse_percent": comp[quantity]["nrmse"] * 100.0}
            for comp in metrics["comparisons"].values()
            if comp["fine_level"] in {"h500", "h450", "h400"} and comp["coarse_level"] in {"h600", "h500", "h450"}
        ]
    path = figure_root / "c1a_r5_waveform_error_vs_h.svg"
    svg_line(path, "C1A-R5 Waveform Error vs Actual h", "actual characteristic h (m)", "NRMSE (%)", error_rows, "nrmse_percent", x_key="h_m")
    outputs.append({"figure": str(path), "source_metrics": str(metrics_path)})
    bed_rows = {"coupling_bed": [{"h_m": metrics["levels"][comp["fine_level"]]["actual_characteristic_mesh_size_m"], "rmse_m": comp["bed_projection_common_support"]["rmse_m"]} for comp in metrics["comparisons"].values() if comp["fine_level"] in {"h500", "h450", "h400"} and comp["coarse_level"] in {"h600", "h500", "h450"}]}
    path = figure_root / "c1a_r5_bed_projection_error_vs_h.svg"
    svg_line(path, "C1A-R5 Bed Projection Error vs Actual h", "actual characteristic h (m)", "bed RMSE (m)", bed_rows, "rmse_m", x_key="h_m")
    outputs.append({"figure": str(path), "source_metrics": str(metrics_path)})
    runtime_rows = {"runtime": [{"h_m": level["actual_characteristic_mesh_size_m"], "runtime_h": level["runtime_wall_clock_s"] / 3600.0} for level in metrics["levels"].values()]}
    path = figure_root / "c1a_r5_runtime_vs_h.svg"
    svg_line(path, "C1A-R5 Runtime vs Actual h", "actual characteristic h (m)", "wall time (h)", runtime_rows, "runtime_h", x_key="h_m")
    outputs.append({"figure": str(path), "source_metrics": str(metrics_path)})
    manifest = {"schema": {"name": "tsunami.c1a_r5_figure_manifest", "version": "1.0.0"}, "generated_at_utc": utc_now(), "study_id": STUDY_ID, "outputs": outputs}
    write_json(figure_root / "c1a_r5_figure_manifest.json", manifest)
    return manifest


def update_docs(metrics: dict[str, Any], docs_root: Path, figure_manifest: dict[str, Any]) -> None:
    docs_root.mkdir(parents=True, exist_ok=True)
    write_json(docs_root / "regional_frozen_terrain_v5_metrics.json", metrics)
    rows = []
    for pair_id, comp in metrics["comparisons"].items():
        rows.append(
            {
                "comparison_id": pair_id,
                "coarse_level": comp["coarse_level"],
                "fine_level": comp["fine_level"],
                "refinement_ratio_actual_h": comp["refinement_ratio_actual_h"],
                "eta_waveform_nrmse": comp["eta_waveform"]["nrmse"],
                "qn_waveform_nrmse": comp["qn_waveform"]["nrmse"],
                "Qn_waveform_nrmse": comp["Qn_waveform"]["nrmse"],
                "qbar_waveform_nrmse": comp["qbar_waveform"]["nrmse"],
                "eta_distributed_nrmse": comp["eta_distributed_common_support"]["nrmse"],
                "qn_distributed_nrmse": comp["qn_distributed_common_support"]["nrmse"],
                "bed_projection_rmse_m": comp["bed_projection_common_support"]["rmse_m"],
                "source_volume_relative_change": comp["source_projection"]["integrated_surface_perturbation_relative_change"],
            }
        )
    write_csv(docs_root / "regional_frozen_terrain_v5_comparisons.csv", list(rows[0]), rows)
    level_rows = []
    for level_id, level in metrics["levels"].items():
        qoi = level["forcing_window_qoi"]
        ts = level["timestep"]
        level_rows.append(
            {
                "level_id": level_id,
                "requested_solver_target_m": level["requested_solver_target_m"],
                "actual_characteristic_mesh_size_m": level["actual_characteristic_mesh_size_m"],
                "active_cells": level["active_cells"],
                "mesh_sha256": level["mesh_sha256"],
                "wall_clock_s": level["runtime_wall_clock_s"],
                "step_count": ts["step_count"],
                "mean_dt_s": ts["mean_dt_s"],
                "median_dt_s": ts["median_dt_s"],
                "minimum_dt_s": ts["minimum_dt_s"],
                "maximum_dt_s": ts["maximum_dt_s"],
                "peak_eta_abs_m": qoi["peak_eta_abs_m"],
                "peak_qn_abs_m2_per_s": qoi["peak_qn_abs_m2_per_s"],
                "peak_Qn_abs_m3_per_s": qoi["peak_Qn_abs_m3_per_s"],
                "peak_qbar_abs_m2_per_s": qoi["peak_qbar_abs_m2_per_s"],
            }
        )
    write_csv(docs_root / "regional_frozen_terrain_v5_levels.csv", list(level_rows[0]), level_rows)
    summary = f"""# C1A-R5 Finer Frozen-Terrain Regional2D Spatial Convergence

R5 reused the valid C1A-R4 h600 solution and executed full 600 s serial h500, h450, and h400 runs concurrently under `{STUDY_ID}`.

Outcome classification: `{metrics['qualification']['outcome_classification']}`.

Formal 2% qualification: `{metrics['qualification']['formal_2_percent_status']}`.

Temporal convergence may begin: `{metrics['qualification']['temporal_convergence_may_begin']}`.

h300 recommended: `{metrics['qualification']['h300_recommended']}`.

No observations, calibration, Local3D, temporal convergence, or h300 run were performed.
"""
    (docs_root / "regional_frozen_terrain_v5_summary.md").write_text(summary, encoding="utf-8")
    write_json(docs_root / "regional_frozen_terrain_v5_figure_manifest.json", figure_manifest)


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    metrics, levels = build_metrics(args.external_root)
    metrics_path = args.external_root / "frozen_terrain_v5_metrics.json"
    write_json(metrics_path, metrics)
    figure_manifest = generate_figures(metrics, levels, args.figure_root, metrics_path)
    update_docs(metrics, args.docs_root, figure_manifest)
    print(json.dumps({"status": "analyzed", "classification": metrics["qualification"], "metrics": str(metrics_path)}, indent=2, sort_keys=True))
    return metrics


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--r4-root", type=Path, default=DEFAULT_R4_ROOT)
    parser.add_argument("--g6-root", type=Path, default=DEFAULT_G6_ROOT)
    parser.add_argument("--r2d-binary", type=Path, default=DEFAULT_R2D_BINARY)
    parser.add_argument("--docs-root", type=Path, default=DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=DEFAULT_FIGURE_ROOT)
    parser.add_argument("--targets", type=float, nargs="+", default=list(NEW_TARGETS))
    parser.add_argument("--monitor-interval-s", type=float, default=60.0)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("preflight")
    sub.add_parser("run-full")
    sub.add_parser("analyze")
    sub.add_parser("run-and-analyze")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.command == "preflight":
        print(json.dumps(preflight(args), indent=2, sort_keys=True))
    elif args.command == "run-full":
        print(json.dumps(run_full(args), indent=2, sort_keys=True))
    elif args.command == "analyze":
        analyze(args)
    elif args.command == "run-and-analyze":
        run_full(args)
        analyze(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
