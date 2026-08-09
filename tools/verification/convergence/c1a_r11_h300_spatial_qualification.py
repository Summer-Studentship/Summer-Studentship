#!/usr/bin/env python3
"""Analyse C1A-R11 h300 against the frozen-terrain limited-linear ladder."""

from __future__ import annotations

import argparse
import csv
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import c1a_convergence as c1a
import c1a_r4_execute_frozen_terrain as r4
import c1a_r5_finer_spatial_convergence as r5


STUDY_ID = "regional2d-h300-r11"
DEFAULT_R10_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-limited-linear-event-r10")
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-h300-r11")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
LADDER = ("h500", "h400", "h300")
ADJACENT_PAIRS = ("h400_vs_h500", "h300_vs_h400")
DECISIVE_PAIR = "h300_vs_h400"
FORMAL_METRICS = {
    "eta_waveform": ("eta_waveform", "nrmse"),
    "qn_waveform": ("qn_waveform", "nrmse"),
    "Qn_waveform": ("Qn_waveform", "nrmse"),
    "qbar_waveform": ("qbar_waveform", "nrmse"),
    "eta_distributed": ("eta_distributed_common_support", "nrmse"),
    "qn_distributed": ("qn_distributed_common_support", "nrmse"),
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


def write_csv(path: Path, fieldnames: Sequence[str], rows: Sequence[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def sequence_class(values: Sequence[float]) -> str:
    if len(values) < 2:
        return "insufficient"
    if all(b <= a for a, b in zip(values, values[1:])):
        return "monotonic_decreasing"
    if all(b >= a for a, b in zip(values, values[1:])):
        return "monotonic_increasing"
    return "non_monotonic"


def require_passed_run(path: Path, level_id: str) -> dict[str, Any]:
    if not path.is_file():
        raise RuntimeError(f"missing {level_id} run record: {path}")
    run = read_json(path)
    if run.get("status") != "passed":
        raise RuntimeError(f"{level_id} run is not passed: {run.get('status')!r}")
    if run.get("reconstruction") != "limited_linear":
        raise RuntimeError(f"{level_id} run is not limited_linear: {run.get('reconstruction')!r}")
    output_dir = Path(run["output_dir"])
    timestep = r4.timestep_stats_from_diagnostics(output_dir)
    if abs(float(timestep["final_diagnostic_time_s"]) - r5.FULL_TIME_S) > 1.0e-9:
        raise RuntimeError(f"{level_id} final time is not {r5.FULL_TIME_S}s")
    if not (output_dir / "coupling" / r4.SECTION_ID / "samples.csv").is_file():
        raise RuntimeError(f"{level_id} coupling samples are missing")
    return run


def load_runs(r10_root: Path, external_root: Path) -> tuple[dict[str, dict[str, Any]], dict[str, Path]]:
    r10_summary = read_json(r10_root / "spatial_run_summary.json")
    r10_preflight = read_json(r10_root / "preflight.json")
    r10_runs = {
        f"h{float(run['requested_solver_target_m']):g}": run
        for run in r10_summary["runs"]
        if run.get("status") == "passed"
    }
    runs = {
        "h500": r10_runs["h500"],
        "h400": r10_runs["h400"],
        "h300": require_passed_run(external_root / "spatial" / "h300" / "run.json", "h300"),
    }
    case_roots = {
        "h500": Path(r10_preflight["case_root"]),
        "h400": Path(r10_preflight["case_root"]),
        "h300": Path(read_json(external_root / "state" / "h300_mesh_preflight.json")["case_root"]),
    }
    return runs, case_roots


def level_metric(level: r4.LevelData, run: dict[str, Any]) -> dict[str, Any]:
    mesh = run["mesh"]
    return {
        "requested_solver_target_m": level.target_m,
        "active_cells": mesh["active_cells"],
        "total_cells": mesh["total_cells"],
        "actual_characteristic_mesh_size_m": mesh["actual_characteristic_h_m"],
        "mesh_sha256": mesh["mesh_sha256"],
        "minimum_cell_area_m2": mesh.get("minimum_cell_area_m2"),
        "mean_cell_area_m2": mesh.get("mean_cell_area_m2"),
        "maximum_cell_area_m2": mesh.get("maximum_cell_area_m2"),
        "runtime_wall_clock_s": run["resource_usage"]["wall_clock_s"],
        "runtime_cpu_time_s": run["resource_usage"]["cpu_time_s"],
        "peak_memory_kb": run["resource_usage"]["peak_memory_kb"],
        "launch_timestamp_utc": run.get("launch_timestamp_utc"),
        "completion_timestamp_utc": run.get("completion_timestamp_utc"),
        "cpu_affinity": run.get("cpu_affinity"),
        "timestep": r4.timestep_stats_from_diagnostics(level.output_dir),
        "forcing_window_qoi": r4.qoi_summary(level.series),
        "source_projection": r4.source_projection(level),
        "coupling_sample_count": int(level.metadata["sample_count"]),
        "reconstruction": "limited_linear",
    }


def richardson_for_h500_h400_h300(level_metrics: dict[str, Any]) -> dict[str, Any]:
    h_values = [level_metrics[level]["actual_characteristic_mesh_size_m"] for level in ("h300", "h400", "h500")]
    result: dict[str, Any] = {
        "levels_coarse_to_fine": list(LADDER),
        "h_fine_to_coarse_m": h_values,
        "quantities": {},
    }
    for qoi in ("peak_eta_abs_m", "peak_qn_abs_m2_per_s", "peak_Qn_abs_m3_per_s", "peak_qbar_abs_m2_per_s"):
        values = [level_metrics[level]["forcing_window_qoi"][qoi] for level in ("h300", "h400", "h500")]
        monotonic = sequence_class(values) in {"monotonic_decreasing", "monotonic_increasing"}
        result["quantities"][qoi] = {
            "values_fine_to_coarse": values,
            "status": "computed" if monotonic else "not_decision_grade_non_monotonic",
            "result": c1a.richardson_gci(values, h_values) if monotonic else None,
        }
    return result


def classify(comparisons: dict[str, Any]) -> dict[str, Any]:
    formal = {
        key: comparisons[DECISIVE_PAIR][metric][field]
        for key, (metric, field) in FORMAL_METRICS.items()
    }
    failing = [key for key, value in formal.items() if value is None or float(value) > r4.QUALIFICATION_THRESHOLD]
    trends = {}
    for metric in ("eta_waveform", "qn_waveform", "Qn_waveform", "qbar_waveform", "eta_distributed_common_support", "qn_distributed_common_support"):
        values = [comparisons[pair][metric]["nrmse"] for pair in ADJACENT_PAIRS]
        trends[metric] = {"adjacent_nrmse": values, "classification": sequence_class(values)}
    formal_max = max(float(value) for value in formal.values() if value is not None)
    core_monotonic = (
        trends["Qn_waveform"]["classification"] == "monotonic_decreasing"
        and trends["qn_distributed_common_support"]["classification"] == "monotonic_decreasing"
    )
    if not failing:
        status = "SPATIAL_QUALIFIED"
        next_action = "open_temporal_convergence"
    elif core_monotonic and formal_max <= 0.04:
        status = "APPROACHING_SPATIAL_QUALIFICATION"
        next_action = "compute_order_gci_and_review_next_mesh; do_not_launch_h250_automatically"
    else:
        status = "SPATIAL_NOT_QUALIFIED_INVESTIGATE"
        next_action = "stop_before_temporal; investigate terrain ceiling, limiter activity, projection and boundary interaction"
    return {
        "spatial_status": status,
        "formal_2_percent_threshold": r4.QUALIFICATION_THRESHOLD,
        "primary_medium_fine_pair": DECISIVE_PAIR,
        "formal_medium_to_fine": formal,
        "formal_max_nrmse": formal_max,
        "failing_formal_metrics": failing,
        "qoi_trends": trends,
        "temporal_gate_opened": status == "SPATIAL_QUALIFIED",
        "selected_candidate_mesh": "h400" if status == "SPATIAL_QUALIFIED" else None,
        "next_action": next_action,
        "no_h250_automatic_launch": True,
    }


def build_metrics(r10_root: Path, external_root: Path) -> tuple[dict[str, Any], dict[str, r4.LevelData]]:
    runs, case_roots = load_runs(r10_root, external_root)
    levels = {
        level_id: r4.derive_level_data(level_id, float(level_id[1:]), case_roots[level_id], runs[level_id])
        for level_id in LADDER
    }
    level_metrics = {level_id: level_metric(levels[level_id], runs[level_id]) for level_id in LADDER}
    comparisons = {
        "h400_vs_h500": r5.pair_metrics(levels, level_metrics, "h400", "h500"),
        "h300_vs_h400": r5.pair_metrics(levels, level_metrics, "h300", "h400"),
        "h300_vs_h500": r5.pair_metrics(levels, level_metrics, "h300", "h500"),
    }
    mesh_preflight = read_json(external_root / "state" / "h300_mesh_preflight.json")
    r10_preflight = read_json(r10_root / "preflight.json")
    binary_authority = read_json(external_root / "state" / "lane_a_binary_authority.json")
    qualification = classify(comparisons)
    metrics = {
        "schema": {"name": "tsunami.c1a_r11_h300_spatial_qualification", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "external_root": str(external_root),
        "r10_root": str(r10_root),
        "git_head": binary_authority.get("git_head"),
        "binary": binary_authority,
        "reconstruction": "limited_linear",
        "frozen_family_source": {
            "h500_h400": "C1A-R10 completed limited-linear evidence",
            "h300": "C1A-R11 completed h300 run",
        },
        "forcing_window_s": list(r4.FORCING_WINDOW_S),
        "physical_invariance": {
            "status": "passed",
            "r10": r10_preflight["physical_invariance"],
            "h300": mesh_preflight["case_invariance"],
        },
        "mesh_quality": {
            "h300_pathology_gate": mesh_preflight["pathology_gate"],
            "h300_vs_h400_quality_comparison": mesh_preflight["quality_comparison"],
        },
        "levels": level_metrics,
        "refinement_ratios": {
            "h500_over_h400": level_metrics["h500"]["actual_characteristic_mesh_size_m"] / level_metrics["h400"]["actual_characteristic_mesh_size_m"],
            "h400_over_h300": level_metrics["h400"]["actual_characteristic_mesh_size_m"] / level_metrics["h300"]["actual_characteristic_mesh_size_m"],
            "h500_over_h300": level_metrics["h500"]["actual_characteristic_mesh_size_m"] / level_metrics["h300"]["actual_characteristic_mesh_size_m"],
        },
        "comparisons": comparisons,
        "richardson_gci": {
            "h500_h400_h300": richardson_for_h500_h400_h300(level_metrics),
        },
        "qualification": qualification,
        "predicted_next_mesh_if_reviewed": {
            "requested_target_m": 250.0,
            "predicted_actual_h_m": level_metrics["h300"]["actual_characteristic_mesh_size_m"] * (250.0 / 300.0),
            "automatic_launch": False,
        },
        "no_observations_used": True,
        "no_calibration_performed": True,
        "local3d_not_started": True,
        "temporal_convergence_started": False,
        "fabricated_results": False,
    }
    return metrics, levels


def generate_figures(metrics: dict[str, Any], levels: dict[str, r4.LevelData], figure_root: Path, metrics_path: Path) -> dict[str, Any]:
    outputs = []
    for filename, title, y_key, ylabel in [
        ("c1a_r11_h300_eta_convergence.svg", "C1A-R11 h300 eta Convergence", "eta_m", "eta perturbation (m)"),
        ("c1a_r11_h300_qn_convergence.svg", "C1A-R11 h300 qn Convergence", "qn_m2_per_s", "qn perturbation (m^2/s)"),
        ("c1a_r11_h300_Qn_convergence.svg", "C1A-R11 h300 Qn Convergence", "Qn_m3_per_s", "Qn (m^3/s)"),
    ]:
        path = figure_root / filename
        r5.svg_line(path, title, "time since event start (s)", ylabel, {level_id: r4.rows_in_window(level.series) for level_id, level in levels.items()}, y_key)
        outputs.append({"figure": str(path), "source_metrics": str(metrics_path)})
    error_rows = {}
    for metric in ("eta_waveform", "qn_waveform", "Qn_waveform"):
        error_rows[metric] = [
            {"h_m": metrics["levels"][comparison["fine_level"]]["actual_characteristic_mesh_size_m"], "nrmse_percent": comparison[metric]["nrmse"] * 100.0}
            for pair_id, comparison in metrics["comparisons"].items()
            if pair_id in ADJACENT_PAIRS
        ]
    path = figure_root / "c1a_r11_h300_waveform_error_vs_h.svg"
    r5.svg_line(path, "C1A-R11 h300 Waveform Error vs Actual h", "actual characteristic h (m)", "NRMSE (%)", error_rows, "nrmse_percent", x_key="h_m")
    outputs.append({"figure": str(path), "source_metrics": str(metrics_path)})
    manifest = {
        "schema": {"name": "tsunami.c1a_r11_h300_figure_manifest", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "classification": metrics["qualification"]["spatial_status"],
        "outputs": outputs,
    }
    write_json(figure_root / "c1a_r11_h300_figure_manifest.json", manifest)
    return manifest


def write_outputs(metrics: dict[str, Any], levels: dict[str, r4.LevelData], docs_root: Path, figure_root: Path, external_root: Path) -> dict[str, Any]:
    metrics_path = external_root / "diagnostics" / "h300_spatial_qualification_metrics.json"
    write_json(metrics_path, metrics)
    figure_manifest = generate_figures(metrics, levels, figure_root, metrics_path)
    write_json(docs_root / "regional2d_r11_h300_spatial_qualification.json", metrics)
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
    write_csv(docs_root / "regional2d_r11_h300_spatial_qualification.csv", list(rows[0]), rows)
    write_json(docs_root / "regional2d_r11_h300_figure_manifest.json", figure_manifest)
    decision = f"""# C1A-R11 h300 Spatial Qualification Decision

Status: {metrics['qualification']['spatial_status']}.

The R11 analysis combines the completed C1A-R10 `h500` and `h400`
limited-linear event runs with the R11 `h300` run. The decisive
medium-to-fine pair is `{DECISIVE_PAIR}` and the formal threshold is
{r4.QUALIFICATION_THRESHOLD:.0%} NRMSE.

Failing formal metrics: {', '.join(metrics['qualification']['failing_formal_metrics']) or 'none'}.

Temporal convergence gate opened: {metrics['qualification']['temporal_gate_opened']}.

No observations were used, no calibration was performed, no Local3D work was
started, and no h250 run was launched automatically.
"""
    (docs_root / "regional2d_r11_h300_spatial_qualification.md").write_text(decision, encoding="utf-8")
    return figure_manifest


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r10-root", type=Path, default=DEFAULT_R10_ROOT)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--docs-root", type=Path, default=DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=DEFAULT_FIGURE_ROOT)
    parser.add_argument("command", choices=("analyze",))
    return parser


def main() -> int:
    args = build_parser().parse_args()
    metrics, levels = build_metrics(args.r10_root, args.external_root)
    write_outputs(metrics, levels, args.docs_root, args.figure_root, args.external_root)
    print(json.dumps(metrics["qualification"], indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
