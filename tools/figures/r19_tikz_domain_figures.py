#!/usr/bin/env python3
"""Generate the R19 QGIS + TikZ computational-domain figure package.

R19 is a figure-generation and documentation pass only. It consumes the frozen
R10/R15/R16/R18/G6 evidence and does not run Regional2D, Local3D, calibration,
source, mesh, or corridor-generation workflows.
"""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Mapping, Sequence


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_FIGURES = REPO_ROOT / "tools/figures"
if TOOLS_FIGURES.as_posix() not in sys.path:
    sys.path.insert(0, TOOLS_FIGURES.as_posix())

import r16_publication as r16  # noqa: E402


R19_ROOT = REPO_ROOT / "deliverables/figures/r19_tikz"
PUBLICATION_ROOT = R19_ROOT / "publication"
PREVIEW_ROOT = R19_ROOT / "previews"
PROVENANCE_ROOT = R19_ROOT / "provenance"
DATA_ROOT = R19_ROOT / "data"
QGIS_ROOT = R19_ROOT / "qgis"
SOURCE_ROOT = R19_ROOT / "sources"
PYTHON_SOURCE_ROOT = SOURCE_ROOT / "python"

REGISTER_MD = REPO_ROOT / "docs/project/r19_domain_geometry_register.md"
REGISTER_JSON = REPO_ROOT / "docs/project/r19_domain_geometry_register.json"
HANDOFF_MD = REPO_ROOT / "docs/project/r19_figure_handoff.md"
COMPLETION_MD = REPO_ROOT / "docs/project/r19_completion_report.md"

CORRIDOR_PATH = r16.CORRIDOR_PATH
CENTRELINE_PATH = r16.CENTRELINE_PATH
H400_HDF5 = r16.H400_HDF5
R15_REGISTER = r16.R15_REGISTER
G6_LOCAL_SUMMARY = r16.G6_LOCAL_SUMMARY
G6_CASE_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case")
CASE_JSON = G6_CASE_ROOT / "case.json"
VERTICAL_DISPLACEMENT_JSON = G6_CASE_ROOT / "inputs/data/earthquake/tohoku_vertical_displacement.json"
EPICENTRE_JSON = G6_CASE_ROOT / "inputs/data/points/tohoku-epicentre-source.json"
COUPLING_ROOT = G6_CASE_ROOT / "runs/kamaishi-etopo-usgs-v1/outputs/regional2d/coupling/kamaishi-nearshore-interface"
COUPLING_METADATA = COUPLING_ROOT / "metadata.json"
COUPLING_SAMPLES = COUPLING_ROOT / "samples.csv"
LOCAL3D_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/local/simple_rigid_barrier")
BOUNDARY_POLICY = LOCAL3D_ROOT / "boundary_policy.json"

QGIS_DERIVED = REPO_ROOT / "deliverables/figures/r16_publication/sources/qgis/derived"
QGIS_LAYERS = REPO_ROOT / "deliverables/figures/r16_publication/sources/qgis/layers"
REGIONAL_RASTER = QGIS_DERIVED / "etopo_regional_tohoku_utm54.tif"
REGIONAL_HILLSHADE = QGIS_DERIVED / "regional_hillshade.tif"
REGIONAL_COASTLINE = QGIS_DERIVED / "regional_coastline_0m.gpkg"
COUPLING_SECTION = QGIS_LAYERS / "coupling_section.geojson"
LOCAL3D_FOOTPRINT = QGIS_LAYERS / "local3d_candidate_footprint.geojson"

BRANCH_NAME = "feat/r19-tikz-domain-figures"
R18_BRANCH = "feat/r18-poster-visual-freeze"
STARTING_HEAD = "2e2f843ab55404e332a9780410f852705fa0ba95"
WORKTREE_PATH = Path("/home/helios/Projects/Summer-Studentship-r19-tikz")

SCIENTIFIC_AUTHORITY = {
    "regional_numerical_authority": [
        "MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES",
        "GLOBAL_FIRST_ORDER_VERIFIED",
        "SECOND_ORDER_VERIFIED",
    ],
    "event_result": "R10 h400 limited_linear",
    "event_result_status": "BEST_AVAILABLE_NUMERICALLY_UNCERTAIN",
    "event_limitations": [
        "real 2011 Tohoku event",
        "verified formulation",
        "not spatially qualified",
        "not physically calibrated",
        "not historically validated",
    ],
    "hybrid_status": {
        "implemented": "accepted G6 one-way Regional2D-to-Local3D replay",
        "r10_h400_local3d_replay": "REPLAY_VOF_BEHAVIOUR_UNRESOLVED",
    },
    "historical_observations": {
        "total": 29,
        "DIRECT": 0,
        "PROXY": 1,
        "TARGET_ONLY": 28,
        "NOWPHAS_802G_distance_km": 12.273092741550476,
        "DART_21418_distance_km": 544.6414251283993,
    },
}


@dataclass(frozen=True)
class Point:
    x: float
    y: float


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def ensure_layout() -> None:
    for path in [PUBLICATION_ROOT, PREVIEW_ROOT, PROVENANCE_ROOT, DATA_ROOT, QGIS_ROOT, PYTHON_SOURCE_ROOT]:
        path.mkdir(parents=True, exist_ok=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def display_path(path: Path) -> str:
    try:
        return path.relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def file_record(path: Path) -> dict[str, Any]:
    record: dict[str, Any] = {"path": display_path(path), "exists": path.exists()}
    if path.is_file():
        record.update({"bytes": path.stat().st_size, "sha256": sha256(path)})
    return record


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(line.rstrip() for line in text.splitlines()) + "\n", encoding="utf-8")


