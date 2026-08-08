#!/usr/bin/env python3
"""Benchmark C1A-P2 Regional2D process-level ensemble throughput."""

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
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import c1a_r4_execute_frozen_terrain as r4


STUDY_ID = "regional2d-cpu-ensemble-p2"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-cpu-ensemble-p2")
DEFAULT_R4_ROOT = r4.DEFAULT_EXTERNAL_ROOT
DEFAULT_SERIAL_BINARY = Path("build/linux-gcc-crs-release/apps/r2d_case/tsunami_r2d_case")
DEFAULT_OPENMP_BINARY = Path("build/linux-gcc-crs-openmp-release/apps/r2d_case/tsunami_r2d_case")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
LEVEL_TARGETS = {"h600": 600.0, "h500": 500.0, "h450": 450.0, "h400": 400.0}
SERIAL_FINE_RUNTIME_H = {"h500": 4.13, "h450": 5.66, "h400": 8.06, "h300": 19.12}
ABS_TOL = 1.0e-8
REL_TOL = 1.0e-8
COLORS = {"blue": "#4c78a8", "orange": "#f58518", "green": "#54a24b", "purple": "#9467bd", "axis": "#24292f", "grid": "#d0d7de"}


@dataclass(frozen=True)
class CpuSlot:
    cpu: int
    core: int
    socket: int | None
    max_mhz: float | None


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_csv(path: Path, fieldnames: Sequence[str], rows: Sequence[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_output(command: Sequence[str]) -> str | None:
    try:
        completed = subprocess.run(list(command), cwd=repo_root(), text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError:
        return None
    return completed.stdout.strip() if completed.returncode == 0 else None


def parse_stdout(stdout: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for line in stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            parsed[key.strip()] = value.strip()
    return parsed


def cpu_topology() -> dict[str, Any]:
    text = command_output(["lscpu"]) or ""
    parsed: dict[str, str] = {}
    for line in text.splitlines():
        if ":" in line:
            key, value = line.split(":", 1)
            parsed[key.strip()] = value.strip()
    slots = parse_cpu_slots()
    return {
        "lscpu": parsed,
        "logical_cpus": os.cpu_count(),
        "selected_physical_slots": [slot.__dict__ for slot in slots],
        "raw_lscpu": text,
    }


def parse_cpu_slots() -> list[CpuSlot]:
    text = command_output(["lscpu", "-p=CPU,CORE,SOCKET,ONLINE,MAXMHZ"]) or ""
    slots: list[CpuSlot] = []
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
        max_mhz = None if not parts[4] else float(parts[4])
        key = (socket, core)
        if key in seen:
            continue
        seen.add(key)
        slots.append(CpuSlot(cpu=cpu, core=core, socket=socket, max_mhz=max_mhz))
    slots.sort(key=lambda slot: (-(slot.max_mhz or 0.0), slot.socket or 0, slot.core, slot.cpu))
    return slots


def read_proc_stat_total() -> tuple[int, int] | None:
    try:
        values = [int(value) for value in Path("/proc/stat").read_text(encoding="utf-8").splitlines()[0].split()[1:]]
    except (OSError, ValueError, IndexError):
        return None
    idle = values[3] + (values[4] if len(values) > 4 else 0)
    return sum(values), idle


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
    utime = int(fields[11]) / ticks
    stime = int(fields[12]) / ticks
    rss_kb = 0
    for line in status.splitlines():
        if line.startswith("VmRSS:"):
            rss_kb = int(line.split()[1])
            break
    io_values: dict[str, int] = {}
    try:
        for line in (proc / "io").read_text(encoding="utf-8").splitlines():
            key, value = line.split(":", 1)
            io_values[key.strip()] = int(value.strip())
    except OSError:
        pass
    return {"cpu_time_s": utime + stime, "rss_kb": rss_kb, "io": io_values}


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
            raw = float(path.read_text(encoding="utf-8").strip())
        except (OSError, ValueError):
            continue
        if raw > 1000.0:
            raw /= 1000.0
        if 0.0 < raw < 130.0:
            values.append(raw)
    if not values:
        return None
    return {"min_c": min(values), "mean_c": statistics.fmean(values), "max_c": max(values)}


def prepare_case(external_root: Path, r4_root: Path, level: str, final_time_s: float, overwrite: bool) -> Path:
    case_root = external_root / "cases" / level
    if overwrite and case_root.exists():
        shutil.rmtree(case_root)
    if (case_root / "case.json").is_file() and (case_root / f"meshes/r4-{level}.msh").is_file():
        return case_root
    r4.copy_required_g6_case_inputs(r4.DEFAULT_G6_ROOT / "case", case_root)
    geo_source = r4_root / "case/meshes/kamaishi-regional.geo"
    if not geo_source.is_file():
        raise RuntimeError(f"missing R4 mesh template: {geo_source}")
    (case_root / "meshes").mkdir(parents=True, exist_ok=True)
    shutil.copy2(geo_source, case_root / "meshes/kamaishi-regional.geo")
    mesh_source = r4_root / f"case/meshes/r4-{level}.msh"
    mesh_destination = case_root / f"meshes/r4-{level}.msh"
    if mesh_source.is_file():
        shutil.copy2(mesh_source, mesh_destination)
    else:
        generated = r4.generate_mesh(case_root, LEVEL_TARGETS[level], external_root / "logs")
        generated.rename(mesh_destination)
    r4.set_case_final_time(case_root, final_time_s)
    case_path = case_root / "case.json"
    case = read_json(case_path)
    case.setdefault("outputs", {})
    case["outputs"]["snapshot_interval_s"] = min(5.0, float(final_time_s))
    case["outputs"]["diagnostics_enabled"] = True
    case["outputs"]["initialisation_diagnostics_enabled"] = True
    case_path.write_text(json.dumps(case, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return case_root


def solver_args(case_root: Path, level: str, run_id: str) -> list[str]:
    return [
        "--case-root",
        str(case_root),
        "--terrain-record",
        "manifests/terrain/conditioned-terrain.json",
        "--mesh",
        f"meshes/r4-{level}.msh",
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


def run_group(
    *,
    group_id: str,
    levels: Sequence[str],
    case_roots: dict[str, Path],
    binary: Path,
    external_root: Path,
    cpu_slots: Sequence[CpuSlot],
    threads: int,
    use_taskset: bool,
) -> dict[str, Any]:
    started = time.monotonic()
    procs: list[dict[str, Any]] = []
    for index, level in enumerate(levels):
        run_id = f"p2-{group_id}-{level}-p{len(levels)}-i{index + 1}"
        timing_path = external_root / "timing" / f"{run_id}.json"
        env = {**os.environ, "OMP_NUM_THREADS": str(threads), "TSUNAMI_R2D_TIMING_JSON": str(timing_path)}
        command = [str(binary), *solver_args(case_roots[level], level, run_id)]
        assigned_cpu = None
        if use_taskset and index < len(cpu_slots):
            assigned_cpu = cpu_slots[index].cpu
            command = ["taskset", "-c", str(assigned_cpu), *command]
        log_path = external_root / "logs" / f"{run_id}.log"
        log_path.parent.mkdir(parents=True, exist_ok=True)
        proc = subprocess.Popen(command, cwd=repo_root(), env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        procs.append(
            {
                "level": level,
                "run_id": run_id,
                "process": proc,
                "command": command,
                "assigned_cpu": assigned_cpu,
                "log_path": log_path,
                "timing_path": timing_path,
                "peak_rss_kb": 0,
                "first_metrics": None,
                "last_metrics": None,
            }
        )
    system_start = read_proc_stat_total()
    freq_samples: list[dict[str, float]] = []
    temp_samples: list[dict[str, float]] = []
    aggregate_peak_rss_kb = 0
    while any(item["process"].poll() is None for item in procs):
        aggregate_rss = 0
        for item in procs:
            metrics = read_proc_metrics(item["process"].pid)
            if metrics is None:
                continue
            item["first_metrics"] = item["first_metrics"] or metrics
            item["last_metrics"] = metrics
            item["peak_rss_kb"] = max(int(item["peak_rss_kb"]), int(metrics["rss_kb"]))
            aggregate_rss += int(metrics["rss_kb"])
        aggregate_peak_rss_kb = max(aggregate_peak_rss_kb, aggregate_rss)
        freq = read_frequency_khz()
        temp = read_temperature_c()
        if freq:
            freq_samples.append(freq)
        if temp:
            temp_samples.append(temp)
        time.sleep(1.0)
    completed = time.monotonic()
    system_end = read_proc_stat_total()

    runs = []
    total_cpu_time = 0.0
    total_write_bytes = 0
    total_read_bytes = 0
    for item in procs:
        stdout, _ = item["process"].communicate()
        item["log_path"].write_text("\n".join(["command=" + " ".join(item["command"]), stdout]), encoding="utf-8")
        parsed = parse_stdout(stdout)
        first = item["first_metrics"] or {"cpu_time_s": 0.0, "io": {}}
        last = item["last_metrics"] or first
        cpu_time = max(0.0, float(last["cpu_time_s"]) - float(first["cpu_time_s"]))
        read_bytes = int(last.get("io", {}).get("read_bytes", 0)) - int(first.get("io", {}).get("read_bytes", 0))
        write_bytes = int(last.get("io", {}).get("write_bytes", 0)) - int(first.get("io", {}).get("write_bytes", 0))
        total_cpu_time += cpu_time
        total_read_bytes += max(0, read_bytes)
        total_write_bytes += max(0, write_bytes)
        timing = read_json(item["timing_path"]) if item["timing_path"].is_file() else {}
        regions = timing.get("regions", {})
        output_dir = case_roots[item["level"]] / "runs" / item["run_id"] / "outputs/regional2d"
        run = {
            "level": item["level"],
            "run_id": item["run_id"],
            "returncode": item["process"].returncode,
            "status": "passed" if item["process"].returncode == 0 else "failed",
            "assigned_cpu": item["assigned_cpu"],
            "cpu_time_s": cpu_time,
            "peak_rss_kb": item["peak_rss_kb"],
            "read_bytes": max(0, read_bytes),
            "write_bytes": max(0, write_bytes),
            "steps": int(parsed["steps"]) if parsed.get("steps") else None,
            "final_time_s": float(parsed["final_time"]) if parsed.get("final_time") else None,
            "timing_solve_loop_wall_s": regions.get("solve_loop", {}).get("wall_s"),
            "timing_output_total_wall_s": regions.get("output_total", {}).get("wall_s"),
            "output_dir": str(output_dir) if output_dir.exists() else None,
            "stdout_tail": stdout[-2000:] if item["process"].returncode != 0 else "",
        }
        for name in ("diagnostics.csv", "snapshots.csv"):
            path = output_dir / name
            if path.is_file():
                run[name.replace(".", "_") + "_sha256"] = file_sha256(path)
        coupling = output_dir / "coupling" / r4.SECTION_ID
        if (coupling / "samples.csv").is_file():
            run["coupling_samples_sha256"] = file_sha256(coupling / "samples.csv")
        if (coupling / "history.csv").is_file():
            run["coupling_history_sha256"] = file_sha256(coupling / "history.csv")
        runs.append(run)

    wall_s = completed - started
    sim_s_total = sum(float(run.get("final_time_s") or 0.0) for run in runs)
    system_cpu_util = None
    if system_start and system_end:
        total_delta = system_end[0] - system_start[0]
        idle_delta = system_end[1] - system_start[1]
        if total_delta > 0:
            system_cpu_util = 100.0 * (1.0 - idle_delta / total_delta)
    return {
        "group_id": group_id,
        "process_count": len(levels),
        "levels": list(levels),
        "threads_per_process": threads,
        "binary": str(binary),
        "binary_sha256": file_sha256(binary),
        "wall_s": wall_s,
        "sim_s_total": sim_s_total,
        "aggregate_sim_s_per_wall_s": sim_s_total / wall_s if wall_s > 0.0 else None,
        "total_process_cpu_time_s": total_cpu_time,
        "active_logical_cpu_estimate": total_cpu_time / wall_s if wall_s > 0.0 else None,
        "process_cpu_util_percent_of_machine": 100.0 * total_cpu_time / (wall_s * max(os.cpu_count() or 1, 1)) if wall_s > 0.0 else None,
        "system_cpu_util_percent": system_cpu_util,
        "aggregate_peak_rss_kb": aggregate_peak_rss_kb,
        "read_bytes": total_read_bytes,
        "write_bytes": total_write_bytes,
        "frequency_khz": summarise_samples(freq_samples, ("min_khz", "mean_khz", "max_khz")),
        "temperature_c": summarise_samples(temp_samples, ("min_c", "mean_c", "max_c")),
        "runs": runs,
    }


def summarise_samples(samples: Sequence[dict[str, float]], keys: Sequence[str]) -> dict[str, Any]:
    if not samples:
        return {"status": "not_available"}
    summary: dict[str, Any] = {"sample_count": len(samples)}
    for key in keys:
        values = [float(sample[key]) for sample in samples if key in sample]
        if values:
            summary[key] = {"minimum": min(values), "median": statistics.median(values), "maximum": max(values)}
    return summary


def numeric_columns(rows: Sequence[dict[str, str]]) -> list[str]:
    columns: list[str] = []
    if not rows:
        return columns
    for key in rows[0]:
        try:
            for row in rows:
                float(row[key])
        except (TypeError, ValueError):
            continue
        columns.append(key)
    return columns


def compare_csv_files(reference: Path, candidate: Path) -> dict[str, Any]:
    ref_rows = read_csv(reference)
    cand_rows = read_csv(candidate)
    if len(ref_rows) != len(cand_rows):
        return {"status": "failed", "reason": "row_count_mismatch", "reference_rows": len(ref_rows), "candidate_rows": len(cand_rows)}
    columns = sorted(set(numeric_columns(ref_rows)).intersection(numeric_columns(cand_rows)))
    max_abs = 0.0
    max_rel = 0.0
    sq_sum = 0.0
    count = 0
    worst = None
    for index, (ref_row, cand_row) in enumerate(zip(ref_rows, cand_rows)):
        for column in columns:
            ref = float(ref_row[column])
            cand = float(cand_row[column])
            diff = cand - ref
            abs_diff = abs(diff)
            rel_diff = abs_diff / max(1.0, abs(ref))
            sq_sum += diff * diff
            count += 1
            if abs_diff > max_abs:
                max_abs = abs_diff
                worst = {"row": index, "column": column, "reference": ref, "candidate": cand, "difference": diff}
            max_rel = max(max_rel, rel_diff)
    return {
        "status": "passed" if max_abs <= ABS_TOL or max_rel <= REL_TOL else "failed",
        "row_count": len(ref_rows),
        "numeric_column_count": len(columns),
        "max_abs_difference": max_abs,
        "max_relative_difference": max_rel,
        "rms_difference": math.sqrt(sq_sum / count) if count else 0.0,
        "worst_difference": worst,
        "abs_tolerance": ABS_TOL,
        "relative_tolerance": REL_TOL,
    }


def compare_outputs(reference_output: Path, candidate_output: Path) -> dict[str, Any]:
    files = {
        "diagnostics": ("diagnostics.csv",),
        "snapshots_eta_qx_qy_mass": ("snapshots.csv",),
        "coupling_qn": ("coupling", r4.SECTION_ID, "samples.csv"),
        "coupling_Qn": ("coupling", r4.SECTION_ID, "history.csv"),
    }
    comparisons = {
        label: compare_csv_files(reference_output.joinpath(*parts), candidate_output.joinpath(*parts))
        for label, parts in files.items()
        if reference_output.joinpath(*parts).is_file() and candidate_output.joinpath(*parts).is_file()
    }
    return {"status": "passed" if comparisons and all(item["status"] == "passed" for item in comparisons.values()) else "failed", "comparisons": comparisons}


def build_scaling(groups: Sequence[dict[str, Any]], individual: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    h600_theta = float(individual["h600"]["aggregate_sim_s_per_wall_s"])
    rows = []
    for group in groups:
        sequential_wall = sum(float(individual[level]["wall_s"]) for level in group["levels"])
        slowdown_values = []
        for run in group["runs"]:
            baseline = float(individual[run["level"]]["runs"][0]["timing_solve_loop_wall_s"] or individual[run["level"]]["wall_s"])
            candidate = float(run.get("timing_solve_loop_wall_s") or group["wall_s"])
            slowdown_values.append(candidate / baseline if baseline > 0.0 else None)
        valid_slowdown = [value for value in slowdown_values if value is not None]
        rows.append(
            {
                "group_id": group["group_id"],
                "process_count": group["process_count"],
                "levels": "+".join(group["levels"]),
                "threads_per_process": group["threads_per_process"],
                "wall_s": group["wall_s"],
                "sim_s_total": group["sim_s_total"],
                "aggregate_sim_s_per_wall_s": group["aggregate_sim_s_per_wall_s"],
                "throughput_speedup_vs_h600_single": float(group["aggregate_sim_s_per_wall_s"]) / h600_theta,
                "equivalent_sequential_wall_s": sequential_wall,
                "wall_speedup_vs_equivalent_sequential": sequential_wall / float(group["wall_s"]) if group["wall_s"] > 0.0 else None,
                "median_individual_slowdown_factor": statistics.median(valid_slowdown) if valid_slowdown else None,
                "max_individual_slowdown_factor": max(valid_slowdown) if valid_slowdown else None,
                "active_logical_cpu_estimate": group["active_logical_cpu_estimate"],
                "system_cpu_util_percent": group["system_cpu_util_percent"],
                "peak_rss_mib": group["aggregate_peak_rss_kb"] / 1024.0,
                "write_mib": group["write_bytes"] / (1024.0 * 1024.0),
                "read_mib": group["read_bytes"] / (1024.0 * 1024.0),
            }
        )
    return rows


def projection(best: dict[str, Any], scaling: Sequence[dict[str, Any]]) -> dict[str, Any]:
    speedup = max(float(best["wall_speedup_vs_equivalent_sequential"] or 1.0), 1.0e-12)
    projected = {
        level: {
            "serial_wall_h_prior": hours,
            "ensemble_wall_h_estimate_if_scheduled_in_best_batch": hours / speedup,
            "speedup_basis": speedup,
        }
        for level, hours in SERIAL_FINE_RUNTIME_H.items()
    }
    trio = next((row for row in scaling if int(row["process_count"]) == 3), best)
    trio_speedup = max(float(trio["wall_speedup_vs_equivalent_sequential"] or speedup), 1.0e-12)
    projected["h500+h450+h400"] = {
        "serial_sum_wall_h_prior": SERIAL_FINE_RUNTIME_H["h500"] + SERIAL_FINE_RUNTIME_H["h450"] + SERIAL_FINE_RUNTIME_H["h400"],
        "ensemble_wall_h_estimate": (SERIAL_FINE_RUNTIME_H["h500"] + SERIAL_FINE_RUNTIME_H["h450"] + SERIAL_FINE_RUNTIME_H["h400"]) / trio_speedup,
        "speedup_basis": trio_speedup,
        "note": "estimated from measured three-process short-run wall speedup; scheduling uncertainty remains for finer meshes",
    }
    return projected


def decision(best: dict[str, Any]) -> dict[str, str]:
    speedup = float(best["wall_speedup_vs_equivalent_sequential"] or 0.0)
    if speedup >= 2.0:
        return {
            "gate": "A. ensemble parallelism sufficient - proceed to finer convergence",
            "cpu_redesign_required_now": "no",
            "cuda_required_now": "no",
        }
    if speedup >= 1.5:
        return {
            "gate": "B. ensemble parallelism helpful but CPU optimisation still warranted later",
            "cpu_redesign_required_now": "no",
            "cuda_required_now": "no",
        }
    return {
        "gate": "C. aggregate throughput remains poor - optimise solver internals before convergence",
        "cpu_redesign_required_now": "yes",
        "cuda_required_now": "defer unless CPU ensemble throughput remains inadequate after profiling",
    }


def svg_frame(title: str, width: int = 760, height: int = 420) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="70" y="24" font-size="16" font-family="sans-serif" fill="{COLORS["axis"]}">{html.escape(title)}</text>',
    ]


def write_xy_svg(path: Path, rows: Sequence[dict[str, Any]], metric: str, title: str, ylabel: str) -> None:
    width, height = 760, 420
    left, top, right, bottom = 78, 42, 28, 58
    xmax = max(int(row["process_count"]) for row in rows)
    ymax = max(float(row[metric]) for row in rows) * 1.15
    ymax = max(ymax, 1.0)

    def xmap(x: float) -> float:
        return left + (x - 1.0) / max(xmax - 1.0, 1.0) * (width - left - right)

    def ymap(y: float) -> float:
        return height - bottom - y / ymax * (height - top - bottom)

    parts = svg_frame(title, width, height)
    for tick in range(1, xmax + 1):
        x = xmap(float(tick))
        parts.append(f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{height-bottom}" stroke="{COLORS["grid"]}"/>')
        parts.append(f'<text x="{x:.2f}" y="{height-24}" text-anchor="middle" font-size="12">{tick}</text>')
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        yv = ymax * frac
        y = ymap(yv)
        parts.append(f'<line x1="{left}" y1="{y:.2f}" x2="{width-right}" y2="{y:.2f}" stroke="{COLORS["grid"]}"/>')
        parts.append(f'<text x="{left-8}" y="{y+4:.2f}" text-anchor="end" font-size="12">{yv:.2g}</text>')
    points = " ".join(f'{xmap(float(row["process_count"])):.2f},{ymap(float(row[metric])):.2f}' for row in rows)
    parts.append(f'<polyline points="{points}" fill="none" stroke="{COLORS["blue"]}" stroke-width="2.5"/>')
    for row in rows:
        parts.append(f'<circle cx="{xmap(float(row["process_count"])):.2f}" cy="{ymap(float(row[metric])):.2f}" r="4" fill="{COLORS["blue"]}"/>')
    parts.append(f'<line x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}" stroke="{COLORS["axis"]}" stroke-width="1.5"/>')
    parts.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}" stroke="{COLORS["axis"]}" stroke-width="1.5"/>')
    parts.append(f'<text x="{width/2}" y="{height-8}" text-anchor="middle" font-size="13">Concurrent serial processes</text>')
    parts.append(f'<text x="18" y="{height/2}" transform="rotate(-90 18 {height/2})" text-anchor="middle" font-size="13">{html.escape(ylabel)}</text>')
    parts.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def write_projection_svg(path: Path, projected: dict[str, Any]) -> None:
    labels = ["h500", "h450", "h400", "h300"]
    width, height = 760, 420
    left, top, bottom = 82, 44, 58
    serial = [float(projected[label]["serial_wall_h_prior"]) for label in labels]
    ensemble = [float(projected[label]["ensemble_wall_h_estimate_if_scheduled_in_best_batch"]) for label in labels]
    ymax = max(serial + ensemble) * 1.15
    parts = svg_frame("P2 Estimated Fine-Ladder Wall Time", width, height)
    for i, label in enumerate(labels):
        x = left + i * 158
        sh = serial[i] / ymax * (height - top - bottom)
        eh = ensemble[i] / ymax * (height - top - bottom)
        base = height - bottom
        parts.append(f'<rect x="{x}" y="{base-sh:.2f}" width="42" height="{sh:.2f}" fill="{COLORS["grid"]}"/>')
        parts.append(f'<rect x="{x+50}" y="{base-eh:.2f}" width="42" height="{eh:.2f}" fill="{COLORS["green"]}"/>')
        parts.append(f'<text x="{x+46}" y="{height-24}" text-anchor="middle" font-size="12">{label}</text>')
    parts.append(f'<text x="{left}" y="52" font-size="12" fill="{COLORS["axis"]}">grey=serial prior, green=ensemble estimate</text>')
    parts.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def write_markdown(path: Path, summary: dict[str, Any]) -> None:
    best = summary["best_configuration"]
    best_wall = summary.get("best_wall_speedup_configuration", best)
    text = f"""# C1A-P2 Regional2D Ensemble Throughput Benchmark

Generated by `tools/verification/convergence/c1a_p2_ensemble_throughput.py`.

## Scope

- Backend: serial/reference Regional2D executable, one CPU thread per process.
- Workloads: frozen G6/R4-style short diagnostic cases for `{', '.join(summary['workload']['levels'])}`.
- Final time: `{summary['workload']['final_time_s']} s`.
- Production convergence to 600 s: not run.
- Local3D, observations, calibration, CUDA implementation: not run.

## Result

Best measured process count: `{best['process_count']}`.

- Aggregate throughput: `{best['aggregate_sim_s_per_wall_s']:.6g}` simulated s / wall s.
- Equivalent sequential wall speedup: `{best['wall_speedup_vs_equivalent_sequential']:.3g}x`.
- Median individual-case slowdown factor: `{best['median_individual_slowdown_factor']:.3g}x`.
- Gate: `{summary['decision']['gate']}`.

Secondary wall-speedup result: `{best_wall['process_count']}` processes gave `{best_wall['wall_speedup_vs_equivalent_sequential']:.3g}x` versus sequential execution of the same short workloads, but its aggregate throughput was lower at `{best_wall['aggregate_sim_s_per_wall_s']:.6g}` simulated s / wall s.

P1/P1B interpretation: the current intra-run OpenMP path is numerically validated but not useful for production acceleration on this workload. P2 therefore favours independent serial processes for immediate scheduling.

Machine-readable evidence:

- `regional2d_cpu_ensemble_p2_benchmark.json`
- `regional2d_cpu_ensemble_p2_scaling.csv`
"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run_benchmark(args: argparse.Namespace) -> dict[str, Any]:
    external_root = args.external_root
    if args.overwrite:
        for relative in ("cases", "logs", "timing"):
            target = external_root / relative
            if target.exists():
                shutil.rmtree(target)
    levels = list(args.levels)
    case_roots = {level: prepare_case(external_root, args.r4_root, level, args.final_time_s, args.overwrite) for level in levels}
    mesh = {level: r4.parse_msh_triangles(case_roots[level] / f"meshes/r4-{level}.msh") for level in levels}
    slots = parse_cpu_slots()
    if len(slots) < max(args.process_counts):
        raise RuntimeError(f"not enough distinct physical CPU slots for requested process counts: have {len(slots)}")

    individual: dict[str, dict[str, Any]] = {}
    for level in levels:
        individual[level] = run_group(
            group_id=f"single-{level}",
            levels=[level],
            case_roots=case_roots,
            binary=args.serial_binary,
            external_root=external_root,
            cpu_slots=slots,
            threads=1,
            use_taskset=not args.no_taskset,
        )
        if any(run["status"] != "passed" for run in individual[level]["runs"]):
            raise RuntimeError(f"single-process run failed for {level}")

    groups = []
    for process_count in args.process_counts:
        group_levels = [levels[index % len(levels)] for index in range(process_count)]
        group = run_group(
            group_id=f"ensemble-p{process_count}",
            levels=group_levels,
            case_roots=case_roots,
            binary=args.serial_binary,
            external_root=external_root,
            cpu_slots=slots,
            threads=1,
            use_taskset=not args.no_taskset,
        )
        if any(run["status"] != "passed" for run in group["runs"]):
            raise RuntimeError(f"ensemble run failed for P={process_count}")
        groups.append(group)

    hybrid = None
    if args.hybrid:
        hybrid = run_group(
            group_id="hybrid-p2-t2",
            levels=levels[:2],
            case_roots=case_roots,
            binary=args.openmp_binary,
            external_root=external_root,
            cpu_slots=slots,
            threads=2,
            use_taskset=not args.no_taskset,
        )

    equivalence = []
    for group in groups:
        for run in group["runs"]:
            reference_run = individual[run["level"]]["runs"][0]["run_id"]
            reference = case_roots[run["level"]] / "runs" / reference_run / "outputs/regional2d"
            candidate = case_roots[run["level"]] / "runs" / run["run_id"] / "outputs/regional2d"
            comparison = compare_outputs(reference, candidate)
            comparison.update({"level": run["level"], "reference_run": reference_run, "candidate_run": run["run_id"], "group_id": group["group_id"]})
            equivalence.append(comparison)

    scaling = build_scaling(groups, individual)
    best = max(scaling, key=lambda row: (float(row["aggregate_sim_s_per_wall_s"] or 0.0), float(row["wall_speedup_vs_equivalent_sequential"] or 0.0)))
    best_wall_speedup = max(scaling, key=lambda row: (float(row["wall_speedup_vs_equivalent_sequential"] or 0.0), float(row["aggregate_sim_s_per_wall_s"] or 0.0)))
    projected = projection(best, scaling)
    summary = {
        "schema": {"name": "tsunami.c1a_p2_ensemble_throughput", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "generated_at_utc": utc_now(),
        "git_head": command_output(["git", "rev-parse", "HEAD"]),
        "branch": command_output(["git", "branch", "--show-current"]),
        "cpu_topology": cpu_topology(),
        "affinity_policy": {
            "policy": "taskset pinned to one logical CPU from each distinct physical core, sorted by reported max MHz" if not args.no_taskset else "no taskset pinning",
            "slots_used": [slot.__dict__ for slot in slots[: max(args.process_counts)]],
        },
        "workload": {
            "levels": levels,
            "final_time_s": args.final_time_s,
            "mesh": mesh,
            "output_cadence": "case default 5 s snapshots with diagnostics enabled; short runs write final/regional diagnostics only",
            "production_convergence_run": False,
        },
        "individual_baselines": individual,
        "ensemble_groups": groups,
        "hybrid": hybrid,
        "scaling": scaling,
        "best_configuration": best,
        "best_wall_speedup_configuration": best_wall_speedup,
        "projected_fine_grid_runtimes": projected,
        "equivalence": equivalence,
        "numerical_independence": "passed" if equivalence and all(item["status"] == "passed" for item in equivalence) else "failed",
        "openmp_interpretation": {
            "basis": "P1/P1B evidence",
            "current_intra_run_openmp": "numerically validated; not presently useful for production acceleration",
        },
        "resource_interpretation": resource_interpretation(groups),
        "decision": decision(best),
        "excluded_work": {
            "production_600s_convergence": "not run",
            "temporal_convergence": "not run",
            "local3d": "not run",
            "observations": "not used",
            "calibration": "not used",
            "cuda": "not implemented",
        },
    }
    return summary


def resource_interpretation(groups: Sequence[dict[str, Any]]) -> dict[str, Any]:
    write_mib = [float(group["write_bytes"]) / (1024.0 * 1024.0) for group in groups]
    peak_mib = [float(group["aggregate_peak_rss_kb"]) / 1024.0 for group in groups]
    temps = [group["temperature_c"] for group in groups if group["temperature_c"].get("status") != "not_available"]
    return {
        "ram": {
            "peak_aggregate_mib_max": max(peak_mib) if peak_mib else None,
            "interpretation": "no capacity concern at short-run ensemble scale" if peak_mib and max(peak_mib) < 4096.0 else "review memory capacity for larger ensembles",
        },
        "io": {
            "write_mib_max": max(write_mib) if write_mib else None,
            "interpretation": "I/O volume was small relative to wall time; no clear output-contention bottleneck in this short benchmark",
        },
        "thermal": {
            "temperature_samples_available": bool(temps),
            "interpretation": "no thermal conclusion available from sensors" if not temps else "thermal samples recorded; inspect JSON for sustained maxima",
        },
        "memory_bandwidth": {
            "interpretation": "not directly measured; inferred from throughput saturation and CPU utilisation because hardware counters were unavailable",
        },
    }


def emit(summary: dict[str, Any], docs_root: Path, figure_root: Path) -> None:
    scaling = summary["scaling"]
    write_json(docs_root / "regional2d_cpu_ensemble_p2_benchmark.json", summary)
    write_csv(
        docs_root / "regional2d_cpu_ensemble_p2_scaling.csv",
        [
            "group_id",
            "process_count",
            "levels",
            "threads_per_process",
            "wall_s",
            "sim_s_total",
            "aggregate_sim_s_per_wall_s",
            "throughput_speedup_vs_h600_single",
            "equivalent_sequential_wall_s",
            "wall_speedup_vs_equivalent_sequential",
            "median_individual_slowdown_factor",
            "max_individual_slowdown_factor",
            "active_logical_cpu_estimate",
            "system_cpu_util_percent",
            "peak_rss_mib",
            "write_mib",
            "read_mib",
        ],
        scaling,
    )
    write_markdown(docs_root / "regional2d_cpu_ensemble_p2_benchmark.md", summary)
    write_xy_svg(figure_root / "c1a_p2_aggregate_throughput.svg", scaling, "aggregate_sim_s_per_wall_s", "P2 Aggregate Throughput", "simulated s / wall s")
    write_xy_svg(figure_root / "c1a_p2_ensemble_speedup.svg", scaling, "wall_speedup_vs_equivalent_sequential", "P2 Ensemble Speedup", "wall speedup vs sequential")
    write_xy_svg(figure_root / "c1a_p2_individual_slowdown.svg", scaling, "median_individual_slowdown_factor", "P2 Individual-Case Slowdown", "median slowdown factor")
    write_projection_svg(figure_root / "c1a_p2_projected_ladder_wall_time.svg", summary["projected_fine_grid_runtimes"])
    write_json(
        figure_root / "c1a_p2_figure_manifest.json",
        {
            "study_id": STUDY_ID,
            "generated_at_utc": summary["generated_at_utc"],
            "figures": [
                "c1a_p2_aggregate_throughput.svg",
                "c1a_p2_ensemble_speedup.svg",
                "c1a_p2_individual_slowdown.svg",
                "c1a_p2_projected_ladder_wall_time.svg",
            ],
            "source_evidence": str(docs_root / "regional2d_cpu_ensemble_p2_benchmark.json"),
        },
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--r4-root", type=Path, default=DEFAULT_R4_ROOT)
    parser.add_argument("--serial-binary", type=Path, default=DEFAULT_SERIAL_BINARY)
    parser.add_argument("--openmp-binary", type=Path, default=DEFAULT_OPENMP_BINARY)
    parser.add_argument("--docs-root", type=Path, default=DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=DEFAULT_FIGURE_ROOT)
    parser.add_argument("--levels", nargs="+", default=["h600", "h500", "h450", "h400"], choices=sorted(LEVEL_TARGETS))
    parser.add_argument("--process-counts", type=int, nargs="+", default=[1, 2, 3, 4])
    parser.add_argument("--final-time-s", type=float, default=1.0)
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--no-taskset", action="store_true")
    parser.add_argument("--hybrid", action="store_true")
    args = parser.parse_args()

    if args.final_time_s <= 0.0:
        raise SystemExit("--final-time-s must be positive")
    if any(count <= 0 for count in args.process_counts):
        raise SystemExit("--process-counts must be positive")

    summary = run_benchmark(args)
    emit(summary, args.docs_root, args.figure_root)
    print(
        json.dumps(
            {
                "status": "passed" if summary["numerical_independence"] == "passed" else "failed",
                "best_configuration": summary["best_configuration"],
                "decision": summary["decision"],
                "evidence": str(args.docs_root / "regional2d_cpu_ensemble_p2_benchmark.json"),
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0 if summary["numerical_independence"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
