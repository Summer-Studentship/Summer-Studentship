#!/usr/bin/env python3
"""Benchmark and verify OpenMP Regional2D execution on the frozen C1A-R4 case."""

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
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import c1a_convergence as c1a
import c1a_r4_execute_frozen_terrain as r4


STUDY_ID = "regional2d-cpu-parallel-p1"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-cpu-parallel-p1")
DEFAULT_R4_ROOT = r4.DEFAULT_EXTERNAL_ROOT
DEFAULT_SERIAL_BINARY = Path("build/linux-gcc-crs-release/apps/r2d_case/tsunami_r2d_case")
DEFAULT_OPENMP_BINARY = Path("build/linux-gcc-crs-openmp-release/apps/r2d_case/tsunami_r2d_case")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
NUMERIC_ABS_TOL = 1.0e-9
NUMERIC_REL_TOL = 1.0e-9
COLORS = {
    "serial": "#4c78a8",
    "openmp": "#f58518",
    "efficiency": "#54a24b",
    "grid": "#d0d7de",
    "axis": "#24292f",
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


def optional_tool(name: str) -> str | None:
    found = shutil.which(name)
    return str(Path(found)) if found else None


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


def prepare_case(external_root: Path, r4_root: Path, final_time_s: float, *, overwrite: bool) -> Path:
    case_root = external_root / "case"
    if overwrite and case_root.exists():
        shutil.rmtree(case_root)
    r4.copy_required_g6_case_inputs(r4.DEFAULT_G6_ROOT / "case", case_root)
    mesh_source = r4_root / "case/meshes/r4-h1000.msh"
    if not mesh_source.is_file():
        raise RuntimeError(f"missing frozen R4 h1000 mesh: {mesh_source}")
    mesh_destination = case_root / "meshes/r4-h1000.msh"
    mesh_destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(mesh_source, mesh_destination)
    geo_source = r4_root / "case/meshes/kamaishi-regional.geo"
    if geo_source.is_file():
        shutil.copy2(geo_source, case_root / "meshes/kamaishi-regional.geo")
    r4.set_case_final_time(case_root, final_time_s)
    case_path = case_root / "case.json"
    case = read_json(case_path)
    case.setdefault("outputs", {})
    case["outputs"]["snapshot_interval_s"] = min(float(final_time_s), float(case["outputs"].get("snapshot_interval_s") or final_time_s))
    case_path.write_text(json.dumps(case, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return case_root


def command_for_run(case_root: Path, run_id: str) -> list[str]:
    return [
        "--case-root",
        str(case_root),
        "--terrain-record",
        "manifests/terrain/conditioned-terrain.json",
        "--mesh",
        "meshes/r4-h1000.msh",
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


def parse_stdout(stdout: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for line in stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            parsed[key.strip()] = value.strip()
    return parsed


def run_regional(
    binary: Path,
    case_root: Path,
    logs_root: Path,
    *,
    variant: str,
    threads: int,
    repeat: int,
) -> dict[str, Any]:
    run_id = f"p1-{variant}-t{threads}-r{repeat}"
    env = {
        "OMP_NUM_THREADS": str(threads),
        "OMP_PROC_BIND": "close",
        "OMP_PLACES": "cores",
    }
    result = run_command(
        [str(binary), *command_for_run(case_root, run_id)],
        cwd=repo_root(),
        env=env,
        log_path=logs_root / f"{run_id}.log",
    )
    parsed = parse_stdout(result.stdout)
    output_dir = case_root / "runs" / run_id / "outputs/regional2d"
    row = {
        "variant": variant,
        "threads": threads,
        "repeat": repeat,
        "run_id": run_id,
        "status": "passed" if result.returncode == 0 else "failed",
        "returncode": result.returncode,
        "wall_clock_s": result.wall_clock_s,
        "cpu_time_s": result.cpu_time_s,
        "peak_memory_kb": result.peak_memory_kb,
        "binary": str(binary),
        "binary_sha256": file_sha256(binary),
        "output_dir": str(output_dir) if output_dir.exists() else None,
        "steps": int(parsed["steps"]) if parsed.get("steps") else None,
        "final_time_s": float(parsed["final_time"]) if parsed.get("final_time") else None,
        "stdout_tail": result.stdout[-2000:] if result.returncode != 0 else "",
    }
    if output_dir.exists():
        row["diagnostics_sha256"] = file_sha256(output_dir / "diagnostics.csv")
        row["snapshots_sha256"] = file_sha256(output_dir / "snapshots.csv")
        coupling = output_dir / "coupling" / r4.SECTION_ID
        row["coupling_samples_sha256"] = file_sha256(coupling / "samples.csv") if (coupling / "samples.csv").is_file() else None
        row["coupling_history_sha256"] = file_sha256(coupling / "history.csv") if (coupling / "history.csv").is_file() else None
    return row


def numeric_columns(rows: Sequence[dict[str, str]]) -> list[str]:
    columns: list[str] = []
    if not rows:
        return columns
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
        return {
            "status": "failed",
            "reason": "row_count_mismatch",
            "reference_rows": len(ref_rows),
            "candidate_rows": len(cand_rows),
        }
    columns = sorted(set(numeric_columns(ref_rows)).intersection(numeric_columns(cand_rows)))
    max_abs = 0.0
    max_rel = 0.0
    sq_sum = 0.0
    count = 0
    worst: dict[str, Any] | None = None
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
    rms = math.sqrt(sq_sum / count) if count else 0.0
    passed = max_abs <= NUMERIC_ABS_TOL or max_rel <= NUMERIC_REL_TOL
    return {
        "status": "passed" if passed else "failed",
        "row_count": len(ref_rows),
        "numeric_column_count": len(columns),
        "max_abs_difference": max_abs,
        "max_relative_difference": max_rel,
        "rms_difference": rms,
        "worst_difference": worst,
        "abs_tolerance": NUMERIC_ABS_TOL,
        "relative_tolerance": NUMERIC_REL_TOL,
    }


def compare_outputs(case_root: Path, reference_run: str, candidate_run: str) -> dict[str, Any]:
    reference = case_root / "runs" / reference_run / "outputs/regional2d"
    candidate = case_root / "runs" / candidate_run / "outputs/regional2d"
    files = {
        "diagnostics": ("diagnostics.csv",),
        "snapshots": ("snapshots.csv",),
        "coupling_samples": ("coupling", r4.SECTION_ID, "samples.csv"),
        "coupling_history": ("coupling", r4.SECTION_ID, "history.csv"),
    }
    comparisons = {
        label: compare_csv_files(reference.joinpath(*parts), candidate.joinpath(*parts))
        for label, parts in files.items()
        if reference.joinpath(*parts).is_file() and candidate.joinpath(*parts).is_file()
    }
    return {
        "reference_run": reference_run,
        "candidate_run": candidate_run,
        "status": "passed" if all(item["status"] == "passed" for item in comparisons.values()) else "failed",
        "comparisons": comparisons,
    }


def medians(rows: Sequence[dict[str, Any]]) -> dict[tuple[str, int], dict[str, Any]]:
    result: dict[tuple[str, int], dict[str, Any]] = {}
    groups: dict[tuple[str, int], list[dict[str, Any]]] = {}
    for row in rows:
        if row["status"] == "passed":
            groups.setdefault((row["variant"], int(row["threads"])), []).append(row)
    for key, values in groups.items():
        result[key] = {
            "wall_clock_s": statistics.median(float(row["wall_clock_s"]) for row in values),
            "cpu_time_s": statistics.median(float(row["cpu_time_s"]) for row in values),
            "peak_memory_kb": max(int(row["peak_memory_kb"]) for row in values),
            "repeat_count": len(values),
            "steps": values[0].get("steps"),
        }
    return result


def scaling_rows(benchmark_rows: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped = medians(benchmark_rows)
    serial = grouped.get(("serial", 1))
    if serial is None:
        raise RuntimeError("missing serial one-thread timing")
    baseline_wall = float(serial["wall_clock_s"])
    rows = []
    for (variant, threads), payload in sorted(grouped.items(), key=lambda item: (item[0][0], item[0][1])):
        wall = float(payload["wall_clock_s"])
        speedup = baseline_wall / wall if wall > 0.0 else None
        rows.append(
            {
                "variant": variant,
                "threads": threads,
                "median_wall_clock_s": wall,
                "median_cpu_time_s": payload["cpu_time_s"],
                "peak_memory_kb": payload["peak_memory_kb"],
                "repeat_count": payload["repeat_count"],
                "steps": payload["steps"],
                "speedup_vs_serial_binary_t1": speedup,
                "parallel_efficiency_vs_serial_binary_t1": speedup / threads if speedup is not None and threads else None,
            }
        )
    return rows


def svg_polyline(points: Sequence[tuple[float, float]]) -> str:
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in points)


def write_speedup_svg(path: Path, rows: Sequence[dict[str, Any]], *, metric: str) -> None:
    openmp = [row for row in rows if row["variant"] == "openmp" and row.get(metric) not in (None, "")]
    serial = [row for row in rows if row["variant"] == "serial"]
    values = [float(row[metric]) for row in openmp]
    if metric == "speedup_vs_serial_binary_t1" and serial:
        values.append(1.0)
    threads = [int(row["threads"]) for row in openmp] or [1]
    width, height = 760, 420
    left, top, right, bottom = 70, 32, 28, 62
    xmin, xmax = 1.0, float(max(threads))
    ymax = max(values + [1.0]) * 1.15
    ymax = max(ymax, 1.05)

    def xmap(value: float) -> float:
        return left + (value - xmin) / max(xmax - xmin, 1.0) * (width - left - right)

    def ymap(value: float) -> float:
        return height - bottom - value / ymax * (height - top - bottom)

    label = "Speedup vs serial t1" if metric == "speedup_vs_serial_binary_t1" else "Parallel efficiency"
    points = [(xmap(float(row["threads"])), ymap(float(row[metric]))) for row in openmp]
    ideal = [(xmap(1.0), ymap(1.0))]
    if metric == "speedup_vs_serial_binary_t1":
        ideal.append((xmap(xmax), ymap(xmax)))
    else:
        ideal.append((xmap(xmax), ymap(1.0)))
    x_ticks = sorted(set([1, *threads]))
    y_ticks = [0.0, ymax * 0.25, ymax * 0.5, ymax * 0.75, ymax]
    grid = []
    for tick in x_ticks:
        x = xmap(float(tick))
        grid.append(f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{height-bottom}" stroke="{COLORS["grid"]}" stroke-width="1"/>')
        grid.append(f'<text x="{x:.2f}" y="{height-24}" text-anchor="middle" font-size="12">{tick}</text>')
    for tick in y_ticks:
        y = ymap(tick)
        grid.append(f'<line x1="{left}" y1="{y:.2f}" x2="{width-right}" y2="{y:.2f}" stroke="{COLORS["grid"]}" stroke-width="1"/>')
        grid.append(f'<text x="{left-10}" y="{y+4:.2f}" text-anchor="end" font-size="12">{tick:.2g}</text>')
    circles = [
        f'<circle cx="{x:.2f}" cy="{y:.2f}" r="4.5" fill="{COLORS["openmp"]}"/>'
        for x, y in points
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "\n".join(
            [
                '<svg xmlns="http://www.w3.org/2000/svg" width="760" height="420" viewBox="0 0 760 420">',
                '<rect width="760" height="420" fill="white"/>',
                f'<text x="{left}" y="22" font-size="16" font-family="sans-serif" fill="{COLORS["axis"]}">{label}: C1A-P1 frozen h1000</text>',
                *grid,
                f'<line x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}" stroke="{COLORS["axis"]}" stroke-width="1.5"/>',
                f'<line x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}" stroke="{COLORS["axis"]}" stroke-width="1.5"/>',
                f'<polyline points="{svg_polyline(ideal)}" fill="none" stroke="{COLORS["grid"]}" stroke-width="2" stroke-dasharray="6 4"/>',
                f'<polyline points="{svg_polyline(points)}" fill="none" stroke="{COLORS["openmp"]}" stroke-width="2.5"/>',
                *circles,
                f'<text x="{width/2:.2f}" y="{height-8}" text-anchor="middle" font-size="13" font-family="sans-serif">OpenMP threads</text>',
                f'<text x="18" y="{height/2:.2f}" text-anchor="middle" font-size="13" font-family="sans-serif" transform="rotate(-90 18 {height/2:.2f})">{label}</text>',
                "</svg>",
            ]
        )
        + "\n",
        encoding="utf-8",
    )


def repo_relative(path: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root()).as_posix()
    except ValueError:
        return str(path)


def build_summary(args: argparse.Namespace, benchmark_rows: list[dict[str, Any]], equivalence: list[dict[str, Any]]) -> dict[str, Any]:
    case_root = args.external_root / "case"
    mesh_path = case_root / "meshes/r4-h1000.msh"
    mesh = r4.parse_msh_triangles(mesh_path)
    scaling = scaling_rows(benchmark_rows)
    serial_rows = [row for row in scaling if row["variant"] == "serial"]
    openmp_rows = [row for row in scaling if row["variant"] == "openmp"]
    best = min(openmp_rows, key=lambda row: float(row["median_wall_clock_s"])) if openmp_rows else None
    tools = {name: optional_tool(name) for name in ("perf", "gprof", "valgrind", "hyperfine")}
    return {
        "schema": {"name": "tsunami.c1a_p1_parallel_benchmark", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "branch": subprocess.run(["git", "branch", "--show-current"], cwd=repo_root(), text=True, stdout=subprocess.PIPE).stdout.strip(),
        "git_head": subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo_root(), text=True, stdout=subprocess.PIPE).stdout.strip(),
        "case": {
            "external_root": str(args.external_root),
            "case_root": str(case_root),
            "source_r4_root": str(args.r4_root),
            "mesh": mesh,
            "final_time_s": args.final_time_s,
            "forbidden_ladder_not_run": True,
            "frozen_terrain_sha256": file_sha256(case_root / "outputs/terrain/conditioned-terrain.tif"),
            "frozen_source_sha256": file_sha256(case_root / "inputs/data/earthquake/tohoku_vertical_displacement.tif"),
        },
        "implementation": {
            "parallel_backend": "OpenMP when TSUNAMI_ENABLE_OPENMP=ON",
            "parallelised_kernel": "well-balanced Regional2D Rusanov face residual",
            "race_avoidance": "thread-local residual, spectral-radius, and outgoing-mass buffers reduced in deterministic cell-major/thread-major order",
            "serial_authority": "TSUNAMI_ENABLE_OPENMP=OFF serial release binary",
            "small_mesh_path": "serial face loop retained below the OpenMP activation threshold",
        },
        "binaries": {
            "serial": {"path": str(args.serial_binary), "sha256": file_sha256(args.serial_binary)},
            "openmp": {"path": str(args.openmp_binary), "sha256": file_sha256(args.openmp_binary)},
        },
        "profiling": {
            "available_external_tools": tools,
            "used_method": "subprocess wall-clock, child CPU time, peak RSS, solver step count, and output equivalence checks",
            "component_breakdown": "not_available_no_instrumented_component_timer_in_current_solver",
            "serial_baseline": serial_rows[0] if serial_rows else None,
            "best_openmp": best,
        },
        "benchmark_rows": benchmark_rows,
        "scaling_rows": scaling,
        "equivalence": equivalence,
        "qualification": {
            "equivalence_status": "passed" if all(item["status"] == "passed" for item in equivalence) else "failed",
            "benchmark_status": "passed" if all(row["status"] == "passed" for row in benchmark_rows) else "failed",
            "observations_used": False,
            "physical_calibration_performed": False,
            "local3d_started": False,
        },
    }


def command_run(args: argparse.Namespace) -> int:
    case_root = prepare_case(args.external_root, args.r4_root, args.final_time_s, overwrite=args.overwrite)
    logs_root = args.external_root / "logs"
    benchmark_rows: list[dict[str, Any]] = []
    for repeat in range(1, args.repeats + 1):
        benchmark_rows.append(run_regional(args.serial_binary, case_root, logs_root, variant="serial", threads=1, repeat=repeat))
    for threads in args.threads:
        for repeat in range(1, args.repeats + 1):
            benchmark_rows.append(run_regional(args.openmp_binary, case_root, logs_root, variant="openmp", threads=int(threads), repeat=repeat))
    failed = [row for row in benchmark_rows if row["status"] != "passed"]
    if failed:
        write_json(args.external_root / "parallel_benchmark_failed.json", {"runs": benchmark_rows})
        return 1
    reference_run = "p1-serial-t1-r1"
    equivalence = [
        compare_outputs(case_root, reference_run, f"p1-openmp-t{int(threads)}-r1")
        for threads in args.threads
    ]
    summary = build_summary(args, benchmark_rows, equivalence)
    scaling = summary["scaling_rows"]
    write_json(args.external_root / "parallel_benchmark_summary.json", summary)
    write_csv(args.external_root / "parallel_benchmark_runs.csv", sorted(benchmark_rows[0].keys()), benchmark_rows)
    write_csv(
        args.external_root / "parallel_scaling.csv",
        [
            "variant",
            "threads",
            "median_wall_clock_s",
            "median_cpu_time_s",
            "peak_memory_kb",
            "repeat_count",
            "steps",
            "speedup_vs_serial_binary_t1",
            "parallel_efficiency_vs_serial_binary_t1",
        ],
        scaling,
    )
    write_json(args.docs_root / "regional2d_cpu_parallel_p1_benchmark.json", summary)
    write_csv(
        args.docs_root / "regional2d_cpu_parallel_p1_scaling.csv",
        [
            "variant",
            "threads",
            "median_wall_clock_s",
            "median_cpu_time_s",
            "peak_memory_kb",
            "repeat_count",
            "steps",
            "speedup_vs_serial_binary_t1",
            "parallel_efficiency_vs_serial_binary_t1",
        ],
        scaling,
    )
    write_speedup_svg(args.figure_root / "c1a_p1_parallel_speedup.svg", scaling, metric="speedup_vs_serial_binary_t1")
    write_speedup_svg(args.figure_root / "c1a_p1_parallel_efficiency.svg", scaling, metric="parallel_efficiency_vs_serial_binary_t1")
    write_json(
        args.figure_root / "c1a_p1_parallel_figure_manifest.json",
        {
            "schema": {"name": "tsunami.c1a_p1_parallel_figure_manifest", "version": "1.0.0"},
            "generated_at_utc": utc_now(),
            "study_id": STUDY_ID,
            "figures": [
                {
                    "path": repo_relative(args.figure_root / "c1a_p1_parallel_speedup.svg"),
                    "source": repo_relative(args.docs_root / "regional2d_cpu_parallel_p1_scaling.csv"),
                },
                {
                    "path": repo_relative(args.figure_root / "c1a_p1_parallel_efficiency.svg"),
                    "source": repo_relative(args.docs_root / "regional2d_cpu_parallel_p1_scaling.csv"),
                },
            ],
        },
    )
    print(json.dumps(summary["qualification"], indent=2, sort_keys=True))
    return 0 if summary["qualification"]["equivalence_status"] == "passed" else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--r4-root", type=Path, default=DEFAULT_R4_ROOT)
    parser.add_argument("--serial-binary", type=Path, default=repo_root() / DEFAULT_SERIAL_BINARY)
    parser.add_argument("--openmp-binary", type=Path, default=repo_root() / DEFAULT_OPENMP_BINARY)
    parser.add_argument("--docs-root", type=Path, default=repo_root() / DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=repo_root() / DEFAULT_FIGURE_ROOT)
    parser.add_argument("--final-time-s", type=float, default=1.0)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 2, 4, 8])
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--overwrite", action="store_true")
    parser.set_defaults(func=command_run)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.final_time_s <= 0.0 or not math.isfinite(args.final_time_s):
        parser.error("--final-time-s must be positive and finite")
    if args.repeats <= 0:
        parser.error("--repeats must be positive")
    if any(threads <= 0 for threads in args.threads):
        parser.error("--threads entries must be positive")
    for binary in (args.serial_binary, args.openmp_binary):
        if not binary.is_file():
            parser.error(f"binary does not exist: {binary}")
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