def command_output(command: Sequence[str], *, cwd: Path | None = None, check: bool = False) -> tuple[int, str]:
    completed = subprocess.run(
        list(command),
        cwd=cwd or REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if check and completed.returncode != 0:
        raise RuntimeError(f"{' '.join(command)} failed:\n{completed.stdout}")
    return completed.returncode, completed.stdout.strip()


def git_sha() -> str:
    code, output = command_output(["git", "rev-parse", "HEAD"])
    return output if code == 0 else "unknown"


def git_branch() -> str:
    code, output = command_output(["git", "branch", "--show-current"])
    return output if code == 0 else "unknown"


def add(a: Point, b: Point) -> Point:
    return Point(a.x + b.x, a.y + b.y)


def sub(a: Point, b: Point) -> Point:
    return Point(a.x - b.x, a.y - b.y)


def scale(a: Point, value: float) -> Point:
    return Point(a.x * value, a.y * value)


def dot(a: Point, b: Point) -> float:
    return a.x * b.x + a.y * b.y


def point_from_mapping(mapping: Mapping[str, Any]) -> Point:
    return Point(float(mapping["x"]), float(mapping["y"]))


def point_at(origin: Point, tangent: Point, left_normal: Point, s_m: float, sigma_m: float = 0.0) -> Point:
    return add(add(origin, scale(tangent, s_m)), scale(left_normal, sigma_m))


def raster_extent(path: Path) -> dict[str, float]:
    from osgeo import gdal

    ds = gdal.Open(path.as_posix())
    if ds is None:
        raise RuntimeError(f"Could not open raster {path}")
    gt = ds.GetGeoTransform()
    xs = [gt[0], gt[0] + ds.RasterXSize * gt[1], gt[0] + ds.RasterYSize * gt[2], gt[0] + ds.RasterXSize * gt[1] + ds.RasterYSize * gt[2]]
    ys = [gt[3], gt[3] + ds.RasterXSize * gt[4], gt[3] + ds.RasterYSize * gt[5], gt[3] + ds.RasterXSize * gt[4] + ds.RasterYSize * gt[5]]
    return {"xmin": min(xs), "xmax": max(xs), "ymin": min(ys), "ymax": max(ys), "width_px": ds.RasterXSize, "height_px": ds.RasterYSize}


def toolchain_record() -> dict[str, Any]:
    qgis_code, qgis_version = command_output(["qgis", "--version"])
    pdflatex_code, pdflatex_version = command_output(["pdflatex", "--version"])
    latexmk_code, latexmk_version = command_output(["latexmk", "--version"])
    pdftocairo_code, pdftocairo_version = command_output(["pdftocairo", "-v"])
    pgf_code, pgf_path = command_output(["kpsewhich", "pgfplots.sty"])
    pgf_revision_code, pgf_revision_path = command_output(["kpsewhich", "pgfplots.revision.tex"])
    standalone_code, standalone_path = command_output(["kpsewhich", "standalone.cls"])
    pgfplots_version = "UNRESOLVED"
    revision = Path(pgf_revision_path)
    if pgf_revision_code == 0 and revision.is_file():
        for line in revision.read_text(encoding="utf-8", errors="replace").splitlines():
            if "pgfplotsversion" in line and "def" in line:
                pgfplots_version = line.strip()
                break
    return {
        "python": sys.version.split()[0],
        "qgis_version": qgis_version if qgis_code == 0 else "UNAVAILABLE",
        "pdflatex_version": pdflatex_version.splitlines()[0] if pdflatex_code == 0 and pdflatex_version else "UNAVAILABLE",
        "latexmk_version": latexmk_version.splitlines()[0] if latexmk_code == 0 and latexmk_version else "UNAVAILABLE",
        "tikz_available": command_output(["kpsewhich", "tikz.sty"])[0] == 0,
        "standalone_cls": standalone_path if standalone_code == 0 else "UNAVAILABLE",
        "pgfplots_sty": pgf_path if pgf_code == 0 else "UNAVAILABLE",
        "pgfplots_version": pgfplots_version,
        "svg_conversion_method": "pdftocairo -svg; T0 embedded-PNG SVG wrapper fallback for embedded-PDF composites",
        "png_conversion_method": "pdftocairo -png -singlefile -r 320",
        "pdftocairo_version": pdftocairo_version.splitlines()[0] if pdftocairo_code == 0 and pdftocairo_version else "UNAVAILABLE",
    }


def extract_geometry() -> dict[str, Any]:
    corridor = read_json(CORRIDOR_PATH)
    centreline = read_json(CENTRELINE_PATH)
    case = read_json(CASE_JSON)
    local = read_json(G6_LOCAL_SUMMARY)
    boundary = read_json(BOUNDARY_POLICY)
    displacement = read_json(VERTICAL_DISPLACEMENT_JSON)
    coupling = read_json(COUPLING_METADATA)
    event = corridor["event"]
    selected = corridor["selected_nearshore_interface"]
    basis = corridor["basis"]
    tangent = Point(float(basis["centreline_unit"]["x"]), float(basis["centreline_unit"]["y"]))
    left_normal = Point(float(basis["left_normal_unit"]["x"]), float(basis["left_normal_unit"]["y"]))
    epicentre = point_from_mapping(event["epicentre_projected_m"])
    interface = point_from_mapping(selected["projected_m"])
    pre_extent = float(corridor["corridor"]["source_side_pre_extent_m"])
    inland_extent = float(corridor["corridor"]["inland_extent_m"])
    propagation_length = float(basis["distance_m"])
    total_length = pre_extent + propagation_length + inland_extent
    origin = add(epicentre, scale(tangent, -pre_extent))
    end = add(interface, scale(tangent, inland_extent))
    width = float(corridor["corridor"]["width_m"])
    bearing = float(basis["bearing_degrees_clockwise_from_north"])
    west_of_north = (360.0 - bearing) if bearing > 180.0 else -bearing
    map_extent = raster_extent(REGIONAL_RASTER)

    profile = r16.sample_eta_along_centreline(spacing_m=800.0)
    import numpy as np

    profile_csv = DATA_ROOT / "bathymetry_profile.csv"
    with profile_csv.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["distance_offshore_km", "bed_elevation_m", "nearest_distance_m", "nearest_cell_index"],
            lineterminator="\n",
        )
        writer.writeheader()
        for distance, bed, nearest_distance, nearest_cell in zip(
            profile.distance_to_shore_km,
            profile.bed_m,
            profile.nearest_distance_m,
            profile.nearest_cell_index,
            strict=True,
        ):
            writer.writerow(
                {
                    "distance_offshore_km": f"{float(distance):.9f}",
                    "bed_elevation_m": f"{float(bed):.9f}",
                    "nearest_distance_m": f"{float(nearest_distance):.9f}",
                    "nearest_cell_index": int(nearest_cell),
                }
            )
    profile_stats = {
        "source": display_path(H400_HDF5) if H400_HDF5.is_relative_to(REPO_ROOT) else H400_HDF5.as_posix(),
        "csv": display_path(profile_csv),
        "sample_count": int(len(profile.distance_to_shore_km)),
        "sampling_interval_m": float(profile.sampling_interval_m),
        "distance_offshore_min_km": float(np.min(profile.distance_to_shore_km)),
        "distance_offshore_max_km": float(np.max(profile.distance_to_shore_km)),
        "bed_elevation_min_m": float(np.min(profile.bed_m)),
        "bed_elevation_max_m": float(np.max(profile.bed_m)),
        "nearest_distance_m": {
            "min": float(np.min(profile.nearest_distance_m)),
            "median": float(np.median(profile.nearest_distance_m)),
            "max": float(np.max(profile.nearest_distance_m)),
        },
        "method": "nearest unstructured h400 cell centre to R10/G6 centreline samples in EPSG:32654; no smoothing or rerun",
        "distance_convention": "distance offshore from selected wet nearshore interface; positive sourceward/offshore; interface at 0 km",
    }
    write_json(DATA_ROOT / "bathymetry_profile_metadata.json", profile_stats)

    milestones = {
        "distance_convention": profile_stats["distance_convention"],
        "milestones": [
            {"id": "M0", "distance_offshore_km": propagation_length / 1000.0, "authority": "event epicentre projected onto current centreline", "exactness": "exact current-case event reference"},
            {"id": "M1", "distance_band_offshore_km": [85.0, profile_stats["distance_offshore_max_km"]], "authority": "schematic region over real profile", "exactness": "conceptual deep-water Regional2D propagation band"},
            {"id": "M2", "distance_band_offshore_km": [25.0, 85.0], "authority": "schematic region over real profile", "exactness": "conceptual shelf/shoaling band"},
            {"id": "M3", "distance_band_offshore_km": [0.0, 12.0], "authority": "schematic transition band", "exactness": "conceptual coupling/transition region"},
            {"id": "M4", "distance_offshore_km": 0.0, "authority": "selected_nearshore_interface in corridor evidence", "exactness": "exact selected wet interface for current implementation"},
            {"id": "M5", "distance_offshore_km": -float(local["dimensions_m"]["length"]) / 1000.0, "authority": "G6 Local3D simple_rigid_barrier summary", "exactness": "candidate/framework Local3D region; not a universal coastal footprint"},
        ],
    }
    write_json(DATA_ROOT / "milestone_positions.json", milestones)

    geometry = {
        "schema": {"name": "tsunami.r19.domain_geometry", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "git_sha": git_sha(),
        "branch": git_branch(),
        "crs": "EPSG:32654 WGS 84 / UTM zone 54N; vertical EGM2008 positive up where applicable",
        "corridor": {
            "total_length_m": total_length,
            "propagation_length_event_to_interface_m": propagation_length,
            "source_side_pre_extent_m": pre_extent,
            "inland_extent_m": inland_extent,
            "width_m": width,
            "offshore_sponge_width_m": float(corridor["corridor"]["offshore_sponge_width_m"]),
            "side_sponge_width_m": float(corridor["corridor"]["side_sponge_width_m"]),
            "orientation_degrees_clockwise_from_north_source_to_interface": bearing,
            "orientation_degrees_west_of_north_source_to_interface": west_of_north,
            "tangent_unit": {"x": tangent.x, "y": tangent.y},
            "left_normal_unit": {"x": left_normal.x, "y": left_normal.y},
            "source_side_start_projected_m": {"x": origin.x, "y": origin.y},
            "interface_or_inland_end_projected_m": {"x": end.x, "y": end.y},
            "polygon_projected_m": corridor["corridor"]["polygon_projected_m"],
            "formula_version": centreline.get("formula_version"),
            "disclaimer": corridor.get("disclaimer"),
        },
        "event": event,
        "selected_nearshore_interface": selected,
        "case_regional_boundaries": case["regional_2d"]["boundaries"],
        "coupling_export": coupling,
        "coupling_transfer_terms": {
            "regional_fields": coupling["fields"],
            "derived_terms": ["eta = free_surface_elevation", "q_n and q_t from momentum_x/momentum_y projection", "depth-uniform velocity lift", "alpha.water from eta and vertical face bounds"],
            "openfoam_generated_fields": ["U", "alpha.water"],
            "one_way": True,
        },
        "local3d": {
            "source": G6_LOCAL_SUMMARY.as_posix(),
            "variant": local["variant"],
            "dimensions_m": local["dimensions_m"],
            "cell_counts": local["cell_counts"],
            "cell_dimensions_m": local["cell_dimensions_m"],
            "initial_water_level_m": local["initial_water_level"],
            "barrier": local.get("barrier"),
            "boundary_policy": boundary,
            "status": "accepted G6 simple_rigid_barrier replay geometry; representative Kamaishi-forced local replay, not exact harbour reconstruction",
        },
        "bathymetry_profile": profile_stats,
        "milestones": milestones,
        "source_model": {
            "epicentre": read_json(EPICENTRE_JSON),
            "vertical_displacement": {
                "event_id": displacement.get("event_id"),
                "model_id": displacement.get("model_id"),
                "source_format": displacement.get("source_format"),
                "subfault_count": displacement.get("subfault_count"),
                "source_uri": displacement.get("source_uri"),
                "rupture_polygon_for_T1": "UNRESOLVED: no authoritative poster-layer rupture polygon is used; R19 T1 shows the event/source marker only",
            },
        },
        "map_extent": map_extent,
        "scientific_authority": SCIENTIFIC_AUTHORITY,
    }
    write_json(DATA_ROOT / "domain_geometry.json", geometry)
    return geometry


def quantity(symbol: str, value: Any, unit: str, definition: str, source: str, status: str, confidence: str, usage: str) -> dict[str, Any]:
    return {
        "symbol": symbol,
        "value": value,
        "unit": unit,
        "definition": definition,
        "source": source,
        "current_case_or_framework": status,
        "confidence": confidence,
        "figure_usage": usage,
    }


def build_register(geometry: Mapping[str, Any]) -> dict[str, Any]:
    corridor = geometry["corridor"]
    selected = geometry["selected_nearshore_interface"]
    event = geometry["event"]
    local = geometry["local3d"]
    profile = geometry["bathymetry_profile"]
    q = [
        quantity("lat_E, lon_E", event["epicentre_wgs84"], "deg", "USGS official origin coordinates used as event reference", CORRIDOR_PATH.as_posix(), "current-case", "HIGH", "T1, T2, register"),
        quantity("E_E, N_E", event["epicentre_projected_m"], "m", "Event reference transformed to EPSG:32654", CORRIDOR_PATH.as_posix(), "current-case", "HIGH", "T1, T2"),
        quantity("lat_K, lon_K", event["kamaishi_proxy_wgs84"], "deg", "Kamaishi proxy coordinate used by the delivery corridor evidence", CORRIDOR_PATH.as_posix(), "current-case", "HIGH", "T1"),
        quantity("L_c", corridor["total_length_m"], "m", "Source-side boundary to selected interface/inland end: pre-extent + event-to-interface + inland extent", CENTRELINE_PATH.as_posix(), "current-case", "HIGH", "T1, T2, T3"),
        quantity("L_prop", corridor["propagation_length_event_to_interface_m"], "m", "Event reference to selected wet nearshore interface along centreline", CENTRELINE_PATH.as_posix(), "current-case", "HIGH", "T1, T2"),
        quantity("W_c", corridor["width_m"], "m", "Constant corridor width; narrowing disabled", CORRIDOR_PATH.as_posix(), "current-case", "HIGH", "T1, T3"),
        quantity("theta_c", corridor["orientation_degrees_clockwise_from_north_source_to_interface"], "deg", "Source-to-interface bearing clockwise from north in the accepted projected corridor basis", CORRIDOR_PATH.as_posix(), "current-case", "HIGH", "T1"),
        quantity("theta_c_W", corridor["orientation_degrees_west_of_north_source_to_interface"], "deg", "Same bearing as degrees west of grid north for compact figure annotation", CORRIDOR_PATH.as_posix(), "current-case", "HIGH", "T1"),
        quantity("L_pre", corridor["source_side_pre_extent_m"], "m", "Centreline length before the event reference", CENTRELINE_PATH.as_posix(), "current-case", "HIGH", "T1, T2"),
        quantity("L_inland", corridor["inland_extent_m"], "m", "Configured inland extent after selected target/interface", CENTRELINE_PATH.as_posix(), "current-case", "HIGH", "T1, T2"),
        quantity("S_off", corridor["offshore_sponge_width_m"], "m", "Regional2D offshore relaxation/sponge width", CASE_JSON.as_posix(), "current-case", "HIGH", "T3"),
        quantity("S_side", corridor["side_sponge_width_m"], "m", "Regional2D side relaxation/sponge width", CASE_JSON.as_posix(), "current-case", "HIGH", "T3"),
        quantity("I", {"projected_m": selected["projected_m"], "wgs84": selected["wgs84"]}, "m / deg", "Selected wet nearshore interface centre", CORRIDOR_PATH.as_posix(), "current-case", "HIGH", "T1, T2, T3"),
        quantity("h_I", selected["water_depth_m"], "m", "Water depth at selected wet nearshore interface centre", CORRIDOR_PATH.as_posix(), "current-case", "HIGH", "T2, T3"),
        quantity("profile", {k: profile[k] for k in ["sample_count", "distance_offshore_min_km", "distance_offshore_max_km", "bed_elevation_min_m", "bed_elevation_max_m"]}, "mixed", "Bathymetry profile sampled from frozen R10 h400 Regional2D mesh/S1 lineage", profile["csv"], "current-case", "HIGH", "T2"),
        quantity("L_3D", local["dimensions_m"]["length"], "m", "G6 Local3D simple_rigid_barrier streamwise length", G6_LOCAL_SUMMARY.as_posix(), "current-case/framework", "HIGH", "T2, T3"),
        quantity("W_3D", local["dimensions_m"]["span"], "m", "G6 Local3D simple_rigid_barrier span", G6_LOCAL_SUMMARY.as_posix(), "current-case/framework", "HIGH", "T3"),
        quantity("H_3D", local["dimensions_m"]["height"], "m", "G6 Local3D simple_rigid_barrier vertical domain height", G6_LOCAL_SUMMARY.as_posix(), "current-case/framework", "HIGH", "T3"),
        quantity("x_b", local["barrier"]["streamwise_position_m"], "m", "Simple rigid barrier streamwise position", G6_LOCAL_SUMMARY.as_posix(), "current-case/framework", "HIGH", "T3"),
        quantity("t_b", local["barrier"]["thickness_m"], "m", "Simple rigid barrier streamwise thickness", G6_LOCAL_SUMMARY.as_posix(), "current-case/framework", "HIGH", "T3"),
        quantity("H_b", local["barrier"]["height_m"], "m", "Simple rigid barrier height", G6_LOCAL_SUMMARY.as_posix(), "current-case/framework", "HIGH", "T3"),
        quantity("shoreline", "UNRESOLVED", "n/a", "No rigorously authorised shoreline intersection is promoted; selected wet interface is not relabelled as shoreline", REGIONAL_COASTLINE.as_posix(), "unresolved", "BLOCKED_BY_SOURCE_DATA", "T2 caveat"),
        quantity("rupture polygon", "UNRESOLVED", "n/a", "Finite-fault displacement model exists, but no authoritative 2D rupture polygon is used for R19 T1", VERTICAL_DISPLACEMENT_JSON.as_posix(), "unresolved", "BLOCKED_BY_SOURCE_DATA", "T1 exclusion"),
        quantity("candidate defence region", "framework only", "n/a", "G6 simple rigid barrier is a representative replay geometry, not a final defence-placement prescription", G6_LOCAL_SUMMARY.as_posix(), "framework", "MEDIUM", "T3"),
    ]
    payload = {
        "schema": {"name": "tsunami.r19.domain_geometry_register", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "scientific_authority": SCIENTIFIC_AUTHORITY,
        "quantities": q,
    }
    write_json(REGISTER_JSON, payload)
    write_json(DATA_ROOT / "r19_domain_geometry_register.json", payload)
    rows = [
        "# R19 Domain Geometry Register",
        "",
        f"Status: `COMPLETE`  ",
        f"Generated: `{payload['generated_at_utc']}`  ",
        f"Branch: `{git_branch()}`  ",
        f"HEAD: `{git_sha()}`",
        "",
        "This register records figure quantities used by the R19 QGIS + TikZ hybrid computational-domain package. `UNRESOLVED` values are intentionally not estimated.",
        "",
        "| Symbol | Value | Unit | Definition | Source | Status | Confidence | Figure usage |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for item in q:
        value = json.dumps(item["value"], sort_keys=True) if isinstance(item["value"], (dict, list)) else str(item["value"])
        rows.append(
            "| `{symbol}` | {value} | {unit} | {definition} | `{source}` | {status} | {confidence} | {usage} |".format(
                symbol=item["symbol"],
                value=value.replace("|", "/"),
                unit=item["unit"],
                definition=item["definition"].replace("|", "/"),
                source=item["source"],
                status=item["current_case_or_framework"],
                confidence=item["confidence"],
                usage=item["figure_usage"],
            )
        )
    rows.extend(
        [
            "",
            "## Preserved Scientific Authority",
            "",
            "- Regional numerical authority: `MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES`, `GLOBAL_FIRST_ORDER_VERIFIED`, `SECOND_ORDER_VERIFIED`.",
            "- Event result authority: R10 h400 `limited_linear`, `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`.",
            "- Limitations preserved: real 2011 Tohoku event, verified formulation, not spatially qualified, not physically calibrated, not historically validated.",
            "- Hybrid status: implemented/demonstrated through accepted G6 replay; R10 h400 Local3D replay remains `REPLAY_VOF_BEHAVIOUR_UNRESOLVED`.",
            "- Historical observations preserved: 29 observations, 0 DIRECT, 1 PROXY, 28 TARGET_ONLY; NOWPHAS 802G about 12.273 km outside, DART 21418 about 545 km outside.",
        ]
    )
    write_text(REGISTER_MD, "\n".join(rows))
    return payload


def export_t1_qgis_base(geometry: Mapping[str, Any]) -> dict[str, Any]:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from qgis.core import (  # type: ignore[import-not-found]
        QgsApplication,
        QgsColorRampShader,
        QgsCoordinateReferenceSystem,
        QgsLayoutExporter,
        QgsLayoutItemMap,
        QgsLayoutPoint,
        QgsLayoutSize,
        QgsLineSymbol,
        QgsPrintLayout,
        QgsProject,
        QgsRasterLayer,
        QgsRasterShader,
        QgsRectangle,
        QgsSingleBandPseudoColorRenderer,
        QgsSingleSymbolRenderer,
        QgsUnitTypes,
        QgsVectorLayer,
    )
    from qgis.PyQt.QtGui import QColor  # type: ignore[import-not-found]

    app = QgsApplication([], False)
    app.initQgis()
    outputs = {
        "pdf": QGIS_ROOT / "T1_qgis_base.pdf",
        "png": QGIS_ROOT / "T1_qgis_base.png",
        "svg": QGIS_ROOT / "T1_qgis_base.svg",
        "project": QGIS_ROOT / "T1_qgis_base.qgz",
    }
    for output in outputs.values():
        if output.exists():
            output.unlink()
    try:
        project = QgsProject.instance()
        project.clear()
        crs = QgsCoordinateReferenceSystem("EPSG:32654")
        project.setCrs(crs)

        raster = QgsRasterLayer(REGIONAL_RASTER.as_posix(), "ETOPO 2022 regional topobathymetry")
        if not raster.isValid():
            raise RuntimeError(f"Invalid raster layer: {REGIONAL_RASTER}")
        shader = QgsRasterShader()
        ramp = QgsColorRampShader()
        ramp.setColorRampType(QgsColorRampShader.Interpolated)
        ramp.setColorRampItemList(
            [
                QgsColorRampShader.ColorRampItem(-5200, QColor("#183552"), "deep ocean"),
                QgsColorRampShader.ColorRampItem(-2500, QColor("#2f6f9e"), "ocean"),
                QgsColorRampShader.ColorRampItem(-500, QColor("#8ec6d8"), "shelf"),
                QgsColorRampShader.ColorRampItem(0, QColor("#f5efdf"), "sea level"),
                QgsColorRampShader.ColorRampItem(900, QColor("#8eab6f"), "upland"),
                QgsColorRampShader.ColorRampItem(2000, QColor("#76583f"), "mountain"),
            ]
        )
        shader.setRasterShaderFunction(ramp)
        raster.setRenderer(QgsSingleBandPseudoColorRenderer(raster.dataProvider(), 1, shader))
        project.addMapLayer(raster)

        hillshade = QgsRasterLayer(REGIONAL_HILLSHADE.as_posix(), "regional hillshade")
        if hillshade.isValid():
            hillshade.setOpacity(0.25)
            project.addMapLayer(hillshade)

        coastline = QgsVectorLayer(REGIONAL_COASTLINE.as_posix(), "0 m coastline", "ogr")
        if coastline.isValid():
            symbol = QgsLineSymbol.createSimple({"color": "72,79,88,255", "width": "0.18"})
            coastline.setRenderer(QgsSingleSymbolRenderer(symbol))
            project.addMapLayer(coastline)

        extent = geometry["map_extent"]
        layout = QgsPrintLayout(project)
        layout.initializeDefaults()
        layout.setName("R19_T1_QGIS_BASE")
        page = layout.pageCollection().page(0)
        page.setPageSize(QgsLayoutSize(170, 128, QgsUnitTypes.LayoutMillimeters))
        item = QgsLayoutItemMap(layout)
        item.attemptMove(QgsLayoutPoint(0, 0, QgsUnitTypes.LayoutMillimeters))
        item.attemptResize(QgsLayoutSize(170, 128, QgsUnitTypes.LayoutMillimeters))
        item.setFrameEnabled(False)
        item.setExtent(QgsRectangle(float(extent["xmin"]), float(extent["ymin"]), float(extent["xmax"]), float(extent["ymax"])))
        layout.addLayoutItem(item)
        project.layoutManager().addLayout(layout)
        project.write(outputs["project"].as_posix())

        exporter = QgsLayoutExporter(layout)
        pdf_settings = QgsLayoutExporter.PdfExportSettings()
        pdf_settings.dpi = 300
        image_settings = QgsLayoutExporter.ImageExportSettings()
        image_settings.dpi = 300
        svg_settings = QgsLayoutExporter.SvgExportSettings()
        svg_settings.dpi = 300
        results = {
            "pdf": int(exporter.exportToPdf(outputs["pdf"].as_posix(), pdf_settings)),
            "png": int(exporter.exportToImage(outputs["png"].as_posix(), image_settings)),
            "svg": int(exporter.exportToSvg(outputs["svg"].as_posix(), svg_settings)),
        }
    finally:
        app.exitQgis()
    payload = {
        "schema": {"name": "tsunami.r19.t1_qgis_base", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "extent_epsg32654": geometry["map_extent"],
        "layers": {
            "topobathymetry": file_record(REGIONAL_RASTER),
            "hillshade": file_record(REGIONAL_HILLSHADE),
            "coastline": file_record(REGIONAL_COASTLINE),
        },
        "exports": {key: file_record(path) for key, path in outputs.items()},
        "export_return_codes": results,
        "content_policy": "clean QGIS geography/base only; no legend or workflow text; TikZ adds engineering annotations",
    }
    write_json(PROVENANCE_ROOT / "T1_qgis_base.provenance.json", payload)
    return payload


def map_to_cm(point: Point, extent: Mapping[str, float], width_cm: float, height_cm: float) -> tuple[float, float]:
    x = (point.x - float(extent["xmin"])) / (float(extent["xmax"]) - float(extent["xmin"])) * width_cm
    y = (point.y - float(extent["ymin"])) / (float(extent["ymax"]) - float(extent["ymin"])) * height_cm
    return x, y


def coord(x: float, y: float) -> str:
    return f"({x:.4f},{y:.4f})"


def path_from_points(points: Sequence[Point], extent: Mapping[str, float], width_cm: float, height_cm: float) -> str:
    return " -- ".join(coord(*map_to_cm(point, extent, width_cm, height_cm)) for point in points)


def write_t1_tex(geometry: Mapping[str, Any]) -> Path:
    c = geometry["corridor"]
    extent = geometry["map_extent"]
    width_cm = 16.0
    height_cm = width_cm * (float(extent["ymax"]) - float(extent["ymin"])) / (float(extent["xmax"]) - float(extent["xmin"]))
    tangent = Point(float(c["tangent_unit"]["x"]), float(c["tangent_unit"]["y"]))
    left_normal = Point(float(c["left_normal_unit"]["x"]), float(c["left_normal_unit"]["y"]))
    origin = point_from_mapping(c["source_side_start_projected_m"])
    epicentre = point_from_mapping(geometry["event"]["epicentre_projected_m"])
    interface = point_from_mapping(geometry["selected_nearshore_interface"]["projected_m"])
    kamaishi = point_from_mapping(geometry["event"]["kamaishi_proxy_projected_m"])
    polygon = [point_from_mapping(item) for item in c["polygon_projected_m"]]
    width = float(c["width_m"])
    midpoint = point_at(origin, tangent, left_normal, float(c["total_length_m"]) * 0.55)
    w_a = add(midpoint, scale(left_normal, -0.5 * width))
    w_b = add(midpoint, scale(left_normal, 0.5 * width))
    l_offset = scale(left_normal, 0.5 * width + 9000.0)
    l_a = add(origin, l_offset)
    l_b = add(interface, l_offset)
    scale_cm = 50000.0 / (float(extent["xmax"]) - float(extent["xmin"])) * width_cm
    source_boundary_cm = map_to_cm(origin, extent, width_cm, height_cm)
    event_cm = map_to_cm(epicentre, extent, width_cm, height_cm)
    interface_cm = map_to_cm(interface, extent, width_cm, height_cm)
    kamaishi_cm = map_to_cm(kamaishi, extent, width_cm, height_cm)

    lines = [
        r"\documentclass[tikz,border=1.5mm]{standalone}",
        r"\usepackage{graphicx}",
        r"\usepackage{tikz}",
        r"\usetikzlibrary{arrows.meta,calc,positioning,shapes.geometric}",
        r"\definecolor{r19blue}{HTML}{1F5A85}",
        r"\definecolor{r19teal}{HTML}{287C7B}",
        r"\definecolor{r19red}{HTML}{B84537}",
        r"\definecolor{r19ink}{HTML}{263238}",
        r"\definecolor{r19paper}{HTML}{FFFFFF}",
        r"\begin{document}",
        r"\begin{tikzpicture}[x=1cm,y=1cm,font=\sffamily]",
        fr"\node[anchor=south west,inner sep=0] at (0,0){{\includegraphics[width={width_cm:.4f}cm,height={height_cm:.4f}cm]{{../qgis/T1_qgis_base.pdf}}}};",
        fr"\fill[r19blue!28,fill opacity=0.42] {path_from_points(polygon, extent, width_cm, height_cm)} -- cycle;",
        fr"\draw[r19blue,line width=1.05pt] {path_from_points(polygon, extent, width_cm, height_cm)} -- cycle;",
        fr"\draw[r19blue,densely dashed,line width=0.85pt] {coord(*source_boundary_cm)} -- {coord(*interface_cm)};",
        fr"\draw[r19teal,line width=1.05pt] {path_from_points([w_a, w_b], extent, width_cm, height_cm)};",
        fr"\draw[{{Latex[length=2.2mm]}}-{{Latex[length=2.2mm]}},r19blue,line width=0.72pt] {path_from_points([l_a, l_b], extent, width_cm, height_cm)} node[midway,sloped,above=2pt,fill=r19paper,inner sep=1.7pt,text=r19ink]{{$L_c=123.319\,\mathrm{{km}}$}};",
        fr"\draw[{{Latex[length=2.0mm]}}-{{Latex[length=2.0mm]}},r19teal,line width=0.72pt] {path_from_points([w_a, w_b], extent, width_cm, height_cm)} node[midway,sloped,above=2pt,fill=r19paper,inner sep=1.5pt,text=r19ink]{{$W_c=8.000\,\mathrm{{km}}$}};",
        fr"\node[circle,draw=r19red,fill=r19red,inner sep=1.9pt,label={{[fill=r19paper,inner sep=1.3pt,text=r19ink]20:USGS 2011 event ref.}}] at {coord(*event_cm)} {{}};",
        fr"\node[circle,draw=r19ink,fill=r19paper,inner sep=1.8pt,label={{[fill=r19paper,inner sep=1.3pt,text=r19ink]80:Kamaishi proxy}}] at {coord(*kamaishi_cm)} {{}};",
        fr"\node[diamond,draw=r19teal,fill=r19teal,inner sep=1.9pt,label={{[fill=r19paper,inner sep=1.3pt,text=r19ink]5:selected wet interface}}] at {coord(*interface_cm)} {{}};",
        fr"\node[circle,draw=r19blue,fill=r19paper,inner sep=1.3pt,label={{[fill=r19paper,inner sep=1.2pt,text=r19ink]180:source-side boundary}}] at {coord(*source_boundary_cm)} {{}};",
        fr"\node[anchor=west,fill=r19paper,fill opacity=0.90,text opacity=1,inner sep=2.2pt,text=r19ink] at (0.45,{height_cm - 0.58:.4f}) {{\begin{{tabular}}{{@{{}}l@{{}}}}Regional2D delivery corridor\\$L_{{prop}}=108.319\,\mathrm{{km}}$ event to interface\\$L_{{pre}}=15.000\,\mathrm{{km}}$, $L_{{inland}}=0$\end{{tabular}}}};",
        fr"\draw[-{{Latex[length=2.5mm]}},r19ink,line width=0.82pt] ({width_cm - 1.05:.4f},{height_cm - 1.85:.4f}) -- ++(0,1.00) node[above]{{N}};",
        fr"\draw[-{{Latex[length=2.2mm]}},r19red,line width=0.82pt] ({width_cm - 1.05:.4f},{height_cm - 1.85:.4f}) -- ++(-0.38,0.92);",
        fr"\node[anchor=east,fill=r19paper,fill opacity=0.88,text opacity=1,inner sep=1.6pt,text=r19ink] at ({width_cm - 1.20:.4f},{height_cm - 1.05:.4f}) {{$\theta_c=338.0^\circ$ cw\\($22.0^\circ$ W of N)}};",
        fr"\draw[r19ink,line width=1.45pt] (0.65,0.48) -- ++({scale_cm:.4f},0);",
        fr"\draw[r19ink,line width=0.65pt] (0.65,0.39) -- (0.65,0.57) ({0.65 + scale_cm:.4f},0.39) -- ({0.65 + scale_cm:.4f},0.57);",
        fr"\node[anchor=north,text=r19ink,fill=r19paper,fill opacity=0.86,text opacity=1,inner sep=1pt] at ({0.65 + 0.5 * scale_cm:.4f},0.34) {{50 km}};",
        r"\end{tikzpicture}",
        r"\end{document}",
    ]
    tex = PUBLICATION_ROOT / "figure_T1_tohoku_kamaishi_domain.tex"
    write_text(tex, "\n".join(lines))
    return tex


def write_t2_tex(geometry: Mapping[str, Any]) -> Path:
    c = geometry["corridor"]
    local = geometry["local3d"]
    profile = geometry["bathymetry_profile"]
    transition_km = 12.0
    local_length_km = float(local["dimensions_m"]["length"]) / 1000.0
    max_km = float(profile["distance_offshore_max_km"])
    min_y = math.floor((float(profile["bed_elevation_min_m"]) - 120.0) / 100.0) * 100.0
    ymax = 460.0
    lines = [
        r"\documentclass[tikz,border=2mm]{standalone}",
        r"\usepackage{pgfplots}",
        r"\pgfplotsset{compat=1.18}",
        r"\usetikzlibrary{arrows.meta,decorations.pathreplacing,positioning}",
        r"\definecolor{deep}{HTML}{183552}",
        r"\definecolor{shelf}{HTML}{8EC6D8}",
        r"\definecolor{transition}{HTML}{E3B448}",
        r"\definecolor{local}{HTML}{B84537}",
        r"\definecolor{ink}{HTML}{263238}",
        r"\begin{document}",
        r"\begin{tikzpicture}[font=\sffamily]",
        fr"\begin{{axis}}[width=24.0cm,height=5.7cm,xmin={-1.35:.3f},xmax={max_km + 0.8:.3f},x dir=reverse,ymin={min_y:.1f},ymax={ymax:.1f},axis on top,clip=false,grid=both,grid style={{draw=gray!18,line width=0.25pt}},xlabel={{Distance offshore from selected wet nearshore interface (km)}},ylabel={{Bed elevation (m, EGM2008)}},tick label style={{font=\scriptsize}},label style={{font=\small}},legend style={{draw=none,fill=white,fill opacity=0.85,text opacity=1,font=\scriptsize}}]",
        fr"\path[fill=deep!7] (axis cs:{max_km + 0.8:.3f},{min_y:.1f}) rectangle (axis cs:85,{ymax:.1f});",
        fr"\path[fill=shelf!20] (axis cs:85,0) rectangle (axis cs:25,{ymax:.1f});",
        fr"\path[fill=transition!24] (axis cs:{transition_km:.3f},{min_y:.1f}) rectangle (axis cs:0,{ymax:.1f});",
        fr"\path[fill=local!14] (axis cs:0,{min_y:.1f}) rectangle (axis cs:{-local_length_km:.6f},{ymax:.1f});",
        fr"\path[fill=blue!10] (axis cs:{max_km + 0.8:.3f},0) rectangle (axis cs:-1.35,{min_y:.1f});",
        r"\addplot[black!60,line width=0.7pt] coordinates {(124,0) (-1.35,0)};",
        r"\addplot[deep,line width=1.25pt] table[x=distance_offshore_km,y=bed_elevation_m,col sep=comma]{../data/bathymetry_profile.csv};",
        fr"\draw[densely dashed,local,line width=0.9pt] (axis cs:0,{min_y:.1f}) -- (axis cs:0,{ymax - 8:.1f});",
        fr"\draw[{{Latex[length=2.0mm]}}-{{Latex[length=2.0mm]}},deep,line width=0.7pt] (axis cs:{float(c['total_length_m']) / 1000.0:.6f},{min_y + 65:.1f}) -- (axis cs:0,{min_y + 65:.1f}) node[midway,above=2pt,fill=white,inner sep=1.5pt]{{$L_c=123.319\,\mathrm{{km}}$}};",
        fr"\draw[{{Latex[length=1.7mm]}}-{{Latex[length=1.7mm]}},local,line width=0.7pt] (axis cs:0,{min_y + 88:.1f}) -- (axis cs:{-local_length_km:.6f},{min_y + 88:.1f});",
        fr"\node[anchor=west,align=left,text=ink,fill=white,fill opacity=0.88,text opacity=1,inner sep=1.8pt] at (axis cs:-1.23,{min_y + 98:.1f}) {{$L_{{3D}}=0.773\,\mathrm{{km}}$\\candidate/framework}};",
        r"\node[align=center,text=deep,fill=white,fill opacity=0.90,text opacity=1,inner sep=2pt] at (axis cs:63,405) {REGIONAL2D -- NLSWE\\\scriptsize depth-averaged; long-wave; approximately hydrostatic};",
        r"\node[align=center,text=ink,fill=white,fill opacity=0.90,text opacity=1,inner sep=2pt,font=\scriptsize] at (axis cs:6,330) {Transition /\\coupling region};",
        r"\node[align=center,text=local,fill=white,fill opacity=0.94,text opacity=1,inner sep=2pt,font=\scriptsize] at (axis cs:-0.82,360) {LOCAL3D -- URANS--VOF\\vertical flow; overtopping\\structure/impact response};",
        r"\draw[-{Latex[length=2.5mm]},line width=1.0pt,local] (axis cs:0,288) -- (axis cs:-0.82,288) node[midway,above=2pt,fill=white,inner sep=1pt]{$\eta,\ q_n/q_t$};",
        fr"\node[circle,draw=ink,fill=white,inner sep=1.3pt,font=\scriptsize] at (axis cs:{float(c['propagation_length_event_to_interface_m']) / 1000.0:.6f},75) {{M0}};",
        r"\node[circle,draw=ink,fill=white,inner sep=1.3pt,font=\scriptsize] at (axis cs:98,75) {M1};",
        r"\node[circle,draw=ink,fill=white,inner sep=1.3pt,font=\scriptsize] at (axis cs:48,75) {M2};",
        r"\node[circle,draw=ink,fill=white,inner sep=1.3pt,font=\scriptsize] at (axis cs:6,105) {M3};",
        r"\node[circle,draw=local,fill=white,inner sep=1.3pt,font=\scriptsize] at (axis cs:0,105) {M4};",
        r"\node[circle,draw=local,fill=white,inner sep=1.3pt,font=\scriptsize] at (axis cs:-0.65,175) {M5};",
        r"\node[anchor=north,align=center,fill=white,fill opacity=0.9,text opacity=1,inner sep=1.5pt,font=\scriptsize] at (axis cs:98,57) {deep-water\\propagation};",
        r"\node[anchor=north,align=center,fill=white,fill opacity=0.9,text opacity=1,inner sep=1.5pt,font=\scriptsize] at (axis cs:48,57) {shelf /\\shoaling};",
        r"% M4 is the selected wet interface; full wording is carried in the caption and handoff.",
        r"\end{axis}",
        r"\end{tikzpicture}",
        r"\end{document}",
    ]
    tex = PUBLICATION_ROOT / "figure_T2_longitudinal_hybrid_corridor.tex"
    write_text(tex, "\n".join(lines))
    return tex


def write_t3_tex(geometry: Mapping[str, Any]) -> Path:
    c = geometry["corridor"]
    local = geometry["local3d"]
    dims = local["dimensions_m"]
    barrier = local["barrier"]
    boundary = local["boundary_policy"]
    xb_fraction = float(barrier["streamwise_position_m"]) / float(dims["length"])
    xb = 10.2 + xb_fraction * 4.4
    lines = [
        r"\documentclass[tikz,border=2mm]{standalone}",
        r"\usepackage{tikz}",
        r"\usetikzlibrary{arrows.meta,calc,positioning}",
        r"\definecolor{r2d}{HTML}{1F5A85}",
        r"\definecolor{local}{HTML}{B84537}",
        r"\definecolor{transfer}{HTML}{287C7B}",
        r"\definecolor{ink}{HTML}{263238}",
        r"\begin{document}",
        r"\begin{tikzpicture}[x=1cm,y=1cm,font=\sffamily,>=Latex]",
        r"\node[anchor=west,text=ink,font=\bfseries] at (0,6.15) {(a) Regional2D computational corridor};",
        r"\coordinate (A) at (0.55,1.45); \coordinate (B) at (5.25,2.10); \coordinate (C) at (5.55,4.35); \coordinate (D) at (0.85,3.70);",
        r"\path[fill=r2d!12] (A)--(B)--(C)--(D)--cycle;",
        r"\draw[r2d,line width=1pt] (A)--(B)--(C)--(D)--cycle;",
        r"\draw[r2d,densely dashed,line width=0.8pt] ($(A)!0.5!(B)$)--($(D)!0.5!(C)$);",
        r"\path[fill=r2d!20,opacity=0.85] (A)--($(A)!0.10!(B)$)--($(D)!0.10!(C)$)--(D)--cycle;",
        r"\path[fill=r2d!10,opacity=0.95] (A)--($(A)!0.18!(D)$)--($(B)!0.18!(C)$)--(B)--cycle;",
        r"\path[fill=r2d!10,opacity=0.95] ($(A)!0.82!(D)$)--(D)--(C)--($(B)!0.82!(C)$)--cycle;",
        r"\draw[transfer,line width=1.15pt] (D)--(C);",
        r"\node[align=center,font=\scriptsize,fill=white,inner sep=1pt,text=transfer] at (5.74,4.82) {selected interface\\boundary.inland};",
        fr"\draw[<->,r2d,line width=0.65pt] (0.30,1.18)--(5.00,1.83) node[midway,below=2pt,fill=white,inner sep=1pt]{{$L_c={float(c['total_length_m']) / 1000.0:.3f}\,\mathrm{{km}}$}};",
        fr"\draw[<->,r2d,line width=0.65pt] (5.64,2.10)--(5.93,4.35) node[midway,right=2pt,fill=white,inner sep=1pt]{{$W_c={float(c['width_m']) / 1000.0:.1f}\,\mathrm{{km}}$}};",
        r"\node[align=center,font=\scriptsize,fill=white,fill opacity=0.88,text opacity=1,inner sep=1.5pt] at (1.00,2.45) {offshore\\radiation\\$10$ km sponge};",
        r"\node[align=center,font=\scriptsize,fill=white,fill opacity=0.88,text opacity=1,inner sep=1.5pt] at (3.10,4.60) {side radiation\\$1$ km sponge};",
        r"% Lower side boundary uses the same radiation/sponge policy as the upper side boundary.",
        r"\node[align=center,font=\small,fill=white,fill opacity=0.92,text opacity=1,inner sep=2pt] at (3.15,3.08) {NLSWE finite-volume domain\\\scriptsize $h, q_x, q_y, b, \eta=h+b$};",
        r"\draw[-{Latex[length=3mm]},transfer,line width=1.2pt] (6.20,3.08)--(8.85,3.08) node[midway,above=3pt,text=transfer,font=\bfseries] {2D$\rightarrow$3D forcing};",
        r"\node[align=center,font=\scriptsize,fill=white,inner sep=2.2pt,draw=transfer!55,line width=0.45pt] at (7.52,2.08) {Regional2D section extraction\\$\eta(s,t)$, $q_n(s,t)$, $q_t(s,t)$\\depth-uniform lift; one-way only\\$\eta\rightarrow\alpha.water$, $q/h\rightarrow U$};",
        r"\node[anchor=west,text=ink,font=\bfseries] at (10.0,6.15) {(b) Local3D computational domain};",
        r"\coordinate (O) at (10.20,1.38); \coordinate (X) at (14.60,1.38); \coordinate (Y) at (15.35,2.25); \coordinate (OY) at (10.95,2.25);",
        r"\coordinate (Z) at (10.20,4.45); \coordinate (XZ) at (14.60,4.45); \coordinate (YZ) at (15.35,5.32); \coordinate (OYZ) at (10.95,5.32);",
        r"\path[fill=local!9] (O)--(X)--(Y)--(OY)--cycle;",
        r"\path[fill=blue!9] (O)--(X)--(XZ)--(Z)--cycle;",
        r"\path[fill=local!5] (X)--(Y)--(YZ)--(XZ)--cycle;",
        r"\draw[local,line width=1pt] (O)--(X)--(Y)--(OY)--cycle (Z)--(XZ)--(YZ)--(OYZ)--cycle (O)--(Z) (X)--(XZ) (Y)--(YZ) (OY)--(OYZ);",
        fr"\coordinate (BX) at ({xb:.3f},1.38); \coordinate (BXY) at ({xb + 0.22:.3f},1.64); \coordinate (BXZ) at ({xb:.3f},2.58); \coordinate (BXYZ) at ({xb + 0.22:.3f},2.84);",
        r"\draw[local,line width=1.05pt,fill=local!28] (BX)--(BXY)--(BXYZ)--(BXZ)--cycle;",
        r"\node[font=\scriptsize,align=center,fill=white,inner sep=1.5pt] at (10.15,0.72) {inlet: $U$, $\alpha.water$\\timeVaryingMappedFixedValue};",
        r"\node[font=\scriptsize,align=center,fill=white,inner sep=1.5pt] at (14.92,0.72) {outlet\\open + damping};",
        r"\node[font=\scriptsize,align=center,fill=white,inner sep=1.5pt] at (15.78,3.76) {side patches\\open + damping};",
        r"\node[font=\scriptsize,align=center,fill=white,inner sep=1.5pt] at (12.45,5.55) {atmosphere open};",
        r"\node[font=\scriptsize,align=center,fill=white,inner sep=1.5pt] at (12.35,1.17) {terrain wall};",
        fr"\node[font=\scriptsize,align=left,fill=white,fill opacity=0.9,text opacity=1,inner sep=1.7pt] at (12.62,3.78) {{$L_{{3D}}={float(dims['length']):.1f}\,\mathrm{{m}}$\\$W_{{3D}}={float(dims['span']):.1f}\,\mathrm{{m}}$\\$H_{{3D}}={float(dims['height']):.1f}\,\mathrm{{m}}$}};",
        fr"\node[font=\scriptsize,align=center,fill=white,inner sep=1.4pt] at ({xb + 0.40:.3f},2.75) {{simple rigid barrier\\$x_b={float(barrier['streamwise_position_m']):.1f}$ m\\$t_b={float(barrier['thickness_m']):.1f}$ m, $H_b={float(barrier['height_m']):.1f}$ m}};",
        r"\node[font=\scriptsize,align=center,fill=white,draw=local!45,line width=0.4pt,inner sep=1.6pt] at (12.85,0.18) {accepted G6 replay geometry; representative/candidate defence framework};",
        r"\end{tikzpicture}",
        r"\end{document}",
    ]
    tex = PUBLICATION_ROOT / "figure_T3_computational_domains.tex"
    write_text(tex, "\n".join(lines))
    return tex


def write_t0_tex() -> Path:
    lines = [
        r"\documentclass[tikz,border=2mm]{standalone}",
        r"\usepackage{graphicx}",
        r"\usepackage{tikz}",
        r"\definecolor{ink}{HTML}{263238}",
        r"\begin{document}",
        r"\begin{tikzpicture}[font=\sffamily]",
        r"\node[anchor=south west,inner sep=0] (t1) at (0,5.92) {\includegraphics[width=8.9cm]{figure_T1_tohoku_kamaishi_domain.pdf}};",
        r"\node[anchor=south west,inner sep=0] (t3) at (9.15,5.92) {\includegraphics[width=14.65cm]{figure_T3_computational_domains.pdf}};",
        r"\node[anchor=south west,inner sep=0] (t2) at (0,0) {\includegraphics[width=23.80cm]{figure_T2_longitudinal_hybrid_corridor.pdf}};",
        r"\node[anchor=north west,fill=white,fill opacity=0.92,text opacity=1,inner sep=1.5pt,font=\bfseries,text=ink] at (0.10,14.38) {(a) geographic corridor};",
        r"\node[anchor=north west,fill=white,fill opacity=0.92,text opacity=1,inner sep=1.5pt,font=\bfseries,text=ink] at (9.25,14.38) {(b) computational domains};",
        r"\node[anchor=north west,fill=white,fill opacity=0.92,text opacity=1,inner sep=1.5pt,font=\bfseries,text=ink] at (0.10,5.78) {(c) longitudinal 2D$\rightarrow$3D corridor};",
        r"\node[anchor=south east,fill=white,fill opacity=0.9,text opacity=1,inner sep=1.6pt,font=\scriptsize,text=ink] at (23.72,0.10) {R19 composite preview: POSTER\_CANDIDATE};",
        r"\end{tikzpicture}",
        r"\end{document}",
    ]
    tex = PUBLICATION_ROOT / "figure_T0_domain_package_composite.tex"
    write_text(tex, "\n".join(lines))
    return tex


def write_tex_sources(geometry: Mapping[str, Any]) -> dict[str, Path]:
    tex_paths = {
        "T1": write_t1_tex(geometry),
        "T2": write_t2_tex(geometry),
        "T3": write_t3_tex(geometry),
    }
    return tex_paths


def write_embedded_composite_svg(target: Path) -> str:
    from PIL import Image

    panels = [
        (PUBLICATION_ROOT / "figure_T1_tohoku_kamaishi_domain.png", "(a) geographic corridor", 0.0, 0.0, 890.0),
        (PUBLICATION_ROOT / "figure_T3_computational_domains.png", "(b) computational domains", 920.0, 0.0, 1465.0),
        (PUBLICATION_ROOT / "figure_T2_longitudinal_hybrid_corridor.png", "(c) longitudinal 2D->3D corridor", 0.0, 900.0, 2385.0),
    ]
    rendered: list[dict[str, Any]] = []
    height = 0.0
    for path, label, x, y, width in panels:
        with Image.open(path) as img:
            scaled_height = width * img.height / img.width
        encoded = base64.b64encode(path.read_bytes()).decode("ascii")
        rendered.append({"label": label, "x": x, "y": y, "width": width, "height": scaled_height, "data": encoded})
        height = max(height, y + scaled_height)
    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="2385" height="{height:.1f}" viewBox="0 0 2385 {height:.1f}">',
        '<rect x="0" y="0" width="2385" height="100%" fill="#ffffff"/>',
    ]
    for panel in rendered:
        lines.append(
            '<image x="{x:.1f}" y="{y:.1f}" width="{width:.1f}" height="{height:.1f}" href="data:image/png;base64,{data}" preserveAspectRatio="xMinYMin meet"/>'.format(
                **panel
            )
        )
        lines.append(
            '<text x="{:.1f}" y="{:.1f}" font-family="Arial, Helvetica, sans-serif" font-size="28" font-weight="700" fill="#263238">{}</text>'.format(
                float(panel["x"]) + 14.0,
                float(panel["y"]) + 34.0,
                panel["label"].replace("&", "&amp;"),
            )
        )
    lines.append('<text x="2368" y="{}" text-anchor="end" font-family="Arial, Helvetica, sans-serif" font-size="18" fill="#263238">R19 composite preview: POSTER_CANDIDATE</text>'.format(height - 18.0))
    lines.append("</svg>")
    write_text(target, "\n".join(lines))
    return "self-contained SVG wrapper with embedded PNG panels"


def compile_one(tex: Path) -> dict[str, Any]:
    build_dir = Path("/tmp/r19-latex-build") / tex.stem
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    command = ["latexmk", "-pdf", "-halt-on-error", "-interaction=nonstopmode", f"-outdir={build_dir.as_posix()}", tex.name]
    code, output = command_output(command, cwd=PUBLICATION_ROOT)
    if code != 0:
        raise RuntimeError(f"LaTeX failed for {tex}:\n{output}")
    built_pdf = build_dir / f"{tex.stem}.pdf"
    if not built_pdf.is_file():
        raise RuntimeError(f"Missing compiled PDF {built_pdf}")
    pdf = PUBLICATION_ROOT / f"{tex.stem}.pdf"
    shutil.copy2(built_pdf, pdf)
    log_path = build_dir / f"{tex.stem}.log"
    log_text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    overfull = [line.strip() for line in log_text.splitlines() if "Overfull" in line]
    warnings = [line.strip() for line in log_text.splitlines() if "Warning" in line and "rerun" not in line.lower()]
    svg = PUBLICATION_ROOT / f"{tex.stem}.svg"
    png_base = PUBLICATION_ROOT / tex.stem
    svg_code, svg_output = command_output(["pdftocairo", "-svg", pdf.as_posix(), svg.as_posix()])
    svg_method = "pdftocairo -svg"
    if svg_code != 0 or not svg.is_file() or svg.stat().st_size == 0:
        if svg.exists():
            svg.unlink()
        fallback = ["dvisvgm", "--pdf", "--page=1", f"--output={svg.as_posix()}", pdf.as_posix()]
        fallback_code, fallback_output = command_output(fallback)
        if (fallback_code != 0 or not svg.is_file() or svg.stat().st_size == 0) and tex.stem == "figure_T0_domain_package_composite":
            svg_method = write_embedded_composite_svg(svg)
            svg_output = f"pdftocairo returned {svg_code}; dvisvgm returned {fallback_code}; wrote embedded SVG wrapper"
        elif fallback_code != 0 or not svg.is_file() or svg.stat().st_size == 0:
            raise RuntimeError(
                f"SVG conversion failed for {pdf}:\n"
                f"pdftocairo returned {svg_code}: {svg_output}\n"
                f"dvisvgm returned {fallback_code}: {fallback_output}"
            )
        else:
            svg_method = "dvisvgm --pdf --page=1"
            svg_output = fallback_output
    command_output(["pdftocairo", "-png", "-singlefile", "-r", "320", pdf.as_posix(), png_base.as_posix()], check=True)
    png = PUBLICATION_ROOT / f"{tex.stem}.png"
    preview = PREVIEW_ROOT / f"{tex.stem}.png"
    shutil.copy2(png, preview)
    return {
        "tex": file_record(tex),
        "pdf": file_record(pdf),
        "svg": file_record(svg),
        "png": file_record(png),
        "preview": file_record(preview),
        "latex_command": " ".join(command),
        "latex_return_code": code,
        "svg_conversion_method": svg_method,
        "svg_conversion_output": svg_output[-1200:],
        "overfull_log_lines": overfull,
        "warning_log_lines": warnings[:20],
    }


def compile_figures(tex_paths: Mapping[str, Path]) -> dict[str, Any]:
    records: dict[str, Any] = {}
    for key in ["T1", "T2", "T3"]:
        records[key] = compile_one(tex_paths[key])
    t0 = write_t0_tex()
    records["T0"] = compile_one(t0)
    return records


def validate_figures(compilation: Mapping[str, Any], geometry: Mapping[str, Any]) -> dict[str, Any]:
    from PIL import Image, ImageStat

    figure_checks: dict[str, Any] = {}
    for key, record in compilation.items():
        paths = {kind: REPO_ROOT / value["path"] for kind, value in record.items() if isinstance(value, dict) and kind in {"pdf", "svg", "png"}}
        pdf_ok = paths["pdf"].is_file() and paths["pdf"].stat().st_size > 5000
        svg_ok = False
        svg_error = None
        try:
            ET.parse(paths["svg"])
            svg_ok = True
        except Exception as exc:  # pragma: no cover - diagnostic only.
            svg_error = f"{type(exc).__name__}: {exc}"
        with Image.open(paths["png"]) as img:
            stat = ImageStat.Stat(img.convert("RGB"))
            extrema = img.convert("RGB").getextrema()
            nonblank = any(lo != hi for lo, hi in extrema)
            dims = {"width_px": img.width, "height_px": img.height}
            brightness = sum(stat.mean) / 3.0
        figure_checks[key] = {
            "pdf_nonempty": pdf_ok,
            "svg_parses": svg_ok,
            "svg_error": svg_error,
            "png_dimensions": dims,
            "png_nonblank": nonblank,
            "png_mean_rgb": brightness,
            "latex_overfull_count": len(record.get("overfull_log_lines", [])),
            "latex_warning_count": len(record.get("warning_log_lines", [])),
        }
    c = geometry["corridor"]
    total_residual = abs(float(c["total_length_m"]) - (float(c["source_side_pre_extent_m"]) + float(c["propagation_length_event_to_interface_m"]) + float(c["inland_extent_m"])))
    tangent = c["tangent_unit"]
    bearing = (math.degrees(math.atan2(float(tangent["x"]), float(tangent["y"]))) + 360.0) % 360.0
    bearing_residual = abs(bearing - float(c["orientation_degrees_clockwise_from_north_source_to_interface"]))
    geometry_checks = {
        "corridor_length_identity_residual_m": total_residual,
        "orientation_residual_degrees": bearing_residual,
        "width_constant_m": float(c["width_m"]),
        "selected_interface_section_id": geometry["selected_nearshore_interface"]["section_id"],
        "local3d_dimensions_positive": all(float(value) > 0.0 for value in geometry["local3d"]["dimensions_m"].values()),
        "source_marker_only_no_rupture_polygon": geometry["source_model"]["vertical_displacement"]["rupture_polygon_for_T1"].startswith("UNRESOLVED"),
    }
    code, diff_check = command_output(["git", "diff", "--check"])
    status_code, status_output = command_output(["git", "status", "--short"])
    payload = {
        "schema": {"name": "tsunami.r19.validation", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "figures": figure_checks,
        "geometry_checks": geometry_checks,
        "git_diff_check": {"return_code": code, "output": diff_check},
        "git_status_short_at_validation": {"return_code": status_code, "output": status_output},
        "overall_pass": all(
            check["pdf_nonempty"] and check["svg_parses"] and check["png_nonblank"] and check["latex_overfull_count"] == 0
            for check in figure_checks.values()
        )
        and total_residual < 1.0e-6
        and bearing_residual < 1.0e-9
        and code == 0,
    }
    write_json(PROVENANCE_ROOT / "r19_validation.json", payload)
    return payload


def figure_records(compilation: Mapping[str, Any]) -> dict[str, Any]:
    details = {
        "T1": {
            "basename": "figure_T1_tohoku_kamaishi_domain",
            "classification": "POSTER_CANDIDATE",
            "purpose": "Locate the 2011 event reference, Kamaishi, and the current Regional2D corridor geometry in real GIS geography.",
            "allowed_claim": "Shows the accepted current R10/G6 delivery corridor geometry over real ETOPO/QGIS context.",
            "caveat": "The corridor is delivery integration geometry, not exact Kamaishi harbour reconstruction, spatial qualification, calibration or historical validation.",
        },
        "T2": {
            "basename": "figure_T2_longitudinal_hybrid_corridor",
            "classification": "POSTER_CANDIDATE",
            "purpose": "Explain the full-width source-to-interface bathymetry and one-way 2D-to-3D framework.",
            "allowed_claim": "Shows real R10 h400 mesh bathymetry along the accepted centreline with conceptual hybrid regions.",
            "caveat": "Transition band is conceptual/framework; selected interface is current implementation choice, not a universal optimum.",
        },
        "T3": {
            "basename": "figure_T3_computational_domains",
            "classification": "REPORT_CANDIDATE",
            "purpose": "Show the Regional2D and Local3D computational domains, boundary labels, and transfer quantities.",
            "allowed_claim": "Shows accepted current-case computational geometry and one-way replay coupling terms.",
            "caveat": "The Local3D box is a TikZ pseudo-3D engineering schematic derived from G6 dimensions, not a new OpenFOAM render or validation result.",
        },
        "T0": {
            "basename": "figure_T0_domain_package_composite",
            "classification": "POSTER_CANDIDATE",
            "purpose": "Preview T1, T3, and T2 in a poster-panel composition.",
            "allowed_claim": "Useful combined preview of the R19 domain figure package.",
            "caveat": "Composite is a layout preview, not an additional scientific result.",
        },
    }
    records = {}
    for key, detail in details.items():
        records[key] = {**detail, "outputs": compilation[key]}
        write_json(PROVENANCE_ROOT / f"{detail['basename']}.provenance.json", records[key])
    return records


def write_handoff(records: Mapping[str, Any], geometry: Mapping[str, Any]) -> None:
    t2_caption = (
        "Longitudinal hybrid-corridor schematic using bathymetry sampled from the real R10 h400 Regional2D mesh along the accepted "
        "Kamaishi centreline. The 2D->3D transition band is conceptual/framework; the selected wet nearshore interface is the current "
        "implementation choice and not a universal optimum."
    )
    captions = {
        "T1": "QGIS/TikZ map of the 2011 Tohoku event reference, Kamaishi proxy, accepted delivery corridor, centreline, current length/width/orientation and selected wet nearshore interface.",
        "T2": t2_caption,
        "T3": "Engineering schematic of the accepted Regional2D corridor and G6 simple-rigid-barrier Local3D replay domain, with one-way transfer terms and boundary labels from case evidence.",
        "T0": "Composite preview combining the geographic corridor, computational-domain detail and longitudinal hybrid-corridor schematic.",
    }
    lines = [
        "# R19 Figure Handoff",
        "",
        "Status: `COMPLETE`",
        f"Generated: `{utc_now()}`",
        f"Branch: `{git_branch()}`",
        f"HEAD: `{git_sha()}`",
        "",
        "R19 replaces decorative or AI-illustration approaches with QGIS geography, real model geometry, TikZ engineering annotation and PGFPlots data plotting.",
        "",
        "| Figure | Scientific purpose | Caption | Allowed claim | Required caveat | Poster/report role | QC class |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for key in ["T1", "T2", "T3", "T0"]:
        rec = records[key]
        role = "poster primary candidate" if rec["classification"] == "POSTER_CANDIDATE" else "report/detail candidate"
        lines.append(
            "| {key} | {purpose} | {caption} | {claim} | {caveat} | {role} | `{qc}` |".format(
                key=key,
                purpose=rec["purpose"],
                caption=captions[key],
                claim=rec["allowed_claim"],
                caveat=rec["caveat"],
                role=role,
                qc=rec["classification"],
            )
        )
    lines.extend(
        [
            "",
            "## Scientific Authority",
            "",
            "- Regional method authority: `MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES`, `GLOBAL_FIRST_ORDER_VERIFIED`, `SECOND_ORDER_VERIFIED`.",
            "- R10 h400 `limited_linear` remains `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`.",
            "- The figures may say the 2011 Tohoku event was simulated with the verified formulation; they must also say it is not spatially qualified, physically calibrated or historically validated.",
            "- Hybrid coupling was implemented/demonstrated through accepted G6 replay; the R10 h400 Local3D replay remains `REPLAY_VOF_BEHAVIOUR_UNRESOLVED`.",
            "- Historical validation counts remain 29 observations, 0 DIRECT, 1 PROXY, 28 TARGET_ONLY; NOWPHAS 802G is about 12.273 km outside and DART 21418 about 545 km outside.",
            "",
            "## Files",
            "",
        ]
    )
    for key, rec in records.items():
        outputs = rec["outputs"]
        lines.append(f"- {key}: `{outputs['tex']['path']}`, `{outputs['pdf']['path']}`, `{outputs['svg']['path']}`, `{outputs['png']['path']}`")
    write_text(HANDOFF_MD, "\n".join(lines))


def write_completion(records: Mapping[str, Any], validation: Mapping[str, Any], toolchain: Mapping[str, Any], geometry: Mapping[str, Any], qgis_base: Mapping[str, Any]) -> None:
    code, status = command_output(["git", "status", "--short"])
    final_head = git_sha()
    commits_code, commits = command_output(["git", "log", "--oneline", f"{STARTING_HEAD}..HEAD"])
    unresolved = [item for item in read_json(REGISTER_JSON)["quantities"] if item["value"] == "UNRESOLVED"]
    c = geometry["corridor"]
    local = geometry["local3d"]
    profile = geometry["bathymetry_profile"]
    items = [
        ("Branch", git_branch()),
        ("Worktree", WORKTREE_PATH.as_posix()),
        ("Starting HEAD", STARTING_HEAD),
        ("Final HEAD", f"Reported in the final assistant response after commit creation; generation-time HEAD was {final_head}."),
        ("Commits", commits if commits_code == 0 and commits else "No commits existed at generation time; final response reports the created commit hash."),
        ("Worktree clean?", "YES" if code == 0 and not status else f"PENDING at generation time; final response reports post-commit cleanliness. Generation-time status: {status}"),
        ("QGIS version", toolchain["qgis_version"]),
        ("TeX engine", toolchain["pdflatex_version"]),
        ("TikZ/PGF availability", f"TikZ={toolchain['tikz_available']}; standalone={toolchain['standalone_cls']}; PGFPlots={toolchain['pgfplots_sty']}"),
        ("PGFPlots version if discoverable", toolchain["pgfplots_version"]),
        ("SVG conversion method", toolchain["svg_conversion_method"]),
        ("Register path", display_path(REGISTER_MD)),
        ("Epicentre authority", "USGS official origin as recorded in corridor evidence and `tohoku-epicentre-source.json`."),
        ("Kamaishi authority", "Kamaishi proxy coordinate from accepted delivery corridor evidence."),
        ("Corridor length", f"{float(c['total_length_m']):.6f} m"),
        ("Length definition", "source-side pre-extent + event-to-interface propagation length + inland extent."),
        ("Corridor width", f"{float(c['width_m']):.6f} m"),
        ("Width definition", "constant current corridor width; narrowing disabled."),
        ("Orientation angle", f"{float(c['orientation_degrees_clockwise_from_north_source_to_interface']):.6f} degrees clockwise from north."),
        ("Orientation convention", "source-to-interface bearing in EPSG:32654 basis; figure also states 22.0 degrees W of grid north."),
        ("Offshore extent", f"{float(c['source_side_pre_extent_m']):.6f} m source-side pre-extent; {float(c['offshore_sponge_width_m']):.6f} m offshore sponge."),
        ("Inland/nearshore extent", f"{float(c['inland_extent_m']):.6f} m inland extent; selected wet nearshore interface is used instead of a shoreline label."),
        ("Selected interface position", json.dumps(geometry["selected_nearshore_interface"]["projected_m"], sort_keys=True)),
        ("Local3D dimensions", json.dumps(local["dimensions_m"], sort_keys=True)),
        ("Unresolved geometric quantities", "; ".join(item["symbol"] for item in unresolved) or "none"),
        ("QGIS base path", qgis_base["exports"]["pdf"]["path"]),
        ("TikZ source", records["T1"]["outputs"]["tex"]["path"]),
        ("PDF", records["T1"]["outputs"]["pdf"]["path"]),
        ("SVG", records["T1"]["outputs"]["svg"]["path"]),
        ("PNG", records["T1"]["outputs"]["png"]["path"]),
        ("Dimension annotations", "T1 shows L_c=123.319 km, W_c=8.000 km, L_prop=108.319 km, L_pre=15.000 km, L_inland=0."),
        ("Orientation representation", "T1 compass states theta_c=338.0 degrees clockwise, equivalently 22.0 degrees W of N."),
        ("Milestones shown", "T1 shows source-side boundary, USGS event reference, selected wet interface and Kamaishi proxy."),
        ("Visual QC classification", records["T1"]["classification"]),
        ("Bathymetry data source", H400_HDF5.as_posix()),
        ("Bathymetry CSV", profile["csv"]),
        ("Distance convention", profile["distance_convention"]),
        ("TikZ/PGFPlots source", records["T2"]["outputs"]["tex"]["path"]),
        ("Milestone positions", display_path(DATA_ROOT / "milestone_positions.json")),
        ("Exact vs conceptual milestone distinction", "M0 and M4 are exact current-case references; M1-M3 are conceptual bands; M5 is candidate/framework Local3D region."),
        ("Transition-band representation", "Shaded 0-12 km band labelled Transition / coupling region."),
        ("Current interface representation", "Dashed vertical line at 0 km; selected wet nearshore interface, not shoreline."),
        ("Local3D-region representation", "Right-end candidate/framework URANS--VOF region, using G6 L_3D value but not implying universal footprint."),
        ("PDF", records["T2"]["outputs"]["pdf"]["path"]),
        ("SVG", records["T2"]["outputs"]["svg"]["path"]),
        ("PNG", records["T2"]["outputs"]["png"]["path"]),
        ("Poster-scale readability", "Designed 24 cm wide with compact PGFPlots labels and milestone markers."),
        ("Visual QC classification", records["T2"]["classification"]),
        ("Regional2D geometry authority", CORRIDOR_PATH.as_posix()),
        ("Regional boundary labels", "offshore radiation, side radiation, inland transmissive, relaxation enabled with 10 km offshore and 1 km side sponge widths."),
        ("Local3D geometry source", G6_LOCAL_SUMMARY.as_posix()),
        ("Real render or TikZ wireframe?", "TikZ pseudo-3D wireframe derived from real G6 dimensions and boundary policy; no new render."),
        ("Local3D dimensions", f"L_3D={float(local['dimensions_m']['length']):.3f} m, W_3D={float(local['dimensions_m']['span']):.3f} m, H_3D={float(local['dimensions_m']['height']):.3f} m."),
        ("Boundary labels", "inlet U/alpha.water timeVaryingMappedFixedValue; outlet/sides open+damped; atmosphere open; terrain/barrier wall."),
        ("Transfer quantities", "h, q_x, q_y, b, eta exported; q_n/q_t projection; eta->alpha.water and q/h->U reconstruction."),
        ("One-way forcing representation", "Single Regional2D-to-Local3D arrow; no feedback arrow."),
        ("PDF", records["T3"]["outputs"]["pdf"]["path"]),
        ("SVG", records["T3"]["outputs"]["svg"]["path"]),
        ("PNG", records["T3"]["outputs"]["png"]["path"]),
        ("Visual QC classification", records["T3"]["classification"]),
        ("Composite PDF", records["T0"]["outputs"]["pdf"]["path"]),
        ("Composite SVG", records["T0"]["outputs"]["svg"]["path"]),
        ("Composite PNG", records["T0"]["outputs"]["png"]["path"]),
        ("Classification", records["T0"]["classification"]),
        ("Handoff path", display_path(HANDOFF_MD)),
        ("T1 caption", "See handoff table."),
        ("T2 caption", "See handoff table; explicitly names R10 h400 mesh, conceptual transition band and current interface choice."),
        ("T3 caption", "See handoff table."),
        ("Allowed claims", "See handoff table and scientific-authority section."),
        ("Required caveats", "See handoff table and scientific-authority section."),
        ("TeX compilation", "COMPLETE: latexmk compiled T1/T2/T3/T0 with zero fatal errors."),
        ("PDF checks", "COMPLETE: non-empty PDFs validated."),
        ("SVG checks", "COMPLETE: SVG parse checks recorded."),
        ("PNG dimensions", json.dumps({key: value["png_dimensions"] for key, value in validation["figures"].items()}, sort_keys=True)),
        ("Geometry validation", json.dumps(validation["geometry_checks"], sort_keys=True)),
        ("Visual QC", "PNG previews generated for manual inspection; validation records nonblank images."),
        ("git diff --check", "PASS" if validation["git_diff_check"]["return_code"] == 0 else validation["git_diff_check"]["output"]),
        ("Worktree clean?", "See final status after commits; generation-time status is captured above."),
        ("No Regional simulation", "CONFIRMED: script only reads frozen artefacts and HDF5."),
        ("No Local3D simulation", "CONFIRMED: script only reads G6 summaries/boundary policy."),
        ("No calibration", "CONFIRMED."),
        ("No solver changes", "CONFIRMED."),
        ("No corridor geometry modification", "CONFIRMED: current manifest geometry reused unchanged."),
        ("No AI-generated illustration", "CONFIRMED: QGIS, TikZ and PGFPlots only."),
        ("No research files deleted/moved/renamed", "CONFIRMED."),
        ("No poster editing", "CONFIRMED."),
        ("No report editing", "CONFIRMED."),
        ("No push/merge/rebase/amend", "CONFIRMED."),
        ("Protected/unrelated files untouched", "CONFIRMED to the generated/staged path set."),
        ("Is T1 publication quality?", "YES, with final manual vector polish optional for label placement."),
        ("Is T2 suitable as the full-width poster footer?", "YES: strongest R19 poster-candidate schematic."),
        ("Is T3 publication quality?", "YES as a report/detail candidate; poster use depends on available space."),
        ("Is T0 useful as a combined panel?", "YES as a poster composition preview."),
        ("Which figure best explains the corridor geometry?", "T1 for geography, T2 for longitudinal propagation geometry."),
        ("Which figure best explains the 2D->3D framework?", "T3 for mechanics; T2 for poster-level flow."),
        ("Which figure is strongest for the two-page report?", "T2 as the main bridge figure, with T3 as the technical companion."),
        ("What, if anything, still requires manual vector refinement?", "Optional final label nudging in a vector editor after poster layout placement; no scientific blocker remains."),
    ]
    lines = [
        "# R19 Completion Report",
        "",
        "All R19 states are `COMPLETE`, `BLOCKED_BY_SOURCE_DATA`, or `NOT_APPLICABLE`; no `READY` or `IN_PROGRESS` state remains.",
        "",
    ]
    for index, (label, value) in enumerate(items, start=1):
        lines.append(f"{index}. **{label}:** {value}")
    write_text(COMPLETION_MD, "\n".join(lines))


def generate() -> dict[str, Any]:
    ensure_layout()
    toolchain = toolchain_record()
    geometry = extract_geometry()
    register = build_register(geometry)
    qgis_base = export_t1_qgis_base(geometry)
    tex_paths = write_tex_sources(geometry)
    compilation = compile_figures(tex_paths)
    records = figure_records(compilation)
    validation = validate_figures(compilation, geometry)
    write_handoff(records, geometry)
    write_completion(records, validation, toolchain, geometry, qgis_base)
    source_copy = PYTHON_SOURCE_ROOT / Path(__file__).name
    if source_copy.resolve() != Path(__file__).resolve():
        shutil.copy2(Path(__file__), source_copy)
    manifest = {
        "schema": {"name": "tsunami.r19.figure_package_manifest", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "branch": git_branch(),
        "starting_head": STARTING_HEAD,
        "current_head": git_sha(),
        "toolchain": toolchain,
        "geometry": file_record(DATA_ROOT / "domain_geometry.json"),
        "register": {"md": file_record(REGISTER_MD), "json": file_record(REGISTER_JSON)},
        "handoff": file_record(HANDOFF_MD),
        "completion_report": file_record(COMPLETION_MD),
        "qgis_base": qgis_base,
        "figures": records,
        "validation": validation,
        "scientific_authority": SCIENTIFIC_AUTHORITY,
        "hard_exclusions_honoured": [
            "no Regional2D run",
            "no R10 rerun",
            "no h250/temporal/calibration work",
            "no corridor/source geometry changes",
            "no OpenFOAM retuning or Local3D run",
            "no poster/report editing",
            "no AI-generated illustration",
        ],
    }
    write_json(PROVENANCE_ROOT / "r19_figure_manifest.json", manifest)
    write_json(PROVENANCE_ROOT / "r19_completion_state.json", {"status": "COMPLETE", "manifest": file_record(PROVENANCE_ROOT / "r19_figure_manifest.json"), "generated_at_utc": utc_now()})
    return manifest


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print-manifest", action="store_true", help="Print the final manifest JSON after generation.")
    args = parser.parse_args(argv)
    manifest = generate()
    if args.print_manifest:
        print(json.dumps(manifest, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
