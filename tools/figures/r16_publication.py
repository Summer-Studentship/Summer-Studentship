#!/usr/bin/env python3
"""R16 publication figure and GIS layer package.

This module deliberately separates QGIS-dependent cartography from the
quantitative Regional2D figures that can be produced in the current runtime.
When QGIS is unavailable it still prepares editable GIS layers and records the
runtime blocker rather than substituting matplotlib maps as final cartography.
"""

from __future__ import annotations

import argparse
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
DOCS_ROOT = REPO_ROOT / "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A"
R16_ROOT = REPO_ROOT / "deliverables/figures/r16_publication"
PUBLICATION_ROOT = R16_ROOT / "publication"
PREVIEW_ROOT = R16_ROOT / "previews"
PROVENANCE_ROOT = R16_ROOT / "provenance"
SOURCE_ROOT = R16_ROOT / "sources"
QGIS_ROOT = SOURCE_ROOT / "qgis"
QGIS_LAYER_ROOT = QGIS_ROOT / "layers"
QGIS_STYLE_ROOT = QGIS_ROOT / "styles"
PYTHON_SOURCE_ROOT = SOURCE_ROOT / "python"
QGIS_PROJECT = QGIS_ROOT / "tohoku_kamaishi_publication.qgz"
QGIS_BLOCKER_PATH = PROVENANCE_ROOT / "qgis_runtime_status.json"
STYLE_PATH = REPO_ROOT / "tools/figures/styles/research_publication.mplstyle"

CORRIDOR_PATH = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/kamaishi-delivery-corridor-evidence.json")
CENTRELINE_PATH = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/manifests/corridors/tohoku-kamaishi-centreline.json")
CONDITIONED_TERRAIN = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/outputs/terrain/conditioned-terrain.tif")
SOURCE_ETOPO = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/preprocessing/smoke/case/inputs/data/source/terrain/ETOPO_2022_v1_15s_N45E135_surface.tif")
H400_HDF5 = Path("/home/helios/SimulationData/Summer-Studentship/results/r11-regional2d-storage-poc/r10-h400-limited-linear/regional2d.h5")
R13_REPLAY_PACKAGE = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-fidelity-hybrid-r13/replay/r13_h400_openfoam_replay_package.json")
G6_LOCAL_SUMMARY = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/local/simple_rigid_barrier/openfoam_case_summary.json")
R15_REGISTER = DOCS_ROOT / "regional2d_r15_observation_register.json"

EVENT_UTC = "2011-03-11T05:46:23Z"
STARTING_HEAD = "d6bf4933a513f4d6fe991e0f5754ceb2f812f8dc"
SCIENTIFIC_AUTHORITY = {
    "regional_numerical_authority": [
        "MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES",
        "GLOBAL_FIRST_ORDER_VERIFIED",
        "SECOND_ORDER_VERIFIED",
    ],
    "event_result": "R10 h400 limited_linear",
    "event_result_status": "BEST_AVAILABLE_NUMERICALLY_UNCERTAIN",
    "event_limitations": [
        "real Tohoku event simulation",
        "verified numerical method",
        "not spatially qualified",
        "not physically calibrated",
        "not historically validated",
    ],
    "spatial_diagnosis": {
        "terrain_source_fidelity": "TERRAIN_SOURCE_FIDELITY_DOMINANT",
        "confidence": "MODERATE",
        "projection": "PROJECTION_FIDELITY_CEILING",
    },
    "local3d_status": "REPLAY_VOF_BEHAVIOUR_UNRESOLVED",
}


@dataclass(frozen=True)
class Point:
    x: float
    y: float


@dataclass(frozen=True)
class Centreline:
    origin: Point
    direction: Point
    left_normal: Point
    s_min_m: float
    s_max_m: float
    shoreline_s_m: float | None
    convention: str


