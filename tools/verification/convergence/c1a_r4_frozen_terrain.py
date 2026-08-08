#!/usr/bin/env python3
"""Audit C1A-R3 and initialise the C1A-R4 frozen-terrain convergence record."""

from __future__ import annotations

import argparse
import csv
import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

import c1a_convergence as c1a


R3_STUDY_ID = "regional-spatial-fine-resolution-v3"
R4_STUDY_ID = c1a.R4_FROZEN_TERRAIN_STUDY_ID
FORCING_WINDOW_S = [245.0, 545.0]
PLANNED_R4_LADDER = ["h1000", "h800", "h600"]
NOT_AVAILABLE = "not_available_no_valid_r4_runs"


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


def optional_sha(path: Path) -> str | None:
    return c1a.file_sha256(path) if path.is_file() else None


def level_case_roots(r3_external_root: Path, r3_metrics: dict[str, Any]) -> dict[str, Path]:
    h1000 = Path(r3_metrics["h1000_reuse_proof"]["reused_from"]) / "case"
    return {
        "h1000": h1000,
        "h900": r3_external_root / "spatial/h900/case",
        "h800": r3_external_root / "spatial/h800/case",
    }


def r3_level_row(level_id: str, case_root: Path, metrics: dict[str, Any]) -> dict[str, Any]:
    terrain_record_path = case_root / "manifests/terrain/conditioned-terrain.json"
    terrain_record = read_json(terrain_record_path)
    source_path = case_root / c1a.G6_FROZEN_SOURCE_RELATIVE_PATH.relative_to("case")
    source_meta_path = case_root / c1a.G6_FROZEN_SOURCE_METADATA_RELATIVE_PATH.relative_to("case")
    terrain_path = case_root / c1a.G6_FROZEN_TERRAIN_RELATIVE_PATH.relative_to("case")
    mesh_path = case_root / "meshes/kamaishi-regional.msh"
    level_metrics = metrics["levels"][level_id]
    return {
        "run": level_id,
        "source_asset": "ETOPO 2022 v1 15 arc-second surface + USGS finite-fault",
        "source_dataset_resolution_m": c1a.ETOPO_2022_15S_NOMINAL_SOURCE_SPACING_M,
        "terrain_processing_resolution_m": terrain_record["grid"]["spacing_m"],
        "solver_target_m": level_metrics["requested_solver_spacing_m"],
        "actual_h_m": level_metrics["actual_characteristic_mesh_size_m"],
        "terrain_raster_width": terrain_record["grid"]["width"],
        "terrain_raster_height": terrain_record["grid"]["height"],
        "terrain_intermediate_sha256": optional_sha(terrain_path),
        "terrain_metadata_sha256": optional_sha(terrain_record_path),
        "source_representation_sha256": optional_sha(source_path),
        "source_metadata_sha256": optional_sha(source_meta_path),
        "solver_mesh_sha256": optional_sha(mesh_path),
    }


def classify_r3(rows: Sequence[dict[str, Any]]) -> dict[str, Any]:
    terrain_resolutions = {float(row["terrain_processing_resolution_m"]) for row in rows}
    terrain_hashes = {row["terrain_intermediate_sha256"] for row in rows}
    source_hashes = {row["source_representation_sha256"] for row in rows}
    coupled = len(terrain_resolutions) > 1 or len(terrain_hashes) > 1 or len(source_hashes) > 1
    return {
        "varied_terrain_processing_with_mesh": len(terrain_resolutions) > 1,
        "varied_terrain_intermediate_with_mesh": len(terrain_hashes) > 1,
        "varied_source_representation_with_mesh": len(source_hashes) > 1,
        "classification": "coupled solver-mesh + terrain/source-discretisation sensitivity"
        if coupled
        else "clean solver-mesh sensitivity",
        "r3_reuse_for_r4": False if coupled else "conditional_after_full_invariance_check",
    }


