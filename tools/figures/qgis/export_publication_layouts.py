#!/usr/bin/env python3
"""Export R16B QGIS publication layouts and refresh final provenance."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path
from typing import Any, Mapping, Sequence


REPO_ROOT = Path(__file__).resolve().parents[3]
if str(REPO_ROOT / "tools/figures") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "tools/figures"))

import r16_publication as r16
from build_tohoku_kamaishi_project import GROUP_NAMES, LAYOUT_NAMES, VERTICAL_EXAGGERATION


LAYOUT_TO_BASENAME = {
    "01_TOHOKU_EVENT_CORRIDOR": "figure_A_tohoku_kamaishi_corridor",
    "02_CORRIDOR_BATHYMETRY": "figure_B_corridor_bathymetry_plan",
    "03_HYBRID_DOMAIN": "figure_E_hybrid_domain_framework",
    "04_VALIDATION_GEOMETRY": "figure_F_validation_geometry",
    "05_CORRIDOR_BATHYMETRY_OBLIQUE": "figure_C_corridor_bathymetry_oblique",
}

FIGURE_DETAILS: dict[str, dict[str, Any]] = {
    "figure_A_tohoku_kamaishi_corridor": {
        "figure_id": "A",
        "scientific_question": "Where did the 2011 event occur, and what physical region does the current computational framework cover?",
        "layout": "01_TOHOKU_EVENT_CORRIDOR",
        "priority": "HERO",
        "visual_qc_classification": "POSTER_READY_HERO",
        "allowed_claim": "Shows the actual 2011 Tohoku event reference, Kamaishi proxy point and accepted R10/G6 delivery corridor in their regional geographic context.",
        "required_caveat": "R10 h400 limited_linear remains BEST_AVAILABLE_NUMERICALLY_UNCERTAIN; not spatially qualified, calibrated or historically validated.",
    },
    "figure_B_corridor_bathymetry_plan": {
        "figure_id": "B",
        "scientific_question": "What real bathymetric and topographic environment does the accepted corridor cross?",
        "layout": "02_CORRIDOR_BATHYMETRY",
        "priority": "HERO",
        "visual_qc_classification": "POSTER_READY_HERO",
        "allowed_claim": "Shows ETOPO 2022 bathymetry/topography over the accepted Kamaishi corridor without altering corridor geometry.",
        "required_caveat": "This is geographic/bathymetric context only; it does not establish spatial qualification or historical validation.",
    },
    "figure_C_corridor_bathymetry_oblique": {
        "figure_id": "C",
        "scientific_question": "What does the three-dimensional seabed/coastal relief within the propagation corridor look like?",
        "layout": "05_CORRIDOR_BATHYMETRY_OBLIQUE",
        "priority": "SECONDARY",
        "visual_qc_classification": "REPORT_READY_SECONDARY",
        "allowed_claim": "Shows an oblique terrain rendering from the same ETOPO corridor crop used in Figure B, with corridor outline overlaid.",
        "required_caveat": "The oblique view is a visualisation with vertical exaggeration; it is not a Local3D, OpenFOAM or calibrated inundation result.",
    },
    "figure_E_hybrid_domain_framework": {
        "figure_id": "E",
        "scientific_question": "How does the Regional2D corridor connect conceptually to a local high-fidelity impact domain?",
        "layout": "03_HYBRID_DOMAIN",
        "priority": "PRIMARY",
        "visual_qc_classification": "POSTER_READY_PRIMARY",
        "allowed_claim": "Shows the geographic one-way framework from frozen Regional2D corridor to selected wet nearshore interface to a conceptual Local3D footprint.",
        "required_caveat": "Local3D current-generation remains REPLAY_VOF_BEHAVIOUR_UNRESOLVED; the footprint is conceptual, not production closure.",
    },
    "figure_F_validation_geometry": {
        "figure_id": "F",
        "scientific_question": "Why do available observations not constitute direct historical validation for the current corridor?",
        "layout": "04_VALIDATION_GEOMETRY",
        "priority": "PRIMARY",
        "visual_qc_classification": "POSTER_READY_PRIMARY",
        "allowed_claim": "Shows R15 observation geometry relative to the accepted corridor while preserving 0 DIRECT, 1 PROXY and 28 TARGET_ONLY classifications.",
        "required_caveat": "NOWPHAS 802G is about 12.3 km outside the corridor and DART 21418 about 545 km outside; no corridor modification was made.",
    },
}

EXISTING_FIGURE_QC = {
    "D1": ("POSTER_READY_PRIMARY", "PRIMARY", "Use for poster/report wave-evolution evidence."),
    "D2": ("REPORT_READY_COMPANION", "SECONDARY", "Use as the report companion profile panel to D1."),
    "S1": ("REPORT_ONLY_SUPPORTING", "REPORT_ONLY", "Use as supporting longitudinal bathymetry context."),
}


def export_layouts(*, allow_blocked: bool) -> dict[str, Any]:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    env = r16.qgis_environment()
    if env["status"] == "QGIS_RUNTIME_BLOCKED" or not r16.QGIS_PROJECT.is_file():
        payload = {
            "schema": {"name": "tsunami.r16.qgis_layout_export", "version": "2.0.0"},
            "status": "BLOCKED_BY_QGIS_RUNTIME",
            "reason": "QGIS runtime/project unavailable; final cartographic layouts were not exported.",
            "qgis": env,
            "project": r16.QGIS_PROJECT.as_posix(),
            "requested_layouts": LAYOUT_NAMES,
        }
        r16.write_json(r16.PROVENANCE_ROOT / "qgis_layout_export_status.json", payload)
        if allow_blocked:
            return payload
        raise RuntimeError(payload["reason"])

    from qgis.core import QgsApplication, QgsLayoutExporter, QgsProject  # type: ignore[import-not-found]

    app = QgsApplication([], False)
    app.initQgis()
    exported: dict[str, list[str]] = {}
    try:
        project = QgsProject.instance()
        if not project.read(r16.QGIS_PROJECT.as_posix()):
            raise RuntimeError(f"Could not read {r16.QGIS_PROJECT}")
        manager = project.layoutManager()
        for layout_name in LAYOUT_NAMES:
            layout = manager.layoutByName(layout_name)
            if layout is None:
                raise RuntimeError(f"Missing layout: {layout_name}")
            basename = LAYOUT_TO_BASENAME[layout_name]
            exporter = QgsLayoutExporter(layout)
            paths = export_one_layout(exporter, basename, QgsLayoutExporter)
            exported[layout_name] = [path.relative_to(r16.REPO_ROOT).as_posix() for path in paths]
    finally:
        app.exitQgis()

    figure_records = refresh_provenance(exported, env)
    payload = {
        "schema": {"name": "tsunami.r16.qgis_layout_export", "version": "2.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": r16.utc_now(),
        "qgis": env,
        "project": r16.file_record(r16.QGIS_PROJECT),
        "exports": exported,
        "figures_updated": sorted(figure_records),
    }
    r16.write_json(r16.PROVENANCE_ROOT / "qgis_layout_export_status.json", payload)
    return payload


def export_one_layout(exporter: Any, basename: str, exporter_type: Any) -> list[Path]:
    r16.PUBLICATION_ROOT.mkdir(parents=True, exist_ok=True)
    pdf = r16.PUBLICATION_ROOT / f"{basename}.pdf"
    svg = r16.PUBLICATION_ROOT / f"{basename}.svg"
    png = r16.PUBLICATION_ROOT / f"{basename}.png"
    for path in [pdf, svg, png]:
        if path.exists():
            path.unlink()

    results = [
        ("PDF", pdf, exporter.exportToPdf(pdf.as_posix(), exporter_type.PdfExportSettings())),
        ("SVG", svg, exporter.exportToSvg(svg.as_posix(), exporter_type.SvgExportSettings())),
    ]
    image_settings = exporter_type.ImageExportSettings()
    image_settings.dpi = 420
    results.append(("PNG", png, exporter.exportToImage(png.as_posix(), image_settings)))

    for kind, path, result in results:
        if int(result) != 0:
            raise RuntimeError(f"{kind} export failed for {basename}: result code {result}")
        if not path.is_file() or path.stat().st_size <= 0:
            raise RuntimeError(f"{kind} export missing or empty for {basename}: {path}")

    preview = r16.PREVIEW_ROOT / f"{basename}.png"
    preview.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(png, preview)
    return [pdf, svg, png]


def refresh_provenance(exported: Mapping[str, Sequence[str]], env: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    records: dict[str, dict[str, Any]] = {}
    for layout_name, outputs in exported.items():
        basename = LAYOUT_TO_BASENAME[layout_name]
        details = FIGURE_DETAILS[basename]
        record = {
            "schema": {"name": "tsunami.r16.figure_provenance", "version": "2.0.0"},
            "figure_id": details["figure_id"],
            "basename": basename,
            "scientific_question": details["scientific_question"],
            "status": "COMPLETE",
            "outputs": list(outputs),
            "preview": (r16.PREVIEW_ROOT / f"{basename}.png").relative_to(r16.REPO_ROOT).as_posix(),
            "source_datasets": r16.source_authority_records(),
            "model_authority": r16.SCIENTIFIC_AUTHORITY,
            "crs": {
                "publication_map": "EPSG:32654 WGS 84 / UTM zone 54N",
                "source_terrain": "ETOPO 2022 WGS 84 + EGM2008 height, reprojected/cropped to EPSG:32654 for layout use",
                "vertical_positive": "up",
            },
            "software": r16.software_record(),
            "git_sha": r16.git_sha(),
            "generated_at_utc": r16.utc_now(),
            "qgis_layout": layout_name,
            "priority": details["priority"],
            "visual_qc_classification": details["visual_qc_classification"],
            "allowed_claim": details["allowed_claim"],
            "required_caveat": details["required_caveat"],
        }
        if basename == "figure_C_corridor_bathymetry_oblique":
            record.update(
                {
                    "rendering_method": "direct VTK offscreen render embedded in editable QGIS layout",
                    "vertical_exaggeration": VERTICAL_EXAGGERATION,
                    "pyvista_fallback": "PyVista unavailable in system Python; direct VTK fallback used and recorded in r16b_derived_terrain_status.json.",
                }
            )
        for output in outputs:
            output_path = r16.REPO_ROOT / output
            record.setdefault("output_hashes", {})[output] = r16.sha256(output_path)
        r16.write_json(r16.PROVENANCE_ROOT / f"{basename}.provenance.json", record)
        records[details["figure_id"]] = record

    for figure_id, (classification, priority, recommended_use) in EXISTING_FIGURE_QC.items():
        basename = {"D1": "figure_D1_eta_space_time", "D2": "figure_D2_wave_profiles_to_shore", "S1": "figure_S1_longitudinal_bathymetry"}[figure_id]
        path = r16.PROVENANCE_ROOT / f"{basename}.provenance.json"
        record = r16.read_json(path)
        record["visual_qc_classification"] = classification
        record["priority"] = priority
        record["recommended_use"] = recommended_use
        record.setdefault("software", {})["qgis"] = dict(env)
        record["qgis_runtime_note"] = "QGIS was not required to generate this matplotlib/HDF5 figure; current package runtime is recorded as available for R16B cartography."
        record["generated_at_utc"] = record.get("generated_at_utc")
        r16.write_json(path, record)
        records[figure_id] = record

    manifest = write_final_manifest(records, env)
    write_final_handoff(manifest)
    write_completion_state(manifest)
    return records


def write_final_manifest(records: Mapping[str, Mapping[str, Any]], env: Mapping[str, Any]) -> dict[str, Any]:
    figure_order = ["A", "B", "C", "D1", "D2", "E", "F", "S1"]
    figures: dict[str, Mapping[str, Any]] = {}
    for key in figure_order:
        if key in records:
            figures[key] = records[key]
            continue
        basename = {"D1": "figure_D1_eta_space_time", "D2": "figure_D2_wave_profiles_to_shore", "S1": "figure_S1_longitudinal_bathymetry"}[key]
        figures[key] = r16.read_json(r16.PROVENANCE_ROOT / f"{basename}.provenance.json")

    gis_layers_path = r16.PROVENANCE_ROOT / "r16_gis_layer_manifest.json"
    build_status_path = r16.PROVENANCE_ROOT / "qgis_project_build_status.json"
    terrain_status_path = r16.PROVENANCE_ROOT / "r16b_derived_terrain_status.json"
    manifest = {
        "schema": {"name": "tsunami.r16.publication_figure_manifest", "version": "2.0.0"},
        "generated_at_utc": r16.utc_now(),
        "starting_head": r16.STARTING_HEAD,
        "git_sha": r16.git_sha(),
        "branch": r16.current_branch(),
        "worktree": r16.REPO_ROOT.as_posix(),
        "qgis": dict(env),
        "qgis_runtime_history": [
            {
                "phase": "R16 initial package",
                "status": "HISTORICAL_RUNTIME_UNAVAILABLE_RETIRED",
                "note": "The original R16 package recorded QGIS runtime unavailability and staged layers/scripts instead of substituting non-QGIS final cartography.",
            },
            {
                "phase": "R16B completion",
                "status": env["status"],
                "qgis_version": env.get("qgis_version"),
                "note": "QGIS 4.2.0 runtime available; editable project and headless exports completed.",
            },
        ],
        "qgis_project": {
            "path": r16.QGIS_PROJECT.as_posix(),
            "status": "COMPLETE",
            "record": r16.file_record(r16.QGIS_PROJECT),
            "groups": list(GROUP_NAMES.values()),
            "layouts": LAYOUT_NAMES,
            "target_runtime": "QGIS 4.2",
        },
        "gis_layers": r16.read_json(gis_layers_path) if gis_layers_path.is_file() else {},
        "qgis_project_build": r16.read_json(build_status_path) if build_status_path.is_file() else {},
        "derived_terrain": r16.read_json(terrain_status_path) if terrain_status_path.is_file() else {},
        "figures": {key: dict(figures[key]) for key in figure_order},
        "visual_qc_classification": {
            key: figures[key].get("visual_qc_classification", "UNCLASSIFIED") for key in figure_order
        },
        "task_states": {
            "Figure A": figures["A"]["status"],
            "Figure B": figures["B"]["status"],
            "Figure C": figures["C"]["status"],
            "Figure D1": figures["D1"]["status"],
            "Figure D2": figures["D2"]["status"],
            "Figure E": figures["E"]["status"],
            "Figure F": figures["F"]["status"],
            "Supporting bathymetry": figures["S1"]["status"],
            "QGIS project": "COMPLETE",
            "print layouts": "COMPLETE",
            "provenance": "COMPLETE",
            "handoff": "COMPLETE",
        },
        "source_authority": r16.source_authority_records(),
        "scientific_authority": r16.SCIENTIFIC_AUTHORITY,
        "r15_observation_register_summary": {
            "observations": 29,
            "DIRECT": 0,
            "PROXY": 1,
            "TARGET_ONLY": 28,
            "NOWPHAS_802G_distance_to_corridor_km": 12.3,
            "DART_21418_distance_to_corridor_km": 545.0,
        },
        "confirmations": {
            "no_regional_simulation_launched": True,
            "no_h250": True,
            "no_temporal_convergence": True,
            "no_calibration": True,
            "no_local3d_replay": True,
            "no_openfoam_retuning": True,
            "no_hdf5_architecture_work": True,
            "no_video_qr_work": True,
            "no_synthetic_geography": True,
            "no_fabricated_bathymetry": True,
            "corridor_geometry_unchanged": True,
        },
    }
    r16.write_json(r16.PROVENANCE_ROOT / "publication_figure_manifest.json", manifest)
    write_manifest_markdown(manifest)
    return manifest


def write_manifest_markdown(manifest: Mapping[str, Any]) -> None:
    lines = [
        "# R16 Publication Figure Manifest",
        "",
        f"Generated: `{manifest['generated_at_utc']}`",
        f"Branch: `{manifest['branch']}`",
        f"Git SHA: `{manifest['git_sha']}`",
        "",
        "## QGIS Status",
        "",
        f"Status: **{manifest['qgis']['status']}** using `{manifest['qgis'].get('qgis_version')}`.",
        "The original R16 package recorded QGIS unavailability; R16B completes those previously blocked cartographic items with QGIS 4.2.",
        "",
        "| Figure | Status | QC class | Output summary |",
        "|---|---|---|---|",
    ]
    for key in ["A", "B", "C", "D1", "D2", "E", "F", "S1"]:
        record = manifest["figures"][key]
        outputs = ", ".join(record.get("outputs", [])) if record.get("outputs") else "none"
        lines.append(f"| {key} | {record['status']} | {record.get('visual_qc_classification', '')} | {outputs} |")
    lines.extend(
        [
            "",
            "## Data Authority",
            "",
            "- Bathymetry/topography source: ETOPO 2022 WGS84 + EGM2008 tile already used by the simulation preprocessing lineage.",
            "- Regional result: frozen R10 h400 limited_linear HDF5, no rerun.",
            "- Validation register: 29 observations, 0 DIRECT, 1 PROXY, 28 TARGET_ONLY.",
            "- Figure C uses a direct VTK offscreen terrain render because PyVista is unavailable and QGIS 4.2 headless 3D layout export is not exposed in this runtime.",
        ]
    )
    (r16.PROVENANCE_ROOT / "publication_figure_manifest.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_final_handoff(manifest: Mapping[str, Any]) -> None:
    rows = {
        "A": ("HERO", "Poster opening map", "Tohoku event and accepted Kamaishi corridor geography.", "Use with R10 h400 uncertainty caveat."),
        "B": ("HERO", "Poster real-case domain/bathymetry", "Real bathymetry/topography context for the accepted corridor.", "Context only; not spatial qualification."),
        "D1": ("PRIMARY", "Poster or report wave-evolution panel", "Frozen R10 h400 eta evolution sampled along the corridor toward Kamaishi.", "Best available numerically uncertain, uncalibrated, not historically validated."),
        "D2": ("SECONDARY", "Report page 2 companion to D1", "Selected eta profiles show waveform evolution toward the nearshore interface.", "Nearest-cell centreline sampling; no smoothing or rerun."),
        "E": ("PRIMARY", "Framework figure", "Geographic Regional2D to conceptual Local3D one-way forcing relationship.", "Local3D current-generation remains unresolved."),
        "F": ("PRIMARY", "Validation status figure", "R15 validation targets relative to the current corridor.", "0 DIRECT, 1 PROXY, 28 TARGET_ONLY preserved."),
        "C": ("SECONDARY", "Report or optional poster visual", "Oblique real-terrain view of the corridor.", "Visualisation with vertical exaggeration; not a solver result."),
        "S1": ("REPORT_ONLY", "Two-page report bathymetry context", "Centreline bed profile used by D1/D2.", "Wet-conditioned mesh profile, not independent coastal topography."),
    }
    lines = [
        "# R16 Publication Figure Handoff",
        "",
        "R16B completes the QGIS publication cartography that was blocked in the original R16 run. The original QGIS-runtime blocker remains recorded in provenance history; the current package uses QGIS 4.2 headless exports.",
        "",
        "| Figure | Priority | Status | QC class | Recommended use | Caption | Caveat |",
        "|---|---|---|---|---|---|---|",
    ]
    for key in ["A", "B", "D1", "D2", "E", "F", "C", "S1"]:
        record = manifest["figures"][key]
        priority, use, caption, caveat = rows[key]
        lines.append(
            f"| {key} | {priority} | {record['status']} | {record.get('visual_qc_classification', '')} | {use} | {caption} | {caveat} |"
        )
    lines.extend(
        [
            "",
            "## Report Page 2 Recommendations",
            "",
            "- Best corridor/domain figure: Figure B.",
            "- Best bathymetry figure: Figure B, with S1 as report-only longitudinal support.",
            "- Best hybrid-domain schematic: Figure E.",
            "- Best wave-evolution figure: Figure D1, with D2 as the companion explanatory panel.",
            "- Best validation figure: Figure F.",
            "",
            "## Allowed Claims",
            "",
            "The package can claim reproducible QGIS cartography, editable project/layouts and completed frozen-result propagation figures. It must not claim full historical validation, physical calibration, h250/h300/h400 spatial qualification, or Local3D current-generation closure.",
        ]
    )
    (r16.DOCS_ROOT / "r16_publication_figure_handoff.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_completion_state(manifest: Mapping[str, Any]) -> None:
    terminal = {"COMPLETE", "BLOCKED_BY_SOURCE_DATA", "NOT_APPLICABLE"}
    payload = {
        "schema": {"name": "tsunami.r16.completion_state", "version": "2.0.0"},
        "generated_at_utc": r16.utc_now(),
        "task_states": manifest["task_states"],
        "no_early_exit": all(status in terminal for status in manifest["task_states"].values()),
        "qgis_runtime_blocked": False,
    }
    r16.write_json(r16.PROVENANCE_ROOT / "r16_completion_state.json", payload)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allow-blocked", action="store_true", help="Exit 0 while recording QGIS_RUNTIME_BLOCKED.")
    args = parser.parse_args(argv)
    payload = export_layouts(allow_blocked=args.allow_blocked)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
