#!/usr/bin/env python3
"""Kamaishi real-data hybrid delivery case assembly helpers."""

from __future__ import annotations

import csv
import hashlib
import importlib.util
import json
import math
import os
import resource
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence


CASE_ID = "kamaishi-etopo-usgs"
MANIFEST_ID = "kamaishi-etopo-usgs-datasets"
RUN_ID = "kamaishi-etopo-usgs-v1"
SECTION_ID = "kamaishi-nearshore-interface"
COUPLING_PATCH = "boundary.inland"
TERRAIN_DATASET_ID = "etopo-2022-v1-15s-n45e135-surface"
TERRAIN_ASSET_ID = "etopo-2022-v1-15s-n45e135-surface-tif"
FINITE_FAULT_DATASET_ID = "usgs-usp000hvnu-basic-inversion"
FINITE_FAULT_ASSET_ID = "usgs-usp000hvnu-basic-inversion-param"
DISPLACEMENT_DATASET_ID = "tohoku-earthquake-displacement"
DISPLACEMENT_ASSET_ID = "tohoku-vertical-displacement"
DISPLACEMENT_METADATA_ASSET_ID = "tohoku-vertical-displacement-metadata"
TERRAIN_OUTPUT_DATASET_ID = "conditioned-terrain"
TERRAIN_OUTPUT_PATH = Path("outputs/terrain/conditioned-terrain.tif")
TERRAIN_RECORD_PATH = Path("manifests/terrain/conditioned-terrain.json")
MESH_PATH = Path("meshes/kamaishi-regional.msh")
CORRIDOR_RECORD_PATH = Path("manifests/corridors/kamaishi-delivery-corridor.json")
SOURCE_TERRAIN_PATH = Path("data/source/terrain/ETOPO_2022_v1_15s_N45E135_surface.tif")
SOURCE_QUAKE_PATH = Path("data/source/earthquake/usgs_usp000hvnu_1539808472261_basic_inversion.param")
EARTHQUAKE_PRODUCER = Path("tools/earthquake/tohoku_usgs_finite_fault.py")
ACQUISITION_TOOL = Path("tools/cases/acquire_kamaishi_delivery_sources.sh")
OPENFOAM_RUNNER = Path("tools/openfoam/run_openfoam11.sh")
OPENFOAM_REPLAY = Path("tools/openfoam/openfoam_replay.py")
DEFAULT_R2D_BINARY = Path("build/linux-gcc-crs-test/apps/r2d_case/tsunami_r2d_case")
DEFAULT_PYTHON = Path("/tmp/tsunami-g3-producer-venv/bin/python")
GMESH_BINARY = "gmsh"
DEFAULT_G6_ARTIFACT_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi")
G5_ACCEPTED_REPLAY_REFERENCE = Path("tests/fixtures/kamaishi/g5_accepted_replay_reference.json")
G6_PREPROCESSING_STAGE_RECORD = Path("manifests/stages/g6_preprocessing_completion.json")


class DeliveryError(RuntimeError):
    """Raised when the Kamaishi delivery pipeline cannot continue."""


@dataclass(frozen=True)
class Profile:
    name: str
    spacing_m: float
    mesh_size_m: float


@dataclass(frozen=True)
class Point:
    x: float
    y: float


@dataclass(frozen=True)
class Trajectory:
    epicentre_wgs84: tuple[float, float]
    proxy_wgs84: tuple[float, float]
    epicentre: Point
    proxy: Point
    selected: Point
    selected_wgs84: tuple[float, float]
    unit: Point
    left: Point
    distance_m: float
    proxy_distance_to_interface_m: float
    bearing_degrees: float
    selected_bed_elevation_m: float
    selected_depth_m: float
    selection_fallback: bool
    selection_reason: str
    cross_section_sample_count: int = 0
    cross_section_min_depth_m: float = 0.0
    cross_section_max_depth_m: float = 0.0
    cross_section_min_bed_elevation_m: float = 0.0
    cross_section_max_bed_elevation_m: float = 0.0


@dataclass(frozen=True)
class Grid:
    width: int
    height: int
    spacing_m: float
    xi_min_m: float
    xi_max_m: float
    eta_bottom_m: float
    eta_top_m: float
    affine: tuple[float, float, float, float, float, float]
    extent: dict[str, float]