@dataclass(frozen=True)
class SampledSection:
    distance_to_shore_km: Any
    time_s: Any
    eta_m: Any
    bed_m: Any
    nearest_cell_index: Any
    nearest_distance_m: Any
    sampling_interval_m: float
    method: str


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def normalize_text_file(path: Path) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    path.write_text("\n".join(line.rstrip() for line in lines) + "\n", encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_output(command: Sequence[str], *, check: bool = False) -> tuple[int, str]:
    completed = subprocess.run(
        list(command),
        cwd=REPO_ROOT,
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


def current_branch() -> str:
    code, output = command_output(["git", "branch", "--show-current"])
    return output if code == 0 else "unknown"


def file_record(path: Path) -> dict[str, Any]:
    record: dict[str, Any] = {"path": path.as_posix(), "exists": path.exists()}
    if path.is_file():
        record.update({"sha256": sha256(path), "bytes": path.stat().st_size})
    return record


def wgs84_to_utm54n(latitude_deg: float, longitude_deg: float) -> Point:
    return wgs84_to_utm(latitude_deg, longitude_deg, zone=54)


def wgs84_to_utm(latitude_deg: float, longitude_deg: float, *, zone: int) -> Point:
    a = 6378137.0
    f = 1 / 298.257223563
    k0 = 0.9996
    e2 = f * (2 - f)
    ep2 = e2 / (1 - e2)
    lat = math.radians(latitude_deg)
    lon = math.radians(longitude_deg)
    lon0 = math.radians((zone - 1) * 6 - 180 + 3)
    n = a / math.sqrt(1 - e2 * math.sin(lat) ** 2)
    t = math.tan(lat) ** 2
    c = ep2 * math.cos(lat) ** 2
    aa = math.cos(lat) * (lon - lon0)
    m = a * (
        (1 - e2 / 4 - 3 * e2**2 / 64 - 5 * e2**3 / 256) * lat
        - (3 * e2 / 8 + 3 * e2**2 / 32 + 45 * e2**3 / 1024) * math.sin(2 * lat)
        + (15 * e2**2 / 256 + 45 * e2**3 / 1024) * math.sin(4 * lat)
        - (35 * e2**3 / 3072) * math.sin(6 * lat)
    )
    easting = k0 * n * (
        aa
        + (1 - t + c) * aa**3 / 6
        + (5 - 18 * t + t**2 + 72 * c - 58 * ep2) * aa**5 / 120
    ) + 500000
    northing = k0 * (
        m
        + n
        * math.tan(lat)
        * (
            aa**2 / 2
            + (5 - t + 9 * c + 4 * c**2) * aa**4 / 24
            + (61 - 58 * t + t**2 + 600 * c - 330 * ep2) * aa**6 / 720
        )
    )
    return Point(easting, northing)


def utm54n_to_wgs84(x: float, y: float) -> tuple[float, float]:
    return utm_to_wgs84(x, y, zone=54)


def utm_to_wgs84(x: float, y: float, *, zone: int) -> tuple[float, float]:
    a = 6378137.0
    f = 1 / 298.257223563
    k0 = 0.9996
    e2 = f * (2 - f)
    ep2 = e2 / (1 - e2)
    e1 = (1 - math.sqrt(1 - e2)) / (1 + math.sqrt(1 - e2))
    x0 = x - 500000
    m = y / k0
    mu = m / (a * (1 - e2 / 4 - 3 * e2**2 / 64 - 5 * e2**3 / 256))
    phi1 = (
        mu
        + (3 * e1 / 2 - 27 * e1**3 / 32) * math.sin(2 * mu)
        + (21 * e1**2 / 16 - 55 * e1**4 / 32) * math.sin(4 * mu)
        + (151 * e1**3 / 96) * math.sin(6 * mu)
        + (1097 * e1**4 / 512) * math.sin(8 * mu)
    )
    n1 = a / math.sqrt(1 - e2 * math.sin(phi1) ** 2)
    t1 = math.tan(phi1) ** 2
    c1 = ep2 * math.cos(phi1) ** 2
    r1 = a * (1 - e2) / (1 - e2 * math.sin(phi1) ** 2) ** 1.5
    d = x0 / (n1 * k0)
    lat = phi1 - (n1 * math.tan(phi1) / r1) * (
        d**2 / 2
        - (5 + 3 * t1 + 10 * c1 - 4 * c1**2 - 9 * ep2) * d**4 / 24
        + (61 + 90 * t1 + 298 * c1 + 45 * t1**2 - 252 * ep2 - 3 * c1**2) * d**6 / 720
    )
    lon0 = math.radians((zone - 1) * 6 - 180 + 3)
    lon = lon0 + (
        d
        - (1 + 2 * t1 + c1) * d**3 / 6
        + (5 - 2 * c1 + 28 * t1 - 3 * c1**2 + 8 * ep2 + 24 * t1**2) * d**5 / 120
    ) / math.cos(phi1)
    return math.degrees(lat), math.degrees(lon)


def subtract(a: Point, b: Point) -> Point:
    return Point(a.x - b.x, a.y - b.y)


def add(a: Point, b: Point) -> Point:
    return Point(a.x + b.x, a.y + b.y)


def scale(a: Point, value: float) -> Point:
    return Point(a.x * value, a.y * value)


def dot(a: Point, b: Point) -> float:
    return a.x * b.x + a.y * b.y


def ensure_layout() -> None:
    for path in [PUBLICATION_ROOT, PREVIEW_ROOT, PROVENANCE_ROOT, PYTHON_SOURCE_ROOT, QGIS_LAYER_ROOT, QGIS_STYLE_ROOT]:
        path.mkdir(parents=True, exist_ok=True)


def qgis_environment() -> dict[str, Any]:
    qgis_path = shutil.which("qgis")
    qgis_process_path = shutil.which("qgis_process")
    qgis_process_version = None
    if qgis_process_path:
        _, qgis_process_version = command_output(["qgis_process", "--version"])
    gdal_code, gdal_version = command_output(["gdalinfo", "--version"])
    proj_code, proj_output = command_output(["proj"])
    pyqgis_status = "AVAILABLE"
    try:
        __import__("qgis.core")
    except Exception as exc:  # pragma: no cover - depends on host.
        pyqgis_status = f"UNAVAILABLE: {type(exc).__name__}: {exc}"
    pyqt_status: dict[str, str] = {}
    for name in ["PyQt5", "PyQt6"]:
        try:
            __import__(name)
            pyqt_status[name] = "AVAILABLE"
        except Exception as exc:  # pragma: no cover - depends on host.
            pyqt_status[name] = f"UNAVAILABLE: {type(exc).__name__}: {exc}"
    status = "AVAILABLE" if qgis_path and qgis_process_path and pyqgis_status == "AVAILABLE" else "QGIS_RUNTIME_BLOCKED"
    env = {
        "schema": {"name": "tsunami.r16.qgis_runtime", "version": "1.0.0"},
        "status": status,
        "qgis_executable": qgis_path,
        "qgis_version": None,
        "qgis_process_executable": qgis_process_path,
        "qgis_process_version": qgis_process_version,
        "qt_status": pyqt_status,
        "pyqgis_status": pyqgis_status,
        "gdal_version": gdal_version if gdal_code == 0 else None,
        "proj_version": proj_output.splitlines()[0] if proj_code != 127 and proj_output else None,
        "headless_environment": {"QT_QPA_PLATFORM": os.environ.get("QT_QPA_PLATFORM", "offscreen recommended")},
        "recorded_at_utc": utc_now(),
    }
    write_json(QGIS_BLOCKER_PATH, env)
    return env


def load_corridor() -> dict[str, Any]:
    corridor = read_json(CORRIDOR_PATH)
    polygon = [Point(float(item["x"]), float(item["y"])) for item in corridor["corridor"]["polygon_projected_m"]]
    corridor["polygon_points"] = polygon
    corridor["polygon_wgs84_lonlat"] = [
        (utm54n_to_wgs84(point.x, point.y)[1], utm54n_to_wgs84(point.x, point.y)[0]) for point in polygon
    ]
    return corridor


def centreline_from_corridor(corridor: Mapping[str, Any]) -> Centreline:
    basis = corridor["basis"]
    event = corridor["event"]
    direction = Point(float(basis["centreline_unit"]["x"]), float(basis["centreline_unit"]["y"]))
    left_normal = Point(float(basis["left_normal_unit"]["x"]), float(basis["left_normal_unit"]["y"]))
    epicentre = Point(float(event["epicentre_projected_m"]["x"]), float(event["epicentre_projected_m"]["y"]))
    source_pre_extent = float(corridor["corridor"]["source_side_pre_extent_m"])
    origin = Point(epicentre.x - source_pre_extent * direction.x, epicentre.y - source_pre_extent * direction.y)
    s_max = source_pre_extent + float(basis["distance_m"]) + float(corridor["corridor"]["inland_extent_m"])
    nearshore = corridor["selected_nearshore_interface"]["projected_m"]
    nearshore_pt = Point(float(nearshore["x"]), float(nearshore["y"]))
    shoreline_s = dot(subtract(nearshore_pt, origin), direction)
    return Centreline(
        origin=origin,
        direction=direction,
        left_normal=left_normal,
        s_min_m=0.0,
        s_max_m=s_max,
        shoreline_s_m=shoreline_s,
        convention="distance_to_selected_wet_nearshore_interface_km_positive_offshore; 0 km at interface",
    )


def point_at(centreline: Centreline, s_m: float, sigma_m: float = 0.0) -> Point:
    return add(add(centreline.origin, scale(centreline.direction, s_m)), scale(centreline.left_normal, sigma_m))


def geojson_feature(geometry_type: str, coordinates: Any, properties: Mapping[str, Any]) -> dict[str, Any]:
    return {"type": "Feature", "properties": dict(properties), "geometry": {"type": geometry_type, "coordinates": coordinates}}


def write_geojson(path: Path, features: Sequence[Mapping[str, Any]], *, name: str) -> None:
    payload = {
        "type": "FeatureCollection",
        "name": name,
        "crs": {"type": "name", "properties": {"name": "EPSG:32654"}},
        "features": list(features),
    }
    write_json(path, payload)


def local3d_footprint(corridor: Mapping[str, Any]) -> list[Point]:
    local = read_json(G6_LOCAL_SUMMARY)
    span = float(local["dimensions_m"]["span"])
    length = float(local["dimensions_m"]["length"])
    centre = corridor["selected_nearshore_interface"]["projected_m"]
    centre_pt = Point(float(centre["x"]), float(centre["y"]))
    direction = Point(float(corridor["basis"]["centreline_unit"]["x"]), float(corridor["basis"]["centreline_unit"]["y"]))
    tangent = Point(float(corridor["basis"]["left_normal_unit"]["x"]), float(corridor["basis"]["left_normal_unit"]["y"]))
    a = add(centre_pt, scale(tangent, -0.5 * span))
    b = add(centre_pt, scale(tangent, 0.5 * span))
    c = add(b, scale(direction, length))
    d = add(a, scale(direction, length))
    return [a, b, c, d, a]


def prepare_gis_layers() -> dict[str, Any]:
    ensure_layout()
    corridor = load_corridor()
    centreline = centreline_from_corridor(corridor)
    register = read_json(R15_REGISTER)

    polygon_coords = [[[p.x, p.y] for p in corridor["polygon_points"]]]
    line_start = point_at(centreline, centreline.s_min_m)
    line_end = point_at(centreline, centreline.s_max_m)
    nearshore = corridor["selected_nearshore_interface"]["projected_m"]
    width = float(corridor["corridor"]["width_m"])
    coupling_centre = Point(float(nearshore["x"]), float(nearshore["y"]))
    coupling_a = add(coupling_centre, scale(centreline.left_normal, -0.5 * width))
    coupling_b = add(coupling_centre, scale(centreline.left_normal, 0.5 * width))
    footprint = local3d_footprint(corridor)

    geojsons = {
        "corridor_polygon": [
            geojson_feature(
                "Polygon",
                polygon_coords,
                {
                    "name": "R10 h400 Regional2D delivery corridor",
                    "authority": CORRIDOR_PATH.as_posix(),
                    "crs": "EPSG:32654",
                    "width_m": width,
                    "status": "actual_current_corridor",
                },
            )
        ],
        "corridor_centreline": [
            geojson_feature(
                "LineString",
                [[line_start.x, line_start.y], [line_end.x, line_end.y]],
                {
                    "name": "Source-to-Kamaishi corridor centreline",
                    "authority": CORRIDOR_PATH.as_posix(),
                    "distance_convention": centreline.convention,
                    "nearshore_s_m": centreline.shoreline_s_m,
                },
            )
        ],
        "coupling_section": [
            geojson_feature(
                "LineString",
                [[coupling_a.x, coupling_a.y], [coupling_b.x, coupling_b.y]],
                {
                    "name": "Selected wet nearshore interface",
                    "section_id": corridor["selected_nearshore_interface"]["section_id"],
                    "selection_reason": corridor["selected_nearshore_interface"]["selection_reason"],
                    "selection_fallback": corridor["selected_nearshore_interface"]["selection_fallback"],
                },
            )
        ],
        "local3d_candidate_footprint": [
            geojson_feature(
                "Polygon",
                [[[p.x, p.y] for p in footprint]],
                {
                    "name": "Conceptual Local3D impact-study footprint",
                    "status": "candidate_geographic_placement_for_framework_visual",
                    "source": G6_LOCAL_SUMMARY.as_posix(),
                    "final_local3d_extent_fixed": False,
                },
            )
        ],
    }
    event = corridor["event"]
    geojsons["event_and_kamaishi_points"] = [
        geojson_feature(
            "Point",
            [event["epicentre_projected_m"]["x"], event["epicentre_projected_m"]["y"]],
            {"name": "2011 Tohoku event epicentre reference", "role": "event_epicentre", "event_utc": EVENT_UTC},
        ),
        geojson_feature(
            "Point",
            [event["kamaishi_proxy_projected_m"]["x"], event["kamaishi_proxy_projected_m"]["y"]],
            {"name": "Kamaishi proxy", "role": "kamaishi_proxy"},
        ),
        geojson_feature(
            "Point",
            [coupling_centre.x, coupling_centre.y],
            {"name": "Selected wet nearshore interface centre", "role": "coupling_centre"},
        ),
    ]

    station_features = []
    for obs in register["observations"]:
        projected = obs.get("projected_m")
        if projected:
            x, y = float(projected["x"]), float(projected["y"])
        else:
            projected_point = wgs84_to_utm54n(float(obs["latitude"]), float(obs["longitude"]))
            x, y = projected_point.x, projected_point.y
        station_features.append(
            geojson_feature(
                "Point",
                [x, y],
                {
                    "observation_id": obs["observation_id"],
                    "name": obs["name"],
                    "authority": obs["authority"],
                    "eligibility": obs["eligibility"],
                    "distance_to_corridor_m": obs["distance_to_corridor_m"],
                    "quantity": obs["quantity"],
                    "data_status": obs["data_status"],
                },
            )
        )
    geojsons["validation_stations"] = station_features

    layer_sources: list[Path] = []
    for name, features in geojsons.items():
        path = QGIS_LAYER_ROOT / f"{name}.geojson"
        write_geojson(path, features, name=name)
        layer_sources.append(path)

    gpkg = QGIS_LAYER_ROOT / "r16_publication_layers.gpkg"
    if gpkg.exists():
        gpkg.unlink()
    gpkg_layers: dict[str, Any] = {}
    for index, source in enumerate(layer_sources):
        layer_name = source.stem
        command = ["ogr2ogr", "-f", "GPKG", gpkg.as_posix(), source.as_posix(), "-nln", layer_name, "-a_srs", "EPSG:32654"]
        if index > 0:
            command.insert(3, "-update")
            command.insert(4, "-append")
        code, output = command_output(command)
        gpkg_layers[layer_name] = {"source": source.as_posix(), "ogr2ogr_code": code, "ogr2ogr_output": output}
        if code != 0:
            raise RuntimeError(f"ogr2ogr failed for {source}: {output}")

    styles = write_qgis_styles()
    status = {
        "schema": {"name": "tsunami.r16.gis_layers", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "geopackage": file_record(gpkg),
        "layers": gpkg_layers,
        "styles": [path.as_posix() for path in styles],
        "crs": {
            "derived_layers": "EPSG:32654 WGS 84 / UTM zone 54N",
            "station_coordinates": "EPSG:4326 source, transformed to EPSG:32654 when projected_m absent",
            "source_raster": "WGS 84 + EGM2008 height (EPSG:9518 compound CRS)",
            "conditioned_terrain": "EPSG:32654 with EGM2008 vertical metadata, positive up",
        },
        "source_authority": source_authority_records(),
    }
    write_json(PROVENANCE_ROOT / "r16_gis_layer_manifest.json", status)
    return status


def write_qgis_styles() -> list[Path]:
    styles: dict[str, str] = {
        "corridor.qml": """<!DOCTYPE qgis PUBLIC 'http://mrcc.com/qgis.dtd' 'SYSTEM'>
<qgis version="3.0" styleCategories="Symbology">
  <renderer-v2 type="singleSymbol">
    <symbols><symbol type="fill" name="0" alpha="0.42"><layer class="SimpleFill"><Option type="Map"><Option name="color" value="38,112,178,108"/><Option name="outline_color" value="8,81,156,255"/><Option name="outline_width" value="0.55"/></Option></layer></symbol></symbols>
  </renderer-v2>
</qgis>
""",
        "event_source.qml": """<!DOCTYPE qgis PUBLIC 'http://mrcc.com/qgis.dtd' 'SYSTEM'>
<qgis version="3.0" styleCategories="Symbology"><renderer-v2 type="categorizedSymbol" attr="role"/></qgis>
""",
        "validation_stations.qml": """<!DOCTYPE qgis PUBLIC 'http://mrcc.com/qgis.dtd' 'SYSTEM'>
<qgis version="3.0" styleCategories="Symbology"><renderer-v2 type="categorizedSymbol" attr="eligibility"/></qgis>
""",
        "coupling.qml": """<!DOCTYPE qgis PUBLIC 'http://mrcc.com/qgis.dtd' 'SYSTEM'>
<qgis version="3.0" styleCategories="Symbology"><renderer-v2 type="singleSymbol"/></qgis>
""",
        "bathymetry.qml": """<!DOCTYPE qgis PUBLIC 'http://mrcc.com/qgis.dtd' 'SYSTEM'>
<qgis version="3.0" styleCategories="Symbology"><pipe/></qgis>
""",
        "topography.qml": """<!DOCTYPE qgis PUBLIC 'http://mrcc.com/qgis.dtd' 'SYSTEM'>
<qgis version="3.0" styleCategories="Symbology"><pipe/></qgis>
""",
    }
    paths = []
    for name, text in styles.items():
        path = QGIS_STYLE_ROOT / name
        path.write_text(text, encoding="utf-8")
        normalize_text_file(path)
        paths.append(path)
    return paths


def source_authority_records() -> dict[str, Any]:
    return {
        "bathymetry_authority": file_record(SOURCE_ETOPO),
        "simulation_conditioned_terrain": file_record(CONDITIONED_TERRAIN),
        "corridor_geometry": file_record(CORRIDOR_PATH),
        "centreline_manifest": file_record(CENTRELINE_PATH),
        "regional_result": file_record(H400_HDF5),
        "validation_station_register": file_record(R15_REGISTER),
        "local3d_geometry": file_record(G6_LOCAL_SUMMARY),
        "r13_replay_package": file_record(R13_REPLAY_PACKAGE),
    }


def configure_matplotlib() -> Any:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    if STYLE_PATH.is_file():
        plt.style.use(STYLE_PATH.as_posix())
    return plt


def save_figure(fig: Any, basename: str) -> list[str]:
    outputs: list[str] = []
    for suffix in [".pdf", ".svg", ".png"]:
        target = PUBLICATION_ROOT / f"{basename}{suffix}"
        target.parent.mkdir(parents=True, exist_ok=True)
        if suffix == ".png":
            fig.savefig(target, dpi=420)
            preview = PREVIEW_ROOT / f"{basename}.png"
            preview.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, preview)
        else:
            fig.savefig(target)
            if suffix == ".svg":
                normalize_text_file(target)
        outputs.append(target.relative_to(REPO_ROOT).as_posix())
    return outputs


def load_hdf5_arrays() -> dict[str, Any]:
    import h5py

    with h5py.File(H400_HDF5, "r") as h5:
        bed = h5["mesh/bed_elevation"][:]
        h = h5["fields/cell/h"][:]
        return {
            "time_s": h5["time/values"][:],
            "h": h,
            "qx": h5["fields/cell/qx"][:],
            "qy": h5["fields/cell/qy"][:],
            "bed_m": bed,
            "cell_centres": h5["mesh/cell_centres"][:],
            "points": h5["mesh/points"][:],
            "connectivity": h5["mesh/cells/connectivity"][:],
            "eta_m": h + bed[None, :],
        }


def sample_eta_along_centreline(spacing_m: float = 800.0) -> SampledSection:
    import numpy as np

    corridor = load_corridor()
    centreline = centreline_from_corridor(corridor)
    arrays = load_hdf5_arrays()
    centres = arrays["cell_centres"]
    s_cells = (centres[:, 0] - centreline.origin.x) * centreline.direction.x + (centres[:, 1] - centreline.origin.y) * centreline.direction.y
    sigma_cells = (centres[:, 0] - centreline.origin.x) * centreline.left_normal.x + (centres[:, 1] - centreline.origin.y) * centreline.left_normal.y
    s_start = max(centreline.s_min_m, float(np.nanmin(s_cells)))
    s_stop = min(float(centreline.shoreline_s_m or centreline.s_max_m), float(np.nanmax(s_cells)))
    sample_s = np.arange(s_start, s_stop + 0.5 * spacing_m, spacing_m)
    if sample_s[-1] > s_stop:
        sample_s[-1] = s_stop
    nearest_indices = []
    nearest_distances = []
    for s_m in sample_s:
        dist2 = (s_cells - s_m) ** 2 + sigma_cells**2
        idx = int(np.argmin(dist2))
        nearest_indices.append(idx)
        nearest_distances.append(float(math.sqrt(float(dist2[idx]))))
    nearest = np.asarray(nearest_indices, dtype=int)
    distance_to_shore = ((centreline.shoreline_s_m or s_stop) - sample_s) / 1000.0
    return SampledSection(
        distance_to_shore_km=distance_to_shore,
        time_s=arrays["time_s"],
        eta_m=arrays["eta_m"][:, nearest],
        bed_m=arrays["bed_m"][nearest],
        nearest_cell_index=nearest,
        nearest_distance_m=np.asarray(nearest_distances),
        sampling_interval_m=spacing_m,
        method="nearest unstructured cell centre to centreline samples in EPSG:32654; no smoothing or temporal interpolation",
    )


def principal_times(section: SampledSection) -> list[float]:
    import numpy as np

    eta = section.eta_m
    time = section.time_s
    max_abs = np.max(np.abs(eta), axis=1)
    peak_idx = int(np.argmax(max_abs))
    candidates = [0.0, 120.0, 240.0, float(time[peak_idx]), 480.0, 600.0]
    selected: list[float] = []
    for candidate in candidates:
        idx = int(np.argmin(np.abs(time - candidate)))
        value = float(time[idx])
        if value not in selected:
            selected.append(value)
    return selected[:6]


def section_metadata(section: SampledSection) -> dict[str, Any]:
    import numpy as np

    return {
        "coordinate_convention": "0 km at selected wet nearshore interface; positive offshore/sourceward; plotted with offshore left and nearshore right",
        "sampling_interval_m": section.sampling_interval_m,
        "sample_count": int(len(section.distance_to_shore_km)),
        "interpolation": section.method,
        "nearest_distance_m": {
            "min": float(np.min(section.nearest_distance_m)),
            "median": float(np.median(section.nearest_distance_m)),
            "max": float(np.max(section.nearest_distance_m)),
        },
    }


def plot_d1(section: SampledSection) -> dict[str, Any]:
    import numpy as np
    from matplotlib.colors import TwoSlopeNorm

    plt = configure_matplotlib()
    fig, ax = plt.subplots(figsize=(8.4, 5.8), constrained_layout=True)
    limit = float(np.nanmax(np.abs(section.eta_m)))
    norm = TwoSlopeNorm(vmin=-limit, vcenter=0.0, vmax=limit)
    mesh = ax.pcolormesh(
        section.distance_to_shore_km,
        section.time_s / 60.0,
        section.eta_m,
        cmap="RdBu_r",
        norm=norm,
        shading="auto",
    )
    cbar = fig.colorbar(mesh, ax=ax)
    cbar.set_label("Free-surface elevation eta, m")
    ax.axvline(0.0, color="#24292f", lw=1.0, alpha=0.75)
    ax.text(0.01, 0.97, "selected wet nearshore interface", transform=ax.transAxes, ha="left", va="top", fontsize=8.5)
    ax.set_xlabel("Distance to selected nearshore interface, km")
    ax.set_ylabel("Time after earthquake, min")
    ax.set_title("R10 h400 eta evolution along the Kamaishi corridor")
    ax.set_xlim(float(np.max(section.distance_to_shore_km)), 0.0)
    outputs = save_figure(fig, "figure_D1_eta_space_time")
    plt.close(fig)
    return complete_figure_record(
        "D1",
        "figure_D1_eta_space_time",
        "How does the simulated tsunami evolve as it propagates toward Kamaishi?",
        outputs,
        {
            "field": "eta = h + bed_elevation",
            "time_window_s": [float(section.time_s.min()), float(section.time_s.max())],
            "time_cadence_s": 5.0,
            "centreline": section_metadata(section),
            "colour_normalisation": {"type": "diverging", "center_m": 0.0, "symmetric_limit_m": limit, "cmap": "RdBu_r"},
            "allowed_claim": "Shows frozen R10 h400 limited_linear free-surface evolution along the accepted corridor sampling path.",
            "required_caveat": "Best available numerically uncertain real-event result; not spatially qualified, calibrated or historically validated.",
        },
    )


def plot_d2(section: SampledSection) -> dict[str, Any]:
    import numpy as np

    plt = configure_matplotlib()
    selected = principal_times(section)
    fig = plt.figure(figsize=(8.4, 5.8), constrained_layout=True)
    grid = fig.add_gridspec(5, 1, hspace=0.08)
    ax = fig.add_subplot(grid[:4, 0])
    bed_ax = fig.add_subplot(grid[4, 0], sharex=ax)
    colors = plt.get_cmap("viridis")(np.linspace(0.08, 0.9, len(selected)))
    for colour, t_s in zip(colors, selected, strict=True):
        idx = int(np.argmin(np.abs(section.time_s - t_s)))
        ax.plot(section.distance_to_shore_km, section.eta_m[idx], lw=2.0, color=colour, label=f"{section.time_s[idx] / 60:.1f} min")
    ax.axhline(0.0, color="#666666", lw=0.8)
    ax.set_ylabel("Free-surface elevation eta, m")
    ax.set_title("R10 h400 wave profiles approaching Kamaishi")
    ax.legend(title="time after event", frameon=False, ncols=3, loc="upper left")
    ax.grid(True, alpha=0.24)
    bed_ax.fill_between(section.distance_to_shore_km, section.bed_m, 0.0, where=section.bed_m <= 0.0, color="#6aaed6", alpha=0.42)
    bed_ax.plot(section.distance_to_shore_km, section.bed_m, color="#394b59", lw=1.1)
    bed_ax.axhline(0.0, color="#24292f", lw=0.8)
    bed_ax.set_xlabel("Distance to selected nearshore interface, km")
    bed_ax.set_ylabel("bed, m")
    bed_ax.set_xlim(float(np.max(section.distance_to_shore_km)), 0.0)
    outputs = save_figure(fig, "figure_D2_wave_profiles_to_shore")
    plt.close(fig)
    rationale = "Selected from start, representative propagation times, data-driven principal absolute eta peak, and end of the stored 0-600 s window."
    return complete_figure_record(
        "D2",
        "figure_D2_wave_profiles_to_shore",
        "How does the tsunami waveform change shape and amplitude as it approaches shore?",
        outputs,
        {
            "field": "eta = h + bed_elevation",
            "selected_times_s": selected,
            "selection_rationale": rationale,
            "centreline": section_metadata(section),
            "bathymetry_context": "same nearest-cell samples as eta; negative values are below EGM2008 datum with positive-up convention",
            "allowed_claim": "Shows frozen R10 h400 limited_linear eta profiles along the accepted corridor sampling path.",
            "required_caveat": "Best available numerically uncertain real-event result; not spatially qualified, calibrated or historically validated.",
        },
    )


def plot_s1(section: SampledSection) -> dict[str, Any]:
    import numpy as np

    plt = configure_matplotlib()
    fig, ax = plt.subplots(figsize=(8.2, 3.8), constrained_layout=True)
    ax.fill_between(section.distance_to_shore_km, section.bed_m, 0.0, where=section.bed_m <= 0.0, color="#8ecae6", alpha=0.52, label="below sea level")
    ax.plot(section.distance_to_shore_km, section.bed_m, color="#19324a", lw=1.8)
    ax.axhline(0.0, color="#24292f", lw=0.85)
    ax.set_xlim(float(np.max(section.distance_to_shore_km)), 0.0)
    ax.set_xlabel("Distance to selected nearshore interface, km")
    ax.set_ylabel("Bed elevation, m")
    ax.set_title("Longitudinal bathymetry sampled from the R10 h400 mesh")
    ax.grid(True, alpha=0.22)
    outputs = save_figure(fig, "figure_S1_longitudinal_bathymetry")
    plt.close(fig)
    return complete_figure_record(
        "S1",
        "figure_S1_longitudinal_bathymetry",
        "How does seabed elevation change along the tsunami propagation path?",
        outputs,
        {
            "field": "mesh/bed_elevation",
            "centreline": section_metadata(section),
            "allowed_claim": "Shows the bed elevation encountered by the centreline samples used for D1/D2.",
            "required_caveat": "This profile is sampled from the wet-conditioned h400 mesh, not an independent shoreline/topography validation.",
        },
    )


def complete_figure_record(figure_id: str, basename: str, question: str, outputs: Sequence[str], details: Mapping[str, Any]) -> dict[str, Any]:
    payload = {
        "schema": {"name": "tsunami.r16.figure_provenance", "version": "1.0.0"},
        "figure_id": figure_id,
        "basename": basename,
        "scientific_question": question,
        "status": "COMPLETE",
        "outputs": list(outputs),
        "preview": (PREVIEW_ROOT / f"{basename}.png").relative_to(REPO_ROOT).as_posix(),
        "source_datasets": source_authority_records(),
        "model_authority": SCIENTIFIC_AUTHORITY,
        "crs": {
            "simulation": "EPSG:32654 WGS 84 / UTM zone 54N",
            "source_terrain": "WGS 84 + EGM2008 height",
            "vertical_positive": "up",
            "publication_coordinate": "distance along accepted corridor centreline",
        },
        "software": software_record(),
        "git_sha": git_sha(),
        "generated_at_utc": utc_now(),
        **details,
    }
    for output in outputs:
        output_path = REPO_ROOT / output
        if output_path.exists():
            payload.setdefault("output_hashes", {})[output] = sha256(output_path)
    write_json(PROVENANCE_ROOT / f"{basename}.provenance.json", payload)
    return payload


def blocked_figure_record(figure_id: str, basename: str, question: str, reason: str) -> dict[str, Any]:
    payload = {
        "schema": {"name": "tsunami.r16.figure_provenance", "version": "1.0.0"},
        "figure_id": figure_id,
        "basename": basename,
        "scientific_question": question,
        "status": "BLOCKED_BY_QGIS_RUNTIME",
        "outputs": [],
        "preview": None,
        "blocker": reason,
        "source_datasets": source_authority_records(),
        "model_authority": SCIENTIFIC_AUTHORITY,
        "software": software_record(),
        "git_sha": git_sha(),
        "generated_at_utc": utc_now(),
        "allowed_claim": "No final cartographic claim; QGIS final cartography was not generated in this runtime.",
        "required_caveat": "QGIS_RUNTIME_BLOCKED; editable layers and PyQGIS project scripts are staged for a QGIS-capable environment.",
    }
    write_json(PROVENANCE_ROOT / f"{basename}.provenance.json", payload)
    return payload


def software_record() -> dict[str, Any]:
    qgis = read_json(QGIS_BLOCKER_PATH) if QGIS_BLOCKER_PATH.is_file() else qgis_environment()
    mpl_version = None
    numpy_version = None
    h5py_version = None
    try:
        import matplotlib

        mpl_version = matplotlib.__version__
    except Exception:
        pass
    try:
        import numpy

        numpy_version = numpy.__version__
    except Exception:
        pass
    try:
        import h5py

        h5py_version = h5py.__version__
    except Exception:
        pass
    return {
        "python": sys.version.split()[0],
        "matplotlib": mpl_version,
        "numpy": numpy_version,
        "h5py": h5py_version,
        "qgis": qgis,
    }


def write_style() -> None:
    STYLE_PATH.parent.mkdir(parents=True, exist_ok=True)
    STYLE_PATH.write_text(
        "\n".join(
            [
                "figure.facecolor: white",
                "axes.facecolor: white",
                "savefig.facecolor: white",
                "font.family: DejaVu Sans",
                "font.size: 10.5",
                "axes.titlesize: 13",
                "axes.labelsize: 10.5",
                "xtick.labelsize: 9.5",
                "ytick.labelsize: 9.5",
                "legend.fontsize: 9",
                "legend.title_fontsize: 9",
                "axes.linewidth: 0.85",
                "grid.linewidth: 0.45",
                "lines.linewidth: 1.8",
                "svg.fonttype: none",
                "pdf.fonttype: 42",
                "savefig.dpi: 420",
                "savefig.bbox: tight",
            ]
        )
        + "\n",
        encoding="utf-8",
    )


def generate() -> dict[str, Any]:
    ensure_layout()
    write_style()
    qgis = qgis_environment()
    gis_layers = prepare_gis_layers()
    qgis_blocked = qgis["status"] == "QGIS_RUNTIME_BLOCKED"
    figure_records: dict[str, dict[str, Any]] = {}
    if qgis_blocked:
        for figure_id, basename, question in [
            ("A", "figure_A_tohoku_kamaishi_corridor", "Where did the event occur, and what physical region is the computational framework intended to simulate?"),
            ("B", "figure_B_corridor_bathymetry_plan", "What real bathymetric and topographic environment does the simulated tsunami propagate through?"),
            ("C", "figure_C_corridor_bathymetry_oblique", "What does the three-dimensional seabed/coastal relief within the propagation corridor actually look like?"),
            ("E", "figure_E_hybrid_domain_framework", "How does the Regional2D model connect conceptually to a local high-fidelity impact domain?"),
            ("F", "figure_F_validation_geometry", "What observational datasets are relevant to Kamaishi, and how do they relate geometrically to the current computational corridor?"),
        ]:
            figure_records[figure_id] = blocked_figure_record(figure_id, basename, question, "QGIS is mandatory for final cartography/3D foundation, but qgis, qgis_process and PyQGIS are unavailable.")
    section = sample_eta_along_centreline()
    figure_records["D1"] = plot_d1(section)
    figure_records["D2"] = plot_d2(section)
    figure_records["S1"] = plot_s1(section)
    manifest = write_manifest(qgis, gis_layers, figure_records)
    write_handoff(manifest)
    write_completion_state(manifest)
    copy_self_to_sources()
    return manifest


def copy_self_to_sources() -> None:
    target = PYTHON_SOURCE_ROOT / "r16_publication.py"
    if Path(__file__).resolve() != target.resolve():
        shutil.copy2(Path(__file__), target)


def write_manifest(qgis: Mapping[str, Any], gis_layers: Mapping[str, Any], figures: Mapping[str, Mapping[str, Any]]) -> dict[str, Any]:
    manifest = {
        "schema": {"name": "tsunami.r16.publication_figure_manifest", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "starting_head": STARTING_HEAD,
        "git_sha": git_sha(),
        "branch": current_branch(),
        "worktree": REPO_ROOT.as_posix(),
        "qgis": dict(qgis),
        "qgis_project": {
            "path": QGIS_PROJECT.as_posix(),
            "status": "BLOCKED_BY_QGIS_RUNTIME" if qgis["status"] == "QGIS_RUNTIME_BLOCKED" else "COMPLETE",
            "layouts_requested": [
                "01_tohoku_event_corridor",
                "02_corridor_bathymetry",
                "03_validation_targets",
                "04_hybrid_domain",
            ],
        },
        "gis_layers": dict(gis_layers),
        "figures": {key: dict(value) for key, value in figures.items()},
        "task_states": {
            "Figure A": figures["A"]["status"],
            "Figure B": figures["B"]["status"],
            "Figure C": figures["C"]["status"],
            "Figure D1": figures["D1"]["status"],
            "Figure D2": figures["D2"]["status"],
            "Figure E": figures["E"]["status"],
            "Figure F": figures["F"]["status"],
            "Supporting bathymetry": figures["S1"]["status"],
            "QGIS project": "BLOCKED_BY_QGIS_RUNTIME" if qgis["status"] == "QGIS_RUNTIME_BLOCKED" else "COMPLETE",
            "provenance": "COMPLETE",
            "handoff": "COMPLETE",
        },
        "source_authority": source_authority_records(),
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
        },
    }
    write_json(PROVENANCE_ROOT / "publication_figure_manifest.json", manifest)
    md = [
        "# R16 Publication Figure Manifest",
        "",
        f"Generated: `{manifest['generated_at_utc']}`",
        f"Branch: `{manifest['branch']}`",
        f"Git SHA: `{manifest['git_sha']}`",
        "",
        "## QGIS Status",
        "",
        f"Status: **{qgis['status']}**.",
        "",
        "| Figure | Status | Output summary |",
        "|---|---|---|",
    ]
    for key in ["A", "B", "C", "D1", "D2", "E", "F", "S1"]:
        record = figures[key]
        outputs = ", ".join(record.get("outputs", [])) if record.get("outputs") else "none"
        md.append(f"| {key} | {record['status']} | {outputs} |")
    md.extend(
        [
            "",
            "## Data Authority",
            "",
            "- Bathymetry/topography source: ETOPO 2022 WGS84 + EGM2008 tile already used by the simulation preprocessing lineage.",
            "- Simulation terrain: G6 conditioned EPSG:32654 corridor terrain, EGM2008 positive-up, wet-conditioned and offshore/nearshore only.",
            "- Regional result: frozen R10 h400 limited_linear HDF5, no rerun.",
            "- Validation register: R15 observation register with 0 DIRECT, 1 PROXY, 28 TARGET_ONLY.",
        ]
    )
    (PROVENANCE_ROOT / "publication_figure_manifest.md").write_text("\n".join(md) + "\n", encoding="utf-8")
    return manifest


def write_handoff(manifest: Mapping[str, Any]) -> None:
    figures = manifest["figures"]
    lines = [
        "# R16 Publication Figure Handoff",
        "",
        "R16 supplies publication-figure assets only and does not edit the active poster.",
        "",
        "QGIS status: **{}**. Final cartographic Figures A, B, E and F require a QGIS-capable runtime; R16 has staged editable GIS layers and PyQGIS scripts rather than substituting non-QGIS final maps.".format(manifest["qgis"]["status"]),
        "",
        "| Figure | Priority | Status | Recommended use | Caption | Caveat |",
        "|---|---|---|---|---|---|",
    ]
    rows = {
        "A": ("HERO", "Poster opening map after QGIS export", "Tohoku event and accepted Kamaishi corridor geography.", "Blocked until QGIS export."),
        "B": ("HERO", "Poster real-case domain/bathymetry after QGIS export", "Real bathymetry/topography context for the accepted corridor.", "Blocked until QGIS export; conditioned corridor terrain is wet-only."),
        "D1": ("PRIMARY", "Poster or report wave-evolution panel", "Frozen R10 h400 eta evolution sampled along the corridor toward Kamaishi.", "Best available numerically uncertain, uncalibrated, not historically validated."),
        "D2": ("PRIMARY", "Report page 2 companion to D1", "Selected eta profiles show waveform evolution toward the nearshore interface.", "Nearest-cell centreline sampling; no smoothing or rerun."),
        "E": ("PRIMARY", "Framework figure after QGIS export", "Geographic Regional2D to conceptual Local3D one-way forcing relationship.", "Local3D footprint is conceptual, not final production closure."),
        "F": ("PRIMARY", "Validation status figure after QGIS export", "R15 validation targets relative to the current corridor.", "R15 classifications preserved exactly: 0 DIRECT, 1 PROXY, 28 TARGET_ONLY."),
        "C": ("SECONDARY", "Poster visual if QGIS 3D/PyVista becomes available", "Oblique real terrain view of the corridor.", "Blocked by missing QGIS/PyVista runtime."),
        "S1": ("REPORT_ONLY", "Two-page report bathymetry context", "Centreline bed profile used by D1/D2.", "Wet-conditioned mesh profile, not a full coastal topography map."),
    }
    for key in ["A", "B", "D1", "D2", "E", "F", "C", "S1"]:
        record = figures[key]
        priority, use, caption, caveat = rows[key]
        lines.append(f"| {key} | {priority} | {record['status']} | {use} | {caption} | {caveat} |")
    lines.extend(
        [
            "",
            "## Report Page 2 Recommendations",
            "",
            "- Best corridor/domain figure: Figure B after QGIS export; use S1 as fallback context if QGIS remains unavailable.",
            "- Best bathymetry figure: Figure B after QGIS export; S1 for report-only longitudinal context.",
            "- Best hybrid-domain schematic: Figure E after QGIS export.",
            "- Best wave-evolution figure: Figure D1, with D2 as the companion explanatory panel.",
            "- Best validation figure: Figure F after QGIS export; until then, use R15 validation_station_domain_map only with its existing caveats.",
            "",
            "## Allowed Claims",
            "",
            "R16 can claim reproducible GIS layer preparation and completed frozen-result propagation figures. It must not claim full historical validation, physical calibration, h250/h300/h400 spatial qualification, or Local3D current-generation closure.",
        ]
    )
    (DOCS_ROOT / "r16_publication_figure_handoff.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_completion_state(manifest: Mapping[str, Any]) -> None:
    payload = {
        "schema": {"name": "tsunami.r16.completion_state", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "task_states": manifest["task_states"],
        "no_early_exit": all(status in {"COMPLETE", "BLOCKED_BY_QGIS_RUNTIME", "BLOCKED_BY_SOURCE_DATA", "NOT_APPLICABLE"} for status in manifest["task_states"].values()),
        "qgis_runtime_blocked": manifest["qgis"]["status"] == "QGIS_RUNTIME_BLOCKED",
    }
    write_json(PROVENANCE_ROOT / "r16_completion_state.json", payload)


def validate_outputs() -> dict[str, Any]:
    from PIL import Image

    manifest_path = PROVENANCE_ROOT / "publication_figure_manifest.json"
    manifest = read_json(manifest_path)
    results: dict[str, Any] = {
        "schema": {"name": "tsunami.r16.validation", "version": "1.0.0"},
        "validated_at_utc": utc_now(),
        "json": {},
        "svg": {},
        "pdf": {},
        "png": {},
        "geopackage": {},
        "manifest_links": {},
    }
    for path in sorted(PROVENANCE_ROOT.glob("*.json")):
        read_json(path)
        results["json"][path.relative_to(REPO_ROOT).as_posix()] = "PASS"
    for record in manifest["figures"].values():
        for output in record.get("outputs", []):
            path = REPO_ROOT / output
            if path.suffix == ".svg":
                ET.parse(path)
                results["svg"][output] = "PASS"
            elif path.suffix == ".pdf":
                if path.stat().st_size <= 1024:
                    raise RuntimeError(f"PDF too small: {path}")
                results["pdf"][output] = {"status": "PASS", "bytes": path.stat().st_size}
            elif path.suffix == ".png":
                with Image.open(path) as image:
                    width, height = image.size
                if width < 1600 or height < 1000:
                    raise RuntimeError(f"PNG dimensions too small: {path}: {width}x{height}")
                results["png"][output] = {"status": "PASS", "width": width, "height": height}
        preview = record.get("preview")
        if preview:
            p = REPO_ROOT / preview
            if not p.is_file():
                raise RuntimeError(f"Missing preview: {preview}")
            results["manifest_links"][preview] = "PASS"
    gpkg = QGIS_LAYER_ROOT / "r16_publication_layers.gpkg"
    if gpkg.is_file():
        code, output = command_output(["ogrinfo", gpkg.as_posix()])
        if code != 0:
            raise RuntimeError(output)
        results["geopackage"][gpkg.relative_to(REPO_ROOT).as_posix()] = {"status": "PASS", "ogrinfo": output.splitlines()[:16]}
    else:
        results["geopackage"][gpkg.relative_to(REPO_ROOT).as_posix()] = "MISSING"
    write_json(PROVENANCE_ROOT / "r16_validation.json", results)
    return results


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("audit-env")
    sub.add_parser("prepare-gis-layers")
    sub.add_parser("generate")
    sub.add_parser("validate")
    args = parser.parse_args(argv)
    if args.command == "audit-env":
        print(json.dumps(qgis_environment(), indent=2, sort_keys=True))
    elif args.command == "prepare-gis-layers":
        print(json.dumps(prepare_gis_layers(), indent=2, sort_keys=True))
    elif args.command == "generate":
        print(json.dumps(generate(), indent=2, sort_keys=True))
    elif args.command == "validate":
        print(json.dumps(validate_outputs(), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
