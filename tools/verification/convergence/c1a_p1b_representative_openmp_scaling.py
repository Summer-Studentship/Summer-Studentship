#!/usr/bin/env python3
"""Representative C1A-P1B Regional2D OpenMP scaling benchmark."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import resource
import shutil
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import c1a_r4_execute_frozen_terrain as r4


STUDY_ID = "regional2d-cpu-parallel-p1b"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-cpu-parallel-p1b")
DEFAULT_R4_ROOT = r4.DEFAULT_EXTERNAL_ROOT
DEFAULT_SERIAL_BINARY = Path("build/linux-gcc-crs-release/apps/r2d_case/tsunami_r2d_case")
DEFAULT_OPENMP_BINARY = Path("build/linux-gcc-crs-openmp-release/apps/r2d_case/tsunami_r2d_case")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
SECTION_ID = r4.SECTION_ID
COUPLING_PATCH = r4.COUPLING_PATCH
ABS_TOL = 1.0e-8
REL_TOL = 1.0e-8
SERIAL_FINE_RUNTIME_H = {"h500": 4.13, "h450": 5.66, "h400": 8.06, "h300": 19.12}
COLORS = {
    "close": "#4c78a8",
    "spread": "#f58518",
    "default": "#54a24b",
    "runtime": "#9467bd",
    "axis": "#24292f",
    "grid": "#d0d7de",
}


@dataclass(frozen=True)
class CommandResult:
    command: list[str]
    returncode: int
    stdout: str
    wall_clock_s: float
    cpu_time_s: float
    peak_memory_kb: int


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
    if completed.returncode != 0:
        return None
    return completed.stdout.strip()


def run_command(command: Sequence[str], *, cwd: Path, env: dict[str, str], log_path: Path) -> CommandResult:
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.monotonic()
    completed = subprocess.run(
        list(command),
        cwd=cwd,
        env={**os.environ, **env},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    elapsed = time.monotonic() - started
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    result = CommandResult(
        list(command),
        completed.returncode,
        completed.stdout,
        elapsed,
        (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime),
        int(after.ru_maxrss),
    )
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(
        "\n".join(
            [
                "command=" + " ".join(result.command),
                "environment=" + json.dumps(env, sort_keys=True),
                "returncode=" + str(result.returncode),
                "wall_clock_s=" + f"{result.wall_clock_s:.9g}",
                "cpu_time_s=" + f"{result.cpu_time_s:.9g}",
                "peak_memory_kb=" + str(result.peak_memory_kb),
                "",
                result.stdout,
            ]
        ),
        encoding="utf-8",
    )
    return result


def parse_stdout(stdout: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for line in stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            parsed[key.strip()] = value.strip()
    return parsed


def prepare_case(external_root: Path, r4_root: Path, final_time_s: float, *, output_mode: str, overwrite: bool) -> Path:
    case_root = external_root / f"case-{output_mode}"
    if overwrite and case_root.exists():
        shutil.rmtree(case_root)
    r4.copy_required_g6_case_inputs(r4.DEFAULT_G6_ROOT / "case", case_root)
    mesh_source = r4_root / "case/meshes/r4-h600.msh"
    if not mesh_source.is_file():
        raise RuntimeError(f"missing frozen R4 h600 mesh: {mesh_source}")
    mesh_destination = case_root / "meshes/r4-h600.msh"
    mesh_destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(mesh_source, mesh_destination)
    geo_source = r4_root / "case/meshes/kamaishi-regional.geo"
    if geo_source.is_file():
        shutil.copy2(geo_source, case_root / "meshes/kamaishi-regional.geo")
    r4.set_case_final_time(case_root, final_time_s)
    case_path = case_root / "case.json"
    case = read_json(case_path)
    case.setdefault("outputs", {})
    if output_mode == "minimal":
        case["outputs"]["snapshot_interval_s"] = float(final_time_s)
    else:
        case["outputs"]["snapshot_interval_s"] = min(5.0, float(final_time_s))
    case["outputs"]["diagnostics_enabled"] = True
    case["outputs"]["initialisation_diagnostics_enabled"] = True
    case_path.write_text(json.dumps(case, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return case_root


def solver_args(case_root: Path, run_id: str) -> list[str]:
    return [
        "--case-root",
        str(case_root),
        "--terrain-record",
        "manifests/terrain/conditioned-terrain.json",
        "--mesh",
        "meshes/r4-h600.msh",
        "--run-id",
        run_id,
        "--coupling-section",
        SECTION_ID,
        "--coupling-patch",
        COUPLING_PATCH,
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


def placement_env(placement: str, threads: int) -> dict[str, str]:
    env = {
        "OMP_NUM_THREADS": str(threads),
        "TSUNAMI_R2D_TIMING": "1",
    }
    if placement == "close":
        env.update({"OMP_PLACES": "cores", "OMP_PROC_BIND": "close"})
    elif placement == "spread":
        env.update({"OMP_PLACES": "cores", "OMP_PROC_BIND": "spread"})
    return env


def configured_thread_pairs(args: argparse.Namespace) -> list[tuple[str, int]]:
    if not args.placement_thread_map:
        return [(placement, int(threads)) for placement in args.placements for threads in args.threads]
    pairs: list[tuple[str, int]] = []
    for item in args.placement_thread_map:
        placement, sep, thread_text = item.partition(":")
        if sep != ":" or placement not in {"close", "spread", "default"}:
            raise ValueError(f"invalid placement-thread entry: {item}")
        for token in thread_text.split(","):
            if not token:
                continue
            threads = int(token)
            if threads <= 0:
                raise ValueError(f"thread count must be positive in {item}")
            pairs.append((placement, threads))
    seen: set[tuple[str, int]] = set()
    unique = []
    for pair in pairs:
        if pair not in seen:
            unique.append(pair)
            seen.add(pair)
    return unique


def run_regional(
    *,
    binary: Path,
    case_root: Path,
    external_root: Path,
    output_mode: str,
    variant: str,
    placement: str,
    threads: int,
    repeat: int,
) -> dict[str, Any]:
    run_id = f"p1b-{output_mode}-{variant}-{placement}-t{threads}-r{repeat}"
    timing_path = external_root / "timing" / f"{run_id}.json"
    env = placement_env(placement, threads)
    env["TSUNAMI_R2D_TIMING_JSON"] = str(timing_path)
    result = run_command(
        [str(binary), *solver_args(case_root, run_id)],
        cwd=repo_root(),
        env=env,
        log_path=external_root / "logs" / f"{run_id}.log",
    )
    parsed = parse_stdout(result.stdout)
    output_dir = case_root / "runs" / run_id / "outputs/regional2d"
    timing = read_json(timing_path) if timing_path.is_file() else {}
    regions = timing.get("regions", {})
    solve_loop = float(regions.get("solve_loop", {}).get("wall_s", 0.0))
    row = {
        "output_mode": output_mode,
        "variant": variant,
        "placement": placement,
        "threads": threads,
        "repeat": repeat,
        "run_id": run_id,
        "status": "passed" if result.returncode == 0 else "failed",
        "returncode": result.returncode,
        "wall_clock_s": result.wall_clock_s,
        "cpu_time_s": result.cpu_time_s,
        "active_logical_cpu_estimate": result.cpu_time_s / result.wall_clock_s if result.wall_clock_s > 0.0 else None,
        "peak_memory_kb": result.peak_memory_kb,
        "solver_loop_wall_s": solve_loop,
        "binary": str(binary),
        "binary_sha256": file_sha256(binary),
        "timing_json": str(timing_path) if timing_path.is_file() else None,
        "observed_openmp_threads": timing.get("observed_openmp_threads"),
        "steps": int(parsed["steps"]) if parsed.get("steps") else None,
        "final_time_s": float(parsed["final_time"]) if parsed.get("final_time") else None,
        "output_dir": str(output_dir) if output_dir.exists() else None,
        "stdout_tail": result.stdout[-2000:] if result.returncode != 0 else "",
    }
    for region, payload in regions.items():
        row[f"timing_{region}_wall_s"] = payload.get("wall_s")
        row[f"timing_{region}_count"] = payload.get("count")
    if output_dir.exists():
        row["diagnostics_sha256"] = file_sha256(output_dir / "diagnostics.csv")
        row["snapshots_sha256"] = file_sha256(output_dir / "snapshots.csv")
        coupling = output_dir / "coupling" / SECTION_ID
        if (coupling / "samples.csv").is_file():
            row["coupling_samples_sha256"] = file_sha256(coupling / "samples.csv")
        if (coupling / "history.csv").is_file():
            row["coupling_history_sha256"] = file_sha256(coupling / "history.csv")
    return row


def numeric_columns(rows: Sequence[dict[str, str]]) -> list[str]:
    if not rows:
        return []
    columns = []
    for key in rows[0]:
        try:
            for row in rows:
                float(row[key])
            columns.append(key)
        except (TypeError, ValueError):
            pass
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


def compare_outputs(case_root: Path, reference_run: str, candidate_run: str) -> dict[str, Any]:
    reference = case_root / "runs" / reference_run / "outputs/regional2d"
    candidate = case_root / "runs" / candidate_run / "outputs/regional2d"
    files = {
        "diagnostics": ("diagnostics.csv",),
        "snapshots_eta_qx_qy_mass": ("snapshots.csv",),
        "coupling_qn": ("coupling", SECTION_ID, "samples.csv"),
        "coupling_Qn": ("coupling", SECTION_ID, "history.csv"),
    }
    comparisons = {
        label: compare_csv_files(reference.joinpath(*parts), candidate.joinpath(*parts))
        for label, parts in files.items()
        if reference.joinpath(*parts).is_file() and candidate.joinpath(*parts).is_file()
    }
    return {
        "reference_run": reference_run,
        "candidate_run": candidate_run,
        "status": "passed" if comparisons and all(item["status"] == "passed" for item in comparisons.values()) else "failed",
        "comparisons": comparisons,
    }


def stats(values: Sequence[float]) -> dict[str, float]:
    return {"minimum": min(values), "median": statistics.median(values), "maximum": max(values)}


def aggregate_scaling(rows: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    passed = [row for row in rows if row["status"] == "passed" and row["output_mode"] == "minimal"]
    groups: dict[tuple[str, int], list[dict[str, Any]]] = defaultdict(list)
    for row in passed:
        if row["variant"] == "openmp":
            groups[(str(row["placement"]), int(row["threads"]))].append(row)
    serial_rows = [row for row in rows if row["status"] == "passed" and row["variant"] == "serial" and row["output_mode"] == "minimal"]
    if not serial_rows:
        raise RuntimeError("missing serial minimal-output baseline")
    serial_wall = statistics.median(float(row["wall_clock_s"]) for row in serial_rows)
    serial_loop = statistics.median(float(row["solver_loop_wall_s"]) for row in serial_rows)
    scaling = [
        {
            "placement": "serial",
            "threads": 1,
            "repeat_count": len(serial_rows),
            "end_to_end_wall_s_min": stats([float(row["wall_clock_s"]) for row in serial_rows])["minimum"],
            "end_to_end_wall_s_median": serial_wall,
            "end_to_end_wall_s_max": stats([float(row["wall_clock_s"]) for row in serial_rows])["maximum"],
            "solver_loop_wall_s_min": stats([float(row["solver_loop_wall_s"]) for row in serial_rows])["minimum"],
            "solver_loop_wall_s_median": serial_loop,
            "solver_loop_wall_s_max": stats([float(row["solver_loop_wall_s"]) for row in serial_rows])["maximum"],
            "end_to_end_speedup": 1.0,
            "solver_loop_speedup": 1.0,
            "parallel_efficiency_solver_loop": 1.0,
            "median_active_logical_cpus": statistics.median(float(row["active_logical_cpu_estimate"]) for row in serial_rows),
            "observed_openmp_threads": statistics.median(float(row.get("observed_openmp_threads") or 1.0) for row in serial_rows),
        }
    ]
    for (placement, threads), values in sorted(groups.items(), key=lambda item: (item[0][0], item[0][1])):
        wall_values = [float(row["wall_clock_s"]) for row in values]
        loop_values = [float(row["solver_loop_wall_s"]) for row in values]
        wall_median = statistics.median(wall_values)
        loop_median = statistics.median(loop_values)
        solver_speedup = serial_loop / loop_median if loop_median > 0.0 else None
        end_speedup = serial_wall / wall_median if wall_median > 0.0 else None
        scaling.append(
            {
                "placement": placement,
                "threads": threads,
                "repeat_count": len(values),
                "end_to_end_wall_s_min": min(wall_values),
                "end_to_end_wall_s_median": wall_median,
                "end_to_end_wall_s_max": max(wall_values),
                "solver_loop_wall_s_min": min(loop_values),
                "solver_loop_wall_s_median": loop_median,
                "solver_loop_wall_s_max": max(loop_values),
                "end_to_end_speedup": end_speedup,
                "solver_loop_speedup": solver_speedup,
                "parallel_efficiency_solver_loop": solver_speedup / threads if solver_speedup is not None else None,
                "median_active_logical_cpus": statistics.median(float(row["active_logical_cpu_estimate"]) for row in values),
                "observed_openmp_threads": statistics.median(float(row.get("observed_openmp_threads") or 1.0) for row in values),
            }
        )
    return scaling


def best_candidate(scaling: Sequence[dict[str, Any]]) -> dict[str, Any]:
    candidates = [row for row in scaling if row["placement"] != "serial" and row.get("solver_loop_speedup")]
    return max(candidates, key=lambda row: (float(row["solver_loop_speedup"]), -int(row["threads"])))


def amdahl_serial_fraction(speedup: float, threads: int) -> float | None:
    if speedup <= 0.0 or threads <= 1:
        return None
    return max(0.0, min(1.0, ((1.0 / speedup) - (1.0 / threads)) / (1.0 - (1.0 / threads))))


def normal_output_fraction(rows: Sequence[dict[str, Any]], best: dict[str, Any]) -> dict[str, Any]:
    minimal = [
        row for row in rows
        if row["status"] == "passed" and row["output_mode"] == "minimal" and row["placement"] == best["placement"] and int(row["threads"]) == int(best["threads"])
    ]
    normal = [
        row for row in rows
        if row["status"] == "passed" and row["output_mode"] == "normal" and row["placement"] == best["placement"] and int(row["threads"]) == int(best["threads"])
    ]
    if not minimal or not normal:
        return {"status": "not_available"}
    minimal_wall = statistics.median(float(row["wall_clock_s"]) for row in minimal)
    normal_wall = statistics.median(float(row["wall_clock_s"]) for row in normal)
    return {
        "status": "computed",
        "minimal_wall_s": minimal_wall,
        "normal_wall_s": normal_wall,
        "f_io": (normal_wall - minimal_wall) / normal_wall if normal_wall > 0.0 else None,
    }


def thread_local_buffer_audit(mesh: dict[str, Any], thread_counts: Sequence[int]) -> dict[str, Any]:
    cells = int(mesh["active_cells"])
    fields = 5
    bytes_per_real = 8
    return {
        "cell_count": cells,
        "fields_per_thread": fields,
        "bytes_per_real": bytes_per_real,
        "per_thread_storage_bytes": cells * fields * bytes_per_real,
        "by_thread_count": {
            str(p): {
                "storage_bytes": cells * fields * bytes_per_real * int(p),
                "storage_mib": cells * fields * bytes_per_real * int(p) / (1024.0 * 1024.0),
                "deterministic_reduction_terms": cells * fields * int(p),
            }
            for p in sorted(set(int(t) for t in thread_counts))
        },
        "scaling_model": "thread-local storage and deterministic reduction work scale approximately as p * N_cells * 5 Real values",
    }


def decision_gate(best: dict[str, Any]) -> dict[str, Any]:
    speedup = float(best["solver_loop_speedup"])
    if speedup >= 1.5:
        gate = "A. OpenMP gives useful production speedup"
        recommendation = "Proceed next to finer spatial convergence using OpenMP."
        cuda = "not recommended"
    elif speedup >= 1.1:
        gate = "B. OpenMP speedup is modest but worthwhile"
        recommendation = "Decide whether expected wall time is acceptable within the remaining studentship schedule."
        cuda = "not recommended"
    else:
        gate = "C. OpenMP speedup remains negligible"
        recommendation = "Profile/redesign CPU execution before expensive fine-grid runs, or reassess CUDA/HPC."
        cuda = "not recommended"
    return {"gate": gate, "next_recommendation": recommendation, "cuda_now": cuda}


def projected_runtimes(best: dict[str, Any]) -> dict[str, Any]:
    speedup = max(float(best["solver_loop_speedup"]), 1.0e-12)
    return {
        level: {
            "serial_wall_h_prior": hours,
            "parallel_wall_h_estimate": hours / speedup,
            "speedup_basis": speedup,
            "uncertainty": "representative h600 short-run median; excludes queueing, thermal drift, and finer-mesh cache effects",
        }
        for level, hours in SERIAL_FINE_RUNTIME_H.items()
    }


def svg_axes(width: int, height: int, title: str) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="70" y="24" font-size="16" font-family="sans-serif" fill="{COLORS["axis"]}">{title}</text>',
    ]


def write_line_svg(path: Path, rows: Sequence[dict[str, Any]], metric: str, title: str, ylabel: str) -> None:
    data = [row for row in rows if row["placement"] != "serial" and row.get(metric) not in (None, "")]
    width, height = 760, 420
    left, top, right, bottom = 72, 38, 28, 58
    xmax = max([int(row["threads"]) for row in data] + [1])
    ymax = max([float(row[metric]) for row in data] + [1.0]) * 1.15
    ymax = max(ymax, 1.05)

    def xmap(x: float) -> float:
        return left + (x - 1.0) / max(float(xmax - 1), 1.0) * (width - left - right)

    def ymap(y: float) -> float:
        return height - bottom - y / ymax * (height - top - bottom)

    parts = svg_axes(width, height, title)
    for tick in sorted(set([1, *[int(row["threads"]) for row in data]])):
        x = xmap(float(tick))
        parts.append(f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{height-bottom}" stroke="{COLORS["grid"]}"/>')
        parts.append(f'<text x="{x:.2f}" y="{height-24}" text-anchor="middle" font-size="12">{tick}</text>')
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        value = ymax * frac
        y = ymap(value)
        parts.append(f'<line x1="{left}" y1="{y:.2f}" x2="{width-right}" y2="{y:.2f}" stroke="{COLORS["grid"]}"/>')
        parts.append(f'<text x="{left-8}" y="{y+4:.2f}" text-anchor="end" font-size="12">{value:.2g}</text>')
    for placement in ("close", "spread", "default"):
        series = sorted([row for row in data if row["placement"] == placement], key=lambda row: int(row["threads"]))
        if not series:
            continue
        points = " ".join(f'{xmap(float(row["threads"])):.2f},{ymap(float(row[metric])):.2f}' for row in series)
        parts.append(f'<polyline points="{points}" fill="none" stroke="{COLORS[placement]}" stroke-width="2.5"/>')
        for row in series:
            parts.append(f'<circle cx="{xmap(float(row["threads"])):.2f}" cy="{ymap(float(row[metric])):.2f}" r="4" fill="{COLORS[placement]}"/>')
    parts.append(f'<line x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}" stroke="{COLORS["axis"]}" stroke-width="1.5"/>')
    parts.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}" stroke="{COLORS["axis"]}" stroke-width="1.5"/>')
    parts.append(f'<text x="{width/2}" y="{height-8}" text-anchor="middle" font-size="13">OpenMP threads</text>')
    parts.append(f'<text x="18" y="{height/2}" transform="rotate(-90 18 {height/2})" text-anchor="middle" font-size="13">{ylabel}</text>')
    parts.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def write_breakdown_svg(path: Path, run: dict[str, Any]) -> None:
    keys = ["source_update", "boundary_application", "face_residual", "residual_reduction", "state_update", "cfl_reduction", "positivity_reduction", "state_combination", "output_total"]
    values = [float(run.get(f"timing_{key}_wall_s") or 0.0) for key in keys]
    total = max(sum(values), 1.0e-12)
    width, height = 760, 420
    left, top = 180, 54
    bar_width = 500
    bar_height = 26
    parts = svg_axes(width, height, "P1B Runtime Breakdown: Best Configuration")
    for index, (key, value) in enumerate(zip(keys, values)):
        y = top + index * 44
        w = bar_width * value / total
        parts.append(f'<text x="{left-12}" y="{y+18}" text-anchor="end" font-size="12">{key}</text>')
        parts.append(f'<rect x="{left}" y="{y}" width="{bar_width}" height="{bar_height}" fill="#f6f8fa" stroke="{COLORS["grid"]}"/>')
        parts.append(f'<rect x="{left}" y="{y}" width="{w:.2f}" height="{bar_height}" fill="{COLORS["runtime"]}"/>')
        parts.append(f'<text x="{left+bar_width+8}" y="{y+18}" font-size="12">{value:.3g}s</text>')
    parts.append("</svg>")
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def write_projection_svg(path: Path, projections: dict[str, Any]) -> None:
    width, height = 760, 420
    left, top, bottom = 84, 42, 58
    labels = list(projections)
    serial = [float(projections[label]["serial_wall_h_prior"]) for label in labels]
    parallel = [float(projections[label]["parallel_wall_h_estimate"]) for label in labels]
    ymax = max(serial + parallel) * 1.12
    parts = svg_axes(width, height, "P1B Projected Fine-Grid Runtime")
    group_width = 120
    for i, label in enumerate(labels):
        x = left + i * 160
        s_h = serial[i] / ymax * (height - top - bottom)
        p_h = parallel[i] / ymax * (height - top - bottom)
        parts.append(f'<rect x="{x}" y="{height-bottom-s_h:.2f}" width="42" height="{s_h:.2f}" fill="#c8d7f0"/>')
        parts.append(f'<rect x="{x+52}" y="{height-bottom-p_h:.2f}" width="42" height="{p_h:.2f}" fill="#f2c08b"/>')
        parts.append(f'<text x="{x+47}" y="{height-24}" text-anchor="middle" font-size="12">{label}</text>')
    parts.append(f'<text x="{left}" y="{height-8}" font-size="12">blue=prior serial, orange=P1B parallel estimate</text>')
    parts.append("</svg>")
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def run_matrix(args: argparse.Namespace) -> tuple[list[dict[str, Any]], list[dict[str, Any]], dict[str, Any]]:
    minimal_case = prepare_case(args.external_root, args.r4_root, args.final_time_s, output_mode="minimal", overwrite=args.overwrite)
    normal_case = prepare_case(args.external_root, args.r4_root, args.final_time_s, output_mode="normal", overwrite=args.overwrite)
    rows: list[dict[str, Any]] = []
    for repeat in range(1, args.repeats + 1):
        rows.append(
            run_regional(
                binary=args.serial_binary,
                case_root=minimal_case,
                external_root=args.external_root,
                output_mode="minimal",
                variant="serial",
                placement="serial",
                threads=1,
                repeat=repeat,
            )
        )
    pairs = configured_thread_pairs(args)
    for placement, threads in pairs:
        for repeat in range(1, args.repeats + 1):
            rows.append(
                run_regional(
                    binary=args.openmp_binary,
                    case_root=minimal_case,
                    external_root=args.external_root,
                    output_mode="minimal",
                    variant="openmp",
                    placement=placement,
                    threads=int(threads),
                    repeat=repeat,
                )
            )
    failed = [row for row in rows if row["status"] != "passed"]
    if failed:
        write_json(args.external_root / "p1b_failed_runs.json", {"runs": rows})
        raise RuntimeError(f"{len(failed)} minimal benchmark runs failed")
    scaling = aggregate_scaling(rows)
    best = best_candidate(scaling)
    for repeat in range(1, args.normal_repeats + 1):
        rows.append(
            run_regional(
                binary=args.openmp_binary,
                case_root=normal_case,
                external_root=args.external_root,
                output_mode="normal",
                variant="openmp",
                placement=str(best["placement"]),
                threads=int(best["threads"]),
                repeat=repeat,
            )
        )
    equivalence = [
        compare_outputs(minimal_case, "p1b-minimal-serial-serial-t1-r1", f"p1b-minimal-openmp-{placement}-t{threads}-r1")
        for placement, threads in pairs
        if threads in set(args.equivalence_threads)
        if f"p1b-minimal-openmp-{placement}-t{threads}-r1" in {row["run_id"] for row in rows}
    ]
    mesh = r4.parse_msh_triangles(minimal_case / "meshes/r4-h600.msh")
    return rows, equivalence, {"minimal_case": minimal_case, "normal_case": normal_case, "mesh": mesh}


def build_summary(args: argparse.Namespace, rows: list[dict[str, Any]], equivalence: list[dict[str, Any]], context: dict[str, Any]) -> dict[str, Any]:
    scaling = aggregate_scaling(rows)
    best = best_candidate(scaling)
    decision = decision_gate(best)
    projections = projected_runtimes(best)
    normal_io = normal_output_fraction(rows, best)
    representative_runs = [row for row in rows if row["status"] == "passed" and row["output_mode"] == "minimal" and row["placement"] == best["placement"] and int(row["threads"]) == int(best["threads"])]
    representative = min(representative_runs, key=lambda row: abs(float(row["solver_loop_wall_s"]) - float(best["solver_loop_wall_s_median"])))
    serial_loop = next(row for row in scaling if row["placement"] == "serial")["solver_loop_wall_s_median"]
    return {
        "schema": {"name": "tsunami.c1a_p1b_representative_openmp_scaling", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "git_head": command_output(["git", "rev-parse", "HEAD"]),
        "branch": command_output(["git", "branch", "--show-current"]),
        "case": {
            "mesh": context["mesh"],
            "final_time_s": args.final_time_s,
            "benchmark_is_convergence_evidence": False,
            "production_600s_runs_executed": False,
            "normal_case": str(context["normal_case"]),
            "minimal_case": str(context["minimal_case"]),
            "frozen_terrain_sha256": file_sha256(context["minimal_case"] / "outputs/terrain/conditioned-terrain.tif"),
            "frozen_source_sha256": file_sha256(context["minimal_case"] / "inputs/data/earthquake/tohoku_vertical_displacement.tif"),
        },
        "host": {
            "os_cpu_count": os.cpu_count(),
            "lscpu": command_output(["lscpu"]),
            "available_tools": {name: shutil.which(name) for name in ("perf", "pidstat", "time", "gprof")},
        },
        "timing_method": {
            "end_to_end": "subprocess wall clock around tsunami_r2d_case",
            "solver_loop": "opt-in TSUNAMI_R2D_TIMING_JSON instrumentation around solve_regional_model timestep loop",
            "regions": "opt-in scoped timers in residual, reduction, update, CFL, output, state-combination, and source-timestep code",
        },
        "runs": rows,
        "scaling": scaling,
        "equivalence": equivalence,
        "normal_vs_minimal_output": normal_io,
        "thread_local_buffer_audit": thread_local_buffer_audit(context["mesh"], [threads for _, threads in configured_thread_pairs(args)]),
        "best_configuration": {
            "recommended_thread_count": int(best["threads"]),
            "recommended_omp_places": "cores" if best["placement"] in ("close", "spread") else "default",
            "recommended_omp_proc_bind": best["placement"],
            "solver_loop_speedup": best["solver_loop_speedup"],
            "end_to_end_speedup": best["end_to_end_speedup"],
            "parallel_efficiency": best["parallel_efficiency_solver_loop"],
            "amdahl_effective_serial_fraction": amdahl_serial_fraction(float(best["solver_loop_speedup"]), int(best["threads"])),
        },
        "runtime_breakdown_representative_run": representative,
        "projected_fine_grid_runtimes": projections,
        "decision_gate": decision,
        "bottleneck_assessment": {
            "source_update_fraction_of_solver_loop": float(representative.get("timing_source_update_wall_s") or 0.0) / max(float(representative["solver_loop_wall_s"]), 1.0e-12),
            "boundary_application_fraction_of_solver_loop": float(representative.get("timing_boundary_application_wall_s") or 0.0) / max(float(representative["solver_loop_wall_s"]), 1.0e-12),
            "face_residual_fraction_of_solver_loop": float(representative.get("timing_face_residual_wall_s") or 0.0) / max(float(representative["solver_loop_wall_s"]), 1.0e-12),
            "residual_reduction_fraction_of_solver_loop": float(representative.get("timing_residual_reduction_wall_s") or 0.0) / max(float(representative["solver_loop_wall_s"]), 1.0e-12),
            "output_fraction": normal_io.get("f_io"),
            "interpretation": "classified from measured speedup, timing-region fractions, and active logical CPU estimate; no hardware counters were available if perf is absent",
        },
        "qualification": {
            "benchmark_status": "passed" if all(row["status"] == "passed" for row in rows) else "failed",
            "equivalence_status": "passed" if equivalence and all(item["status"] == "passed" for item in equivalence) else "failed",
            "openmp_runtime_thread_observation": "passed" if any((row.get("observed_openmp_threads") or 0) >= int(row["threads"]) for row in rows if row["variant"] == "openmp" and row["placement"] != "serial") else "failed",
            "observations_used": False,
            "physical_calibration_performed": False,
            "local3d_started": False,
            "cuda_implemented": False,
            "serial_solver_loop_baseline_s": serial_loop,
        },
    }


def command_run(args: argparse.Namespace) -> int:
    if args.overwrite:
        for child in ("logs", "timing"):
            path = args.external_root / child
            if path.exists():
                shutil.rmtree(path)
        for child in ("p1b_benchmark_summary.json", "p1b_runs.csv", "p1b_scaling.csv", "p1b_failed_runs.json"):
            path = args.external_root / child
            if path.exists():
                path.unlink()
    rows, equivalence, context = run_matrix(args)
    summary = build_summary(args, rows, equivalence, context)
    scaling = summary["scaling"]
    docs_root = args.docs_root
    figure_root = args.figure_root
    write_json(args.external_root / "p1b_benchmark_summary.json", summary)
    run_fields = sorted({key for row in rows for key in row})
    write_csv(args.external_root / "p1b_runs.csv", run_fields, rows)
    write_csv(args.external_root / "p1b_scaling.csv", list(scaling[0].keys()), scaling)
    write_json(docs_root / "regional2d_cpu_parallel_p1b_benchmark.json", summary)
    write_csv(docs_root / "regional2d_cpu_parallel_p1b_scaling.csv", list(scaling[0].keys()), scaling)
    write_json(docs_root / "regional2d_cpu_parallel_p1b_equivalence.json", {"study_id": STUDY_ID, "equivalence": equivalence})
    write_line_svg(figure_root / "c1a_p1b_solver_loop_speedup.svg", scaling, "solver_loop_speedup", "P1B Solver-Loop Speedup vs Threads", "Solver-loop speedup")
    write_line_svg(figure_root / "c1a_p1b_end_to_end_speedup.svg", scaling, "end_to_end_speedup", "P1B End-to-End Speedup vs Threads", "End-to-end speedup")
    write_line_svg(figure_root / "c1a_p1b_parallel_efficiency.svg", scaling, "parallel_efficiency_solver_loop", "P1B Parallel Efficiency vs Threads", "Parallel efficiency")
    write_breakdown_svg(figure_root / "c1a_p1b_runtime_breakdown.svg", summary["runtime_breakdown_representative_run"])
    write_projection_svg(figure_root / "c1a_p1b_projected_convergence_runtime.svg", summary["projected_fine_grid_runtimes"])
    write_json(
        figure_root / "c1a_p1b_figure_manifest.json",
        {
            "schema": {"name": "tsunami.c1a_p1b_figure_manifest", "version": "1.0.0"},
            "generated_at_utc": utc_now(),
            "study_id": STUDY_ID,
            "figures": [
                "c1a_p1b_solver_loop_speedup.svg",
                "c1a_p1b_end_to_end_speedup.svg",
                "c1a_p1b_parallel_efficiency.svg",
                "c1a_p1b_runtime_breakdown.svg",
                "c1a_p1b_projected_convergence_runtime.svg",
            ],
            "source": "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/regional2d_cpu_parallel_p1b_benchmark.json",
        },
    )
    print(json.dumps(summary["best_configuration"], indent=2, sort_keys=True))
    print(json.dumps(summary["decision_gate"], indent=2, sort_keys=True))
    return 0 if summary["qualification"]["benchmark_status"] == "passed" and summary["qualification"]["equivalence_status"] == "passed" else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--r4-root", type=Path, default=DEFAULT_R4_ROOT)
    parser.add_argument("--serial-binary", type=Path, default=repo_root() / DEFAULT_SERIAL_BINARY)
    parser.add_argument("--openmp-binary", type=Path, default=repo_root() / DEFAULT_OPENMP_BINARY)
    parser.add_argument("--docs-root", type=Path, default=repo_root() / DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=repo_root() / DEFAULT_FIGURE_ROOT)
    parser.add_argument("--final-time-s", type=float, default=14.0)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 2, 4, 8, 12, 16])
    parser.add_argument("--equivalence-threads", type=int, nargs="+", default=[1, 2, 4, 8, 16])
    parser.add_argument("--placements", choices=["close", "spread", "default"], nargs="+", default=["close", "spread", "default"])
    parser.add_argument(
        "--placement-thread-map",
        nargs="+",
        help="Optional selective matrix such as close:1,2,4,8,12,16 spread:8,16 default:8.",
    )
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--normal-repeats", type=int, default=1)
    parser.add_argument("--overwrite", action="store_true")
    parser.set_defaults(func=command_run)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.final_time_s <= 0.0 or not math.isfinite(args.final_time_s):
        parser.error("--final-time-s must be positive and finite")
    if args.repeats <= 0 or args.normal_repeats <= 0:
        parser.error("repeat counts must be positive")
    for binary in (args.serial_binary, args.openmp_binary):
        if not binary.is_file():
            parser.error(f"binary does not exist: {binary}")
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