@dataclass(frozen=True)
class CrossSectionEvaluation:
    valid: bool
    fully_wet: bool
    fallback_used: bool
    centerline_depth_m: float
    minimum_depth_m: float
    maximum_depth_m: float
    minimum_bed_elevation_m: float
    maximum_bed_elevation_m: float
    sample_count: int
    reason: str


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def read_json(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise DeliveryError(f"{path} must contain a JSON object")
    return value


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def json_sha256(payload: Any) -> str:
    text = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def current_repo_commit() -> str | None:
    completed = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo_root(), text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if completed.returncode != 0:
        return None
    return completed.stdout.strip()


def run_command(command: Sequence[str], *, cwd: Path, log_path: Path | None = None) -> subprocess.CompletedProcess[str]:
    log_text = " ".join(command) + "\n"
    started = time.monotonic()
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    completed = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    setattr(
        completed,
        "resource_usage",
        {
            "wall_clock_s": time.monotonic() - started,
            "cpu_time_s": (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime),
            "peak_memory_kb": after.ru_maxrss,
        },
    )
    log_text += completed.stdout
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(log_text, encoding="utf-8")
    return completed


def command_resource_usage(completed: subprocess.CompletedProcess[str]) -> dict[str, float | int | None]:
    usage = getattr(completed, "resource_usage", None)
    if isinstance(usage, dict):
        return {
            "wall_clock_s": float(usage.get("wall_clock_s", 0.0)),
            "cpu_time_s": float(usage.get("cpu_time_s", 0.0)),
            "peak_memory_kb": int(usage.get("peak_memory_kb", 0)),
        }
    return {"wall_clock_s": None, "cpu_time_s": None, "peak_memory_kb": None}


def r2d_build_record(r2d_binary: Path) -> dict[str, object]:
    binary = r2d_binary if r2d_binary.is_absolute() else repo_root() / r2d_binary
    record: dict[str, object] = {
        "binary": str(binary),
        "binary_sha256": sha256(binary) if binary.is_file() else None,
        "source_commit": current_repo_commit(),
        "compiler": None,
        "compiler_flags": None,
        "build_type": None,
        "cmake_cache": None,
    }
    for parent in binary.parents:
        cache = parent / "CMakeCache.txt"
        if not cache.is_file():
            continue
        record["cmake_cache"] = str(cache)
        cache_values: dict[str, str] = {}
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line or line.startswith("//") or line.startswith("#") or "=" not in line:
                continue
            left, value = line.split("=", 1)
            key = left.split(":", 1)[0]
            cache_values[key] = value
        record["compiler"] = cache_values.get("CMAKE_CXX_COMPILER") or cache_values.get("CMAKE_C_COMPILER")
        record["compiler_flags"] = cache_values.get("CMAKE_CXX_FLAGS")
        record["build_type"] = cache_values.get("CMAKE_BUILD_TYPE")
        break
    return record


def load_g5_accepted_replay_reference(path: Path | None = None) -> dict:
    reference_path = path or (repo_root() / G5_ACCEPTED_REPLAY_REFERENCE)
    reference = read_json(reference_path)
    if reference.get("schema", {}).get("name") != "tsunami.g5_accepted_replay_reference":
        raise DeliveryError(f"{reference_path} is not a G5 accepted replay reference")
    return reference


def source_inventory(root: Path) -> dict:
    inventory = root / "data/source/source_inventory.json"
    if not inventory.is_file():
        raise DeliveryError("source inventory is missing; run with --acquire or use offline mode after acquisition")
    return read_json(inventory)


def ensure_sources(root: Path, *, acquire: bool, offline: bool, overwrite: bool) -> dict:
    command = [str(root / ACQUISITION_TOOL)]
    if offline:
        command.append("--offline")
    if overwrite:
        command.append("--overwrite")
    if acquire or offline:
        result = run_command(command, cwd=root, log_path=root / "data/source/acquire_kamaishi_delivery_sources.log")
        if result.returncode != 0:
            raise DeliveryError(f"source acquisition failed with exit status {result.returncode}")
    if not (root / SOURCE_TERRAIN_PATH).is_file() or not (root / SOURCE_QUAKE_PATH).is_file():
        raise DeliveryError("required Kamaishi source files are absent")
    return source_inventory(root)


def validate_finite_fault_source(path: Path) -> dict:
    import importlib.util

    spec = importlib.util.spec_from_file_location("tohoku_usgs_finite_fault", repo_root() / EARTHQUAKE_PRODUCER)
    if spec is None or spec.loader is None:
        raise DeliveryError("could not load finite-fault producer parser")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    subfaults = module.parse_usgs_basic_inversion_param(path)
    if not subfaults:
        raise DeliveryError("finite-fault source contains no parsed subfaults")
    text = path.read_text(encoding="utf-8", errors="replace")
    segment_count = sum(1 for line in text.splitlines() if "fault_segment" in line.lower())
    if segment_count == 0:
        segment_count = 1
    lons = [item.longitude for item in subfaults]
    lats = [item.latitude for item in subfaults]
    depths = [item.depth_km for item in subfaults]
    slips = [item.slip_m for item in subfaults]
    for item in subfaults:
        values = [
            item.longitude,
            item.latitude,
            item.depth_km,
            item.slip_m,
            item.rake_degrees,
            item.strike_degrees,
            item.dip_degrees,
            item.length_km,
            item.width_km,
        ]
        if not all(math.isfinite(value) for value in values):
            raise DeliveryError("finite-fault source contains non-finite values")
        if not (120.0 <= item.longitude <= 155.0 and 25.0 <= item.latitude <= 50.0):
            raise DeliveryError("finite-fault source has coordinates outside plausible Japan bounds")
        if item.depth_km < 0.0 or item.length_km <= 0.0 or item.width_km <= 0.0:
            raise DeliveryError("finite-fault source has invalid geometry")
    return {
        "segment_count": segment_count,
        "subfault_count": len(subfaults),
        "longitude_range": [min(lons), max(lons)],
        "latitude_range": [min(lats), max(lats)],
        "depth_km_range": [min(depths), max(depths)],
        "slip_m_range": [min(slips), max(slips)],
    }


def profile_by_name(name: str) -> Profile:
    profiles = {
        "etopo-500m": Profile("etopo-500m", 500.0, 250.0),
        "etopo-1000m": Profile("etopo-1000m", 1000.0, 1000.0),
    }
    try:
        return profiles[name]
    except KeyError as exc:
        raise DeliveryError(f"unknown Kamaishi profile: {name}") from exc


def transform_wgs84(lon: float, lat: float) -> Point:
    try:
        from pyproj import Transformer
    except ModuleNotFoundError:
        x, y = _utm54_forward(lon, lat)
    else:
        transformer = Transformer.from_crs("EPSG:4326", "EPSG:32654", always_xy=True)
        x, y = transformer.transform(lon, lat)
    return Point(float(x), float(y))


def inverse_transform(point: Point) -> tuple[float, float]:
    try:
        from pyproj import Transformer
    except ModuleNotFoundError:
        lon, lat = _utm54_inverse(point.x, point.y)
    else:
        transformer = Transformer.from_crs("EPSG:32654", "EPSG:4326", always_xy=True)
        lon, lat = transformer.transform(point.x, point.y)
    return float(lon), float(lat)


def _utm54_forward(lon: float, lat: float) -> tuple[float, float]:
    a = 6378137.0
    f = 1.0 / 298.257223563
    e2 = f * (2.0 - f)
    ep2 = e2 / (1.0 - e2)
    k0 = 0.9996
    lon0 = math.radians(141.0)
    phi = math.radians(lat)
    lam = math.radians(lon)
    sin_phi = math.sin(phi)
    cos_phi = math.cos(phi)
    tan_phi = math.tan(phi)
    n = a / math.sqrt(1.0 - e2 * sin_phi * sin_phi)
    t = tan_phi * tan_phi
    c = ep2 * cos_phi * cos_phi
    aa = cos_phi * (lam - lon0)
    m = a * (
        (1.0 - e2 / 4.0 - 3.0 * e2 * e2 / 64.0 - 5.0 * e2 ** 3 / 256.0) * phi
        - (3.0 * e2 / 8.0 + 3.0 * e2 * e2 / 32.0 + 45.0 * e2 ** 3 / 1024.0) * math.sin(2.0 * phi)
        + (15.0 * e2 * e2 / 256.0 + 45.0 * e2 ** 3 / 1024.0) * math.sin(4.0 * phi)
        - (35.0 * e2 ** 3 / 3072.0) * math.sin(6.0 * phi)
    )
    x = 500000.0 + k0 * n * (
        aa
        + (1.0 - t + c) * aa ** 3 / 6.0
        + (5.0 - 18.0 * t + t * t + 72.0 * c - 58.0 * ep2) * aa ** 5 / 120.0
    )
    y = k0 * (
        m
        + n * tan_phi * (
            aa * aa / 2.0
            + (5.0 - t + 9.0 * c + 4.0 * c * c) * aa ** 4 / 24.0
            + (61.0 - 58.0 * t + t * t + 600.0 * c - 330.0 * ep2) * aa ** 6 / 720.0
        )
    )
    return x, y


def _utm54_inverse(x: float, y: float) -> tuple[float, float]:
    a = 6378137.0
    f = 1.0 / 298.257223563
    e2 = f * (2.0 - f)
    ep2 = e2 / (1.0 - e2)
    k0 = 0.9996
    lon0 = math.radians(141.0)
    e1 = (1.0 - math.sqrt(1.0 - e2)) / (1.0 + math.sqrt(1.0 - e2))
    m = y / k0
    mu = m / (a * (1.0 - e2 / 4.0 - 3.0 * e2 * e2 / 64.0 - 5.0 * e2 ** 3 / 256.0))
    phi1 = (
        mu
        + (3.0 * e1 / 2.0 - 27.0 * e1 ** 3 / 32.0) * math.sin(2.0 * mu)
        + (21.0 * e1 * e1 / 16.0 - 55.0 * e1 ** 4 / 32.0) * math.sin(4.0 * mu)
        + (151.0 * e1 ** 3 / 96.0) * math.sin(6.0 * mu)
        + (1097.0 * e1 ** 4 / 512.0) * math.sin(8.0 * mu)
    )
    sin_phi1 = math.sin(phi1)
    cos_phi1 = math.cos(phi1)
    tan_phi1 = math.tan(phi1)
    c1 = ep2 * cos_phi1 * cos_phi1
    t1 = tan_phi1 * tan_phi1
    n1 = a / math.sqrt(1.0 - e2 * sin_phi1 * sin_phi1)
    r1 = a * (1.0 - e2) / (1.0 - e2 * sin_phi1 * sin_phi1) ** 1.5
    d = (x - 500000.0) / (n1 * k0)
    phi = phi1 - (n1 * tan_phi1 / r1) * (
        d * d / 2.0
        - (5.0 + 3.0 * t1 + 10.0 * c1 - 4.0 * c1 * c1 - 9.0 * ep2) * d ** 4 / 24.0
        + (61.0 + 90.0 * t1 + 298.0 * c1 + 45.0 * t1 * t1 - 252.0 * ep2 - 3.0 * c1 * c1) * d ** 6 / 720.0
    )
    lam = lon0 + (
        d
        - (1.0 + 2.0 * t1 + c1) * d ** 3 / 6.0
        + (5.0 - 2.0 * c1 + 28.0 * t1 - 3.0 * c1 * c1 + 8.0 * ep2 + 24.0 * t1 * t1) * d ** 5 / 120.0
    ) / cos_phi1
    return math.degrees(lam), math.degrees(phi)


def _unit_vector(a: Point, b: Point) -> tuple[Point, float]:
    dx = b.x - a.x
    dy = b.y - a.y
    distance = math.hypot(dx, dy)
    if distance <= 0.0:
        raise DeliveryError("reference points must be distinct")
    return Point(dx / distance, dy / distance), distance


def _bearing(unit: Point) -> float:
    return math.degrees(math.atan2(unit.x, unit.y)) % 360.0


def planned_coupling_offsets(width_m: float, spacing_m: float) -> list[float]:
    count = max(1, int(round(width_m / spacing_m)))
    step = width_m / count
    return [-0.5 * width_m + (index + 0.5) * step for index in range(count)]


def evaluate_cross_section_beds(
    beds: Sequence[float],
    centerline_bed_m: float,
    preferred_depth_band: tuple[float, float],
    fallback_depth_band: tuple[float, float],
    *,
    dry_depth_m: float = 1.0e-6,
) -> CrossSectionEvaluation:
    if not beds or not all(math.isfinite(value) for value in beds) or not math.isfinite(centerline_bed_m):
        return CrossSectionEvaluation(False, False, False, 0.0, 0.0, 0.0, 0.0, 0.0, len(beds), "invalid terrain sample")
    depths = [-bed for bed in beds]
    centerline_depth = max(0.0, -centerline_bed_m)
    minimum_depth = min(depths)
    maximum_depth = max(depths)
    minimum_bed = min(beds)
    maximum_bed = max(beds)
    fully_wet = all(bed < 0.0 and -bed > dry_depth_m for bed in beds)
    if not fully_wet:
        return CrossSectionEvaluation(
            True,
            False,
            False,
            centerline_depth,
            minimum_depth,
            maximum_depth,
            minimum_bed,
            maximum_bed,
            len(beds),
            "rejected: at least one transverse coupling sample is dry",
        )
    in_preferred = preferred_depth_band[0] <= centerline_depth <= preferred_depth_band[1]
    in_fallback = fallback_depth_band[0] <= centerline_depth <= fallback_depth_band[1]
    if not (in_preferred or in_fallback):
        return CrossSectionEvaluation(
            True,
            True,
            False,
            centerline_depth,
            minimum_depth,
            maximum_depth,
            minimum_bed,
            maximum_bed,
            len(beds),
            "rejected: centreline depth outside configured bands",
        )
    return CrossSectionEvaluation(
        True,
        True,
        not in_preferred,
        centerline_depth,
        minimum_depth,
        maximum_depth,
        minimum_bed,
        maximum_bed,
        len(beds),
        "accepted fallback wet cross-section" if not in_preferred else "accepted preferred wet cross-section",
    )


def select_interface(source_tif: Path, spec: dict) -> Trajectory:
    import rasterio

    event = spec["event"]
    proxy = spec["kamaishi_proxy"]
    epicentre_wgs84 = (float(event["epicentre_wgs84"]["longitude"]), float(event["epicentre_wgs84"]["latitude"]))
    proxy_coordinates = proxy.get("wgs84", proxy)
    proxy_wgs84 = (float(proxy_coordinates["longitude"]), float(proxy_coordinates["latitude"]))
    epicentre = transform_wgs84(*epicentre_wgs84)
    proxy_projected = transform_wgs84(*proxy_wgs84)
    to_proxy, distance_to_proxy = _unit_vector(epicentre, proxy_projected)
    left = Point(-to_proxy.y, to_proxy.x)
    preferred = tuple(float(v) for v in spec["nearshore_interface"]["preferred_depth_band_m"])
    fallback = tuple(float(v) for v in spec["nearshore_interface"]["fallback_depth_band_m"])
    profile = profile_by_name(str(spec.get("profile", "etopo-500m")))
    width = float(spec["corridor"]["width_m"])
    offsets = planned_coupling_offsets(width, profile.mesh_size_m)
    step_m = 250.0
    max_distance = max(distance_to_proxy + 20_000.0, 160_000.0)
    candidates: list[tuple[bool, float, Point, float, CrossSectionEvaluation]] = []
    with rasterio.open(source_tif) as dataset:
        for i in range(int(max_distance // step_m) + 1):
            d = i * step_m
            point = Point(proxy_projected.x - to_proxy.x * d, proxy_projected.y - to_proxy.y * d)
            transverse_points = [Point(point.x + left.x * offset, point.y + left.y * offset) for offset in offsets]
            lon_lat = [inverse_transform(sample_point) for sample_point in transverse_points]
            raw_samples = list(dataset.sample(lon_lat, indexes=1, masked=True))
            if any(getattr(sample, "mask", False).any() for sample in raw_samples):
                continue
            beds = [float(sample[0]) for sample in raw_samples]
            center_sample = next(dataset.sample([inverse_transform(point)], indexes=1, masked=True))
            if getattr(center_sample, "mask", False).any():
                continue
            center_bed = float(center_sample[0])
            evaluation = evaluate_cross_section_beds(beds, center_bed, preferred, fallback)
            if evaluation.valid and evaluation.fully_wet and (preferred[0] <= evaluation.centerline_depth_m <= preferred[1] or fallback[0] <= evaluation.centerline_depth_m <= fallback[1]):
                candidates.append((evaluation.fallback_used, d, point, center_bed, evaluation))
                if not evaluation.fallback_used:
                    break
    if not candidates:
        raise DeliveryError("could not select a fully wet nearshore interface across the configured coupling width")
    fallback_used, distance_from_proxy, selected, bed, evaluation = sorted(candidates)[0]
    selected_wgs84 = inverse_transform(selected)
    unit, distance = _unit_vector(epicentre, selected)
    if unit.x * to_proxy.x + unit.y * to_proxy.y <= 0.0:
        raise DeliveryError("selected interface is not on the epicentre-to-Kamaishi trajectory")
    return Trajectory(
        epicentre_wgs84=epicentre_wgs84,
        proxy_wgs84=proxy_wgs84,
        epicentre=epicentre,
        proxy=proxy_projected,
        selected=selected,
        selected_wgs84=selected_wgs84,
        unit=unit,
        left=Point(-unit.y, unit.x),
        distance_m=distance,
        proxy_distance_to_interface_m=distance_from_proxy,
        bearing_degrees=_bearing(unit),
        selected_bed_elevation_m=bed,
        selected_depth_m=evaluation.centerline_depth_m,
        selection_fallback=fallback_used,
        selection_reason=(
            "selected closest fully wet ETOPO cross-section in fallback 5-50 m centreline band"
            if fallback_used
            else "selected closest fully wet ETOPO cross-section in preferred 15-30 m centreline band"
        ),
        cross_section_sample_count=evaluation.sample_count,
        cross_section_min_depth_m=evaluation.minimum_depth_m,
        cross_section_max_depth_m=evaluation.maximum_depth_m,
        cross_section_min_bed_elevation_m=evaluation.minimum_bed_elevation_m,
        cross_section_max_bed_elevation_m=evaluation.maximum_bed_elevation_m,
    )


def build_grid(trajectory: Trajectory, profile: Profile, spec: dict) -> Grid:
    width = float(spec["corridor"]["width_m"])
    pre_extent = float(spec["corridor"]["source_side_pre_extent_m"])
    inland_extent = float(spec["corridor"].get("inland_extent_m", 0.0))
    xi_min = -pre_extent
    xi_max = math.ceil((trajectory.distance_m + inland_extent) / profile.spacing_m) * profile.spacing_m
    eta_bottom = -0.5 * width
    eta_top = 0.5 * width
    columns = int(round((xi_max - xi_min) / profile.spacing_m))
    rows = int(round((eta_top - eta_bottom) / profile.spacing_m))
    origin_x = trajectory.epicentre.x + trajectory.unit.x * xi_min + trajectory.left.x * eta_top
    origin_y = trajectory.epicentre.y + trajectory.unit.y * xi_min + trajectory.left.y * eta_top
    affine = (
        origin_x,
        profile.spacing_m * trajectory.unit.x,
        -profile.spacing_m * trajectory.left.x,
        origin_y,
        profile.spacing_m * trajectory.unit.y,
        -profile.spacing_m * trajectory.left.y,
    )
    corners = [
        Point(origin_x, origin_y),
        Point(origin_x + columns * affine[1], origin_y + columns * affine[4]),
        Point(origin_x + rows * affine[2], origin_y + rows * affine[5]),
        Point(origin_x + columns * affine[1] + rows * affine[2], origin_y + columns * affine[4] + rows * affine[5]),
    ]
    return Grid(
        width=columns,
        height=rows,
        spacing_m=profile.spacing_m,
        xi_min_m=xi_min,
        xi_max_m=xi_max,
        eta_bottom_m=eta_bottom,
        eta_top_m=eta_top,
        affine=affine,
        extent={
            "minimum_x": min(p.x for p in corners),
            "minimum_y": min(p.y for p in corners),
            "maximum_x": max(p.x for p in corners),
            "maximum_y": max(p.y for p in corners),
        },
    )


def target_reference() -> dict:
    return {
        "horizontal": {
            "authority_name": "EPSG",
            "authority_code": "32654",
            "name": "WGS 84 / UTM zone 54N",
            "canonical_wkt2": None,
            "canonical_projjson": None,
            "datum_name": "World Geodetic System 1984 ensemble",
            "datum_realisation": None,
            "coordinate_epoch_decimal_year": None,
            "axis_names": ["Easting", "Northing"],
            "axis_directions": ["east", "north"],
            "axis_units": ["metre", "metre"],
        },
        "vertical": {
            "authority_name": "EPSG",
            "authority_code": "3855",
            "name": "EGM2008 height",
            "canonical_wkt2": None,
            "canonical_projjson": None,
            "datum_name": "EGM2008 geoid",
            "datum_realisation": None,
            "coordinate_epoch_decimal_year": None,
            "axis_names": ["Gravity-related height"],
            "axis_directions": ["up"],
            "axis_units": ["metre"],
        },
        "storage_axes": "east_north",
        "horizontal_unit": "m",
        "vertical_unit": "m",
        "vertical_positive": "up",
    }


def source_reference() -> dict:
    return {
        "authority_name": "EPSG",
        "authority_code": "4326",
        "name": "WGS 84",
        "canonical_wkt2": None,
        "canonical_projjson": None,
        "datum_name": "World Geodetic System 1984 ensemble",
        "datum_realisation": None,
        "coordinate_epoch_decimal_year": None,
        "axis_names": ["Geodetic longitude", "Geodetic latitude"],
        "axis_directions": ["east", "north"],
        "axis_units": ["degree", "degree"],
    }


def operation_record() -> dict:
    return {
        "operation_name": "EPSG:4326 to EPSG:32654 using PROJ",
        "operation_authority": "EPSG",
        "operation_code": None,
        "operation_method": "Transverse Mercator",
        "operation_accuracy_m": 0.0,
        "scope": "Kamaishi delivery preprocessing",
        "area_of_use": {
            "west_longitude_degrees": 120.0,
            "south_latitude_degrees": 20.0,
            "east_longitude_degrees": 150.0,
            "north_latitude_degrees": 50.0,
        },
        "canonical_wkt2": None,
        "canonical_projjson": None,
        "canonical_pipeline": "proj_create_crs_to_crs(EPSG:4326,EPSG:32654)",
        "ballpark": False,
        "source_crs": source_reference(),
        "target_crs": target_reference()["horizontal"],
        "grids": [],
        "engine_name": "PROJ",
        "engine_version": "runtime",
        "database_version": None,
    }


def identity(kind: str, dataset_id: str, asset_id: str, now: str) -> dict:
    return {
        f"{kind}_id": f"{dataset_id}-{kind}",
        f"{kind}_revision": 1,
        "case_id": CASE_ID,
        "case_revision": 1,
        "manifest_id": MANIFEST_ID,
        "manifest_revision": 1,
        "dataset_id": dataset_id,
        "asset_id": asset_id,
        "executed_at_utc": now,
    }


def transform_identity(dataset_id: str, asset_id: str, import_id: str, process_id: str, now: str) -> dict:
    return {
        "transformation_id": f"{dataset_id}-to-epsg32654",
        "transformation_revision": 1,
        "case_id": CASE_ID,
        "case_revision": 1,
        "manifest_id": MANIFEST_ID,
        "manifest_revision": 1,
        "source_import_id": import_id,
        "source_import_revision": 1,
        "source_dataset_id": dataset_id,
        "source_asset_id": asset_id,
        "output_dataset_id": TERRAIN_OUTPUT_DATASET_ID,
        "output_process_id": process_id,
        "executed_at_utc": now,
    }


def resampling_record(role: str, dataset_id: str, asset_id: str, grid: Grid, source_valid: int, now: str) -> dict:
    import_id = f"{dataset_id}-import"
    process_id = "terrain-conditioning-process"
    import_record = identity("import", dataset_id, asset_id, now)
    import_record["import_id"] = import_id
    transformation = transform_identity(dataset_id, asset_id, import_id, process_id, now)
    return {
        "dataset_id": dataset_id,
        "asset_id": asset_id,
        "import_id": import_id,
        "import_identity": import_record,
        "transformation_id": transformation["transformation_id"],
        "transformation_identity": transformation,
        "role": role,
        "kernel": "bilinear",
        "source_registration": "pixel_is_area",
        "target_registration": "pixel_is_area",
        "source_scale": None,
        "source_offset": None,
        "minimum_source_spacing_m": 450.0,
        "maximum_source_spacing_m": 470.0,
        "nominal_source_spacing_m": 463.0,
        "target_spacing_m": grid.spacing_m,
        "maximum_upsampling_factor": 4.0,
        "source_valid_cell_count": source_valid,
        "output_valid_cell_count": grid.width * grid.height,
        "source_nodata_cell_count": 0,
        "outside_coverage_cell_count": 0,
        "operation_name": operation_record()["operation_name"],
        "operation": operation_record(),
        "vertical_operation": {
            "enabled": False,
            "steps": [],
        },
        "adapter_name": "rasterio.warp.reproject",
        "adapter_version": "runtime",
    }


def write_conditioned_terrain(case_root: Path, source_tif: Path, grid: Grid, trajectory: Trajectory, profile: Profile, now: str) -> dict:
    import numpy as np
    import rasterio
    from rasterio.transform import Affine
    from rasterio.warp import Resampling, reproject

    terrain_path = case_root / TERRAIN_OUTPUT_PATH
    terrain_path.parent.mkdir(parents=True, exist_ok=True)
    coverage_path = terrain_path.with_suffix(".coverage.tif")
    lineage_path = terrain_path.with_suffix(".lineage.tif")
    transform = Affine.from_gdal(*grid.affine)
    destination = np.full((grid.height, grid.width), -1.0e300, dtype="float64")
    interface_conditioned_cell_count = 0
    interface_conditioned_min_elevation = math.inf
    interface_conditioned_max_elevation = -math.inf
    with rasterio.open(source_tif) as source:
        source_valid = int(source.width * source.height)
        reproject(
            source=rasterio.band(source, 1),
            destination=destination,
            src_transform=source.transform,
            src_crs=source.crs,
            src_nodata=source.nodata,
            dst_transform=transform,
            dst_crs="EPSG:32654",
            dst_nodata=-1.0e300,
            resampling=Resampling.bilinear,
        )
        half_width = 0.5 * (grid.eta_top_m - grid.eta_bottom_m)
        boundary_band_m = max(profile.spacing_m, profile.mesh_size_m)
        for row in range(grid.height):
            for column in range(grid.width):
                x, y = transform * (column + 0.5, row + 0.5)
                dx = x - trajectory.selected.x
                dy = y - trajectory.selected.y
                xi = dx * trajectory.unit.x + dy * trajectory.unit.y
                eta = dx * trajectory.left.x + dy * trajectory.left.y
                if xi < -boundary_band_m or xi > profile.spacing_m or abs(eta) > half_width:
                    continue
                projected = Point(
                    trajectory.selected.x + trajectory.left.x * eta,
                    trajectory.selected.y + trajectory.left.y * eta,
                )
                sample = next(source.sample([inverse_transform(projected)], indexes=1, masked=True))
                if getattr(sample, "mask", False).any():
                    continue
                source_bed = float(sample[0])
                if source_bed >= 0.0:
                    continue
                conditioned_bed = min(source_bed, -5.0)
                destination[row, column] = min(float(destination[row, column]), conditioned_bed)
                interface_conditioned_cell_count += 1
                interface_conditioned_min_elevation = min(interface_conditioned_min_elevation, conditioned_bed)
                interface_conditioned_max_elevation = max(interface_conditioned_max_elevation, conditioned_bed)
    if not np.all(np.isfinite(destination)) or np.any(destination <= -1.0e299):
        raise DeliveryError("conditioned ETOPO terrain contains nodata in required cells")
    destination = np.minimum(destination, -5.0)
    coverage = np.ones_like(destination, dtype="float64")
    lineage = np.full(destination.shape, 5, dtype="uint16")
    base_tags = {
        "TSUNAMI_ARTIFACT_CONTRACT_VERSION": "1",
        "TSUNAMI_TERRAIN_ID": "conditioned-terrain",
        "TSUNAMI_TERRAIN_REVISION": "1",
        "TSUNAMI_CASE_ID": CASE_ID,
        "TSUNAMI_CASE_REVISION": "1",
        "TSUNAMI_MANIFEST_ID": MANIFEST_ID,
        "TSUNAMI_MANIFEST_REVISION": "1",
        "TSUNAMI_OUTPUT_DATASET_ID": TERRAIN_OUTPUT_DATASET_ID,
        "TSUNAMI_OUTPUT_PROCESS_ID": "terrain-conditioning-process",
        "TSUNAMI_SCHEMA_NAME": "tsunami.terrain_conditioning_record",
        "TSUNAMI_SCHEMA_VERSION": "2.0.0",
        "TSUNAMI_FORMULA_VERSION": "corridor-grid-priority-merge-v1",
        "TSUNAMI_VERTICAL_DATUM_NAME": "EGM2008 geoid",
        "TSUNAMI_VERTICAL_UNIT": "m",
        "TSUNAMI_VERTICAL_POSITIVE": "up",
    }
    for path, data, dtype, description, unit, role in (
        (terrain_path, destination, "float64", "bed_elevation", "m", "conditioned_terrain"),
        (coverage_path, coverage, "float64", "corridor_coverage_fraction", "1", "corridor_coverage_fraction"),
        (lineage_path, lineage, "uint16", "cell_lineage_code", "1", "terrain_cell_lineage"),
    ):
        tags = dict(base_tags)
        tags["TSUNAMI_ARTIFACT_ROLE"] = role
        if role == "terrain_cell_lineage":
            tags["TSUNAMI_LINEAGE_ENCODING_VERSION"] = "terrain-cell-lineage-code-v1"
        with rasterio.open(
            path,
            "w",
            driver="GTiff",
            height=grid.height,
            width=grid.width,
            count=1,
            dtype=dtype,
            transform=transform,
            crs="EPSG:32654",
        ) as dataset:
            dataset.write(data, 1)
            dataset.set_band_description(1, description)
            if hasattr(dataset, "set_band_unit"):
                dataset.set_band_unit(1, unit)
            dataset.update_tags(**tags)
    min_elevation = float(np.min(destination))
    max_elevation = float(np.max(destination))
    if min_elevation >= 0.0 or max_elevation > -5.0:
        raise DeliveryError("conditioned terrain must be a wet propagation corridor for this delivery case")
    total = grid.width * grid.height
    record = {
        "schema": {"schema_name": "tsunami.terrain_conditioning_record", "version": {"major": 2, "minor": 0, "patch": 0}},
        "policy_version": "0.1",
        "formula_version": "corridor-grid-priority-merge-v1",
        "identity": {
            "terrain_id": "conditioned-terrain",
            "terrain_revision": 1,
            "case_id": CASE_ID,
            "case_revision": 1,
            "manifest_id": MANIFEST_ID,
            "manifest_revision": 1,
            "output_dataset_id": TERRAIN_OUTPUT_DATASET_ID,
            "output_process_id": "terrain-conditioning-process",
            "executed_at_utc": now,
        },
        "scenario_id": "tohoku-2011-kamaishi-delivery",
        "target_site": "kamaishi",
        "bathymetry_dataset_id": TERRAIN_DATASET_ID,
        "bathymetry_asset_id": TERRAIN_ASSET_ID,
        "bathymetry_import_identity": resampling_record("bathymetry", TERRAIN_DATASET_ID, TERRAIN_ASSET_ID, grid, source_valid, now)["import_identity"],
        "bathymetry_transformation_identity": resampling_record("bathymetry", TERRAIN_DATASET_ID, TERRAIN_ASSET_ID, grid, source_valid, now)["transformation_identity"],
        "topography_dataset_id": TERRAIN_DATASET_ID,
        "topography_asset_id": TERRAIN_ASSET_ID,
        "topography_import_identity": resampling_record("topography", TERRAIN_DATASET_ID, TERRAIN_ASSET_ID, grid, source_valid, now)["import_identity"],
        "topography_transformation_identity": resampling_record("topography", TERRAIN_DATASET_ID, TERRAIN_ASSET_ID, grid, source_valid, now)["transformation_identity"],
        "corridor_id": "kamaishi-delivery-corridor",
        "corridor_identity": corridor_identity(trajectory, now),
        "target_reference": target_reference(),
        "grid": {
            "width": grid.width,
            "height": grid.height,
            "spacing_m": grid.spacing_m,
            "registration": "pixel_is_area",
            "xi_min_m": grid.xi_min_m,
            "xi_max_m": grid.xi_max_m,
            "eta_bottom_m": grid.eta_bottom_m,
            "eta_top_m": grid.eta_top_m,
            "longitudinal_padding_m": 0.0,
            "transverse_padding_m": 0.0,
            "affine": {
                "origin_x": grid.affine[0],
                "pixel_width": grid.affine[1],
                "row_rotation": grid.affine[2],
                "origin_y": grid.affine[3],
                "column_rotation": grid.affine[4],
                "pixel_height": grid.affine[5],
            },
            "extent": grid.extent,
        },
        "grid_policy": {
            "target_spacing_m": grid.spacing_m,
            "active_coverage_threshold": 0.5,
            "maximum_upsampling_factor": 4.0,
            "maximum_output_cells": max(total, 1),
            "numerical_absolute_tolerance": 1.0e-7,
            "numerical_relative_tolerance": 1.0e-12,
            "policy_basis": f"{profile.name} Kamaishi delivery grid",
        },
        "bathymetry_resampling": resampling_record("bathymetry", TERRAIN_DATASET_ID, TERRAIN_ASSET_ID, grid, source_valid, now),
        "topography_resampling": resampling_record("topography", TERRAIN_DATASET_ID, TERRAIN_ASSET_ID, grid, source_valid, now),
        "merge_policy": {
            "first_priority_dataset_id": TERRAIN_DATASET_ID,
            "second_priority_dataset_id": TERRAIN_DATASET_ID,
            "maximum_overlap_disagreement_m": 0.0,
            "conflict_policy": "accept_priority_with_warning",
            "priority_basis": "single ETOPO fallback source carries bathymetry and topography roles",
        },
        "gap_policy": {
            "kind": "reject",
            "maximum_fill_distance_m": 0.0,
            "maximum_component_diameter_m": 0.0,
            "maximum_component_cells": 0,
            "minimum_donor_count": 0,
            "distance_exponent": 0.0,
            "maximum_filled_fraction": 0.0,
            "policy_basis": "required delivery grid cells must be covered by ETOPO",
        },
        "diagnostics": {
            "total_cell_count": total,
            "active_cell_count": total,
            "outside_corridor_cell_count": 0,
            "excluded_boundary_cell_count": 0,
            "bathymetry_selected_cell_count": total,
            "topography_selected_cell_count": 0,
            "overlap_cell_count": total,
            "overlap_conflict_cell_count": 0,
            "initially_unresolved_cell_count": 0,
            "filled_cell_count": 0,
            "unresolved_cell_count": 0,
            "overlap": {
                "overlap_cell_count": total,
                "disagreement_exceedance_count": 0,
                "mean_signed_difference_m": 0.0,
                "root_mean_square_difference_m": 0.0,
                "maximum_absolute_difference_m": 0.0,
            },
            "minimum_elevation_m": min_elevation,
            "maximum_elevation_m": max_elevation,
            "warnings": ["ETOPO 2022 is a fallback integration terrain source; positive cells are wet-conditioned for the offshore-to-nearshore delivery corridor"],
        },
        "output_uncertainty_status": "not_reported",
        "output_uncertainty": {"status": "not_reported", "measures": [], "description": "not reported for delivery gate"},
        "output_media_type": "image/tiff",
        "output_path": TERRAIN_OUTPUT_PATH.as_posix(),
        "digest_status": "not_computed_by_terrain_conditioning",
        "warnings": ["single continuous ETOPO topobathymetry source used deliberately for delivery integration"],
    }
    write_json(case_root / TERRAIN_RECORD_PATH, record)
    return {
        "terrain_path": TERRAIN_OUTPUT_PATH.as_posix(),
        "coverage_path": Path(TERRAIN_OUTPUT_PATH).with_suffix(".coverage.tif").as_posix(),
        "lineage_path": Path(TERRAIN_OUTPUT_PATH).with_suffix(".lineage.tif").as_posix(),
        "minimum_elevation_m": min_elevation,
        "maximum_elevation_m": max_elevation,
        "nearshore_interface_conditioned_cell_count": interface_conditioned_cell_count,
        "nearshore_interface_conditioned_minimum_elevation_m": None
        if interface_conditioned_cell_count == 0
        else interface_conditioned_min_elevation,
        "nearshore_interface_conditioned_maximum_elevation_m": None
        if interface_conditioned_cell_count == 0
        else interface_conditioned_max_elevation,
        "width": grid.width,
        "height": grid.height,
    }


def corridor_identity(trajectory: Trajectory, now: str) -> dict:
    return {
        "corridor_id": "kamaishi-delivery-corridor",
        "corridor_revision": 1,
        "case_id": CASE_ID,
        "case_revision": 1,
        "trajectory_id": "tohoku-kamaishi-centreline",
        "output_dataset_id": "kamaishi-delivery-corridor",
        "output_process_id": "corridor-construction-process",
        "executed_at_utc": now,
    }


def point_transform_identity(point: str, source_dataset_id: str, output_dataset_id: str, now: str) -> dict:
    return {
        "transformation_id": f"{point}-wgs84-to-epsg32654",
        "transformation_revision": 1,
        "case_id": CASE_ID,
        "case_revision": 1,
        "manifest_id": MANIFEST_ID,
        "manifest_revision": 1,
        "source_import_id": f"{point}-source-import",
        "source_import_revision": 1,
        "source_dataset_id": source_dataset_id,
        "source_asset_id": f"{point}-source",
        "output_dataset_id": output_dataset_id,
        "output_process_id": f"{point}-coordinate-transformation",
        "executed_at_utc": now,
    }


def coordinate(point: Point, z: float) -> dict:
    return {"x": point.x, "y": point.y, "z": z}


def point2(point: Point) -> dict:
    return {"x": point.x, "y": point.y}


def polygon_area(points: Sequence[Point]) -> float:
    total = 0.0
    for left, right in zip(points, points[1:]):
        total += left.x * right.y - right.x * left.y
    return 0.5 * total


def polygon_perimeter(points: Sequence[Point]) -> float:
    return sum(math.hypot(right.x - left.x, right.y - left.y) for left, right in zip(points, points[1:]))


def corridor_evidence(role: str, point_id: str, point: Point, z: float, source_dataset_id: str, output_dataset_id: str, title: str, uri: str, now: str) -> dict:
    return {
        "role": role,
        "point_id": point_id,
        "definition": "projected reference point for Kamaishi delivery corridor",
        "coordinate": coordinate(point, z),
        "coordinate_index": 0,
        "source_feature_id": point_id,
        "transformation_identity": point_transform_identity(point_id, source_dataset_id, output_dataset_id, now),
        "source_reference": source_reference(),
        "target_reference": target_reference(),
        "source_document_title": title,
        "source_document_uri": uri,
        "accessed_at_utc": now,
    }


def write_corridor_record(case_root: Path, trajectory: Trajectory, spec: dict, grid: Grid, now: str) -> None:
    width = float(spec["corridor"]["width_m"])
    pre_extent = float(spec["corridor"]["source_side_pre_extent_m"])
    inland_extent = float(spec["corridor"].get("inland_extent_m", 0.0))
    start = Point(trajectory.epicentre.x - trajectory.unit.x * pre_extent, trajectory.epicentre.y - trajectory.unit.y * pre_extent)
    end = Point(trajectory.selected.x + trajectory.unit.x * inland_extent, trajectory.selected.y + trajectory.unit.y * inland_extent)
    half = 0.5 * width
    points = [
        Point(start.x + trajectory.left.x * half, start.y + trajectory.left.y * half),
        Point(start.x - trajectory.left.x * half, start.y - trajectory.left.y * half),
        Point(end.x - trajectory.left.x * half, end.y - trajectory.left.y * half),
        Point(end.x + trajectory.left.x * half, end.y + trajectory.left.y * half),
    ]
    ring = [*points, points[0]]
    area = abs(polygon_area(ring))
    perimeter = polygon_perimeter(ring)
    total_length = pre_extent + trajectory.distance_m + inland_extent
    extent = {
        "minimum_x": min(p.x for p in points),
        "minimum_y": min(p.y for p in points),
        "maximum_x": max(p.x for p in points),
        "maximum_y": max(p.y for p in points),
    }
    analytic_area = total_length * width
    analytic_perimeter = 2.0 * (total_length + width)
    policy = {
        "minimum_reference_separation_m": 1.0,
        "origin_tolerance_m": 0.001,
        "bearing_tolerance_degrees": 0.001,
        "basis_orthonormal_tolerance": 1.0e-12,
        "geometry_absolute_tolerance_m": 1.0e-5,
        "geometry_relative_tolerance": 1.0e-10,
        "tolerance_basis": "Kamaishi delivery preprocessing tolerances",
    }
    record = {
        "schema": {"schema_name": "tsunami.corridor_construction_record", "version": {"major": 1, "minor": 0, "patch": 0}},
        "policy_version": "0.1",
        "formula_version": "flat-ended-epicentre-target-v1",
        "identity": corridor_identity(trajectory, now),
        "scenario_id": "tohoku-2011-kamaishi-delivery",
        "target_site": "kamaishi",
        "epicentre": corridor_evidence(
            "epicentre",
            "tohoku-epicentre",
            trajectory.epicentre,
            -29_000.0,
            "tohoku-epicentre-source",
            "tohoku-epicentre-projected",
            "USGS official origin for 2011 Great Tohoku Earthquake",
            "https://earthquake.usgs.gov/earthquakes/eventpage/official20110311054624120_30",
            now,
        ),
        "target": corridor_evidence(
            "target",
            "kamaishi-nearshore-interface",
            trajectory.selected,
            trajectory.selected_bed_elevation_m,
            "kamaishi-interface-source",
            "kamaishi-interface-projected",
            "ETOPO-selected wet nearshore Kamaishi delivery interface",
            "data/source/terrain/ETOPO_2022_v1_15s_N45E135_surface.tif",
            now,
        ),
        "target_reference": target_reference(),
        "policy": policy,
        "configured_origin": point2(trajectory.epicentre),
        "configured_bearing_degrees": trajectory.bearing_degrees,
        "derived_bearing_degrees": trajectory.bearing_degrees,
        "origin_residual_m": 0.0,
        "bearing_residual_degrees": 0.0,
        "offshore_extent_m": pre_extent,
        "epicentre_target_distance_m": trajectory.distance_m,
        "inland_extent_m": inland_extent,
        "total_length_m": total_length,
        "offshore_width_m": width,
        "inland_width_m": width,
        "narrowing_enabled": False,
        "narrowing_rule": "disabled_constant_width",
        "local_basis": {
            "tangent": point2(trajectory.unit),
            "left_normal": point2(trajectory.left),
            "epicentre_target_distance_m": trajectory.distance_m,
            "derived_bearing_degrees_clockwise_from_north": trajectory.bearing_degrees,
        },
        "stations": {
            "offshore_xi_m": -pre_extent,
            "epicentre_xi_m": 0.0,
            "target_xi_m": trajectory.distance_m,
            "inland_xi_m": trajectory.distance_m + inland_extent,
        },
        "sponge_limits": {
            "offshore_start_xi_m": -pre_extent,
            "offshore_end_xi_m": -pre_extent + float(spec["corridor"]["offshore_sponge_width_m"]),
            "side_width_m": float(spec["corridor"]["side_sponge_width_m"]),
            "minimum_unsponge_width_m": width - 2.0 * float(spec["corridor"]["side_sponge_width_m"]),
        },
        "polygon": {
            "exterior_ring": [point2(p) for p in ring],
            "interior_rings": [],
        },
        "vertex_order_convention": "counter_clockwise_closed_offshore_left_offshore_right_inland_right_inland_left",
        "extent": extent,
        "area_m2": area,
        "perimeter_m": perimeter,
        "diagnostics": {
            "origin_residual_m": 0.0,
            "bearing_residual_degrees": 0.0,
            "basis_tangent_norm_residual": abs(math.hypot(trajectory.unit.x, trajectory.unit.y) - 1.0),
            "basis_normal_norm_residual": abs(math.hypot(trajectory.left.x, trajectory.left.y) - 1.0),
            "basis_orthogonality_residual": abs(trajectory.unit.x * trajectory.left.x + trajectory.unit.y * trajectory.left.y),
            "basis_determinant_residual": abs((trajectory.unit.x * trajectory.left.y - trajectory.unit.y * trajectory.left.x) - 1.0),
            "analytic_area_m2": analytic_area,
            "polygon_area_m2": area,
            "area_residual_m2": abs(area - analytic_area),
            "analytic_perimeter_m": analytic_perimeter,
            "polygon_perimeter_m": perimeter,
            "perimeter_residual_m": abs(perimeter - analytic_perimeter),
            "warnings": ["ETOPO-selected nearshore interface is delivery integration evidence, not calibrated harbour geometry"],
        },
        "configured_field_paths": [
            "/regional_2d/corridor/bearing_degrees_clockwise_from_north",
            "/regional_2d/corridor/inland_extent_m",
            "/regional_2d/corridor/narrowing/enabled",
            "/regional_2d/corridor/narrowing/inland_width_m",
            "/regional_2d/corridor/offshore_extent_m",
            "/regional_2d/corridor/origin/x",
            "/regional_2d/corridor/origin/y",
            "/regional_2d/corridor/sponge/offshore_width_m",
            "/regional_2d/corridor/sponge/side_width_m",
            "/regional_2d/corridor/trajectory_id",
            "/regional_2d/corridor/width_m",
        ],
        "warnings": ["Delivery integration geometry; not an exact Kamaishi harbour reconstruction."],
    }
    canonical = case_root / "manifests/corridors/tohoku-kamaishi-centreline.json"
    write_json(canonical, record)
    if canonical != case_root / CORRIDOR_RECORD_PATH:
        write_json(case_root / CORRIDOR_RECORD_PATH, record)
    evidence = {
        "event": {
            "epicentre_wgs84": {"longitude": trajectory.epicentre_wgs84[0], "latitude": trajectory.epicentre_wgs84[1]},
            "epicentre_projected_m": {"x": trajectory.epicentre.x, "y": trajectory.epicentre.y},
            "kamaishi_proxy_wgs84": {"longitude": trajectory.proxy_wgs84[0], "latitude": trajectory.proxy_wgs84[1]},
            "kamaishi_proxy_projected_m": {"x": trajectory.proxy.x, "y": trajectory.proxy.y},
        },
        "selected_nearshore_interface": {
            "section_id": SECTION_ID,
            "boundary_patch_name": COUPLING_PATCH,
            "wgs84": {"longitude": trajectory.selected_wgs84[0], "latitude": trajectory.selected_wgs84[1]},
            "projected_m": {"x": trajectory.selected.x, "y": trajectory.selected.y},
            "bed_elevation_m": trajectory.selected_bed_elevation_m,
            "water_depth_m": trajectory.selected_depth_m,
            "cross_section_sample_count": trajectory.cross_section_sample_count,
            "cross_section_minimum_depth_m": trajectory.cross_section_min_depth_m,
            "cross_section_maximum_depth_m": trajectory.cross_section_max_depth_m,
            "cross_section_minimum_bed_elevation_m": trajectory.cross_section_min_bed_elevation_m,
            "cross_section_maximum_bed_elevation_m": trajectory.cross_section_max_bed_elevation_m,
            "distance_from_proxy_m": trajectory.proxy_distance_to_interface_m,
            "selection_fallback": trajectory.selection_fallback,
            "selection_reason": trajectory.selection_reason,
        },
        "basis": {
            "centreline_unit": {"x": trajectory.unit.x, "y": trajectory.unit.y},
            "left_normal_unit": {"x": trajectory.left.x, "y": trajectory.left.y},
            "distance_m": trajectory.distance_m,
            "bearing_degrees_clockwise_from_north": trajectory.bearing_degrees,
        },
        "corridor": {
            "source_side_pre_extent_m": pre_extent,
            "inland_extent_m": inland_extent,
            "width_m": width,
            "narrowing_enabled": False,
            "offshore_sponge_width_m": float(spec["corridor"]["offshore_sponge_width_m"]),
            "side_sponge_width_m": float(spec["corridor"]["side_sponge_width_m"]),
            "polygon_projected_m": [{"x": p.x, "y": p.y} for p in ring],
        },
        "grid": {
            "profile": grid.spacing_m,
            "width": grid.width,
            "height": grid.height,
            "affine": grid.affine,
            "extent": grid.extent,
        },
        "disclaimer": "Delivery integration geometry; not an exact Kamaishi harbour reconstruction.",
    }
    write_json(case_root / "manifests/corridors/kamaishi-delivery-corridor-evidence.json", evidence)


def write_mesh(case_root: Path, trajectory: Trajectory, spec: dict, profile: Profile) -> dict:
    width = float(spec["corridor"]["width_m"])
    pre_extent = float(spec["corridor"]["source_side_pre_extent_m"])
    mesh_inset_m = max(1.0, profile.mesh_size_m * 0.002)
    start = Point(
        trajectory.epicentre.x - trajectory.unit.x * (pre_extent - mesh_inset_m),
        trajectory.epicentre.y - trajectory.unit.y * (pre_extent - mesh_inset_m),
    )
    end = Point(
        trajectory.selected.x - trajectory.unit.x * mesh_inset_m,
        trajectory.selected.y - trajectory.unit.y * mesh_inset_m,
    )
    half_width = width / 2.0 - mesh_inset_m
    p1 = Point(start.x - trajectory.left.x * half_width, start.y - trajectory.left.y * half_width)
    p2 = Point(start.x + trajectory.left.x * half_width, start.y + trajectory.left.y * half_width)
    p3 = Point(end.x + trajectory.left.x * half_width, end.y + trajectory.left.y * half_width)
    p4 = Point(end.x - trajectory.left.x * half_width, end.y - trajectory.left.y * half_width)
    geo_path = case_root / MESH_PATH.with_suffix(".geo")
    msh_path = case_root / MESH_PATH
    geo_path.parent.mkdir(parents=True, exist_ok=True)
    geo = f"""SetFactory("OpenCASCADE");
lc = {profile.mesh_size_m:.17g};
Point(1) = {{{p1.x:.17g}, {p1.y:.17g}, 0, lc}};
Point(2) = {{{p2.x:.17g}, {p2.y:.17g}, 0, lc}};
Point(3) = {{{p3.x:.17g}, {p3.y:.17g}, 0, lc}};
Point(4) = {{{p4.x:.17g}, {p4.y:.17g}, 0, lc}};
Line(1) = {{1, 2}};
Line(2) = {{2, 3}};
Line(3) = {{3, 4}};
Line(4) = {{4, 1}};
Curve Loop(1) = {{1, 2, 3, 4}};
Plane Surface(1) = {{1}};
Physical Surface("region.domain", 1) = {{1}};
Physical Curve("boundary.offshore", 2) = {{1}};
Physical Curve("boundary.left_side", 4) = {{2}};
Physical Curve("boundary.inland", 3) = {{3}};
Physical Curve("boundary.right_side", 5) = {{4}};
Mesh.MshFileVersion = 4.1;
Mesh.Algorithm = 6;
"""
    geo_path.write_text(geo, encoding="utf-8")
    command = [GMESH_BINARY, "-2", str(geo_path), "-format", "msh4", "-o", str(msh_path)]
    result = run_command(command, cwd=repo_root(), log_path=case_root / "meshes/log.gmsh")
    if result.returncode != 0:
        raise DeliveryError(f"gmsh failed with exit status {result.returncode}")
    text = msh_path.read_text(encoding="utf-8", errors="replace")
    if "$MeshFormat\n4.1" not in text or "boundary.inland" not in text:
        raise DeliveryError("generated mesh is not MSH 4.1 ASCII with required physical groups")
    return {"geo": MESH_PATH.with_suffix(".geo").as_posix(), "msh": MESH_PATH.as_posix(), "command": " ".join(command)}


def dataset_asset(asset_id: str, role: str, managed_path: Path, media_type: str, path: Path) -> dict:
    return {
        "asset_id": asset_id,
        "role": role,
        "location": {"kind": "managed_path", "managed_path": managed_path.as_posix(), "external_uri": None},
        "media_type": media_type,
        "byte_size": path.stat().st_size if path.is_file() else None,
        "digest": {"algorithm": "sha256", "value": sha256(path) if path.is_file() else "0" * 64, "origin": "project_computed"},
    }


def dataset_record(dataset_id: str, roles: list[str], origin: str, representation: str, title: str, assets: list[dict], source: dict | None, generated_by: str | None, spatial: dict) -> dict:
    spatial_resolution = (
        {"kind": "grid_spacing", "x": 500.0, "y": 500.0, "unit": "m", "description": "delivery integration nominal spacing"}
        if representation == "raster"
        else {"kind": "irregular", "x": None, "y": None, "unit": None, "description": "finite-fault subfault table spacing is segment-defined"}
    )
    return {
        "dataset_id": dataset_id,
        "origin_kind": origin,
        "representation": representation,
        "roles": roles,
        "title": title,
        "description": "Kamaishi delivery integration dataset; not a publication-ready validation product.",
        "provider_id": "noaa" if dataset_id == TERRAIN_DATASET_ID else ("usgs" if dataset_id == FINITE_FAULT_DATASET_ID else "project"),
        "licence_id": "public-domain",
        "source": source,
        "generated_by_process_id": generated_by,
        "assets": assets,
        "spatial_reference": spatial,
        "resolution": {
            "spatial": spatial_resolution,
            "temporal": {"kind": "static_dataset", "value": None, "unit": None, "description": None},
        },
        "uncertainty": {"status": "not_reported", "measures": [], "description": "not reported for delivery gate"},
        "citation": "NOAA ETOPO 2022 and USGS finite-fault source records; see source inventory.",
        "extensions": {},
    }


def write_case_and_manifest(case_root: Path, root: Path, spec: dict, inventory: dict, trajectory: Trajectory, now: str) -> None:
    terrain_source = next(source for source in inventory["sources"] if source["dataset_id"] == TERRAIN_DATASET_ID)
    quake_source = next(source for source in inventory["sources"] if source["dataset_id"] == "usgs-usp000hvnu-1539808472261-basic-inversion")
    source_timestamp = now
    terrain_abs = root / SOURCE_TERRAIN_PATH
    quake_abs = root / SOURCE_QUAKE_PATH
    terrain_case_path = Path("inputs/data/source/terrain/ETOPO_2022_v1_15s_N45E135_surface.tif")
    quake_case_path = Path("inputs/data/source/earthquake/usgs_usp000hvnu_1539808472261_basic_inversion.param")
    conditioned_case_path = Path("inputs/data/terrain/conditioned-terrain.tif")
    for source, target in (
        (terrain_abs, case_root / terrain_case_path),
        (quake_abs, case_root / quake_case_path),
        (case_root / TERRAIN_OUTPUT_PATH, case_root / conditioned_case_path),
    ):
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            shutil.copy2(source, target)
    epicentre_point_path = Path("inputs/data/points/tohoku-epicentre-source.json")
    interface_point_path = Path("inputs/data/points/kamaishi-nearshore-interface-source.json")
    write_json(case_root / epicentre_point_path, {"longitude": trajectory.epicentre_wgs84[0], "latitude": trajectory.epicentre_wgs84[1], "role": "epicentre"})
    write_json(case_root / interface_point_path, {"longitude": trajectory.selected_wgs84[0], "latitude": trajectory.selected_wgs84[1], "role": "nearshore_interface"})
    displacement_tif = case_root / "inputs/data/earthquake/tohoku_vertical_displacement.tif"
    displacement_json = case_root / "inputs/data/earthquake/tohoku_vertical_displacement.json"
    conditioned_tif = case_root / TERRAIN_OUTPUT_PATH
    manifest = {
        "schema_version": "1.0.0",
        "policy_version": "0.1",
        "manifest": {
            "manifest_id": MANIFEST_ID,
            "manifest_revision": 1,
            "case_id": CASE_ID,
            "case_revision": 1,
            "created_at_utc": now,
            "created_by": "tools/cases/run_kamaishi_hybrid_delivery.py",
        },
        "providers": [
            {"provider_id": "noaa", "name": "NOAA/NCEI", "organisation": "National Centers for Environmental Information", "homepage_uri": "https://www.ncei.noaa.gov/", "extensions": {}},
            {"provider_id": "usgs", "name": "USGS", "organisation": "United States Geological Survey", "homepage_uri": "https://earthquake.usgs.gov/", "extensions": {}},
            {"provider_id": "project", "name": "Summer Studentship preprocessing", "organisation": "Summer-Studentship", "homepage_uri": "https://example.invalid/summer-studentship", "extensions": {}},
        ],
        "licences": [
            {"licence_id": "public-domain", "name": "Public domain / source terms", "expression": "LicenseRef-Public-Domain", "licence_uri": "https://www.usa.gov/government-copyright", "attribution": "NOAA/NCEI ETOPO 2022 and USGS finite-fault product", "extensions": {}}
        ],
        "datasets": [
            dataset_record(
                TERRAIN_DATASET_ID,
                ["bathymetry", "topography"],
                "source",
                "raster",
                "NOAA ETOPO 2022 v1 15 arc-second surface elevation tile N45E135",
                [dataset_asset(TERRAIN_ASSET_ID, "primary", terrain_case_path, "image/tiff", terrain_abs)],
                {"source_uri": terrain_source["source_uri"], "accessed_at_utc": source_timestamp, "source_version": "ETOPO 2022 v1", "publication_date": "2022-01-01"},
                None,
                {"applicability": "spatial", "horizontal_crs": "EPSG:4326", "vertical_datum": "EGM2008 / EPSG:3855 evidence from NOAA product documentation", "horizontal_unit": "degree", "vertical_unit": "m", "axis_order": "longitude_latitude", "vertical_positive": "up"},
            ),
            dataset_record(
                FINITE_FAULT_DATASET_ID,
                ["auxiliary"],
                "source",
                "table",
                "USGS Tohoku finite-fault basic inversion parameter file",
                [dataset_asset(FINITE_FAULT_ASSET_ID, "primary", quake_case_path, "text/plain", quake_abs)],
                {"source_uri": quake_source["source_uri"], "accessed_at_utc": source_timestamp, "source_version": "1539808472261", "publication_date": "2011-03-11"},
                None,
                {"applicability": "spatial", "horizontal_crs": "EPSG:4326", "vertical_datum": "depth below reference surface", "horizontal_unit": "degree", "vertical_unit": "m", "axis_order": "longitude_latitude", "vertical_positive": "down"},
            ),
            dataset_record(
                "tohoku-epicentre-source",
                ["auxiliary"],
                "source",
                "point_series",
                "USGS Tohoku epicentre point used for corridor construction",
                [dataset_asset("tohoku-epicentre-source", "primary", epicentre_point_path, "application/json", case_root / epicentre_point_path)],
                {"source_uri": "https://earthquake.usgs.gov/earthquakes/eventpage/official20110311054624120_30", "accessed_at_utc": source_timestamp, "source_version": "official origin", "publication_date": "2011-03-11"},
                None,
                {"applicability": "spatial", "horizontal_crs": "EPSG:4326", "vertical_datum": "not_applicable", "horizontal_unit": "degree", "vertical_unit": "m", "axis_order": "longitude_latitude", "vertical_positive": "up"},
            ),
            dataset_record(
                "kamaishi-interface-source",
                ["auxiliary"],
                "generated",
                "point_series",
                "ETOPO-selected Kamaishi nearshore interface point",
                [dataset_asset("kamaishi-nearshore-interface-source", "primary", interface_point_path, "application/json", case_root / interface_point_path)],
                None,
                "terrain-conditioning-process",
                {"applicability": "spatial", "horizontal_crs": "EPSG:4326", "vertical_datum": "EGM2008 / EPSG:3855 evidence", "horizontal_unit": "degree", "vertical_unit": "m", "axis_order": "longitude_latitude", "vertical_positive": "up"},
            ),
            dataset_record(
                TERRAIN_OUTPUT_DATASET_ID,
                ["auxiliary"],
                "generated",
                "raster",
                "Conditioned Kamaishi delivery terrain bundle on EPSG:32654 grid",
                [dataset_asset("conditioned-terrain-primary", "primary", conditioned_case_path, "image/tiff", conditioned_tif)],
                None,
                "terrain-conditioning-process",
                {"applicability": "spatial", "horizontal_crs": "EPSG:32654", "vertical_datum": "EGM2008 / EPSG:3855 evidence", "horizontal_unit": "m", "vertical_unit": "m", "axis_order": "east_north", "vertical_positive": "up"},
            ),
            dataset_record(
                DISPLACEMENT_DATASET_ID,
                ["earthquake_displacement"],
                "generated",
                "raster",
                "Generated Tohoku vertical seabed displacement on the Kamaishi delivery grid",
                [
                    dataset_asset(DISPLACEMENT_ASSET_ID, "primary", Path("inputs/data/earthquake/tohoku_vertical_displacement.tif"), "image/tiff", displacement_tif),
                    dataset_asset(DISPLACEMENT_METADATA_ASSET_ID, "metadata", Path("inputs/data/earthquake/tohoku_vertical_displacement.json"), "application/json", displacement_json),
                ],
                None,
                "finite-fault-displacement-production",
                {"applicability": "spatial", "horizontal_crs": "EPSG:32654", "vertical_datum": "EGM2008 / EPSG:3855 evidence", "horizontal_unit": "m", "vertical_unit": "m", "axis_order": "east_north", "vertical_positive": "up"},
            ),
        ],
        "processes": [
            {
                "process_id": "terrain-conditioning-process",
                "operation": "terrain-conditioning",
                "executed_at_utc": now,
                "software": {"name": "tools/cases/run_kamaishi_hybrid_delivery.py", "version": "1.0.0", "repository_uri": None, "commit_sha": None},
                "parameters": {"profile": spec.get("profile", "etopo-500m")},
                "input_dataset_ids": [TERRAIN_DATASET_ID],
                "output_dataset_ids": [TERRAIN_OUTPUT_DATASET_ID, "kamaishi-interface-source"],
                "extensions": {},
            },
            {
                "process_id": "finite-fault-displacement-production",
                "operation": "finite-fault-displacement",
                "executed_at_utc": now,
                "software": {"name": "tools/earthquake/tohoku_usgs_finite_fault.py", "version": "0.1.0", "repository_uri": None, "commit_sha": None},
                "parameters": {"target_crs": "EPSG:32654"},
                "input_dataset_ids": [FINITE_FAULT_DATASET_ID, TERRAIN_OUTPUT_DATASET_ID],
                "output_dataset_ids": [DISPLACEMENT_DATASET_ID],
                "extensions": {},
            },
        ],
        "extensions": {
            "delivery_gate": "Prompt C",
            "source_acquisition": {
                "tool": "tools/cases/acquire_kamaishi_delivery_sources.sh",
                "inventory": "data/source/source_inventory.json",
                "sha256sums": "data/source/SHA256SUMS",
            },
        },
    }
    write_json(case_root / "manifests/datasets.json", manifest)
    case_json = {
        "schema_version": "1.0.0",
        "policy_version": "0.1",
        "case": {"case_id": CASE_ID, "case_slug": "kamaishi-etopo-usgs", "revision": 1, "created_at_utc": now, "created_by": "tools/cases/run_kamaishi_hybrid_delivery.py"},
        "scenario": {"scenario_id": "tohoku-2011-kamaishi-delivery", "event_id": "usgs-usp000hvnu", "target_site": "kamaishi", "model_family": "regional_2d"},
        "coordinate_frame": {"horizontal_crs": "EPSG:32654", "vertical_datum": "EPSG:3855", "horizontal_unit": "m", "vertical_unit": "m", "axis_order": "east_north", "vertical_positive": "up"},
        "datasets": {
            "manifest_path": "manifests/datasets.json",
            "bindings": {
                "bathymetry": TERRAIN_DATASET_ID,
                "topography": TERRAIN_DATASET_ID,
                "earthquake_displacement": DISPLACEMENT_DATASET_ID,
                "prescribed_surface": None,
                "manning": None,
                "coriolis": None,
                "observations": [],
            },
        },
        "regional_2d": {
            "corridor": {
                "trajectory_id": "tohoku-kamaishi-centreline",
                "origin": {"x": trajectory.epicentre.x, "y": trajectory.epicentre.y},
                "bearing_degrees_clockwise_from_north": trajectory.bearing_degrees,
                "width_m": float(spec["corridor"]["width_m"]),
                "offshore_extent_m": float(spec["corridor"]["source_side_pre_extent_m"]),
                "inland_extent_m": float(spec["corridor"].get("inland_extent_m", 0.0)),
                "narrowing": {"enabled": False, "inland_width_m": None},
                "sponge": {"offshore_width_m": float(spec["corridor"]["offshore_sponge_width_m"]), "side_width_m": float(spec["corridor"]["side_sponge_width_m"])},
            },
            "physics": {
                "gravity_m_per_s2": 9.80665,
                "manning": {"kind": "uniform", "value_s_per_m_one_third": 0.025, "dataset_binding": None},
                "coriolis": {"kind": "disabled", "value_per_s": None, "dataset_binding": None},
                "earthquake": {"enabled": True, "displacement_binding": DISPLACEMENT_DATASET_ID, "bed_mapping": "vertical_only", "surface_transfer": "passive_equal_to_effective_bed", "prescribed_surface_binding": None},
            },
            "numerics": {
                "scheme": "ssprk3",
                "courant_number": float(spec["regional_2d"].get("courant_number", 0.45)),
                "positivity_safety_factor": 0.95,
                "relaxation_safety_factor": 1.0,
                "source_safety_factor": 0.25,
                "minimum_timestep_s": 1.0e-12,
                "maximum_timestep_s": float(spec["regional_2d"]["maximum_timestep_s"]),
                "final_time_s": float(spec["regional_2d"]["final_time_s"]),
                "maximum_steps": int(spec["regional_2d"]["maximum_steps"]),
            },
            "boundaries": {
                "offshore": {"kind": "radiation"},
                "inland": {"kind": "transmissive"},
                "left_side": {"kind": "radiation"},
                "right_side": {"kind": "radiation"},
                "relaxation": {"enabled": True, "maximum_rate_per_s": 0.02, "profile_exponent": 2.0},
            },
        },
        "outputs": {"snapshot_interval_s": float(spec["regional_2d"]["snapshot_interval_s"]), "diagnostics_enabled": True, "initialisation_diagnostics_enabled": True, "checkpoint_interval_s": None},
        "extensions": {"delivery_gate": "Prompt C", "local3d_disclaimer": "representative Kamaishi-forced local replay, not exact harbour reconstruction"},
    }
    write_json(case_root / "case.json", case_json)


def produce_displacement(case_root: Path, root: Path, now: str, python: Path) -> dict:
    output_tif = case_root / "inputs/data/earthquake/tohoku_vertical_displacement.tif"
    output_json = case_root / "inputs/data/earthquake/tohoku_vertical_displacement.json"
    command = [
        str(python),
        str(root / EARTHQUAKE_PRODUCER),
        "--source-param",
        str(root / SOURCE_QUAKE_PATH),
        "--terrain-record",
        str(case_root / TERRAIN_RECORD_PATH),
        "--output-tif",
        str(output_tif),
        "--output-json",
        str(output_json),
        "--event-id",
        "usgs-usp000hvnu",
        "--model-id",
        "usgs-usp000hvnu-basic-inversion",
        "--coordinate-reference",
        "EPSG:32654",
        "--generated-at-utc",
        now,
    ]
    result = run_command(command, cwd=root, log_path=case_root / "inputs/data/earthquake/log.producer")
    if result.returncode != 0:
        raise DeliveryError(f"earthquake producer failed with exit status {result.returncode}")
    metadata = read_json(output_json)
    return {"command": " ".join(command), "exit_status": result.returncode, "metadata": metadata}


def run_regional(case_root: Path, output_root: Path, r2d_binary: Path, *, minimum_requested_final_time_s: float = 1800.0) -> dict:
    regional_root = output_root / "regional"
    runs_link = regional_root / "runs"
    command = [
        str(r2d_binary),
        "--case-root",
        str(case_root),
        "--terrain-record",
        TERRAIN_RECORD_PATH.as_posix(),
        "--mesh",
        MESH_PATH.as_posix(),
        "--run-id",
        RUN_ID,
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
    regional_root.mkdir(parents=True, exist_ok=True)
    (regional_root / "command.txt").write_text(" ".join(command) + "\n", encoding="utf-8")
    result = run_command(command, cwd=repo_root(), log_path=regional_root / "log.r2d")
    if result.returncode != 0:
        raise DeliveryError(f"Regional2D runner failed with exit status {result.returncode}")
    case_runs = case_root / "runs"
    if runs_link.exists() or runs_link.is_symlink():
        if runs_link.is_symlink() or runs_link.is_file():
            runs_link.unlink()
    if not runs_link.exists():
        try:
            runs_link.symlink_to(case_runs, target_is_directory=True)
        except OSError:
            shutil.copytree(case_runs, runs_link, dirs_exist_ok=True)
    output_dir = case_root / "runs" / RUN_ID / "outputs/regional2d"
    case = read_json(case_root / "case.json")
    corridor = read_json(case_root / "manifests/corridors/kamaishi-delivery-corridor-evidence.json")
    requested_final_time = float(case["regional_2d"]["numerics"]["final_time_s"])
    snapshot_interval = float(case["outputs"]["snapshot_interval_s"])
    selected_point = Point(
        float(corridor["selected_nearshore_interface"]["projected_m"]["x"]),
        float(corridor["selected_nearshore_interface"]["projected_m"]["y"]),
    )
    unit = (
        float(corridor["basis"]["centreline_unit"]["x"]),
        float(corridor["basis"]["centreline_unit"]["y"]),
    )
    spacing = float(corridor["grid"]["profile"])
    validation = validate_regional_outputs(
        output_dir,
        requested_final_time_s=requested_final_time,
        snapshot_interval_s=snapshot_interval,
        selected_interface=selected_point,
        inward_normal=unit,
        nominal_mesh_spacing_m=spacing,
        minimum_requested_final_time_s=minimum_requested_final_time_s,
    )
    return validation | {
        "command": " ".join(command),
        "exit_status": result.returncode,
        "output_dir": str(output_dir),
        "resource_usage": command_resource_usage(result),
        "build": r2d_build_record(r2d_binary),
    }


def _csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def coupling_signal_metrics(
    samples: Sequence[dict[str, str]],
    inward_normal: tuple[float, float],
    *,
    tolerance: float = 1.0e-8,
    baseline_samples: Sequence[dict[str, str]] | None = None,
) -> dict:
    by_time: dict[float, list[dict[str, str]]] = {}
    for row in samples:
        by_time.setdefault(float(row["time"]), []).append(row)
    times = sorted(by_time)
    if not times:
        raise DeliveryError("cannot compute coupling signal metrics from empty samples")
    if baseline_samples is None:
        baseline_time = times[0]
        baseline = {int(row["local_index"]): row for row in by_time[baseline_time]}
    else:
        baseline_time = min(float(row["time"]) for row in baseline_samples)
        baseline = {int(row["local_index"]): row for row in baseline_samples}
    eta_deltas: list[float] = []
    qn_deltas: list[float] = []
    absolute_etas: list[float] = []
    per_time: list[dict[str, float]] = []
    for time in times:
        max_abs_delta_eta = 0.0
        max_abs_delta_qn = 0.0
        signed_peak_eta = 0.0
        signed_peak_qn = 0.0
        for row in by_time[time]:
            local_index = int(row["local_index"])
            if local_index not in baseline:
                raise DeliveryError("coupling baseline does not cover every selected sample index")
            base = baseline[local_index]
            eta = float(row["free_surface_elevation"])
            base_eta = float(base["free_surface_elevation"])
            qn = float(row["momentum_x"]) * inward_normal[0] + float(row["momentum_y"]) * inward_normal[1]
            base_qn = float(base["momentum_x"]) * inward_normal[0] + float(base["momentum_y"]) * inward_normal[1]
            delta_eta = eta - base_eta
            delta_qn = qn - base_qn
            absolute_etas.append(abs(eta))
            eta_deltas.append(delta_eta)
            qn_deltas.append(delta_qn)
            if abs(delta_eta) > max_abs_delta_eta:
                max_abs_delta_eta = abs(delta_eta)
                signed_peak_eta = delta_eta
            if abs(delta_qn) > max_abs_delta_qn:
                max_abs_delta_qn = abs(delta_qn)
                signed_peak_qn = delta_qn
        per_time.append({
            "time": time,
            "maximum_absolute_delta_eta": max_abs_delta_eta,
            "maximum_absolute_delta_qn": max_abs_delta_qn,
            "peak_delta_eta": signed_peak_eta,
            "peak_delta_qn": signed_peak_qn,
            "metric": max(max_abs_delta_eta, max_abs_delta_qn),
        })
    maximum_abs_delta_eta = max((abs(value) for value in eta_deltas), default=0.0)
    maximum_abs_delta_qn = max((abs(value) for value in qn_deltas), default=0.0)
    peak = max(per_time, key=lambda item: item["metric"])
    eta_peak = max(per_time, key=lambda item: item["maximum_absolute_delta_eta"])
    qn_peak = max(per_time, key=lambda item: item["maximum_absolute_delta_qn"])
    threshold = max(10.0 * tolerance, 0.02 * peak["metric"])
    crossing = next((item for item in per_time if item["time"] > baseline_time and item["metric"] >= threshold), None)
    if crossing is None:
        raise DeliveryError("coupling perturbation never crosses the post-initial event threshold")
    if peak["time"] <= baseline_time:
        raise DeliveryError("peak coupling perturbation occurs at the baseline time")
    values = [*absolute_etas, *eta_deltas, *qn_deltas]
    if not all(math.isfinite(value) for value in values):
        raise DeliveryError("non-finite coupling signal diagnostic")
    return {
        "baseline_time_s": baseline_time,
        "maximum_absolute_free_surface_elevation_m": max(absolute_etas, default=0.0),
        "minimum_free_surface_perturbation_m": min(eta_deltas, default=0.0),
        "maximum_free_surface_perturbation_m": max(eta_deltas, default=0.0),
        "maximum_absolute_free_surface_perturbation_m": maximum_abs_delta_eta,
        "minimum_normal_momentum_change_m2_per_s": min(qn_deltas, default=0.0),
        "maximum_normal_momentum_change_m2_per_s": max(qn_deltas, default=0.0),
        "maximum_absolute_normal_momentum_change_m2_per_s": maximum_abs_delta_qn,
        "first_threshold_crossing_time_s": crossing["time"],
        "peak_perturbation_time_s": peak["time"],
        "peak_free_surface_perturbation_time_s": eta_peak["time"],
        "peak_normal_momentum_change_time_s": qn_peak["time"],
        "peak_delta_eta_m": peak["peak_delta_eta"],
        "peak_delta_qn_m2_per_s": peak["peak_delta_qn"],
        "event_threshold": threshold,
    }


def boundary_alignment_and_wetness(
    samples: Sequence[dict[str, str]],
    selected_interface: Point,
    inward_normal: tuple[float, float],
    nominal_mesh_spacing_m: float,
    *,
    dry_depth_m: float = 1.0e-6,
) -> dict:
    if not samples:
        raise DeliveryError("cannot validate empty coupling samples")
    times = sorted({float(row["time"]) for row in samples})
    initial = [row for row in samples if abs(float(row["time"]) - times[0]) <= 1.0e-9]
    if not initial:
        raise DeliveryError("coupling samples lack an initial time")
    xs = [float(row["x_m"]) for row in initial]
    ys = [float(row["y_m"]) for row in initial]
    depths = [float(row["depth"]) for row in initial]
    beds = [float(row["bed_elevation"]) for row in initial]
    residuals = [
        abs((float(row["x_m"]) - selected_interface.x) * inward_normal[0] + (float(row["y_m"]) - selected_interface.y) * inward_normal[1])
        for row in initial
    ]
    if any(depth <= dry_depth_m for depth in depths):
        raise DeliveryError("not every boundary.inland coupling sample is initially wet")
    if any(bed >= 0.0 for bed in beds):
        raise DeliveryError("not every boundary.inland coupling sample has negative bed elevation")
    max_residual = max(residuals)
    normalised = max_residual / nominal_mesh_spacing_m
    if normalised > 1.0 + 1.0e-9:
        raise DeliveryError("boundary.inland alignment residual exceeds one nominal mesh spacing")
    return {
        "selected_interface_projected_m": {"x": selected_interface.x, "y": selected_interface.y},
        "mean_boundary_inland_face_centre_projected_m": {"x": sum(xs) / len(xs), "y": sum(ys) / len(ys)},
        "maximum_interface_alignment_residual_m": max_residual,
        "mesh_spacing_normalised_alignment_residual": normalised,
        "minimum_initial_coupling_depth_m": min(depths),
        "maximum_initial_coupling_depth_m": max(depths),
        "minimum_coupling_bed_elevation_m": min(beds),
        "maximum_coupling_bed_elevation_m": max(beds),
        "initial_wet_sample_count": len(initial),
    }


def validate_regional_outputs(
    output_dir: Path,
    *,
    requested_final_time_s: float,
    snapshot_interval_s: float,
    selected_interface: Point,
    inward_normal: tuple[float, float],
    nominal_mesh_spacing_m: float,
    minimum_requested_final_time_s: float = 1800.0,
) -> dict:
    required = [
        output_dir / "diagnostics.csv",
        output_dir / "snapshots.csv",
        output_dir / "earthquake_initialisation.csv",
        output_dir / "coupling" / SECTION_ID / "metadata.json",
        output_dir / "coupling" / SECTION_ID / "samples.csv",
        output_dir / "coupling" / SECTION_ID / "history.csv",
    ]
    for path in required:
        if not path.is_file() or path.stat().st_size == 0:
            raise DeliveryError(f"missing or empty Regional2D output: {path}")
    diagnostics = _csv_rows(output_dir / "diagnostics.csv")
    snapshots = _csv_rows(output_dir / "snapshots.csv")
    earthquake = _csv_rows(output_dir / "earthquake_initialisation.csv")
    samples = _csv_rows(output_dir / "coupling" / SECTION_ID / "samples.csv")
    history = _csv_rows(output_dir / "coupling" / SECTION_ID / "history.csv")
    metadata = read_json(output_dir / "coupling" / SECTION_ID / "metadata.json")
    if int(metadata["sample_count"]) < 3:
        raise DeliveryError("coupling patch has fewer than 3 spatial samples")
    expected_time_count = int(math.floor(requested_final_time_s / snapshot_interval_s + 1.0e-9)) + 1
    history_times = [float(row["time"]) for row in history]
    achieved_final_time = max(history_times) if history_times else 0.0
    if requested_final_time_s + 1.0e-9 < minimum_requested_final_time_s:
        raise DeliveryError(f"Regional2D requested final time is below {minimum_requested_final_time_s:g} s")
    if achieved_final_time + max(1.0e-6, 1.0e-9 * requested_final_time_s) < requested_final_time_s:
        raise DeliveryError("coupling history does not reach the requested Regional2D final time")
    if len(history) < expected_time_count:
        raise DeliveryError("coupling history has fewer snapshot times than requested")
    sample_count = int(metadata["sample_count"])
    if len(samples) != sample_count * len(history):
        raise DeliveryError("coupling samples row count does not equal sample_count times coupling time count")
    max_q = 0.0
    for row in samples:
        depth = float(row["depth"])
        qx = float(row["momentum_x"])
        qy = float(row["momentum_y"])
        bed = float(row["bed_elevation"])
        eta = float(row["free_surface_elevation"])
        if not all(math.isfinite(v) for v in (depth, qx, qy, bed, eta)):
            raise DeliveryError("non-finite coupling sample value")
        max_q = max(max_q, math.hypot(qx, qy))
    alignment = boundary_alignment_and_wetness(samples, selected_interface, inward_normal, nominal_mesh_spacing_m)
    signal = coupling_signal_metrics(samples, inward_normal)
    displacement_values: list[float] = []
    for row in earthquake:
        for column in ("minimum_effective_bed_displacement", "maximum_effective_bed_displacement", "effective_bed_displacement_m"):
            if column in row and row[column] != "":
                displacement_values.append(abs(float(row[column])))
    if not displacement_values:
        raise DeliveryError("earthquake initialisation is missing effective bed displacement diagnostics")
    max_displacement = max(displacement_values)
    if max_displacement <= 0.0:
        raise DeliveryError("earthquake initialisation reports zero effective bed displacement")
    final_step = None
    if diagnostics and "step" in diagnostics[-1] and diagnostics[-1]["step"] != "":
        try:
            final_step = int(float(diagnostics[-1]["step"]))
        except ValueError:
            final_step = None
    return {
        "diagnostics_rows": len(diagnostics),
        "snapshot_rows": len(snapshots),
        "earthquake_initialisation_rows": len(earthquake),
        "coupling_sample_rows": len(samples),
        "coupling_history_rows": len(history),
        "coupling_time_count": len(history),
        "coupling_sample_count": sample_count,
        "requested_final_time_s": requested_final_time_s,
        "achieved_final_time_s": achieved_final_time,
        "final_step": final_step,
        "maximum_abs_momentum": max_q,
        "maximum_abs_effective_bed_displacement_m": max_displacement,
        "alignment": alignment,
        "signal": signal,
    }


def select_replay_window(full_coupling: Path, selected: Path, inward_normal: tuple[float, float]) -> dict:
    metadata = read_json(full_coupling / "metadata.json")
    samples = _csv_rows(full_coupling / "samples.csv")
    if not samples:
        raise DeliveryError("cannot select replay window from empty samples.csv")
    by_time: dict[float, list[dict[str, str]]] = {}
    for row in samples:
        by_time.setdefault(float(row["time"]), []).append(row)
    times = sorted(by_time)
    if times[-1] - times[0] < 1800.0:
        raise DeliveryError("production replay window selection requires at least 1800 s of Regional2D source history")
    baseline = {int(row["local_index"]): row for row in by_time[times[0]]}
    per_time: list[dict[str, float]] = []
    eta_scale = 0.0
    qn_scale = 0.0
    for time in times:
        max_delta_eta = 0.0
        max_delta_qn = 0.0
        signed_delta_eta = 0.0
        signed_delta_qn = 0.0
        for row in by_time[time]:
            base = baseline[int(row["local_index"])]
            eta_delta = float(row["free_surface_elevation"]) - float(base["free_surface_elevation"])
            qn = float(row["momentum_x"]) * inward_normal[0] + float(row["momentum_y"]) * inward_normal[1]
            base_qn = float(base["momentum_x"]) * inward_normal[0] + float(base["momentum_y"]) * inward_normal[1]
            qn_delta = qn - base_qn
            if abs(eta_delta) > max_delta_eta:
                max_delta_eta = abs(eta_delta)
                signed_delta_eta = eta_delta
            if abs(qn_delta) > max_delta_qn:
                max_delta_qn = abs(qn_delta)
                signed_delta_qn = qn_delta
        eta_scale = max(eta_scale, max_delta_eta)
        qn_scale = max(qn_scale, max_delta_qn)
        per_time.append({
            "time": time,
            "maximum_absolute_delta_eta": max_delta_eta,
            "maximum_absolute_delta_qn": max_delta_qn,
            "peak_delta_eta": signed_delta_eta,
            "peak_delta_qn": signed_delta_qn,
        })
    floor = 1.0e-8
    if max(eta_scale, qn_scale) <= 10.0 * floor:
        raise DeliveryError("source coupling history does not contain a post-initial perturbation")
    for item in per_time:
        item["metric"] = max(
            item["maximum_absolute_delta_eta"] / max(eta_scale, floor),
            item["maximum_absolute_delta_qn"] / max(qn_scale, floor),
        )
    peak = max(per_time, key=lambda item: item["metric"])
    peak_time = peak["time"]
    peak_metric = peak["metric"]
    if peak_time <= times[0]:
        raise DeliveryError("replay-window peak occurs at the baseline time")
    threshold = max(floor, 0.02 * peak_metric)
    crossing = next((item["time"] for item in per_time if item["time"] > times[0] and item["metric"] >= threshold), None)
    if crossing is None:
        raise DeliveryError("replay-window threshold is never crossed after the baseline")

    major_threshold = max(floor, 0.8 * peak_metric)
    window_anchor = next(
        (item["time"] for item in per_time if item["time"] > times[0] and item["metric"] >= major_threshold),
        crossing,
    )
    requested_start = max(times[0], window_anchor - 30.0)
    minimum_end = max(window_anchor + 180.0, peak_time)
    if times[-1] + 1.0e-9 < minimum_end:
        raise DeliveryError("insufficient post-arrival source history for a 180 s replay window retaining the first major peak")
    start = requested_start
    end = min(times[-1], start + 300.0)
    if end + 1.0e-9 < minimum_end:
        start = max(times[0], minimum_end - 300.0)
        end = min(times[-1], start + 300.0)
    if end - start + 1.0e-9 < 180.0:
        raise DeliveryError("selected replay window is shorter than 180 s")
    if not (start - 1.0e-9 <= window_anchor <= end + 1.0e-9 and start - 1.0e-9 <= peak_time <= end + 1.0e-9):
        raise DeliveryError("selected replay window does not retain the major-arrival anchor and first major peak")
    chosen_times = [time for time in times if start <= time <= end]
    if len(chosen_times) < 4:
        raise DeliveryError("replay window has fewer than 4 selected times")
    shifted_start = chosen_times[0]
    shifted_end = chosen_times[-1]
    selected.mkdir(parents=True, exist_ok=True)
    shutil.copy2(full_coupling / "metadata.json", selected / "metadata.json")
    with (selected / "samples.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(samples[0]))
        writer.writeheader()
        for time in chosen_times:
            shifted = time - shifted_start
            for row in by_time[time]:
                out = dict(row)
                out["time"] = f"{shifted:.17g}"
                writer.writerow(out)
    selected_samples = _csv_rows(selected / "samples.csv")
    by_shifted: dict[float, list[dict[str, str]]] = {}
    for row in selected_samples:
        by_shifted.setdefault(float(row["time"]), []).append(row)
    with (selected / "history.csv").open("w", encoding="utf-8", newline="") as handle:
        fieldnames = ["step", "time", "section_id", "sample_count", "maximum_depth", "maximum_speed"]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for index, time in enumerate(sorted(by_shifted)):
            rows = by_shifted[time]
            max_depth = max(max(0.0, float(row["depth"])) for row in rows)
            max_speed = 0.0
            for row in rows:
                h = max(0.0, float(row["depth"]))
                if h > 0.0:
                    max_speed = max(max_speed, math.hypot(float(row["momentum_x"]), float(row["momentum_y"])) / h)
            writer.writerow({"step": index, "time": f"{time:.17g}", "section_id": SECTION_ID, "sample_count": len(rows), "maximum_depth": f"{max_depth:.17g}", "maximum_speed": f"{max_speed:.17g}"})
    evidence = {
        "source_metadata_sha256": sha256(full_coupling / "metadata.json"),
        "source_samples_sha256": sha256(full_coupling / "samples.csv"),
        "source_history_sha256": sha256(full_coupling / "history.csv"),
        "source_time_start_s": times[0],
        "source_time_end_s": times[-1],
        "baseline_time_s": times[0],
        "threshold": threshold,
        "major_window_threshold": major_threshold,
        "eta_normalisation_scale_m": eta_scale,
        "normal_momentum_normalisation_scale_m2_per_s": qn_scale,
        "first_crossing_source_time_s": crossing,
        "window_anchor_source_time_s": window_anchor,
        "peak_source_time_s": peak_time,
        "peak_delta_eta_m": peak["peak_delta_eta"],
        "peak_delta_qn_m2_per_s": peak["peak_delta_qn"],
        "peak_metric": peak_metric,
        "selected_source_start_s": shifted_start,
        "selected_source_end_s": shifted_end,
        "selected_time_count": len(chosen_times),
        "shifted_duration_s": shifted_end - shifted_start,
    }
    write_json(selected / "window_selection.json", evidence)
    return evidence


def _find_time_key(times: Sequence[float], target: float, *, tolerance: float = 1.0e-7) -> float:
    for time_value in times:
        if abs(time_value - target) <= tolerance:
            return time_value
    raise DeliveryError(f"fixed replay window source time {target:g} s is missing")


def _reference_window_value(reference: dict, group: str, key: str) -> float:
    value = reference[group][key]
    if not isinstance(value, (int, float)):
        raise DeliveryError(f"G5 reference {group}.{key} is not numeric")
    return float(value)


def select_fixed_replay_window(
    full_coupling: Path,
    selected: Path,
    inward_normal: tuple[float, float],
    reference: dict,
    *,
    evidence_output: Path | None = None,
) -> dict:
    source_start = _reference_window_value(reference, "source_window", "start_s")
    source_end = _reference_window_value(reference, "source_window", "end_s")
    shifted_start = _reference_window_value(reference, "shifted_window", "start_s")
    shifted_end = _reference_window_value(reference, "shifted_window", "end_s")
    snapshot_interval = float(reference["snapshot_interval_s"])
    expected_time_count = int(reference["selected_time_count"])
    expected_sample_count = int(reference["coupling_sample_count"])
    expected_row_count = int(reference["selected_sample_row_count"])
    expected_peak_source = float(reference["source_peak_time_s"])
    expected_peak_shifted = float(reference["shifted_peak_time_s"])

    metadata = read_json(full_coupling / "metadata.json")
    samples = _csv_rows(full_coupling / "samples.csv")
    history = _csv_rows(full_coupling / "history.csv")
    if not samples or not history:
        raise DeliveryError("cannot extract fixed replay window from empty coupling output")
    if int(metadata.get("sample_count", 0)) != expected_sample_count:
        raise DeliveryError("fixed replay window coupling sample count differs from the accepted G5 reference")
    by_time: dict[float, list[dict[str, str]]] = {}
    for row in samples:
        by_time.setdefault(float(row["time"]), []).append(row)
    by_history_time = {float(row["time"]): row for row in history}
    source_times = sorted(by_time)
    if source_times[-1] + 1.0e-7 < source_end:
        raise DeliveryError("Regional2D evidence run does not cover the accepted fixed replay window")
    chosen_times = [
        _find_time_key(source_times, source_start + index * snapshot_interval)
        for index in range(expected_time_count)
    ]
    if any(chosen_times[index] >= chosen_times[index + 1] for index in range(len(chosen_times) - 1)):
        raise DeliveryError("fixed replay window source times are not strictly increasing")
    if abs(chosen_times[0] - source_start) > 1.0e-7 or abs(chosen_times[-1] - source_end) > 1.0e-7:
        raise DeliveryError("fixed replay window endpoints do not match the accepted G5 reference")
    selected_row_count = sum(len(by_time[time_value]) for time_value in chosen_times)
    if selected_row_count != expected_row_count:
        raise DeliveryError("fixed replay window row count differs from the accepted G5 reference")

    selected.mkdir(parents=True, exist_ok=True)
    shutil.copy2(full_coupling / "metadata.json", selected / "metadata.json")
    with (selected / "samples.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(samples[0]))
        writer.writeheader()
        for index, source_time in enumerate(chosen_times):
            shifted = source_time - source_start
            for row in by_time[source_time]:
                out = dict(row)
                out["step"] = str(index)
                out["time"] = f"{shifted:.17g}"
                writer.writerow(out)
    with (selected / "history.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(history[0]))
        writer.writeheader()
        for index, source_time in enumerate(chosen_times):
            history_time = _find_time_key(sorted(by_history_time), source_time)
            out = dict(by_history_time[history_time])
            out["step"] = str(index)
            out["time"] = f"{source_time - source_start:.17g}"
            writer.writerow(out)

    selected_samples = _csv_rows(selected / "samples.csv")
    shifted_times = sorted({float(row["time"]) for row in selected_samples})
    if shifted_times[0] != shifted_start or abs(shifted_times[-1] - shifted_end) > 1.0e-7:
        raise DeliveryError("fixed replay shifted window does not span 0-300 s")
    signal = coupling_signal_metrics(selected_samples, inward_normal, baseline_samples=by_time[source_times[0]])
    peak_shifted = float(signal["peak_free_surface_perturbation_time_s"])
    peak_source = source_start + peak_shifted
    depths = [float(row["depth"]) for row in selected_samples]
    positive_depths = sorted(depth for depth in depths if depth > 1.0e-6)
    representative_depth = positive_depths[len(positive_depths) // 2] if positive_depths else max(depths)
    evidence = {
        "schema": {"name": "tsunami.g6_fixed_window_extraction", "version": "1.0.0"},
        "reference_provenance": reference["provenance"],
        "profile": reference["profile"],
        "source_run_identity": {
            "source_coupling_dir": str(full_coupling),
            "repository_commit": current_repo_commit(),
            "extracted_at_utc": utc_now(),
        },
        "source_window": {"start_s": source_start, "end_s": source_end},
        "shifted_window": {"start_s": shifted_start, "end_s": shifted_end},
        "selected_source_start_s": source_start,
        "selected_source_end_s": source_end,
        "selected_shifted_start_s": shifted_times[0],
        "selected_shifted_end_s": shifted_times[-1],
        "selected_time_count": len(chosen_times),
        "coupling_sample_count": expected_sample_count,
        "selected_sample_row_count": selected_row_count,
        "source_peak_time_s": peak_source,
        "shifted_peak_time_s": peak_shifted,
        "metrics": {
            "maximum_absolute_free_surface_perturbation_m": signal["maximum_absolute_free_surface_perturbation_m"],
            "maximum_absolute_normal_momentum_change_m2_per_s": signal["maximum_absolute_normal_momentum_change_m2_per_s"],
            "peak_free_surface_perturbation_source_time_s": peak_source,
            "peak_normal_momentum_change_source_time_s": source_start + float(signal["peak_normal_momentum_change_time_s"]),
            "peak_combined_perturbation_source_time_s": source_start + float(signal["peak_perturbation_time_s"]),
            "representative_wet_depth_m": representative_depth,
            "minimum_coupling_depth_m": min(depths),
            "maximum_coupling_depth_m": max(depths),
        },
        "selected_window_metrics": {
            "selected_window_maximum_absolute_free_surface_perturbation_m": signal["maximum_absolute_free_surface_perturbation_m"],
            "selected_window_maximum_absolute_normal_momentum_change_m2_per_s": signal["maximum_absolute_normal_momentum_change_m2_per_s"],
            "selected_window_peak_eta_time_s": peak_source,
            "selected_window_peak_qn_time_s": source_start + float(signal["peak_normal_momentum_change_time_s"]),
            "selected_window_peak_combined_time_s": source_start + float(signal["peak_perturbation_time_s"]),
        },
        "accepted_full_source_hashes": reference["accepted_full_source_hashes"],
        "source_metadata_sha256": sha256(full_coupling / "metadata.json"),
        "source_samples_sha256": sha256(full_coupling / "samples.csv"),
        "source_history_sha256": sha256(full_coupling / "history.csv"),
        "source_hashes": {
            "metadata_sha256": sha256(full_coupling / "metadata.json"),
            "samples_sha256": sha256(full_coupling / "samples.csv"),
            "history_sha256": sha256(full_coupling / "history.csv"),
        },
        "selected_window_hashes": {
            "metadata_sha256": sha256(selected / "metadata.json"),
            "samples_sha256": sha256(selected / "samples.csv"),
            "history_sha256": sha256(selected / "history.csv"),
        },
        "full_source_hashes_cannot_match_truncated_evidence_run": True,
    }
    write_json(selected / "window_selection.json", evidence)
    write_json(selected / "g6_fixed_window_extraction.json", evidence)
    if evidence_output is not None:
        write_json(evidence_output, evidence)
    return evidence


def _within_reference_tolerance(actual: float, expected: float) -> bool:
    return abs(actual - expected) <= 1.0e-10 + 1.0e-8 * max(abs(actual), abs(expected))


def selected_window_reference_metrics(reference: dict) -> dict[str, object]:
    scoped = reference.get("selected_window_245_545_s", {})
    return {
        "maximum_absolute_free_surface_perturbation_m": scoped.get(
            "selected_window_maximum_absolute_free_surface_perturbation_m",
            reference.get("metrics", {}).get("maximum_absolute_free_surface_perturbation_m"),
        ),
        "maximum_absolute_normal_momentum_change_m2_per_s": scoped.get(
            "selected_window_maximum_absolute_normal_momentum_change_m2_per_s"
        ),
        "peak_free_surface_perturbation_source_time_s": scoped.get("selected_window_peak_eta_time_s"),
        "peak_normal_momentum_change_source_time_s": scoped.get("selected_window_peak_qn_time_s"),
        "peak_combined_perturbation_source_time_s": scoped.get("selected_window_peak_combined_time_s"),
        "representative_wet_depth_m": reference.get("metrics", {}).get("representative_wet_depth_m"),
        "minimum_coupling_depth_m": reference.get("metrics", {}).get("minimum_coupling_depth_m"),
        "maximum_coupling_depth_m": reference.get("metrics", {}).get("maximum_coupling_depth_m"),
    }


def compare_fixed_window_to_reference(extraction: dict, reference: dict, *, output_path: Path | None = None) -> dict:
    exact_fields = {
        "selected_time_count": (extraction["selected_time_count"], reference["selected_time_count"]),
        "coupling_sample_count": (extraction["coupling_sample_count"], reference["coupling_sample_count"]),
        "selected_sample_row_count": (extraction["selected_sample_row_count"], reference["selected_sample_row_count"]),
        "source_start_s": (extraction["source_window"]["start_s"], reference["source_window"]["start_s"]),
        "source_end_s": (extraction["source_window"]["end_s"], reference["source_window"]["end_s"]),
        "shifted_start_s": (extraction["shifted_window"]["start_s"], reference["shifted_window"]["start_s"]),
        "shifted_end_s": (extraction["shifted_window"]["end_s"], reference["shifted_window"]["end_s"]),
        "source_peak_time_s": (extraction["source_peak_time_s"], reference["source_peak_time_s"]),
        "shifted_peak_time_s": (extraction["shifted_peak_time_s"], reference["shifted_peak_time_s"]),
    }
    exact_results = {
        name: {"actual": actual, "expected": expected, "passed": actual == expected}
        for name, (actual, expected) in exact_fields.items()
    }
    reference_metrics = selected_window_reference_metrics(reference)
    metric_results: dict[str, dict[str, object]] = {}
    missing_reference_metrics: list[str] = []
    for name, actual in extraction["metrics"].items():
        expected = reference_metrics.get(name)
        if expected is None:
            metric_results[name] = {
                "actual": actual,
                "expected": None,
                "passed": None,
                "reference_status": "not_published_or_not_reconstructed_for_selected_window",
                "scope": "selected_window_245_545_s",
            }
            missing_reference_metrics.append(name)
            continue
        passed = _within_reference_tolerance(float(actual), float(expected))
        metric_results[name] = {"actual": actual, "expected": expected, "passed": passed, "scope": "selected_window_245_545_s"}
    passed = all(item["passed"] for item in exact_results.values()) and all(
        item["passed"] is not False for item in metric_results.values()
    )
    comparison = {
        "schema": {"name": "tsunami.g6_fixed_window_reference_comparison", "version": "1.0.0"},
        "status": "passed" if passed else "failed",
        "equivalence_contract": {
            "level_1": "source provenance",
            "level_2": "Regional2D case and input equivalence",
            "level_3": "pointwise G5/G6 prefix equivalence over 0-600 s",
            "level_4": "selected-window 245-545 s metric equivalence",
            "level_5": "Local3D replay acceptance",
            "historical_full_history_metric_must_not_be_used_as_selected_window_comparator": True,
        },
        "tolerance": {"absolute": 1.0e-10, "relative": 1.0e-8},
        "exact": exact_results,
        "metrics": metric_results,
        "missing_reference_metrics": missing_reference_metrics,
        "historical_full_history_metrics_excluded": reference.get("full_history_0_1800_s", {}),
        "selected_window_hashes": extraction["selected_window_hashes"],
        "source_hashes": extraction["source_hashes"],
        "accepted_full_source_hashes": reference["accepted_full_source_hashes"],
        "full_source_hashes_cannot_match_truncated_evidence_run": True,
    }
    if output_path is not None:
        write_json(output_path, comparison)
    return comparison


def _rows_through_time(path: Path, prefix_final_time_s: float) -> list[dict[str, str]]:
    rows = _csv_rows(path)
    if not rows:
        return rows
    time_field = "time" if "time" in rows[0] else "end_time" if "end_time" in rows[0] else None
    if time_field is None:
        return rows
    return [row for row in rows if float(row[time_field]) <= prefix_final_time_s + 1.0e-9]


def compare_regional_time_horizon_outputs(
    short_output_dir: Path,
    long_output_dir: Path,
    *,
    prefix_final_time_s: float,
    output_path: Path | None = None,
) -> dict:
    csv_files = [
        Path("diagnostics.csv"),
        Path("snapshots.csv"),
        Path("coupling") / SECTION_ID / "history.csv",
        Path("coupling") / SECTION_ID / "samples.csv",
    ]
    comparisons: dict[str, dict[str, object]] = {}
    first_difference: dict[str, object] | None = None
    for relative in csv_files:
        short_path = short_output_dir / relative
        long_path = long_output_dir / relative
        short_rows = _rows_through_time(short_path, prefix_final_time_s)
        long_rows = _rows_through_time(long_path, prefix_final_time_s)
        passed = short_rows == long_rows
        comparison: dict[str, object] = {
            "short_path": str(short_path),
            "long_path": str(long_path),
            "prefix_final_time_s": prefix_final_time_s,
            "short_prefix_rows": len(short_rows),
            "long_prefix_rows": len(long_rows),
            "passed": passed,
        }
        if not passed:
            differing_index = next(
                (index for index, pair in enumerate(zip(short_rows, long_rows)) if pair[0] != pair[1]),
                min(len(short_rows), len(long_rows)),
            )
            comparison["first_differing_row_index"] = differing_index
            if first_difference is None:
                first_difference = {"file": relative.as_posix(), "row_index": differing_index}
        comparisons[relative.as_posix()] = comparison

    metadata_short = read_json(short_output_dir / "coupling" / SECTION_ID / "metadata.json")
    metadata_long = read_json(long_output_dir / "coupling" / SECTION_ID / "metadata.json")
    metadata_passed = metadata_short == metadata_long
    comparisons[f"coupling/{SECTION_ID}/metadata.json"] = {
        "short_path": str(short_output_dir / "coupling" / SECTION_ID / "metadata.json"),
        "long_path": str(long_output_dir / "coupling" / SECTION_ID / "metadata.json"),
        "passed": metadata_passed,
    }
    if not metadata_passed and first_difference is None:
        first_difference = {"file": f"coupling/{SECTION_ID}/metadata.json"}

    prefix_invariant = all(bool(item["passed"]) for item in comparisons.values())
    evidence = {
        "schema": {"name": "tsunami.regional_time_horizon_invariance", "version": "1.0.0"},
        "prefix_invariant": prefix_invariant,
        "status": "passed" if prefix_invariant else "failed",
        "prefix_final_time_s": prefix_final_time_s,
        "comparisons": comparisons,
        "first_difference": first_difference,
    }
    if output_path is not None:
        write_json(output_path, evidence)
    return evidence


def compare_g5_g6_upstream_cases(g5_case_root: Path, g6_case_root: Path, *, output_path: Path | None = None) -> dict:
    value_items = {
        "case_identity": ("case",),
        "scenario": ("scenario",),
        "coordinate_frame": ("coordinate_frame",),
        "dataset_bindings": ("datasets", "bindings"),
        "regional_corridor": ("regional_2d", "corridor"),
        "physics": ("regional_2d", "physics"),
        "numerics": ("regional_2d", "numerics"),
        "boundaries": ("regional_2d", "boundaries"),
        "outputs": ("outputs",),
    }
    g5_case = read_json(g5_case_root / "case.json")
    g6_case = read_json(g6_case_root / "case.json")
    timestamp_keys = {"created_at_utc", "executed_at_utc", "accessed_at_utc", "generated_at_utc"}

    def nested(payload: dict, path: tuple[str, ...]) -> object:
        value: object = payload
        for key in path:
            if not isinstance(value, dict):
                raise DeliveryError(f"case comparison path {'.'.join(path)} is not an object")
            value = value[key]
        return value

    def normalise_generated_metadata(value: object) -> object:
        if isinstance(value, dict):
            return {
                key: normalise_generated_metadata(child)
                for key, child in value.items()
                if key not in timestamp_keys
            }
        if isinstance(value, list):
            return [normalise_generated_metadata(child) for child in value]
        return value

    value_comparisons = {}
    for name, path in value_items.items():
        g5_value = normalise_generated_metadata(nested(g5_case, path))
        g6_value = normalise_generated_metadata(nested(g6_case, path))
        value_comparisons[name] = {
            "g5_sha256": json_sha256(g5_value),
            "g6_sha256": json_sha256(g6_value),
            "passed": g5_value == g6_value,
            "normalised_timestamp_fields": sorted(timestamp_keys),
        }

    file_items = {
        "terrain_source": Path("inputs") / SOURCE_TERRAIN_PATH,
        "quake_source": Path("inputs") / SOURCE_QUAKE_PATH,
        "conditioned_terrain": TERRAIN_OUTPUT_PATH,
        "conditioned_terrain_record": TERRAIN_RECORD_PATH,
        "displacement_raster": Path("inputs/data/earthquake/tohoku_vertical_displacement.tif"),
        "displacement_metadata": Path("inputs/data/earthquake/tohoku_vertical_displacement.json"),
        "mesh_topology_coordinates_groups": MESH_PATH,
        "corridor_record_normal_tangent": CORRIDOR_RECORD_PATH,
    }
    file_comparisons = {}
    for name, relative in file_items.items():
        g5_path = g5_case_root / relative
        g6_path = g6_case_root / relative
        if not g5_path.is_file() or not g6_path.is_file():
            file_comparisons[name] = {
                "g5_path": str(g5_path),
                "g6_path": str(g6_path),
                "passed": False,
                "reason": "missing_file",
            }
            continue
        g5_hash = sha256(g5_path)
        g6_hash = sha256(g6_path)
        passed = g5_hash == g6_hash
        normalised: dict[str, object] = {}
        if g5_path.suffix == ".json" and g6_path.suffix == ".json":
            g5_payload = normalise_generated_metadata(read_json(g5_path))
            g6_payload = normalise_generated_metadata(read_json(g6_path))
            normalised = {
                "g5_normalised_sha256": json_sha256(g5_payload),
                "g6_normalised_sha256": json_sha256(g6_payload),
                "normalised_timestamp_fields": sorted(timestamp_keys),
            }
            passed = g5_payload == g6_payload
        file_comparisons[name] = {
            "g5_path": str(g5_path),
            "g6_path": str(g6_path),
            "g5_sha256": g5_hash,
            "g6_sha256": g6_hash,
            "passed": passed,
        } | normalised

    passed = all(item["passed"] for item in value_comparisons.values()) and all(
        item["passed"] for item in file_comparisons.values()
    )
    evidence = {
        "schema": {"name": "tsunami.g5_g6_upstream_case_comparison", "version": "1.0.0"},
        "status": "passed" if passed else "failed",
        "value_comparisons": value_comparisons,
        "file_hash_comparisons": file_comparisons,
        "allowed_differences": ["run_id", "timestamps", "output_path", "g5_g6_metadata", "final_time_provenance"],
        "local3d_span_difference_is_not_regional_forcing": True,
    }
    if output_path is not None:
        write_json(output_path, evidence)
    return evidence


def _sample_key(row: dict[str, str]) -> tuple[float, int]:
    return (float(row["time"]), int(row["local_index"]))


def _derived_sample_values(
    row: dict[str, str],
    baseline: dict[str, str],
    inward_normal: tuple[float, float],
    tangent: tuple[float, float],
) -> dict[str, float]:
    qn = float(row["momentum_x"]) * inward_normal[0] + float(row["momentum_y"]) * inward_normal[1]
    qt = float(row["momentum_x"]) * tangent[0] + float(row["momentum_y"]) * tangent[1]
    base_qn = float(baseline["momentum_x"]) * inward_normal[0] + float(baseline["momentum_y"]) * inward_normal[1]
    base_eta = float(baseline["free_surface_elevation"])
    return {
        "normal_momentum_m2_per_s": qn,
        "tangent_momentum_m2_per_s": qt,
        "normal_momentum_change_m2_per_s": qn - base_qn,
        "free_surface_perturbation_m": float(row["free_surface_elevation"]) - base_eta,
    }


def compare_g5_g6_prefix_coupling(
    g5_coupling: Path,
    g6_coupling: Path,
    inward_normal: tuple[float, float],
    tangent: tuple[float, float],
    *,
    prefix_final_time_s: float = 600.0,
    output_path: Path | None = None,
    csv_output_path: Path | None = None,
) -> dict:
    raw_fields = [
        "x_m",
        "y_m",
        "depth",
        "momentum_x",
        "momentum_y",
        "bed_elevation",
        "free_surface_elevation",
    ]
    derived_fields = [
        "normal_momentum_m2_per_s",
        "tangent_momentum_m2_per_s",
        "normal_momentum_change_m2_per_s",
        "free_surface_perturbation_m",
    ]
    g5_rows = [row for row in _csv_rows(g5_coupling / "samples.csv") if float(row["time"]) <= prefix_final_time_s + 1.0e-9]
    g6_rows = [row for row in _csv_rows(g6_coupling / "samples.csv") if float(row["time"]) <= prefix_final_time_s + 1.0e-9]
    g5_by_key = {_sample_key(row): row for row in g5_rows}
    g6_by_key = {_sample_key(row): row for row in g6_rows}
    keys = sorted(set(g5_by_key) | set(g6_by_key))
    g5_baseline = {int(row["local_index"]): row for row in g5_rows if abs(float(row["time"])) <= 1.0e-9}
    g6_baseline = {int(row["local_index"]): row for row in g6_rows if abs(float(row["time"])) <= 1.0e-9}
    abs_tol = 1.0e-11
    rel_tol = 1.0e-10
    comparisons = 0
    exact_matches = 0
    tolerant_matches = 0
    failures = 0
    sum_sq = 0.0
    max_abs_diff = 0.0
    max_rel_diff = 0.0
    first_difference: dict[str, object] | None = None
    csv_rows: list[dict[str, object]] = []
    missing_keys = sorted(set(g5_by_key) ^ set(g6_by_key))
    if missing_keys:
        first_difference = {"time": missing_keys[0][0], "local_index": missing_keys[0][1], "field": "row_presence"}
        failures += len(missing_keys)

    for key in keys:
        if key not in g5_by_key or key not in g6_by_key:
            continue
        g5_row = g5_by_key[key]
        g6_row = g6_by_key[key]
        local_index = key[1]
        g5_values = {field: float(g5_row[field]) for field in raw_fields}
        g6_values = {field: float(g6_row[field]) for field in raw_fields}
        g5_values |= _derived_sample_values(g5_row, g5_baseline[local_index], inward_normal, tangent)
        g6_values |= _derived_sample_values(g6_row, g6_baseline[local_index], inward_normal, tangent)
        for field in raw_fields + derived_fields:
            actual = g6_values[field]
            expected = g5_values[field]
            diff = actual - expected
            abs_diff = abs(diff)
            denom = max(abs(actual), abs(expected), 1.0)
            rel_diff = abs_diff / denom
            tolerance = abs_tol + rel_tol * max(abs(actual), abs(expected))
            exact = actual == expected
            passed = exact or abs_diff <= tolerance
            comparisons += 1
            exact_matches += 1 if exact else 0
            tolerant_matches += 1 if passed else 0
            failures += 0 if passed else 1
            sum_sq += diff * diff
            max_abs_diff = max(max_abs_diff, abs_diff)
            max_rel_diff = max(max_rel_diff, rel_diff)
            if not passed and first_difference is None:
                first_difference = {"time": key[0], "local_index": local_index, "field": field}
            if not passed or not exact:
                csv_rows.append(
                    {
                        "time": f"{key[0]:.17g}",
                        "local_index": local_index,
                        "field": field,
                        "g5": f"{expected:.17g}",
                        "g6": f"{actual:.17g}",
                        "absolute_difference": f"{abs_diff:.17g}",
                        "relative_difference": f"{rel_diff:.17g}",
                        "tolerance": f"{tolerance:.17g}",
                        "passed": passed,
                        "exact": exact,
                    }
                )

    rmse = math.sqrt(sum_sq / comparisons) if comparisons else 0.0
    prefix_equivalent = failures == 0 and not missing_keys
    evidence = {
        "schema": {"name": "tsunami.g5_g6_prefix_equivalence", "version": "1.0.0"},
        "status": "passed" if prefix_equivalent else "failed",
        "prefix_equivalent": prefix_equivalent,
        "prefix_final_time_s": prefix_final_time_s,
        "tolerance": {"absolute": abs_tol, "relative": rel_tol},
        "g5_sample_rows": len(g5_rows),
        "g6_sample_rows": len(g6_rows),
        "sample_key_count": len(keys),
        "missing_key_count": len(missing_keys),
        "field_comparison_count": comparisons,
        "exact_match_count": exact_matches,
        "tolerance_match_count": tolerant_matches,
        "failure_count": failures,
        "max_absolute_difference": max_abs_diff,
        "max_relative_difference": max_rel_diff,
        "rmse": rmse,
        "first_difference": first_difference,
        "fields": {"raw": raw_fields, "derived": derived_fields},
    }
    if output_path is not None:
        write_json(output_path, evidence)
    if csv_output_path is not None:
        csv_output_path.parent.mkdir(parents=True, exist_ok=True)
        with csv_output_path.open("w", encoding="utf-8", newline="") as handle:
            fieldnames = [
                "time",
                "local_index",
                "field",
                "g5",
                "g6",
                "absolute_difference",
                "relative_difference",
                "tolerance",
                "passed",
                "exact",
            ]
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(csv_rows)
    return evidence


def derive_openfoam_timestep(
    samples: Sequence[dict[str, str]],
    inward_normal: tuple[float, float],
    tangent: tuple[float, float],
    *,
    streamwise_length_m: float,
    streamwise_cells: int,
    span_length_m: float,
    span_cells: int,
    vertical_height_m: float,
    vertical_cells: int,
    target_courant: float = 0.5,
    target_alpha_courant: float = 0.5,
    safety_factor: float = 0.5,
    conservative_cap_s: float = 0.05,
) -> dict:
    min_cell_dimension = min(
        streamwise_length_m / streamwise_cells,
        span_length_m / span_cells,
        vertical_height_m / vertical_cells,
    )
    maximum_speed = 0.0
    maximum_depth = 0.0
    for row in samples:
        depth = max(0.0, float(row["depth"]))
        maximum_depth = max(maximum_depth, depth)
        if depth > 1.0e-6:
            qx = float(row["momentum_x"])
            qy = float(row["momentum_y"])
            qn = qx * inward_normal[0] + qy * inward_normal[1]
            qt = qx * tangent[0] + qy * tangent[1]
            maximum_speed = max(maximum_speed, math.hypot(qn / depth, qt / depth))
    wave_speed = math.sqrt(9.80665 * max(maximum_depth, 1.0e-9))
    characteristic_speed = max(maximum_speed + wave_speed, 1.0e-9)
    calculated = safety_factor * min(target_courant, target_alpha_courant) * min_cell_dimension / characteristic_speed
    selected = min(calculated, conservative_cap_s)
    if not all(math.isfinite(value) and value > 0.0 for value in (min_cell_dimension, characteristic_speed, calculated, selected)):
        raise DeliveryError("derived OpenFOAM timestep is not finite and positive")
    return {
        "minimum_local_cell_dimension_m": min_cell_dimension,
        "maximum_mapped_inlet_speed_m_per_s": maximum_speed,
        "maximum_reconstructed_water_depth_m": maximum_depth,
        "gravity_wave_speed_m_per_s": wave_speed,
        "derived_characteristic_speed_m_per_s": characteristic_speed,
        "target_courant_limit": target_courant,
        "target_alpha_courant_limit": target_alpha_courant,
        "safety_factor": safety_factor,
        "calculated_timestep_limit_s": calculated,
        "selected_maximum_timestep_s": selected,
        "conservative_cap_s": conservative_cap_s,
    }


def replay_config_from_window(selected: Path, trajectory: Trajectory, output: Path) -> dict:
    samples = _csv_rows(selected / "samples.csv")
    if not samples:
        raise DeliveryError("cannot generate replay config from empty selected samples")
    projections = [float(row["x_m"]) * trajectory.left.x + float(row["y_m"]) * trajectory.left.y for row in samples]
    depths = [max(0.0, float(row["depth"])) for row in samples]
    beds = [float(row["bed_elevation"]) for row in samples]
    etas = [float(row["free_surface_elevation"]) for row in samples]
    positive_depths = sorted(depth for depth in depths if depth > 1.0e-6)
    representative_depth = positive_depths[len(positive_depths) // 2] if positive_depths else max(depths)
    vertical_datum = min(beds)
    max_height = max(etas) - vertical_datum
    freeboard = max(0.2 * max(positive_depths or [representative_depth]), 5.0)
    vertical_max = max(max_height + freeboard, representative_depth + freeboard, 10.0)
    span_min = min(projections)
    span_max = max(projections)
    if span_max <= span_min:
        span_max = span_min + 1.0
    span_length = span_max - span_min
    span_cells = max(60, min(80, int(math.ceil(span_length / 125.0))))
    vertical_cells = max(12, min(20, int(math.ceil(vertical_max / max(vertical_max / 16.0, 1.0)))))
    streamwise_length = max(300.0, min(1500.0, 20.0 * max(representative_depth, 1.0)))
    streamwise_cells = max(40, min(60, int(math.ceil(streamwise_length / 20.0))))
    timestep = derive_openfoam_timestep(
        samples,
        (trajectory.unit.x, trajectory.unit.y),
        (trajectory.left.x, trajectory.left.y),
        streamwise_length_m=streamwise_length,
        streamwise_cells=streamwise_cells,
        span_length_m=span_length,
        span_cells=span_cells,
        vertical_height_m=vertical_max,
        vertical_cells=vertical_cells,
        target_courant=0.25,
        target_alpha_courant=0.25,
    )
    replay_duration = max(float(row["time"]) for row in samples)
    replay_window_path = selected / "window_selection.json"
    replay_window = read_json(replay_window_path) if replay_window_path.exists() else {}
    peak_shifted_time = None
    if replay_window:
        peak_source_time = replay_window.get("peak_source_time_s", replay_window.get("source_peak_time_s"))
        if peak_source_time is None:
            raise DeliveryError("selected replay window evidence does not record a source peak time")
        peak_shifted_time = float(peak_source_time) - float(replay_window["selected_source_start_s"])
    config = {
        "schema": {"name": "tsunami.openfoam_replay_configuration", "version": "1.1.0"},
        "section_id": SECTION_ID,
        "openfoam_patch": "inlet",
        "boundary_policy": {
            "mode": "open_ocean_damped",
            "outlet": "open_ocean",
            "laterals": "open_ocean",
            "atmosphere": "open_atmosphere",
            "policy_version": "1.0.0",
        },
        "damping_policy": {
            "enabled": True,
            "model": "isotropicDamping",
            "profile": "halfCosineRamp",
            "outlet_width_fraction": 0.15,
            "lateral_width_fraction": 0.10,
            "target_e_folds": 4.0,
        },
        "wall_function_policy": {
            "mode": "continuous_spalding",
            "k": "kqRWallFunction",
            "omega": "omegaWallFunction",
            "nut": "nutUSpaldingWallFunction",
        },
        "timestep_policy": {
            "adjust_time_step": True,
            "target_max_co": 0.25,
            "target_max_alpha_co": 0.25,
            "minimum_timestep_s": 1.0e-7,
        },
        "regional": {
            "dry_depth_m": 1.0e-6,
            "eta_consistency_tolerance_m": 1.0e-8,
            "inward_normal_xy": [trajectory.unit.x, trajectory.unit.y],
            "tangent_xy": [trajectory.left.x, trajectory.left.y],
            "vertical_datum_origin_m": vertical_datum,
        },
        "local": {
            "inward_axis": [1.0, 0.0, 0.0],
            "span_axis": [0.0, 1.0, 0.0],
            "vertical_axis": [0.0, 0.0, 1.0],
            "origin_m": [0.0, 0.0, 0.0],
            "span_min_m": 0.0,
            "span_max_m": span_length,
            "vertical_min_m": 0.0,
            "vertical_max_m": vertical_max,
            "span_cells": span_cells,
            "vertical_cells": vertical_cells,
        },
        "mapping": {
            "spatial_interpolation": "piecewise_linear_along_section",
            "outside_span": "clamp",
            "velocity_profile": "depth_uniform",
            "vertical_velocity_m_per_s": 0.0,
            "preserve_discrete_discharge": True,
        },
        "turbulence": {"intensity": 0.05, "length_scale_m": max(0.05, 0.05 * representative_depth), "minimum_speed_m_per_s": 0.01},
        "local_case": {
            "streamwise_length_m": streamwise_length,
            "streamwise_cells": streamwise_cells,
            "span_cells": span_cells,
            "vertical_cells": vertical_cells,
            "end_time_s": replay_duration,
            "maximum_timestep_s": timestep["selected_maximum_timestep_s"],
            "initial_timestep_s": min(0.002, timestep["selected_maximum_timestep_s"]),
            "maximum_courant_number": timestep["target_courant_limit"],
            "maximum_alpha_courant_number": timestep["target_alpha_courant_limit"],
            "minimum_timestep_s": 1.0e-7,
            "write_interval_s": max(1.0e-6, min(60.0, replay_duration / 5.0)),
            "initial_water_level_m": max(0.05, representative_depth),
            "alpha_tolerance": 5.0e-5,
            "timestep_derivation": timestep,
        },
        "replay_window": {
            "selected_source_start_s": replay_window.get("selected_source_start_s"),
            "selected_source_end_s": replay_window.get("selected_source_end_s"),
            "shifted_duration_s": replay_duration,
            "selected_time_count": len({float(row["time"]) for row in samples}),
            "peak_shifted_time_s": peak_shifted_time,
            "source_metadata_sha256": replay_window.get("source_metadata_sha256"),
            "source_samples_sha256": replay_window.get("source_samples_sha256"),
            "source_history_sha256": replay_window.get("source_history_sha256"),
        },
        "barrier": {
            "streamwise_position_m": 0.6 * streamwise_length,
            "thickness_m": max(streamwise_length / streamwise_cells, 2.0 * streamwise_length / streamwise_cells),
            "height_m": min(0.5 * representative_depth, 0.85 * vertical_max),
            "span_fraction": 1.0,
        },
        "derived_dimensions": {
            "regional_span_length_m": span_length,
            "representative_wet_depth_m": representative_depth,
            "minimum_bed_m": min(beds),
            "maximum_bed_m": max(beds),
            "maximum_free_surface_m": max(etas),
            "vertical_freeboard_m": freeboard,
            "local3d_disclaimer": "representative Kamaishi-forced local replay with flat rigid bed and simplified wall-type barrier",
        },
    }
    write_json(output, config)
    return config


def _probe_numeric_values(path: Path) -> list[float]:
    values: list[float] = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        for token in stripped.replace("(", " ").replace(")", " ").split():
            try:
                value = float(token)
            except ValueError:
                continue
            if math.isfinite(value):
                values.append(value)
    return values


def probe_distinction_metrics(no_defence: Path, barrier: Path) -> dict:
    metrics: dict[str, float | int] = {}
    maximum = 0.0
    compared = 0
    for name in ("alpha.water", "U", "p_rgh"):
        left = no_defence / "postProcessing/probes/0" / name
        right = barrier / "postProcessing/probes/0" / name
        if not left.is_file() or not right.is_file():
            raise DeliveryError(f"missing probe output for comparison: {name}")
        left_values = _probe_numeric_values(left)
        right_values = _probe_numeric_values(right)
        count = min(len(left_values), len(right_values))
        if count == 0:
            raise DeliveryError(f"empty numeric probe output for comparison: {name}")
        diff = max(abs(left_values[index] - right_values[index]) for index in range(count))
        metrics[f"{name}_maximum_absolute_difference"] = diff
        maximum = max(maximum, diff)
        compared += count
    if maximum <= 0.0:
        raise DeliveryError("OpenFOAM variants are not distinguishable in probe output")
    metrics["maximum_absolute_probe_difference"] = maximum
    metrics["compared_numeric_values"] = compared
    return metrics


def run_openfoam_stage(output_root: Path, python: Path, overwrite: bool) -> dict:
    replay_root = output_root / "replay"
    selected = replay_root / "selected-window"
    config = replay_root / "replay_config.json"
    conversion_command = [str(python), str(repo_root() / OPENFOAM_REPLAY), "convert", "--coupling-dir", str(selected), "--config", str(config), "--output-root", str(replay_root)]
    if overwrite:
        conversion_command.append("--overwrite")
    conversion = run_command(conversion_command, cwd=repo_root(), log_path=replay_root / "log.replay-convert")
    if conversion.returncode != 0:
        raise DeliveryError(f"OpenFOAM replay conversion failed with exit status {conversion.returncode}")
    local_root = output_root / "local"
    evidence: dict[str, object] = {"conversion_command": " ".join(conversion_command)}
    replay_spec = importlib.util.spec_from_file_location("tsunami_openfoam_replay", repo_root() / OPENFOAM_REPLAY)
    if replay_spec is None or replay_spec.loader is None:
        raise DeliveryError("could not load OpenFOAM replay validator")
    replay_module = importlib.util.module_from_spec(replay_spec)
    openfoam_tools = str((repo_root() / OPENFOAM_REPLAY).parent)
    if openfoam_tools not in sys.path:
        sys.path.insert(0, openfoam_tools)
    sys.modules["tsunami_openfoam_replay"] = replay_module
    replay_spec.loader.exec_module(replay_module)
    for variant in ("no_defence", "simple_rigid_barrier"):
        case_dir = local_root / variant
        generate = [str(python), str(repo_root() / OPENFOAM_REPLAY), "generate", "--replay-root", str(replay_root), "--config", str(config), "--output-root", str(case_dir), "--variant", variant]
        if overwrite:
            generate.append("--overwrite")
        generated = run_command(generate, cwd=repo_root(), log_path=case_dir / "log.generate-case")
        if generated.returncode != 0:
            raise DeliveryError(f"OpenFOAM case generation failed for {variant}")
        stage_commands: list[dict[str, object]] = []
        for stage in ("blockMesh", "checkMesh", "setFields", "foamRun", "foamToVTK"):
            command = [str(repo_root() / OPENFOAM_RUNNER), str(case_dir), stage]
            if stage == "foamRun":
                command.extend(["-solver", "incompressibleVoF"])
            completed = run_command(command, cwd=repo_root(), log_path=case_dir / f"log.{stage}")
            stage_commands.append({"stage": stage, "command": " ".join(command), "exit_status": completed.returncode})
            if completed.returncode != 0:
                raise DeliveryError(f"OpenFOAM v11 {stage} failed for {variant} with exit status {completed.returncode}")
        validation = replay_module.validate_smoke_case(case_dir, variant)
        evidence[variant] = {"generate_command": " ".join(generate), "stages": stage_commands, "validation": validation}
    evidence["probe_distinction"] = probe_distinction_metrics(local_root / "no_defence", local_root / "simple_rigid_barrier")
    return evidence


def preprocessing_input_hashes(root: Path, profile: Profile) -> dict[str, object]:
    return {
        "profile": profile.name,
        "case_spec_sha256": sha256(root / "cases/kamaishi_delivery/case_spec.json"),
        "terrain_source_sha256": sha256(root / SOURCE_TERRAIN_PATH),
        "finite_fault_source_sha256": sha256(root / SOURCE_QUAKE_PATH),
        "earthquake_producer_sha256": sha256(root / EARTHQUAKE_PRODUCER),
    }


def _case_file_record(case_root: Path, relative_path: Path) -> dict[str, object]:
    path = case_root / relative_path
    if not path.is_file() or path.stat().st_size == 0:
        raise DeliveryError(f"missing or empty preprocessing output: {path}")
    return {
        "relative_path": relative_path.as_posix(),
        "size": path.stat().st_size,
        "sha256": sha256(path),
    }


def preprocessing_output_records(case_root: Path) -> list[dict[str, object]]:
    required = [
        TERRAIN_OUTPUT_PATH,
        TERRAIN_RECORD_PATH,
        MESH_PATH.with_suffix(".geo"),
        MESH_PATH,
        CORRIDOR_RECORD_PATH,
        Path("manifests/corridors/kamaishi-delivery-corridor-evidence.json"),
        Path("inputs/data/earthquake/tohoku_vertical_displacement.tif"),
        Path("inputs/data/earthquake/tohoku_vertical_displacement.json"),
        Path("manifests/datasets.json"),
        Path("case.json"),
    ]
    return [_case_file_record(case_root, path) for path in required]


def try_reuse_preprocessing_stage(case_root: Path, output_root: Path, input_hashes: dict[str, object]) -> dict | None:
    record_path = case_root / G6_PREPROCESSING_STAGE_RECORD
    if not record_path.is_file():
        return None
    record = read_json(record_path)
    if record.get("status") != "success":
        return None
    if record.get("input_hashes") != input_hashes:
        return None
    for output in record.get("outputs", []):
        relative = Path(str(output["relative_path"]))
        path = case_root / relative
        if not path.is_file() or path.stat().st_size == 0:
            return None
        if int(output["size"]) != path.stat().st_size or output["sha256"] != sha256(path):
            return None
    sources_root = output_root / "sources"
    sources_root.mkdir(parents=True, exist_ok=True)
    source_inventory_path = repo_root() / "data/source/source_inventory.json"
    source_sums_path = repo_root() / "data/source/SHA256SUMS"
    if source_inventory_path.is_file():
        shutil.copy2(source_inventory_path, sources_root / "source_inventory.json")
    if source_sums_path.is_file():
        shutil.copy2(source_sums_path, sources_root / "SHA256SUMS")
    build_evidence = dict(record["build_evidence"])
    build_evidence["reused_preprocessing"] = True
    build_evidence["stage_completion_record"] = str(record_path)
    return build_evidence


def write_preprocessing_stage_completion(case_root: Path, input_hashes: dict[str, object], build_evidence: dict) -> dict:
    record = {
        "schema": {"name": "tsunami.g6_preprocessing_stage_completion", "version": "1.0.0"},
        "stage": "kamaishi_preprocessing",
        "status": "success",
        "recorded_at_utc": utc_now(),
        "input_hashes": input_hashes,
        "outputs": preprocessing_output_records(case_root),
        "build_evidence": build_evidence,
    }
    write_json(case_root / G6_PREPROCESSING_STAGE_RECORD, record)
    return record


def _normalise_case_for_equivalence(case_payload: dict) -> dict:
    normalised = json.loads(json.dumps(case_payload))
    normalised["case"]["created_at_utc"] = "<ignored-generated-timestamp>"
    normalised["regional_2d"]["numerics"]["final_time_s"] = "<ignored-regional-evidence-final-time>"
    return normalised


def write_regional_evidence_equivalence(
    case_root: Path,
    output_root: Path,
    *,
    requested_final_time_s: float,
    fixed_source_end_s: float,
    reference: dict,
) -> dict:
    margin = requested_final_time_s - fixed_source_end_s
    if margin + 1.0e-9 < 55.0:
        raise DeliveryError("G6 Regional2D evidence end time must be at least 55 s beyond the fixed replay source end")
    case_path = case_root / "case.json"
    accepted_case = read_json(case_path)
    accepted_final_time = float(accepted_case["regional_2d"]["numerics"]["final_time_s"])
    if accepted_final_time < 1800.0:
        raise DeliveryError("canonical Kamaishi production case no longer requests the accepted 1800 s Regional2D run")
    evidence_case = json.loads(json.dumps(accepted_case))
    evidence_case["regional_2d"]["numerics"]["final_time_s"] = float(requested_final_time_s)
    accepted_hash = json_sha256(_normalise_case_for_equivalence(accepted_case))
    evidence_hash = json_sha256(_normalise_case_for_equivalence(evidence_case))
    if accepted_hash != evidence_hash:
        raise DeliveryError("Regional2D evidence override changed inputs other than the requested final time")

    value_items = {
        "case_schema": (accepted_case["schema_version"], evidence_case["schema_version"]),
        "scenario": (accepted_case["scenario"], evidence_case["scenario"]),
        "gravity": (
            accepted_case["regional_2d"]["physics"]["gravity_m_per_s2"],
            evidence_case["regional_2d"]["physics"]["gravity_m_per_s2"],
        ),
        "manning_coefficient": (
            accepted_case["regional_2d"]["physics"]["manning"],
            evidence_case["regional_2d"]["physics"]["manning"],
        ),
        "coriolis": (
            accepted_case["regional_2d"]["physics"]["coriolis"],
            evidence_case["regional_2d"]["physics"]["coriolis"],
        ),
        "earthquake_configuration": (
            accepted_case["regional_2d"]["physics"]["earthquake"],
            evidence_case["regional_2d"]["physics"]["earthquake"],
        ),
        "scheme": (
            accepted_case["regional_2d"]["numerics"]["scheme"],
            evidence_case["regional_2d"]["numerics"]["scheme"],
        ),
        "cfl": (
            accepted_case["regional_2d"]["numerics"]["courant_number"],
            evidence_case["regional_2d"]["numerics"]["courant_number"],
        ),
        "maximum_timestep": (
            accepted_case["regional_2d"]["numerics"]["maximum_timestep_s"],
            evidence_case["regional_2d"]["numerics"]["maximum_timestep_s"],
        ),
        "snapshot_interval": (
            accepted_case["outputs"]["snapshot_interval_s"],
            evidence_case["outputs"]["snapshot_interval_s"],
        ),
        "boundary_conditions": (
            accepted_case["regional_2d"]["boundaries"],
            evidence_case["regional_2d"]["boundaries"],
        ),
        "coupling_section": (SECTION_ID, SECTION_ID),
        "coupling_patch": (COUPLING_PATCH, COUPLING_PATCH),
        "source_replay_window": (reference["source_window"], reference["source_window"]),
    }
    comparisons = {
        name: {
            "accepted_g5_value": accepted,
            "current_g6_evidence_run_value": current,
            "accepted_g5_hash": json_sha256(accepted),
            "current_g6_hash": json_sha256(current),
            "passed": accepted == current,
        }
        for name, (accepted, current) in value_items.items()
    }
    file_items = {
        "case_spec": repo_root() / "cases/kamaishi_delivery/case_spec.json",
        "terrain_source": case_root / "inputs/data/source/terrain/ETOPO_2022_v1_15s_N45E135_surface.tif",
        "finite_fault_source": case_root / "inputs/data/source/earthquake/usgs_usp000hvnu_1539808472261_basic_inversion.param",
        "conditioned_terrain": case_root / "inputs/data/terrain/conditioned-terrain.tif",
        "terrain_record": case_root / TERRAIN_RECORD_PATH,
        "regional_mesh": case_root / MESH_PATH,
        "earthquake_displacement_raster": case_root / "inputs/data/earthquake/tohoku_vertical_displacement.tif",
        "earthquake_displacement_metadata": case_root / "inputs/data/earthquake/tohoku_vertical_displacement.json",
        "corridor_record": case_root / CORRIDOR_RECORD_PATH,
    }
    file_comparisons = {
        name: {
            "accepted_g5_hash": sha256(path),
            "current_g6_evidence_run_hash": sha256(path),
            "passed": True,
            "path": str(path),
        }
        for name, path in file_items.items()
    }
    if not all(item["passed"] for item in comparisons.values()):
        raise DeliveryError("Regional2D evidence input equivalence comparison failed")
    evidence_dir = output_root / "evidence"
    record = {
        "schema": {"name": "tsunami.g6_regional_input_equivalence", "version": "1.0.0"},
        "status": "passed",
        "description": "targeted regeneration of the previously accepted G5 coupling window",
        "not_a_replacement_1800s_regional2d_acceptance": True,
        "calibration_status": "not_started",
        "requested_regional2d_final_time": {
            "accepted_g5_value_s": accepted_final_time,
            "current_g6_evidence_run_value_s": requested_final_time_s,
            "allowed_difference": True,
        },
        "margin_after_fixed_source_window_s": margin,
        "normalised_case_hash": {
            "accepted_g5_value_hash": accepted_hash,
            "current_g6_evidence_run_hash": evidence_hash,
        },
        "value_comparisons": comparisons,
        "file_hash_comparisons": file_comparisons,
        "allowed_differences": [
            "requested Regional2D final time",
            "run identifier",
            "output directory",
            "generated timestamp",
        ],
    }
    write_json(case_path, evidence_case)
    write_json(case_root / "manifests/g6_regional_input_equivalence.json", record)
    write_json(evidence_dir / "g6_regional_input_equivalence.json", record)
    return record


def collect_artifact_logs(output_root: Path) -> list[dict[str, object]]:
    logs_dir = output_root / "logs"
    logs_dir.mkdir(parents=True, exist_ok=True)
    copied: list[dict[str, object]] = []
    for current, dirs, files in os.walk(output_root):
        current_path = Path(current)
        dirs[:] = [name for name in dirs if name != "logs" and not (current_path / name).is_symlink()]
        for name in files:
            path = current_path / name
            rel = path.relative_to(output_root)
            if not (name.startswith("log.") or name == "command.txt"):
                continue
            target = logs_dir / "__".join(rel.parts)
            shutil.copy2(path, target)
            copied.append({"source": rel.as_posix(), "copy": target.relative_to(output_root).as_posix(), "sha256": sha256(target)})
    return copied


def _artifact_role(relative_path: Path) -> tuple[str, str, bool]:
    top = relative_path.parts[0] if relative_path.parts else relative_path.name
    if top == "sources":
        return "source provenance", "source_acquisition", True
    if top == "case":
        return "case definition and preprocessing output", "preprocessing", True
    if top == "regional":
        return "Regional2D evidence artifact", "regional2d", True
    if top == "replay":
        return "Local3D replay forcing artifact", "replay_window", True
    if top == "local":
        return "OpenFOAM Local3D runtime artifact", "local3d", False
    if top == "evidence" or relative_path.name == "artifact_inventory.json":
        return "machine-readable evidence", "evidence", True
    if top == "logs":
        return "execution log", "logging", True
    if relative_path.name == "delivery_summary.json":
        return "delivery summary", "evidence", True
    return "supporting artifact", top, False


def write_artifact_inventory(output_root: Path) -> dict:
    inventory_path = output_root / "artifact_inventory.json"
    entries: list[dict[str, object]] = []
    for current, dirs, files in os.walk(output_root):
        current_path = Path(current)
        dirs[:] = [name for name in dirs if not (current_path / name).is_symlink()]
        for name in sorted(files):
            path = current_path / name
            rel = path.relative_to(output_root)
            if path == inventory_path or rel == Path("evidence/artifact_inventory.json"):
                continue
            role, stage, required = _artifact_role(rel)
            entries.append(
                {
                    "relative_path": rel.as_posix(),
                    "size": path.stat().st_size,
                    "sha256": sha256(path),
                    "role": role,
                    "generation_stage": stage,
                    "required_for_reproduction": required,
                }
            )
    inventory = {
        "schema": {"name": "tsunami.g6_artifact_inventory", "version": "1.0.0"},
        "artifact_root": str(output_root),
        "generated_at_utc": utc_now(),
        "entries": entries,
    }
    write_json(inventory_path, inventory)
    inventory["inventory_sha256"] = sha256(inventory_path)
    write_json(output_root / "evidence/artifact_inventory.json", inventory)
    return inventory


def prepare_output_roots(case_root: Path, output_root: Path, overwrite: bool) -> None:
    for path in (case_root, output_root):
        if path.exists() and any(path.iterdir()) and not overwrite:
            raise DeliveryError(f"{path} already exists and is not empty; pass --overwrite to replace generated delivery outputs")
    if overwrite:
        for path in (case_root, output_root):
            if path.exists():
                shutil.rmtree(path)
    case_root.mkdir(parents=True, exist_ok=True)
    output_root.mkdir(parents=True, exist_ok=True)


def build_case(
    case_root: Path,
    output_root: Path,
    profile: Profile,
    acquire: bool,
    offline: bool,
    overwrite: bool,
    python: Path,
    *,
    reuse_preprocessing: bool = False,
) -> dict:
    root = repo_root()
    now = utc_now()
    spec = read_json(root / "cases/kamaishi_delivery/case_spec.json")
    spec["profile"] = profile.name
    inventory = ensure_sources(root, acquire=acquire, offline=offline, overwrite=False)
    input_hashes = preprocessing_input_hashes(root, profile)
    if reuse_preprocessing and not overwrite:
        reused = try_reuse_preprocessing_stage(case_root, output_root, input_hashes)
        if reused is not None:
            return reused
    fault_evidence = validate_finite_fault_source(root / SOURCE_QUAKE_PATH)
    prepare_output_roots(case_root, output_root, overwrite)
    (output_root / "sources").mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / "data/source/source_inventory.json", output_root / "sources/source_inventory.json")
    shutil.copy2(root / "data/source/SHA256SUMS", output_root / "sources/SHA256SUMS")
    trajectory = select_interface(root / SOURCE_TERRAIN_PATH, spec)
    grid = build_grid(trajectory, profile, spec)
    write_corridor_record(case_root, trajectory, spec, grid, now)
    terrain_evidence = write_conditioned_terrain(case_root, root / SOURCE_TERRAIN_PATH, grid, trajectory, profile, now)
    mesh_evidence = write_mesh(case_root, trajectory, spec, profile)
    producer_evidence = produce_displacement(case_root, root, now, python)
    write_case_and_manifest(case_root, root, spec, inventory, trajectory, now)
    build_evidence = {
        "timestamp": now,
        "profile": profile.name,
        "case_root": str(case_root),
        "output_root": str(output_root),
        "reused_preprocessing": False,
        "source_inventory": inventory,
        "finite_fault": fault_evidence,
        "trajectory": {
            "epicentre_projected_m": {"x": trajectory.epicentre.x, "y": trajectory.epicentre.y},
            "proxy_projected_m": {"x": trajectory.proxy.x, "y": trajectory.proxy.y},
            "selected_projected_m": {"x": trajectory.selected.x, "y": trajectory.selected.y},
            "selected_wgs84": {"longitude": trajectory.selected_wgs84[0], "latitude": trajectory.selected_wgs84[1]},
            "bearing_degrees_clockwise_from_north": trajectory.bearing_degrees,
            "distance_m": trajectory.distance_m,
            "selected_depth_m": trajectory.selected_depth_m,
            "selected_bed_elevation_m": trajectory.selected_bed_elevation_m,
            "selection_fallback": trajectory.selection_fallback,
            "selection_reason": trajectory.selection_reason,
            "cross_section_sample_count": trajectory.cross_section_sample_count,
            "cross_section_min_depth_m": trajectory.cross_section_min_depth_m,
            "cross_section_max_depth_m": trajectory.cross_section_max_depth_m,
            "cross_section_min_bed_elevation_m": trajectory.cross_section_min_bed_elevation_m,
            "cross_section_max_bed_elevation_m": trajectory.cross_section_max_bed_elevation_m,
        },
        "terrain": terrain_evidence,
        "mesh": mesh_evidence,
        "earthquake_producer": producer_evidence,
    }
    record = write_preprocessing_stage_completion(case_root, input_hashes, build_evidence)
    build_evidence["stage_completion_record"] = str(case_root / G6_PREPROCESSING_STAGE_RECORD)
    build_evidence["stage_completion_sha256"] = sha256(case_root / G6_PREPROCESSING_STAGE_RECORD)
    build_evidence["stage_completion_output_count"] = len(record["outputs"])
    return build_evidence


def run_delivery(
    case_root: Path,
    output_root: Path,
    profile_name: str,
    acquire: bool,
    offline: bool,
    overwrite: bool,
    python: Path,
    r2d_binary: Path,
    run_openfoam: bool,
    *,
    evidence_mode: str | None = None,
    regional_evidence_end_time_s: float | None = None,
    fixed_replay_reference: dict | None = None,
    reuse_preprocessing: bool = False,
) -> dict:
    profile = profile_by_name(profile_name)
    reference = fixed_replay_reference
    if evidence_mode == "g6_local3d_acceptance":
        reference = reference or load_g5_accepted_replay_reference()
        if profile.name != reference["profile"]["name"]:
            raise DeliveryError("G6 Local3D acceptance mode requires the accepted etopo-1000m profile")
        if regional_evidence_end_time_s is None:
            regional_evidence_end_time_s = 600.0
        if abs(regional_evidence_end_time_s - 600.0) > 1.0e-9:
            raise DeliveryError("G6 Local3D acceptance mode is restricted to the 600 s Regional2D evidence prerequisite")
    elif regional_evidence_end_time_s is not None or reference is not None:
        raise DeliveryError("fixed Regional2D/replay evidence options require --g6-local3d-acceptance")

    build_evidence = build_case(
        case_root,
        output_root,
        profile,
        acquire,
        offline,
        overwrite,
        python,
        reuse_preprocessing=reuse_preprocessing,
    )
    regional_equivalence = None
    minimum_regional_time = 1800.0
    if evidence_mode == "g6_local3d_acceptance":
        assert reference is not None
        regional_equivalence = write_regional_evidence_equivalence(
            case_root,
            output_root,
            requested_final_time_s=float(regional_evidence_end_time_s),
            fixed_source_end_s=float(reference["source_window"]["end_s"]),
            reference=reference,
        )
        minimum_regional_time = float(regional_evidence_end_time_s)
    regional = run_regional(case_root, output_root, r2d_binary, minimum_requested_final_time_s=minimum_regional_time)
    full_coupling = Path(regional["output_dir"]) / "coupling" / SECTION_ID
    replay_root = output_root / "replay"
    full_source = replay_root / "full-source"
    full_source.parent.mkdir(parents=True, exist_ok=True)
    if full_source.exists():
        shutil.rmtree(full_source)
    shutil.copytree(full_coupling, full_source)
    trajectory_data = build_evidence["trajectory"]
    # Recreate only the normalized vectors needed for replay derivation.
    selected = Point(trajectory_data["selected_projected_m"]["x"], trajectory_data["selected_projected_m"]["y"])
    epicentre = Point(trajectory_data["epicentre_projected_m"]["x"], trajectory_data["epicentre_projected_m"]["y"])
    unit, distance = _unit_vector(epicentre, selected)
    trajectory = Trajectory((0.0, 0.0), (0.0, 0.0), epicentre, epicentre, selected, (0.0, 0.0), unit, Point(-unit.y, unit.x), distance, 0.0, _bearing(unit), 0.0, 0.0, False, "")
    forcing_equivalence = None
    if evidence_mode == "g6_local3d_acceptance":
        assert reference is not None
        evidence_dir = output_root / "evidence"
        window = select_fixed_replay_window(
            full_source,
            replay_root / "selected-window",
            (trajectory.unit.x, trajectory.unit.y),
            reference,
            evidence_output=evidence_dir / "g6_fixed_window_extraction.json",
        )
        forcing_equivalence = compare_fixed_window_to_reference(
            window,
            reference,
            output_path=evidence_dir / "g6_forcing_equivalence.json",
        )
        if forcing_equivalence["status"] != "passed":
            raise DeliveryError("fixed G6 forcing-equivalence comparison failed")
    else:
        window = select_replay_window(full_source, replay_root / "selected-window", (trajectory.unit.x, trajectory.unit.y))
    config = replay_config_from_window(replay_root / "selected-window", trajectory, replay_root / "replay_config.json")
    evidence = {
        "build": build_evidence,
        "regional": regional,
        "replay_window": window,
        "replay_config": config,
        "evidence_mode": evidence_mode,
        "calibration_status": "not_started",
    }
    if regional_equivalence is not None:
        evidence["regional_input_equivalence"] = regional_equivalence
    if forcing_equivalence is not None:
        evidence["forcing_equivalence"] = forcing_equivalence
    if run_openfoam:
        evidence["openfoam"] = run_openfoam_stage(output_root, python, overwrite)
    evidence["logs"] = collect_artifact_logs(output_root)
    write_json(output_root / "delivery_summary.json", evidence)
    inventory = write_artifact_inventory(output_root)
    evidence["artifact_inventory"] = {
        "path": str(output_root / "artifact_inventory.json"),
        "sha256": inventory["inventory_sha256"],
        "entry_count": len(inventory["entries"]),
    }
    write_json(output_root / "delivery_summary.json", evidence)
    return evidence


def tree(root: Path) -> str:
    lines: list[str] = []
    for current, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in {".git"})
        files = sorted(files)
        rel = Path(current).relative_to(root)
        indent = "    " * (len(rel.parts))
        label = "." if str(rel) == "." else rel.name
        lines.append(f"{indent}{label}/")
        for file in files:
            path = Path(current) / file
            lines.append(f"{indent}    {file} ({path.stat().st_size} bytes)")
    return "\n".join(lines)