def build_audit(args: argparse.Namespace) -> dict[str, Any]:
    r3_metrics = read_json(args.r3_external_root / "fine_resolution_v3_metrics.json")
    rows = [r3_level_row(level, root, r3_metrics) for level, root in level_case_roots(args.r3_external_root, r3_metrics).items()]
    r3 = classify_r3(rows)
    authority = c1a.frozen_g6_numerical_authority(args.g6_root)
    planned_contracts = [
        c1a.regional_frozen_terrain_resolution_contract(
            requested_solver_mesh_size_m=float(level[1:]),
            frozen_authority=authority,
            profile_name=f"r4-frozen-g6-terrain-{level}",
        )
        for level in PLANNED_R4_LADDER
    ]
    return {
        "schema": {"name": "tsunami.c1a_r4_frozen_terrain_audit", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "r3_study_id": R3_STUDY_ID,
        "r4_study_id": R4_STUDY_ID,
        "forcing_window_s": FORCING_WINDOW_S,
        "r3_resolution_audit": {
            "table": rows,
            **r3,
            "interpretation_note": (
                "C1A-R3 remains valid evidence that the prior Regional2D setup was not spatially qualified, "
                "but it is not a pure solver-mesh convergence study because the numerical terrain/source raster "
                "changed with mesh level."
            ),
        },
        "frozen_g6_authority": authority,
        "r4_contract": {
            "planned_requested_ladder": PLANNED_R4_LADDER,
            "planned_contracts": planned_contracts,
            "family_invariant": True,
            "common_coupling_section": "kamaishi-nearshore-interface",
            "formal_metrics_remain_unshifted": True,
            "observations_used": False,
            "physical_calibration_performed": False,
            "local3d_started": False,
        },
        "r4_execution": {
            "status": "blocked_pending_frozen_family_execution_adapter_and_real_runs",
            "previous_runs_reusable": False,
            "h600_pilot_runtime": NOT_AVAILABLE,
            "h600_pilot_memory": NOT_AVAILABLE,
            "projected_600s_runtime": NOT_AVAILABLE,
            "actual_characteristic_sizes_m": NOT_AVAILABLE,
            "active_cell_counts": NOT_AVAILABLE,
            "source_volume_variation": NOT_AVAILABLE,
            "source_centroid_variation": NOT_AVAILABLE,
            "bed_rmse": NOT_AVAILABLE,
            "travel_time_proxy": NOT_AVAILABLE,
            "waveform_errors": NOT_AVAILABLE,
            "phase_lags": NOT_AVAILABLE,
            "observed_orders": NOT_AVAILABLE,
            "gci": {"status": "not_applicable_no_valid_three_grid_frozen_family"},
            "spatial_qualification_status": "not_started",
            "temporal_convergence_status": "not_started_spatial_gate_closed",
            "dominant_residual_numerical_mechanism": "not_determined_until_real_frozen_runs_exist",
        },
        "protected_activity": {
            "no_observational_data_used": True,
            "no_physical_calibration": True,
            "no_local3d_convergence": True,
            "no_push": True,
        },
        "fabricated_results": False,
    }


def update_docs(docs_root: Path, figure_root: Path, audit: dict[str, Any]) -> None:
    write_json(docs_root / "regional_frozen_terrain_v4_audit.json", audit)
    rows = audit["r3_resolution_audit"]["table"]
    write_csv(
        docs_root / "regional_frozen_terrain_v4_r3_resolution_audit.csv",
        [
            "run",
            "source_asset",
            "source_dataset_resolution_m",
            "terrain_processing_resolution_m",
            "solver_target_m",
            "actual_h_m",
            "terrain_raster_width",
            "terrain_raster_height",
            "terrain_intermediate_sha256",
            "terrain_metadata_sha256",
            "source_representation_sha256",
            "source_metadata_sha256",
            "solver_mesh_sha256",
        ],
        rows,
    )
    summary_path = docs_root / "regional_convergence_summary.json"
    summary = read_json(summary_path)
    summary["r3_scientific_interpretation"] = {
        "classification": audit["r3_resolution_audit"]["classification"],
        "varied_terrain_processing_with_mesh": audit["r3_resolution_audit"]["varied_terrain_processing_with_mesh"],
        "varied_source_representation_with_mesh": audit["r3_resolution_audit"]["varied_source_representation_with_mesh"],
        "r4_reuse_status": "not_reusable_for_frozen_terrain_solver_mesh_convergence",
        "audit_record": "regional_frozen_terrain_v4_audit.json",
    }
    summary["r4_frozen_terrain_status"] = audit["r4_execution"]
    write_json(summary_path, summary)
    production_path = docs_root / "regional_production_discretisation.md"
    production = production_path.read_text(encoding="utf-8")
    marker = "\n## C1A-R4 Frozen-Terrain Audit\n"
    note = (
        marker
        + "\n"
        + "C1A-R4 reclassifies the scientific interpretation of C1A-R3 as coupled "
        + "solver-mesh plus terrain/source-discretisation sensitivity. R3 still proves "
        + "the previous Regional2D family was not spatially qualified, but the h1000/h900/h800 "
        + "runs cannot be reused as a pure frozen-terrain solver-mesh convergence ladder because "
        + "their terrain processing resolution, terrain raster hash, and source raster hash vary by level.\n\n"
        + f"The frozen authority for R4 is the accepted G6 terrain `{audit['frozen_g6_authority']['terrain']['path']}` "
        + f"with SHA-256 `{audit['frozen_g6_authority']['terrain']['sha256']}`. Temporal convergence remains gated "
        + "until real frozen-family Regional2D runs qualify spatially.\n"
    )
    if marker in production:
        production = production.split(marker, 1)[0].rstrip() + "\n" + note
    else:
        production = production.rstrip() + "\n" + note
    production_path.write_text(production, encoding="utf-8")
    write_json(
        figure_root / "c1a_r4_figure_manifest.json",
        {
            "schema": {"name": "tsunami.c1a_r4_figure_manifest", "version": "1.0.0"},
            "generated_at_utc": utc_now(),
            "study_id": R4_STUDY_ID,
            "status": "not_generated_no_real_r4_data",
            "required_figures": [
                "r4_eta_physical_time_convergence.svg",
                "r4_qn_physical_time_convergence.svg",
                "r4_Qn_convergence.svg",
                "r4_phase_lag_vs_mesh.svg",
                "r4_bed_projection_error_vs_mesh.svg",
                "r4_travel_time_proxy_vs_crest_time.svg",
                "r4_runtime_vs_Qn_error.svg",
            ],
            "reason": "No valid frozen-terrain R4 Regional2D runs exist yet; figures must only be generated from real R4 data.",
        },
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--r3-external-root",
        type=Path,
        default=Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional-spatial-fine-resolution-v3"),
    )
    parser.add_argument(
        "--r4-external-root",
        type=Path,
        default=Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional-spatial-frozen-terrain-v4"),
    )
    parser.add_argument("--g6-root", type=Path, default=Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi"))
    parser.add_argument(
        "--docs-root",
        type=Path,
        default=repo_root() / "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A",
    )
    parser.add_argument("--figure-root", type=Path, default=repo_root() / "deliverables/figures/convergence")
    parser.add_argument("--no-external-write", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    audit = build_audit(args)
    update_docs(args.docs_root, args.figure_root, audit)
    if not args.no_external_write:
        write_json(args.r4_external_root / "regional_frozen_terrain_v4_audit.json", audit)
    print(json.dumps({"status": "generated", "audit": str(args.docs_root / "regional_frozen_terrain_v4_audit.json")}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
