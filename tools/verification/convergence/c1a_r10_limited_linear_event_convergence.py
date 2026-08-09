#!/usr/bin/env python3
"""Execute and analyse C1A-R10 limited-linear Tohoku event convergence."""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import c1a_convergence as c1a
import c1a_r4_execute_frozen_terrain as r4
import c1a_r5_finer_spatial_convergence as r5


STUDY_ID = "regional2d-limited-linear-event-r10"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-limited-linear-event-r10")
DEFAULT_R5_ROOT = r5.DEFAULT_EXTERNAL_ROOT
DEFAULT_G6_ROOT = r4.DEFAULT_G6_ROOT
DEFAULT_R2D_BINARY = Path("build/linux-gcc-crs-release/apps/r2d_case/tsunami_r2d_case")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
TARGETS = (600.0, 500.0, 400.0)
EXPECTED_MESH_SHA256 = {
    "h600": "3bda1d2b4e9d0abc6c35fa4afde4fd885b8dcf9e99947eab1f50b07a99a6faa3",
    "h500": "c1140e32db3baf8625d6bb172acc397c2c6046b743ac8ef789701dac05526cf3",
    "h400": "6431004341b17656f8be10f22135bff5d427fc782529d099be49252fcde20982",
}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_state(external_root: Path, state: str, **extra: Any) -> None:
    payload = {
        "schema": {"name": "tsunami.c1a_r10_execution_state", "version": "1.0.0"},
        "updated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "state": state,
        **extra,
    }
    write_json(external_root / "execution_state.json", payload)


