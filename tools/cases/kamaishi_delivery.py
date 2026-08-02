#!/usr/bin/env python3
"""Kamaishi real-data hybrid delivery case assembly helpers."""

from __future__ import annotations

import csv
import hashlib
import importlib.util
import json
import math
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Sequence


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


def run_command(command: Sequence[str], *, cwd: Path, log_path: Path | None = None) -> subprocess.CompletedProcess[str]:
    log_text = " ".join(command) + "\n"
    completed = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log_text += completed.stdout
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(log_text, encoding="utf-8")
    return completed


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
        "etopo-500m": Profile("etopo-500m", 500.0, 500.0),
        "etopo-1000m": Profile("etopo-1000m", 1000.0, 1000.0),
    }
    try:
        return profiles[name]
    except KeyError as exc:
        raise DeliveryError(f"unknown Kamaishi profile: {name}") from exc


def transform_wgs84(lon: float, lat: float) -> Point:
    from pyproj import Transformer

    transformer = Transformer.from_crs("EPSG:4326", "EPSG:32654", always_xy=True)
    x, y = transformer.transform(lon, lat)
    return Point(float(x), float(y))


def inverse_transform(point: Point) -> tuple[float, float]:
    from pyproj import Transformer

    transformer = Transformer.from_crs("EPSG:32654", "EPSG:4326", always_xy=True)
    lon, lat = transformer.transform(point.x, point.y)
    return float(lon), float(lat)


def _unit_vector(a: Point, b: Point) -> tuple[Point, float]:
    dx = b.x - a.x
    dy = b.y - a.y
    distance = math.hypot(dx, dy)
    if distance <= 0.0:
        raise DeliveryError("reference points must be distinct")
    return Point(dx / distance, dy / distance), distance