def command_output(command: Sequence[str]) -> str | None:
    try:
        completed = subprocess.run(list(command), cwd=repo_root(), text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError:
        return None
    return completed.stdout.strip() if completed.returncode == 0 else None


def solver_args(case_root: Path, mesh_path: Path, run_id: str) -> list[str]:
    return [
        *r5.solver_args(case_root, mesh_path, run_id),
        "--reconstruction",
        "limited_linear",
    ]


def prepare_case(external_root: Path, g6_root: Path, r5_root: Path) -> tuple[Path, dict[str, Any], list[dict[str, Any]]]:
    case_root = external_root / "case"
    if not (case_root / "case.json").is_file():
        r4.copy_required_g6_case_inputs(g6_root / "case", case_root)
        (case_root / "meshes").mkdir(parents=True, exist_ok=True)
        shutil.copy2(r5_root / "case/meshes/kamaishi-regional.geo", case_root / "meshes/kamaishi-regional.geo")
    r4.set_case_final_time(case_root, r5.FULL_TIME_S)
    case_record = r4.fail_closed_invariance(case_root)

    rows: list[dict[str, Any]] = []
    records: list[dict[str, Any]] = []
    for target in TARGETS:
        level_id = f"h{target:g}"
        source_mesh = r5_root / "case" / "meshes" / f"r4-{level_id}.msh"
        if not source_mesh.is_file():
            raise RuntimeError(f"historical mesh artifact is missing: {source_mesh}")
        mesh_path = case_root / "meshes" / f"r4-{level_id}.msh"
        if not mesh_path.exists():
            shutil.copy2(source_mesh, mesh_path)
        mesh = r4.parse_msh_triangles(mesh_path)
        if mesh["mesh_sha256"] != EXPECTED_MESH_SHA256[level_id]:
            raise RuntimeError(f"{level_id} mesh hash mismatch: {mesh['mesh_sha256']}")
        record = r4.adapter_level_record(case_record, target, mesh_path)
        record["mesh"] = mesh
        record["actual_characteristic_mesh_size"]["value_m"] = mesh["actual_characteristic_h_m"]
        records.append(record)
        rows.append(
            {
                "level_id": level_id,
                "requested_solver_target_m": target,
                "mesh_path": str(mesh_path),
                "mesh_sha256": mesh["mesh_sha256"],
                "actual_characteristic_h_m": mesh["actual_characteristic_h_m"],
                "active_cells": mesh["active_cells"],
                "total_cells": mesh["total_cells"],
                "terrain_sha256": case_record["terrain"]["sha256"],
                "source_sha256": case_record["source"]["sha256"],
                "reconstruction": "limited_linear",
            }
        )
    c1a.assert_regional_frozen_family_invariance(records)
    return case_root, case_record, rows


def preflight(args: argparse.Namespace) -> dict[str, Any]:
    args.external_root.mkdir(parents=True, exist_ok=True)
    for name in ("spatial", "temporal", "diagnostics", "logs"):
        (args.external_root / name).mkdir(parents=True, exist_ok=True)
    write_state(args.external_root, "started")
    case_root, case_record, meshes = prepare_case(args.external_root, args.g6_root, args.r5_root)
    binary = args.r2d_binary
    if not binary.is_file():
        raise RuntimeError(f"R10 executable is missing: {binary}")
    help_text = command_output([str(binary), "--help"]) or ""
    if "--reconstruction" not in help_text:
        raise RuntimeError("R10 executable does not expose --reconstruction")
    disk = shutil.disk_usage(args.external_root)
    payload = {
        "schema": {"name": "tsunami.c1a_r10_preflight", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "git_head": command_output(["git", "rev-parse", "HEAD"]),
        "branch": command_output(["git", "branch", "--show-current"]),
        "case_root": str(case_root),
        "binary": {
            "path": str(binary),
            "sha256": r4.file_sha256(binary),
            "compiler": command_output(["c++", "--version"]),
            "build_type": "Release",
        },
        "reconstruction": "limited_linear",
        "final_time_s": r5.FULL_TIME_S,
        "forcing_window_s": list(r4.FORCING_WINDOW_S),
        "case_record": case_record,
        "mesh_artifacts": meshes,
        "physical_invariance": {
            "status": "passed",
            "terrain_sha256": case_record["terrain"]["sha256"],
            "source_sha256": case_record["source"]["sha256"],
            "physical_configuration_sha256": case_record["physical_configuration_sha256"],
            "domain_sha256": case_record["domain_sha256"],
            "coupling_section_sha256": case_record["coupling_section_sha256"],
        },
        "disk": {
            "path": str(args.external_root),
            "free_bytes": disk.free,
            "total_bytes": disk.total,
            "status": "passed" if disk.free > args.minimum_free_bytes else "failed",
        },
        "exact_historical_meshes_reused": True,
    }
    if payload["disk"]["status"] != "passed":
        raise RuntimeError("insufficient disk space for R10 event runs")
    write_json(args.external_root / "preflight.json", payload)
    r5.write_csv(args.external_root / "mesh_preflight.csv", list(meshes[0]), meshes)
    write_state(args.external_root, "preflight_complete", preflight=payload)
    return payload


def run_one(command: Sequence[str], *, log_path: Path, env: dict[str, str]) -> dict[str, Any]:
    started = time.monotonic()
    launch = utc_now()
    completed = subprocess.run(list(command), cwd=repo_root(), env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    finished = utc_now()
    wall = time.monotonic() - started
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text("command=" + " ".join(command) + "\n" + completed.stdout, encoding="utf-8")
    parsed = r5.parse_runner_stdout(completed.stdout)
    return {
        "returncode": completed.returncode,
        "launch_timestamp_utc": launch,
        "completion_timestamp_utc": finished,
        "wall_clock_s": wall,
        "stdout": completed.stdout,
        "parsed_stdout": parsed,
    }


def smoke(args: argparse.Namespace) -> dict[str, Any]:
    pre = preflight(args)
    smoke_root = args.external_root / "smoke"
    case_root = smoke_root / "case"
    if case_root.exists():
        shutil.rmtree(case_root)
    r4.copy_required_g6_case_inputs(args.g6_root / "case", case_root)
    (case_root / "meshes").mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.r5_root / "case/meshes/kamaishi-regional.geo", case_root / "meshes/kamaishi-regional.geo")
    shutil.copy2(Path(pre["mesh_artifacts"][0]["mesh_path"]), case_root / "meshes/r4-h600.msh")
    r4.set_case_final_time(case_root, args.smoke_time_s)
    r4.fail_closed_invariance(case_root)
    run_id = "r10-smoke-h600"
    env = {**os.environ, "OMP_NUM_THREADS": "1"}
    result = run_one([str(args.r2d_binary), *solver_args(case_root, case_root / "meshes/r4-h600.msh", run_id)], log_path=args.external_root / "logs" / f"{run_id}.log", env=env)
    output_dir = case_root / "runs" / run_id / "outputs/regional2d"
    passed = (
        result["returncode"] == 0
        and result["parsed_stdout"].get("reconstruction") == "limited_linear"
        and (output_dir / "diagnostics.csv").is_file()
        and (output_dir / "coupling" / r4.SECTION_ID / "samples.csv").is_file()
    )
    payload = {
        "schema": {"name": "tsunami.c1a_r10_smoke", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "status": "passed" if passed else "failed",
        "level_id": "h600",
        "run_id": run_id,
        "final_time_s": args.smoke_time_s,
        "result": {key: value for key, value in result.items() if key != "stdout"},
        "output_dir": str(output_dir),
        "finite_state": passed,
        "coupling_export": (output_dir / "coupling" / r4.SECTION_ID / "samples.csv").is_file(),
    }
    write_json(args.external_root / "smoke_test.json", payload)
    if not passed:
        raise RuntimeError("R10 limited-linear smoke test failed")
    write_state(args.external_root, "preflight_complete", smoke_test=payload)
    return payload


def completed_run_valid(path: Path) -> bool:
    if not path.is_file():
        return False
    run = read_json(path)
    if run.get("status") != "passed" or run.get("reconstruction") != "limited_linear":
        return False
    output = Path(run["output_dir"])
    if not (output / "diagnostics.csv").is_file() or not (output / "coupling" / r4.SECTION_ID / "samples.csv").is_file():
        return False
    stats = r4.timestep_stats_from_diagnostics(output)
    return abs(float(stats.get("final_diagnostic_time_s") or 0.0) - r5.FULL_TIME_S) < 1.0e-9


def run_full(args: argparse.Namespace) -> dict[str, Any]:
    pre = preflight(args)
    if not (args.external_root / "smoke_test.json").is_file():
        smoke(args)
    case_root = Path(pre["case_root"])
    slots = r5.parse_cpu_slots()
    if len(slots) < len(TARGETS):
        raise RuntimeError(f"not enough physical CPU slots: have {len(slots)}, need {len(TARGETS)}")
    pending = [target for target in TARGETS if not completed_run_valid(args.external_root / "spatial" / f"h{target:g}" / "run.json")]
    processes: list[dict[str, Any]] = []
    first_launch = utc_now()
    for index, target in enumerate(pending):
        level_id = f"h{target:g}"
        run_id = f"r10-limited-linear-{level_id}"
        mesh_path = case_root / "meshes" / f"r4-{level_id}.msh"
        cpu = slots[index]["cpu"]
        command = ["taskset", "-c", str(cpu), str(args.r2d_binary), *solver_args(case_root, mesh_path, run_id)]
        env = {**os.environ, "OMP_NUM_THREADS": "1"}
        proc = subprocess.Popen(command, cwd=repo_root(), env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        processes.append(
            {
                "target": target,
                "level_id": level_id,
                "run_id": run_id,
                "process": proc,
                "command": command,
                "assigned_cpu": cpu,
                "launch_timestamp_utc": utc_now(),
                "launch_monotonic_s": time.monotonic(),
                "completion_timestamp_utc": None,
                "completion_monotonic_s": None,
                "first_metrics": None,
                "last_metrics": None,
                "peak_rss_kb": 0,
            }
        )
        write_state(args.external_root, f"{level_id}_started")
    freq_samples: list[dict[str, float]] = []
    temp_samples: list[dict[str, float]] = []
    monitor_rows: list[dict[str, Any]] = []
    while any(item["process"].poll() is None for item in processes):
        alive = []
        aggregate_rss = 0
        for item in processes:
            if item["process"].poll() is not None:
                if item["completion_monotonic_s"] is None:
                    item["completion_monotonic_s"] = time.monotonic()
                    item["completion_timestamp_utc"] = utc_now()
                continue
            metrics = r5.read_proc_metrics(item["process"].pid)
            if metrics:
                item["first_metrics"] = item["first_metrics"] or metrics
                item["last_metrics"] = metrics
                item["peak_rss_kb"] = max(int(item["peak_rss_kb"]), int(metrics["rss_kb"]))
                aggregate_rss += int(metrics["rss_kb"])
            alive.append(item["level_id"])
        freq = r5.read_frequency_khz()
        temp = r5.read_temperature_c()
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
    r5.write_csv(args.external_root / "resource_monitor.csv", ["timestamp_utc", "alive_levels", "aggregate_rss_kb", "mean_frequency_khz", "max_temperature_c"], monitor_rows)
    for item in processes:
        stdout, _ = item["process"].communicate()
        if item["completion_monotonic_s"] is None:
            item["completion_monotonic_s"] = time.monotonic()
            item["completion_timestamp_utc"] = utc_now()
        parsed = r5.parse_runner_stdout(stdout)
        output_dir = case_root / "runs" / item["run_id"] / "outputs/regional2d"
        first = item["first_metrics"] or {"cpu_time_s": 0.0}
        last = item["last_metrics"] or first
        run = {
            "status": "passed" if item["process"].returncode == 0 and parsed.get("reconstruction") == "limited_linear" else "failed",
            "run_id": item["run_id"],
            "returncode": item["process"].returncode,
            "reconstruction": parsed.get("reconstruction"),
            "requested_solver_target_m": item["target"],
            "output_dir": str(output_dir),
            "mesh": r4.parse_msh_triangles(case_root / "meshes" / f"r4-{item['level_id']}.msh"),
            "launch_timestamp_utc": item["launch_timestamp_utc"],
            "completion_timestamp_utc": item["completion_timestamp_utc"],
            "cpu_affinity": {"taskset_cpu": item["assigned_cpu"], "policy": "P2 distinct physical CPU, OMP_NUM_THREADS=1"},
            "resource_usage": {
                "wall_clock_s": float(item["completion_monotonic_s"]) - float(item["launch_monotonic_s"]),
                "cpu_time_s": max(0.0, float(last["cpu_time_s"]) - float(first["cpu_time_s"])),
                "peak_memory_kb": item["peak_rss_kb"],
            },
            "stdout": stdout.strip(),
            "case_invariance": {
                "terrain_sha256": pre["case_record"]["terrain"]["sha256"],
                "source_sha256": pre["case_record"]["source"]["sha256"],
                "physical_configuration_sha256": pre["case_record"]["physical_configuration_sha256"],
                "domain_sha256": pre["case_record"]["domain_sha256"],
                "coupling_section_sha256": pre["case_record"]["coupling_section_sha256"],
            },
        }
        if run["status"] == "passed":
            run["timestep"] = r4.timestep_stats_from_diagnostics(output_dir)
            write_state(args.external_root, f"{item['level_id']}_complete")
        else:
            run["log_tail"] = stdout[-4000:]
        write_json(args.external_root / "spatial" / item["level_id"] / "run.json", run)
    runs = [read_json(args.external_root / "spatial" / f"h{target:g}" / "run.json") for target in TARGETS]
    makespan = max((float(run["resource_usage"]["wall_clock_s"]) for run in runs), default=0.0)
    aggregate = sum(float(run["resource_usage"]["wall_clock_s"]) for run in runs)
    summary = {
        "schema": {"name": "tsunami.c1a_r10_limited_linear_spatial_runs", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "first_launch_timestamp_utc": first_launch,
        "last_completion_timestamp_utc": max((run.get("completion_timestamp_utc") or utc_now()) for run in runs),
        "concurrent_makespan_s": makespan,
        "sequential_equivalent_compute_s": aggregate,
        "effective_ensemble_speedup": aggregate / makespan if makespan > 0.0 else None,
        "frequency_khz": r5.summarise_samples(freq_samples, ("min_khz", "mean_khz", "max_khz")),
        "temperature_c": r5.summarise_samples(temp_samples, ("min_c", "mean_c", "max_c")),
        "runs": runs,
    }
    write_json(args.external_root / "spatial_run_summary.json", summary)
    return summary


def sequence_class(values: Sequence[float]) -> str:
    if len(values) < 2:
        return "insufficient_evidence"
    if all(b <= a for a, b in zip(values, values[1:])):
        return "monotonic"
    return "non_monotonic"


def level_data_from_runs(external_root: Path) -> tuple[dict[str, Any], dict[str, r4.LevelData]]:
    pre = read_json(external_root / "preflight.json")
    summary = read_json(external_root / "spatial_run_summary.json")
    case_root = Path(pre["case_root"])
    by_level = {f"h{float(run['requested_solver_target_m']):g}": run for run in summary["runs"] if run.get("status") == "passed"}
    if set(by_level) != {"h600", "h500", "h400"}:
        raise RuntimeError("R10 requires passed h600, h500 and h400 limited-linear runs")
    levels = {level_id: r4.derive_level_data(level_id, float(level_id[1:]), case_root, by_level[level_id]) for level_id in ("h600", "h500", "h400")}
    return by_level, levels


def build_metrics(external_root: Path, r5_docs_root: Path) -> tuple[dict[str, Any], dict[str, r4.LevelData]]:
    pre = read_json(external_root / "preflight.json")
    runs_summary = read_json(external_root / "spatial_run_summary.json")
    by_level, levels = level_data_from_runs(external_root)
    level_metrics: dict[str, Any] = {}
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
            "reconstruction": "limited_linear",
        }
    ordered_ids = ["h600", "h500", "h400"]
    comparisons: dict[str, Any] = {}
    for coarse_id, fine_id in (("h600", "h500"), ("h500", "h400"), ("h600", "h400")):
        comparisons[f"{fine_id}_vs_{coarse_id}"] = r5.pair_metrics(levels, level_metrics, fine_id, coarse_id)
    first_metrics = read_json(r5_docs_root / "regional_frozen_terrain_v5_metrics.json")
    first_vs_second: dict[str, Any] = {}
    for level_id, level in levels.items():
        first_run_level = first_metrics["levels"][level_id]
        first_case = Path(first_metrics["case_root"])
        first_run = next(run for run in read_json(DEFAULT_R5_ROOT / "spatial_run_summary.json")["runs"] if f"h{float(run['requested_solver_target_m']):g}" == level_id)
        first_level = r4.derive_level_data(level_id, float(level_id[1:]), first_case, first_run)
        first_vs_second[level_id] = {
            "eta_waveform": r4.waveform_metric(level.series, first_level.series, "eta_m"),
            "qn_waveform": r4.waveform_metric(level.series, first_level.series, "qn_m2_per_s"),
            "Qn_waveform": r4.waveform_metric(level.series, first_level.series, "Qn_m3_per_s"),
            "qbar_waveform": r4.waveform_metric(level.series, first_level.series, "qbar_m2_per_s"),
            "first_order_peaks": first_run_level["forcing_window_qoi"],
            "limited_linear_peaks": level_metrics[level_id]["forcing_window_qoi"],
        }
    primary = comparisons["h400_vs_h500"]
    formal = {
        "eta": primary["eta_waveform"]["nrmse"],
        "qn": primary["qn_waveform"]["nrmse"],
        "Qn": primary["Qn_waveform"]["nrmse"],
        "qbar": primary["qbar_waveform"]["nrmse"],
        "distributed_eta": primary["eta_distributed_common_support"]["nrmse"],
        "distributed_qn": primary["qn_distributed_common_support"]["nrmse"],
    }
    failing = [key for key, value in formal.items() if value is None or float(value) > r4.QUALIFICATION_THRESHOLD]
    adjacent_qn = [comparisons["h500_vs_h600"]["qn_waveform"]["nrmse"], comparisons["h400_vs_h500"]["qn_waveform"]["nrmse"]]
    adjacent_qn_dist = [comparisons["h500_vs_h600"]["qn_distributed_common_support"]["nrmse"], comparisons["h400_vs_h500"]["qn_distributed_common_support"]["nrmse"]]
    if not failing:
        status = "SPATIAL_QUALIFIED"
    elif sequence_class(adjacent_qn) == "monotonic" and sequence_class(adjacent_qn_dist) == "monotonic" and max(formal.values()) < 0.10:
        status = "APPROACHING_SPATIAL_QUALIFICATION"
    elif sequence_class(adjacent_qn) == "non_monotonic" or sequence_class(adjacent_qn_dist) == "non_monotonic":
        status = "SPATIAL_NON_ASYMPTOTIC"
    else:
        status = "SPATIAL_NOT_QUALIFIED"
    metrics = {
        "schema": {"name": "tsunami.c1a_r10_limited_linear_event_convergence", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "git_head": pre["git_head"],
        "binary": pre["binary"],
        "reconstruction": "limited_linear",
        "exact_historical_meshes_reused": True,
        "forcing_window_s": list(r4.FORCING_WINDOW_S),
        "physical_invariance": pre["physical_invariance"],
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
            "h600_over_h500": level_metrics["h600"]["actual_characteristic_mesh_size_m"] / level_metrics["h500"]["actual_characteristic_mesh_size_m"],
            "h500_over_h400": level_metrics["h500"]["actual_characteristic_mesh_size_m"] / level_metrics["h400"]["actual_characteristic_mesh_size_m"],
            "h600_over_h400": level_metrics["h600"]["actual_characteristic_mesh_size_m"] / level_metrics["h400"]["actual_characteristic_mesh_size_m"],
        },
        "comparisons": comparisons,
        "first_vs_second_order": first_vs_second,
        "qualification": {
            "spatial_status": status,
            "formal_2_percent_threshold": r4.QUALIFICATION_THRESHOLD,
            "primary_medium_fine_pair": "h500_vs_h400",
            "formal_medium_to_fine": formal,
            "failing_formal_metrics": failing,
            "temporal_gate_opened": status == "SPATIAL_QUALIFIED",
            "selected_production_mesh": "h500" if status == "SPATIAL_QUALIFIED" else None,
        },
        "richardson_gci": r5.richardson_for_triples(ordered_ids, level_metrics),
        "no_observations_used": True,
        "no_calibration_performed": True,
        "local3d_not_started": True,
        "h300_not_run": True,
        "mathematical_model_unchanged": True,
        "first_order_backend_available": True,
    }
    return metrics, levels


def write_svg(path: Path, title: str, xlabel: str, ylabel: str, series: dict[str, list[dict[str, float]]], y_key: str, *, x_key: str = "time_s") -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    path.parent.mkdir(parents=True, exist_ok=True)
    fig, ax = plt.subplots(figsize=(7.6, 4.2), constrained_layout=True)
    for label, rows in series.items():
        ax.plot([float(row[x_key]) for row in rows], [float(row[y_key]) for row in rows], marker="o", linewidth=1.8, markersize=3.2, label=label)
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.3)
    ax.legend(frameon=False)
    fig.savefig(path, format="svg")
    plt.close(fig)
    provenance = {
        "schema": {"name": "tsunami.figure.provenance", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "figure": str(path),
        "generator": Path(__file__).as_posix(),
        "matplotlib": matplotlib.__version__,
        "seaborn": False,
        "subplots": False,
    }
    write_json(path.with_suffix(".provenance.json"), provenance)


def generate_figures(metrics: dict[str, Any], levels: dict[str, r4.LevelData], figure_root: Path) -> dict[str, Any]:
    outputs = []
    for filename, title, y_key, ylabel in [
        ("c1a_r10_limited_linear_eta_convergence.svg", "C1A-R10 Limited-Linear eta Convergence", "eta_m", "eta perturbation (m)"),
        ("c1a_r10_limited_linear_qn_convergence.svg", "C1A-R10 Limited-Linear qn Convergence", "qn_m2_per_s", "qn perturbation (m^2/s)"),
        ("c1a_r10_limited_linear_Qn_convergence.svg", "C1A-R10 Limited-Linear Qn Convergence", "Qn_m3_per_s", "Qn (m^3/s)"),
    ]:
        path = figure_root / filename
        write_svg(path, title, "time since event start (s)", ylabel, {level_id: r4.rows_in_window(level.series) for level_id, level in levels.items()}, y_key)
        outputs.append({"figure": str(path), "provenance": str(path.with_suffix(".provenance.json"))})
    error_rows = {
        "first_order_Qn": [
            {"h_m": metrics["levels"][fine]["actual_characteristic_mesh_size_m"], "nrmse_percent": read_json(DEFAULT_DOCS_ROOT / "regional_frozen_terrain_v5_metrics.json")["comparisons"][pair]["Qn_waveform"]["nrmse"] * 100.0}
            for pair, fine in (("h500_vs_h600", "h500"), ("h400_vs_h500", "h400"))
        ],
        "limited_linear_Qn": [
            {"h_m": metrics["levels"][comp["fine_level"]]["actual_characteristic_mesh_size_m"], "nrmse_percent": comp["Qn_waveform"]["nrmse"] * 100.0}
            for comp in (metrics["comparisons"]["h500_vs_h600"], metrics["comparisons"]["h400_vs_h500"])
        ],
    }
    path = figure_root / "c1a_r10_forcing_error_vs_h.svg"
    write_svg(path, "C1A-R10 Forcing Error vs Actual h", "actual characteristic h (m)", "Qn NRMSE (%)", error_rows, "nrmse_percent", x_key="h_m")
    outputs.append({"figure": str(path), "provenance": str(path.with_suffix(".provenance.json"))})
    path = figure_root / "c1a_r10_first_vs_limited_Qn_h400.svg"
    first_metrics = read_json(DEFAULT_DOCS_ROOT / "regional_frozen_terrain_v5_metrics.json")
    first_run = next(run for run in read_json(DEFAULT_R5_ROOT / "spatial_run_summary.json")["runs"] if f"h{float(run['requested_solver_target_m']):g}" == "h400")
    first_level = r4.derive_level_data("h400", 400.0, Path(first_metrics["case_root"]), first_run)
    write_svg(path, "C1A-R10 First-Order vs Limited-Linear Qn h400", "time since event start (s)", "Qn (m^3/s)", {"first_order_h400": r4.rows_in_window(first_level.series), "limited_linear_h400": r4.rows_in_window(levels["h400"].series)}, "Qn_m3_per_s")
    outputs.append({"figure": str(path), "provenance": str(path.with_suffix(".provenance.json"))})
    runtime_rows = {
        "limited_linear": [{"h_m": level["actual_characteristic_mesh_size_m"], "runtime_h": level["runtime_wall_clock_s"] / 3600.0} for level in metrics["levels"].values()]
    }
    path = figure_root / "c1a_r10_runtime_vs_forcing_error.svg"
    write_svg(path, "C1A-R10 Runtime vs Actual h", "actual characteristic h (m)", "wall time (h)", runtime_rows, "runtime_h", x_key="h_m")
    outputs.append({"figure": str(path), "provenance": str(path.with_suffix(".provenance.json"))})
    summary_rows = {"primary_metrics": [{"h_m": metrics["levels"]["h400"]["actual_characteristic_mesh_size_m"], "nrmse_percent": value * 100.0} for value in metrics["qualification"]["formal_medium_to_fine"].values()]}
    path = figure_root / "c1a_r10_spatial_convergence_summary.svg"
    write_svg(path, "C1A-R10 Spatial Convergence Summary", "h400 reference marker", "h500 to h400 NRMSE (%)", summary_rows, "nrmse_percent", x_key="h_m")
    outputs.append({"figure": str(path), "provenance": str(path.with_suffix(".provenance.json"))})
    manifest = {"schema": {"name": "tsunami.c1a_r10_figure_manifest", "version": "1.0.0"}, "generated_at_utc": utc_now(), "study_id": STUDY_ID, "outputs": outputs}
    write_json(figure_root / "c1a_r10_figure_manifest.json", manifest)
    return manifest


def update_docs(metrics: dict[str, Any], docs_root: Path, figure_manifest: dict[str, Any]) -> None:
    docs_root.mkdir(parents=True, exist_ok=True)
    write_json(docs_root / "regional2d_r10_limited_linear_event_convergence.json", metrics)
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
            }
        )
    r5.write_csv(docs_root / "regional2d_r10_limited_linear_event_convergence.csv", list(rows[0]), rows)
    write_json(docs_root / "regional2d_r10_first_vs_second_order_comparison.json", metrics["first_vs_second_order"])
    write_json(docs_root / "regional2d_r10_figure_manifest.json", figure_manifest)


def analyze(args: argparse.Namespace) -> dict[str, Any]:
    metrics, levels = build_metrics(args.external_root, args.docs_root)
    write_json(args.external_root / "diagnostics" / "limited_linear_event_metrics.json", metrics)
    figure_manifest = generate_figures(metrics, levels, args.figure_root)
    update_docs(metrics, args.docs_root, figure_manifest)
    write_state(args.external_root, "spatial_decision_complete", spatial_status=metrics["qualification"]["spatial_status"])
    print(json.dumps({"status": metrics["qualification"]["spatial_status"], "temporal_gate_opened": metrics["qualification"]["temporal_gate_opened"]}, indent=2, sort_keys=True))
    return metrics


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--r5-root", type=Path, default=DEFAULT_R5_ROOT)
    parser.add_argument("--g6-root", type=Path, default=DEFAULT_G6_ROOT)
    parser.add_argument("--r2d-binary", type=Path, default=DEFAULT_R2D_BINARY)
    parser.add_argument("--docs-root", type=Path, default=DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=DEFAULT_FIGURE_ROOT)
    parser.add_argument("--monitor-interval-s", type=float, default=60.0)
    parser.add_argument("--smoke-time-s", type=float, default=5.0)
    parser.add_argument("--minimum-free-bytes", type=int, default=20 * 1024**3)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("preflight")
    sub.add_parser("smoke")
    sub.add_parser("run-full")
    sub.add_parser("analyze")
    sub.add_parser("run-and-analyze")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.command == "preflight":
        print(json.dumps(preflight(args), indent=2, sort_keys=True))
    elif args.command == "smoke":
        print(json.dumps(smoke(args), indent=2, sort_keys=True))
    elif args.command == "run-full":
        print(json.dumps(run_full(args), indent=2, sort_keys=True))
    elif args.command == "analyze":
        analyze(args)
    elif args.command == "run-and-analyze":
        run_full(args)
        metrics = analyze(args)
        if metrics["qualification"]["temporal_gate_opened"]:
            write_state(args.external_root, "temporal_gate_open", spatial_status=metrics["qualification"]["spatial_status"])
        else:
            write_state(args.external_root, "decision_complete", spatial_status=metrics["qualification"]["spatial_status"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