def _bearing(unit: Point) -> float:
    return math.degrees(math.atan2(unit.x, unit.y)) % 360.0


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
    step_m = 250.0
    max_distance = max(distance_to_proxy + 20_000.0, 160_000.0)
    candidates: list[tuple[bool, float, Point, float, float]] = []
    first_wet_distance: float | None = None
    with rasterio.open(source_tif) as dataset:
        for i in range(int(max_distance // step_m) + 1):
            d = i * step_m
            point = Point(proxy_projected.x - to_proxy.x * d, proxy_projected.y - to_proxy.y * d)
            lon, lat = inverse_transform(point)
            sample = next(dataset.sample([(lon, lat)], indexes=1, masked=True))
            if getattr(sample, "mask", False).any():
                continue
            bed = float(sample[0])
            if not math.isfinite(bed):
                continue
            depth = max(0.0, -bed)
            if depth <= 0.0:
                continue
            if first_wet_distance is None:
                first_wet_distance = d
            in_preferred = preferred[0] <= depth <= preferred[1]
            in_fallback = fallback[0] <= depth <= fallback[1]
            if in_preferred or in_fallback:
                candidates.append((not in_preferred, d, point, bed, depth))
                if in_preferred:
                    break
    if not candidates:
        raise DeliveryError("could not select a valid wet nearshore interface in the configured depth bands")
    fallback_used, distance_from_proxy, selected, bed, depth = sorted(candidates)[0]
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
        selected_depth_m=depth,
        selection_fallback=fallback_used,
        selection_reason=(
            "selected closest wet ETOPO sample in fallback band"
            if fallback_used
            else "selected closest wet ETOPO sample in preferred 15-30 m band"
        ),
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
    if not np.all(np.isfinite(destination)) or np.any(destination <= -1.0e299):
        raise DeliveryError("conditioned ETOPO terrain contains nodata in required cells")
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
    if min_elevation >= 0.0 or max_elevation <= 0.0:
        raise DeliveryError("conditioned terrain must include a coastline transition for this delivery case")
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
            "warnings": ["ETOPO 2022 is a fallback integration terrain source, not final high-resolution Kamaishi terrain"],
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
    inland_extent = float(spec["corridor"].get("inland_extent_m", 0.0))
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
                "courant_number": 0.45,
                "positivity_safety_factor": 0.95,
                "relaxation_safety_factor": 1.0,
                "source_safety_factor": 1.0,
                "minimum_timestep_s": 0.001,
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


def run_regional(case_root: Path, output_root: Path, r2d_binary: Path) -> dict:
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
    return validate_regional_outputs(output_dir) | {"command": " ".join(command), "exit_status": result.returncode, "output_dir": str(output_dir)}


def _csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def validate_regional_outputs(output_dir: Path) -> dict:
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
    if len(history) < 4:
        raise DeliveryError("coupling output has fewer than 4 snapshot times")
    max_eta = 0.0
    max_q = 0.0
    wet = False
    for row in samples:
        depth = float(row["depth"])
        qx = float(row["momentum_x"])
        qy = float(row["momentum_y"])
        bed = float(row["bed_elevation"])
        eta = float(row["free_surface_elevation"])
        if not all(math.isfinite(v) for v in (depth, qx, qy, bed, eta)):
            raise DeliveryError("non-finite coupling sample value")
        wet = wet or depth > 1.0e-6
        max_eta = max(max_eta, abs(eta))
        max_q = max(max_q, math.hypot(qx, qy))
    if not wet:
        raise DeliveryError("coupling output contains no wet samples")
    if max_eta <= 1.0e-7 and max_q <= 1.0e-7:
        raise DeliveryError("coupling output does not show propagated event signal")
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
    return {
        "diagnostics_rows": len(diagnostics),
        "snapshot_rows": len(snapshots),
        "earthquake_initialisation_rows": len(earthquake),
        "coupling_sample_rows": len(samples),
        "coupling_history_rows": len(history),
        "coupling_sample_count": int(metadata["sample_count"]),
        "maximum_abs_eta_m": max_eta,
        "maximum_abs_momentum": max_q,
        "maximum_abs_effective_bed_displacement_m": max_displacement,
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
    baseline = {int(row["local_index"]): row for row in by_time[times[0]]}
    metrics: list[tuple[float, float]] = []
    for time in times:
        metric = 0.0
        for row in by_time[time]:
            base = baseline[int(row["local_index"])]
            eta_delta = abs(float(row["free_surface_elevation"]) - float(base["free_surface_elevation"]))
            qn = abs(float(row["momentum_x"]) * inward_normal[0] + float(row["momentum_y"]) * inward_normal[1])
            metric = max(metric, eta_delta, qn)
        metrics.append((time, metric))
    peak_time, peak_metric = max(metrics, key=lambda item: item[1])
    threshold = max(1.0e-8, 0.02 * peak_metric)
    crossing = next((time for time, metric in metrics if metric >= threshold), times[0])
    start = max(times[0], crossing - 30.0)
    end = min(times[-1], max(crossing + 180.0, peak_time))
    if end - start > 300.0:
        end = start + 300.0
    chosen_times = [time for time in times if start <= time <= end]
    if len(chosen_times) < 4:
        chosen_times = times[: min(len(times), 4)]
        start = chosen_times[0]
        end = chosen_times[-1]
    if len(chosen_times) < 4:
        raise DeliveryError("replay window has fewer than 4 selected times")
    selected.mkdir(parents=True, exist_ok=True)
    shutil.copy2(full_coupling / "metadata.json", selected / "metadata.json")
    with (selected / "samples.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(samples[0]))
        writer.writeheader()
        for time in chosen_times:
            shifted = time - start
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
        "threshold": threshold,
        "first_crossing_source_time_s": crossing,
        "peak_source_time_s": peak_time,
        "peak_metric": peak_metric,
        "selected_source_start_s": start,
        "selected_source_end_s": end,
        "selected_time_count": len(chosen_times),
        "shifted_duration_s": end - start,
    }
    write_json(selected / "window_selection.json", evidence)
    return evidence


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
    span_cells = max(6, min(20, int(math.ceil(span_length / 500.0))))
    vertical_cells = max(12, min(20, int(math.ceil(vertical_max / max(vertical_max / 16.0, 1.0)))))
    streamwise_length = max(300.0, min(1500.0, 20.0 * max(representative_depth, 1.0)))
    streamwise_cells = max(40, min(60, int(math.ceil(streamwise_length / 20.0))))
    config = {
        "schema": {"name": "tsunami.openfoam_replay_configuration", "version": "1.0.0"},
        "section_id": SECTION_ID,
        "openfoam_patch": "inlet",
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
            "end_time_s": max(float(row["time"]) for row in samples),
            "maximum_timestep_s": 0.005,
            "write_interval_s": max(1.0, max(float(row["time"]) for row in samples) / 5.0),
            "initial_water_level_m": max(0.05, representative_depth),
            "alpha_tolerance": 5.0e-5,
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
        for stage in ("blockMesh", "setFields", "foamRun", "foamToVTK"):
            command = [str(repo_root() / OPENFOAM_RUNNER), str(case_dir), stage]
            if stage == "foamRun":
                command.extend(["-solver", "incompressibleVoF"])
            completed = run_command(command, cwd=repo_root(), log_path=case_dir / f"log.{stage}")
            stage_commands.append({"stage": stage, "command": " ".join(command), "exit_status": completed.returncode})
            if completed.returncode != 0:
                raise DeliveryError(f"OpenFOAM v11 {stage} failed for {variant} with exit status {completed.returncode}")
        validation = replay_module.validate_smoke_case(case_dir, variant)
        evidence[variant] = {"generate_command": " ".join(generate), "stages": stage_commands, "validation": validation}
    return evidence


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


def build_case(case_root: Path, output_root: Path, profile: Profile, acquire: bool, offline: bool, overwrite: bool, python: Path) -> dict:
    root = repo_root()
    now = utc_now()
    spec = read_json(root / "cases/kamaishi_delivery/case_spec.json")
    spec["profile"] = profile.name
    inventory = ensure_sources(root, acquire=acquire, offline=offline, overwrite=False)
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
    return {
        "timestamp": now,
        "profile": profile.name,
        "case_root": str(case_root),
        "output_root": str(output_root),
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
            "selection_fallback": trajectory.selection_fallback,
        },
        "terrain": terrain_evidence,
        "mesh": mesh_evidence,
        "earthquake_producer": producer_evidence,
    }


def run_delivery(case_root: Path, output_root: Path, profile_name: str, acquire: bool, offline: bool, overwrite: bool, python: Path, r2d_binary: Path, run_openfoam: bool) -> dict:
    profile = profile_by_name(profile_name)
    build_evidence = build_case(case_root, output_root, profile, acquire, offline, overwrite, python)
    regional = run_regional(case_root, output_root, r2d_binary)
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
    window = select_replay_window(full_source, replay_root / "selected-window", (trajectory.unit.x, trajectory.unit.y))
    config = replay_config_from_window(replay_root / "selected-window", trajectory, replay_root / "replay_config.json")
    evidence = {"build": build_evidence, "regional": regional, "replay_window": window, "replay_config": config}
    if run_openfoam:
        evidence["openfoam"] = run_openfoam_stage(output_root, python, overwrite)
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
