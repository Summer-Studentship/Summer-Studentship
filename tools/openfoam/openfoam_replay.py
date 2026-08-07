#!/usr/bin/env python3
"""Regional2D-to-OpenFOAM replay conversion and synthetic case generation."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Sequence

from simple_png import write_line_plot_png


CONVERTER_VERSION = "0.1.0"
REPLAY_SCHEMA = {"name": "tsunami.openfoam_replay_conversion", "version": "1.0.0"}
CONFIG_SCHEMA_NAME = "tsunami.openfoam_replay_configuration"
CONFIG_SCHEMA_VERSION = "1.0.0"
PRODUCTION_CONFIG_SCHEMA_VERSION = "1.1.0"
SUPPORTED_CONFIG_SCHEMA_VERSIONS = {CONFIG_SCHEMA_VERSION, PRODUCTION_CONFIG_SCHEMA_VERSION}
G3_CONTRACT_VERSION = 1
REQUIRED_SAMPLE_COLUMNS = [
    "step",
    "time",
    "section_id",
    "local_index",
    "cell",
    "face",
    "x_m",
    "y_m",
    "depth",
    "momentum_x",
    "momentum_y",
    "bed_elevation",
    "free_surface_elevation",
]
REQUIRED_HISTORY_COLUMNS = ["step", "time", "section_id", "sample_count", "maximum_depth", "maximum_speed"]


class ReplayError(ValueError):
    """Raised for invalid replay configuration, coupling input, or generated output."""


@dataclass(frozen=True)
class MetadataSample:
    local_index: int
    cell: int
    face: int
    x_m: float
    y_m: float


@dataclass(frozen=True)
class SampleRow:
    step: int
    time: float
    section_id: str
    local_index: int
    cell: int
    face: int
    x_m: float
    y_m: float
    depth: float
    momentum_x: float
    momentum_y: float
    bed_elevation: float
    free_surface_elevation: float


@dataclass(frozen=True)
class HistoryRow:
    step: int
    time: float
    section_id: str
    sample_count: int
    maximum_depth: float
    maximum_speed: float


@dataclass(frozen=True)
class CouplingExport:
    root: Path
    metadata_path: Path
    samples_path: Path
    history_path: Path
    metadata: dict
    samples: list[SampleRow]
    history: list[HistoryRow]
    ordered_samples: list[MetadataSample]
    supports: list[dict]
    times: list[float]


def _finite(value: float, label: str) -> float:
    if not math.isfinite(value):
        raise ReplayError(f"{label} must be finite")
    return value


def _positive(value: float, label: str) -> float:
    value = _finite(value, label)
    if value <= 0.0:
        raise ReplayError(f"{label} must be positive")
    return value


def _int(value: object, label: str) -> int:
    if isinstance(value, bool):
        raise ReplayError(f"{label} must be an integer")
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise ReplayError(f"{label} must be an integer") from exc
    return parsed


def _float(value: object, label: str) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError) as exc:
        raise ReplayError(f"{label} must be numeric") from exc
    return _finite(parsed, label)


def _vector(values: object, length: int, label: str) -> tuple[float, ...]:
    if not isinstance(values, list) or len(values) != length:
        raise ReplayError(f"{label} must be a {length}-component array")
    return tuple(_float(value, f"{label}[{index}]") for index, value in enumerate(values))


def _norm(values: Sequence[float]) -> float:
    return math.sqrt(sum(value * value for value in values))


def _dot(left: Sequence[float], right: Sequence[float]) -> float:
    return sum(a * b for a, b in zip(left, right))


def _cross(left: Sequence[float], right: Sequence[float]) -> tuple[float, float, float]:
    return (
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    )


def _unit(values: Sequence[float], label: str, tolerance: float = 1.0e-10) -> None:
    if abs(_norm(values) - 1.0) > tolerance:
        raise ReplayError(f"{label} must be unit length")


def _orthogonal(left: Sequence[float], right: Sequence[float], label: str, tolerance: float = 1.0e-10) -> None:
    if abs(_dot(left, right)) > tolerance:
        raise ReplayError(f"{label} must be orthogonal")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ReplayError(f"{path}: invalid JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise ReplayError(f"{path}: root must be an object")
    return value


def _schema_version(config: dict) -> str:
    schema = config.get("schema")
    if not isinstance(schema, dict) or schema.get("name") != CONFIG_SCHEMA_NAME:
        raise ReplayError("unsupported replay configuration schema")
    version = schema.get("version")
    if version not in SUPPORTED_CONFIG_SCHEMA_VERSIONS:
        raise ReplayError("unsupported replay configuration schema")
    return str(version)


def _default_boundary_policy(version: str) -> dict:
    if version == CONFIG_SCHEMA_VERSION:
        return {
            "mode": "symmetry_test",
            "outlet": "legacy_inletOutlet",
            "laterals": "symmetry",
            "atmosphere": "open_atmosphere",
            "policy_version": "1.0.0",
        }
    return {
        "mode": "open_ocean_damped",
        "outlet": "open_ocean",
        "laterals": "open_ocean",
        "atmosphere": "open_atmosphere",
        "policy_version": "1.0.0",
    }


def _default_damping_policy(version: str) -> dict:
    if version == CONFIG_SCHEMA_VERSION:
        return {"enabled": False, "model": "disabled", "profile": "disabled"}
    return {
        "enabled": True,
        "model": "isotropicDamping",
        "profile": "halfCosineRamp",
        "outlet_width_fraction": 0.15,
        "lateral_width_fraction": 0.10,
        "target_e_folds": 4.0,
    }


def _default_wall_function_policy(version: str) -> dict:
    if version == CONFIG_SCHEMA_VERSION:
        return {
            "mode": "legacy_nutk",
            "k": "kqRWallFunction",
            "omega": "omegaWallFunction",
            "nut": "nutkWallFunction",
        }
    return {
        "mode": "continuous_spalding",
        "k": "kqRWallFunction",
        "omega": "omegaWallFunction",
        "nut": "nutUSpaldingWallFunction",
    }


def _default_timestep_policy(config: dict, version: str) -> dict:
    local_case = config.get("local_case", {})
    if not isinstance(local_case, dict):
        local_case = {}
    return {
        "adjust_time_step": True,
        "target_max_co": float(local_case.get("maximum_courant_number", 0.5 if version == CONFIG_SCHEMA_VERSION else 0.25)),
        "target_max_alpha_co": float(local_case.get("maximum_alpha_courant_number", 0.5 if version == CONFIG_SCHEMA_VERSION else 0.25)),
        "minimum_timestep_s": float(local_case.get("minimum_timestep_s", 1.0e-7 if version == PRODUCTION_CONFIG_SCHEMA_VERSION else 0.0)),
    }


def normalise_replay_config(config: dict) -> dict:
    """Return a config with explicit policy sections for all supported schemas."""
    version = _schema_version(config)
    normalised = json.loads(json.dumps(config))
    normalised.setdefault("boundary_policy", _default_boundary_policy(version))
    normalised.setdefault("damping_policy", _default_damping_policy(version))
    normalised.setdefault("wall_function_policy", _default_wall_function_policy(version))
    normalised.setdefault("timestep_policy", _default_timestep_policy(normalised, version))
    return normalised


def _validate_policy_sections(config: dict, version: str) -> None:
    boundary = config.get("boundary_policy")
    damping = config.get("damping_policy")
    wall = config.get("wall_function_policy")
    timestep = config.get("timestep_policy")
    if not all(isinstance(item, dict) for item in (boundary, damping, wall, timestep)):
        raise ReplayError("boundary_policy, damping_policy, wall_function_policy and timestep_policy must be objects")

    mode = boundary.get("mode")
    if mode not in {"symmetry_test", "open_ocean_damped"}:
        raise ReplayError("unsupported boundary_policy.mode")
    if version == PRODUCTION_CONFIG_SCHEMA_VERSION and mode != "open_ocean_damped":
        raise ReplayError("production replay configuration must use open_ocean_damped")
    if mode == "symmetry_test":
        if bool(damping.get("enabled")):
            raise ReplayError("symmetry_test must not enable open-boundary damping")
        if wall.get("mode") != "legacy_nutk":
            raise ReplayError("legacy symmetry_test must use legacy_nutk wall-function mode")
    else:
        if boundary.get("outlet") != "open_ocean" or boundary.get("laterals") != "open_ocean":
            raise ReplayError("open_ocean_damped requires open_ocean outlet and laterals")
        if bool(damping.get("enabled")) is not True:
            raise ReplayError("open_ocean_damped requires damping_policy.enabled")
        if damping.get("model") != "isotropicDamping":
            raise ReplayError("G6 production damping model must be isotropicDamping")
        if damping.get("profile") != "halfCosineRamp":
            raise ReplayError("G6 production damping profile must be halfCosineRamp")
        _positive(_float(damping.get("outlet_width_fraction"), "damping_policy.outlet_width_fraction"), "damping_policy.outlet_width_fraction")
        _positive(_float(damping.get("lateral_width_fraction"), "damping_policy.lateral_width_fraction"), "damping_policy.lateral_width_fraction")
        _positive(_float(damping.get("target_e_folds"), "damping_policy.target_e_folds"), "damping_policy.target_e_folds")
        if wall.get("mode") != "continuous_spalding":
            raise ReplayError("open_ocean_damped requires continuous_spalding wall functions")
    if wall.get("k") != "kqRWallFunction" or wall.get("omega") != "omegaWallFunction":
        raise ReplayError("unsupported k/omega wall-function policy")
    if wall.get("mode") == "continuous_spalding" and wall.get("nut") != "nutUSpaldingWallFunction":
        raise ReplayError("continuous_spalding requires nutUSpaldingWallFunction")
    if wall.get("mode") == "legacy_nutk" and wall.get("nut") != "nutkWallFunction":
        raise ReplayError("legacy_nutk requires nutkWallFunction")
    _positive(_float(timestep.get("target_max_co"), "timestep_policy.target_max_co"), "timestep_policy.target_max_co")
    _positive(_float(timestep.get("target_max_alpha_co"), "timestep_policy.target_max_alpha_co"), "timestep_policy.target_max_alpha_co")
    minimum_dt = _float(timestep.get("minimum_timestep_s", 0.0), "timestep_policy.minimum_timestep_s")
    if minimum_dt < 0.0:
        raise ReplayError("timestep_policy.minimum_timestep_s must be non-negative")


def load_replay_config(path: Path) -> dict:
    config = normalise_replay_config(load_json(path))
    version = _schema_version(config)
    section_id = config.get("section_id")
    if not isinstance(section_id, str) or not section_id:
        raise ReplayError("section_id is required")
    if not isinstance(config.get("openfoam_patch"), str) or not config["openfoam_patch"]:
        raise ReplayError("openfoam_patch is required")

    regional = config.get("regional", {})
    local = config.get("local", {})
    mapping = config.get("mapping", {})
    turbulence = config.get("turbulence", {})
    if not all(isinstance(item, dict) for item in (regional, local, mapping, turbulence)):
        raise ReplayError("regional, local, mapping and turbulence sections must be objects")
    _validate_policy_sections(config, version)

    _positive(_float(regional.get("dry_depth_m"), "regional.dry_depth_m"), "regional.dry_depth_m")
    _positive(_float(regional.get("eta_consistency_tolerance_m"), "regional.eta_consistency_tolerance_m"), "regional.eta_consistency_tolerance_m")
    normal = _vector(regional.get("inward_normal_xy"), 2, "regional.inward_normal_xy")
    tangent = _vector(regional.get("tangent_xy"), 2, "regional.tangent_xy")
    _unit(normal, "regional.inward_normal_xy")
    _unit(tangent, "regional.tangent_xy")
    _orthogonal(normal, tangent, "regional normal/tangent")
    _finite(_float(regional.get("vertical_datum_origin_m"), "regional.vertical_datum_origin_m"), "regional.vertical_datum_origin_m")

    inward = _vector(local.get("inward_axis"), 3, "local.inward_axis")
    span = _vector(local.get("span_axis"), 3, "local.span_axis")
    vertical = _vector(local.get("vertical_axis"), 3, "local.vertical_axis")
    for label, axis in (("local.inward_axis", inward), ("local.span_axis", span), ("local.vertical_axis", vertical)):
        _unit(axis, label)
    _orthogonal(inward, span, "local inward/span axes")
    _orthogonal(inward, vertical, "local inward/vertical axes")
    _orthogonal(span, vertical, "local span/vertical axes")
    if _dot(_cross(inward, span), vertical) <= 0.0:
        raise ReplayError("local axes must form a right-handed frame")
    if _float(local.get("span_max_m"), "local.span_max_m") <= _float(local.get("span_min_m"), "local.span_min_m"):
        raise ReplayError("local span extent must be positive")
    if _float(local.get("vertical_max_m"), "local.vertical_max_m") <= _float(local.get("vertical_min_m"), "local.vertical_min_m"):
        raise ReplayError("local vertical extent must be positive")
    if _int(local.get("span_cells"), "local.span_cells") <= 0 or _int(local.get("vertical_cells"), "local.vertical_cells") <= 0:
        raise ReplayError("local cell counts must be positive")
    _vector(local.get("origin_m"), 3, "local.origin_m")

    if mapping.get("spatial_interpolation") != "piecewise_linear_along_section":
        raise ReplayError("unsupported spatial interpolation mode")
    if mapping.get("outside_span") != "clamp":
        raise ReplayError("unsupported outside_span mode")
    if mapping.get("velocity_profile") != "depth_uniform":
        raise ReplayError("unsupported velocity profile")
    _finite(_float(mapping.get("vertical_velocity_m_per_s"), "mapping.vertical_velocity_m_per_s"), "mapping.vertical_velocity_m_per_s")
    if bool(mapping.get("preserve_discrete_discharge")) is not True:
        raise ReplayError("preserve_discrete_discharge must be true")

    intensity = _positive(_float(turbulence.get("intensity"), "turbulence.intensity"), "turbulence.intensity")
    if intensity >= 1.0:
        raise ReplayError("turbulence.intensity must be less than 1")
    _positive(_float(turbulence.get("length_scale_m"), "turbulence.length_scale_m"), "turbulence.length_scale_m")
    _positive(_float(turbulence.get("minimum_speed_m_per_s"), "turbulence.minimum_speed_m_per_s"), "turbulence.minimum_speed_m_per_s")
    return config


def validate_metadata(path: Path, section_id: str) -> tuple[dict, list[MetadataSample]]:
    metadata = load_json(path)
    if metadata.get("contract_version") != G3_CONTRACT_VERSION:
        raise ReplayError("unsupported coupling metadata contract_version")
    if metadata.get("section_id") != section_id:
        raise ReplayError("metadata section_id mismatch")
    if not isinstance(metadata.get("boundary_patch_name"), str) or not metadata["boundary_patch_name"]:
        raise ReplayError("metadata boundary_patch_name is required")
    if not isinstance(metadata.get("mesh_id"), str) or not metadata["mesh_id"]:
        raise ReplayError("metadata mesh_id is required")
    sample_count = _int(metadata.get("sample_count"), "metadata.sample_count")
    if sample_count <= 0:
        raise ReplayError("metadata sample_count must be positive")
    raw_samples = metadata.get("samples")
    if not isinstance(raw_samples, list) or len(raw_samples) != sample_count:
        raise ReplayError("metadata samples length must match sample_count")
    samples: list[MetadataSample] = []
    local_indices: set[int] = set()
    faces: set[int] = set()
    for index, item in enumerate(raw_samples):
        if not isinstance(item, dict):
            raise ReplayError("metadata sample must be an object")
        sample = MetadataSample(
            _int(item.get("local_index"), f"metadata.samples[{index}].local_index"),
            _int(item.get("cell"), f"metadata.samples[{index}].cell"),
            _int(item.get("face"), f"metadata.samples[{index}].face"),
            _float(item.get("x_m"), f"metadata.samples[{index}].x_m"),
            _float(item.get("y_m"), f"metadata.samples[{index}].y_m"),
        )
        if sample.local_index in local_indices:
            raise ReplayError("duplicate metadata local_index")
        if sample.face in faces:
            raise ReplayError("duplicate metadata face")
        local_indices.add(sample.local_index)
        faces.add(sample.face)
        samples.append(sample)
    return metadata, samples


def _read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise ReplayError(f"{path}: missing CSV header")
        return list(reader)


def validate_samples(path: Path, metadata_samples: list[MetadataSample], section_id: str, eta_tolerance: float) -> list[SampleRow]:
    rows = _read_csv(path)
    with path.open("r", encoding="utf-8", newline="") as handle:
        fieldnames = csv.DictReader(handle).fieldnames
    if fieldnames != REQUIRED_SAMPLE_COLUMNS:
        raise ReplayError("samples.csv columns do not match the G3 contract")
    by_local = {sample.local_index: sample for sample in metadata_samples}
    seen: set[tuple[float, int]] = set()
    parsed: list[SampleRow] = []
    for index, row in enumerate(rows):
        sample = SampleRow(
            _int(row["step"], f"samples[{index}].step"),
            _float(row["time"], f"samples[{index}].time"),
            row["section_id"],
            _int(row["local_index"], f"samples[{index}].local_index"),
            _int(row["cell"], f"samples[{index}].cell"),
            _int(row["face"], f"samples[{index}].face"),
            _float(row["x_m"], f"samples[{index}].x_m"),
            _float(row["y_m"], f"samples[{index}].y_m"),
            _float(row["depth"], f"samples[{index}].depth"),
            _float(row["momentum_x"], f"samples[{index}].momentum_x"),
            _float(row["momentum_y"], f"samples[{index}].momentum_y"),
            _float(row["bed_elevation"], f"samples[{index}].bed_elevation"),
            _float(row["free_surface_elevation"], f"samples[{index}].free_surface_elevation"),
        )
        if sample.section_id != section_id:
            raise ReplayError("samples.csv section_id mismatch")
        expected = by_local.get(sample.local_index)
        if expected is None:
            raise ReplayError("samples.csv references unknown local_index")
        if (sample.cell, sample.face) != (expected.cell, expected.face):
            raise ReplayError("metadata/sample cell or face mismatch")
        if abs(sample.x_m - expected.x_m) > eta_tolerance or abs(sample.y_m - expected.y_m) > eta_tolerance:
            raise ReplayError("metadata/sample coordinate mismatch")
        if sample.depth < -eta_tolerance:
            raise ReplayError("samples.csv depth is more negative than tolerance")
        if abs(sample.free_surface_elevation - (sample.bed_elevation + max(sample.depth, 0.0))) > eta_tolerance:
            raise ReplayError("samples.csv eta is inconsistent with bed + depth")
        key = (sample.time, sample.local_index)
        if key in seen:
            raise ReplayError("duplicate samples.csv time/local_index pair")
        seen.add(key)
        parsed.append(sample)

    by_time: dict[float, list[SampleRow]] = {}
    for row in parsed:
        by_time.setdefault(row.time, []).append(row)
    times = sorted(by_time)
    if times != list(by_time):
        raise ReplayError("samples.csv times must be monotonic")
    expected_count = len(metadata_samples)
    step_by_time: dict[float, int] = {}
    for time in times:
        group = by_time[time]
        if len(group) != expected_count:
            raise ReplayError("samples.csv must contain one row per metadata sample at every time")
        steps = {row.step for row in group}
        if len(steps) != 1:
            raise ReplayError("samples.csv step/time mapping is inconsistent")
        step = next(iter(steps))
        if time in step_by_time and step_by_time[time] != step:
            raise ReplayError("samples.csv duplicate time has inconsistent step")
        step_by_time[time] = step
    return parsed


def validate_history(path: Path, samples: list[SampleRow], section_id: str, tolerance: float) -> list[HistoryRow]:
    rows = _read_csv(path)
    with path.open("r", encoding="utf-8", newline="") as handle:
        fieldnames = csv.DictReader(handle).fieldnames
    if fieldnames != REQUIRED_HISTORY_COLUMNS:
        raise ReplayError("history.csv columns do not match the G3 contract")
    parsed = [
        HistoryRow(
            _int(row["step"], f"history[{index}].step"),
            _float(row["time"], f"history[{index}].time"),
            row["section_id"],
            _int(row["sample_count"], f"history[{index}].sample_count"),
            _float(row["maximum_depth"], f"history[{index}].maximum_depth"),
            _float(row["maximum_speed"], f"history[{index}].maximum_speed"),
        )
        for index, row in enumerate(rows)
    ]
    sample_groups: dict[float, list[SampleRow]] = {}
    for sample in samples:
        sample_groups.setdefault(sample.time, []).append(sample)
    if len(parsed) != len(sample_groups):
        raise ReplayError("history.csv must contain one row per sample time")
    for row in parsed:
        if row.section_id != section_id:
            raise ReplayError("history.csv section_id mismatch")
        group = sample_groups.get(row.time)
        if group is None:
            raise ReplayError("history.csv time is absent from samples.csv")
        if row.sample_count != len(group):
            raise ReplayError("history.csv sample_count mismatch")
        maximum_depth = max(max(sample.depth, 0.0) for sample in group)
        maximum_speed = 0.0
        for sample in group:
            depth = max(sample.depth, 0.0)
            if depth > 0.0:
                maximum_speed = max(maximum_speed, math.hypot(sample.momentum_x, sample.momentum_y) / depth)
        if abs(row.maximum_depth - maximum_depth) > tolerance:
            raise ReplayError("history.csv maximum_depth mismatch")
        if abs(row.maximum_speed - maximum_speed) > tolerance:
            raise ReplayError("history.csv maximum_speed mismatch")
    return parsed


def infer_order_and_supports(samples: list[MetadataSample], tangent_xy: Sequence[float], tolerance: float) -> tuple[list[MetadataSample], list[dict]]:
    decorated = []
    for sample in samples:
        projection = sample.x_m * tangent_xy[0] + sample.y_m * tangent_xy[1]
        decorated.append((projection, sample.local_index, sample.face, sample))
    decorated.sort()
    for left, right in zip(decorated, decorated[1:]):
        if abs(left[0] - right[0]) <= tolerance and left[1:] != right[1:]:
            raise ReplayError("duplicate projected regional coordinates cannot be resolved deterministically")
    ordered = [item[3] for item in decorated]
    projections = [item[0] for item in decorated]
    if len(projections) == 1:
        supports = [{
            "local_index": ordered[0].local_index,
            "projected_tangent_m": projections[0],
            "support_min_m": None,
            "support_max_m": None,
            "support_width_m": None,
            "derivation": "single_sample_maps_uniformly_to_local_span",
        }]
    else:
        edges = [projections[0] - 0.5 * (projections[1] - projections[0])]
        edges.extend(0.5 * (left + right) for left, right in zip(projections, projections[1:]))
        edges.append(projections[-1] + 0.5 * (projections[-1] - projections[-2]))
        supports = []
        for index, sample in enumerate(ordered):
            supports.append({
                "local_index": sample.local_index,
                "projected_tangent_m": projections[index],
                "support_min_m": edges[index],
                "support_max_m": edges[index + 1],
                "support_width_m": edges[index + 1] - edges[index],
                "derivation": "midpoint_voronoi_tangent_support",
            })
    return ordered, supports


def load_coupling_export(coupling_dir: Path, config: dict) -> CouplingExport:
    section_id = config["section_id"]
    metadata_path = coupling_dir / "metadata.json"
    samples_path = coupling_dir / "samples.csv"
    history_path = coupling_dir / "history.csv"
    for path in (metadata_path, samples_path, history_path):
        if not path.is_file():
            raise ReplayError(f"missing required coupling file: {path}")
    eta_tolerance = float(config["regional"]["eta_consistency_tolerance_m"])
    metadata, metadata_samples = validate_metadata(metadata_path, section_id)
    samples = validate_samples(samples_path, metadata_samples, section_id, eta_tolerance)
    history = validate_history(history_path, samples, section_id, eta_tolerance)
    ordered, supports = infer_order_and_supports(
        metadata_samples,
        config["regional"]["tangent_xy"],
        eta_tolerance,
    )
    times = sorted({sample.time for sample in samples})
    return CouplingExport(coupling_dir, metadata_path, samples_path, history_path, metadata, samples, history, ordered, supports, times)


def _fmt(value: float) -> str:
    if not math.isfinite(value):
        raise ReplayError("cannot format non-finite OpenFOAM value")
    if abs(value) < 5.0e-324:
        value = 0.0
    return f"{value:.17g}"


def _time_name(value: float) -> str:
    text = f"{value:.12g}"
    return "0" if text in {"-0", "0"} else text


def _time_tolerance(value: float) -> float:
    return max(1.0e-8, 1.0e-9 * max(1.0, abs(value)))


def _foam_header(class_name: str, object_name: str, location: str | None = None) -> str:
    location_line = f'    location    "{location}";\n' if location is not None else ""
    return (
        "/*--------------------------------*- C++ -*----------------------------------*\\\n"
        "  =========                 |\n"
        "  \\\\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox\n"
        "   \\\\    /   O peration     | Version:  11\n"
        "    \\\\  /    A nd           |\n"
        "     \\\\/     M anipulation  |\n"
        "\\*---------------------------------------------------------------------------*/\n"
        "FoamFile\n"
        "{\n"
        "    format      ascii;\n"
        f"    class       {class_name};\n"
        f"{location_line}"
        f"    object      {object_name};\n"
        "}\n"
        "// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //\n\n"
    )


def _write_foam_scalar_list(path: Path, values: Sequence[float]) -> None:
    lines = ["// Data on points", str(len(values)), "("]
    lines.extend(_fmt(value) for value in values)
    lines.extend([")", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def _write_foam_vector_list(path: Path, values: Sequence[Sequence[float]]) -> None:
    lines = ["// Data on points", str(len(values)), "("]
    lines.extend(f"({_fmt(v[0])} {_fmt(v[1])} {_fmt(v[2])})" for v in values)
    lines.extend([")", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def _write_foam_points(path: Path, points: Sequence[Sequence[float]]) -> None:
    lines = ["// Points", str(len(points)), "("]
    lines.extend(f"({_fmt(p[0])} {_fmt(p[1])} {_fmt(p[2])})" for p in points)
    lines.extend([")", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def _rows_by_time(samples: list[SampleRow]) -> dict[float, dict[int, SampleRow]]:
    groups: dict[float, dict[int, SampleRow]] = {}
    for sample in samples:
        groups.setdefault(sample.time, {})[sample.local_index] = sample
    return groups


def _support_edges(supports: list[dict], local_span_min: float, local_span_max: float) -> list[float]:
    if len(supports) == 1 or supports[0]["support_min_m"] is None:
        return [local_span_min, local_span_max]
    widths = [float(item["support_width_m"]) for item in supports]
    total = sum(widths)
    if total <= 0.0:
        raise ReplayError("regional support widths must sum to a positive value")
    edges = [local_span_min]
    span = local_span_max - local_span_min
    running = local_span_min
    for width in widths[:-1]:
        running += span * width / total
        edges.append(running)
    edges.append(local_span_max)
    return edges


def _support_for_span(span_position: float, edges: Sequence[float]) -> int:
    if span_position <= edges[0]:
        return 0
    for index in range(len(edges) - 1):
        if edges[index] <= span_position <= edges[index + 1]:
            return min(index, len(edges) - 2)
    return len(edges) - 2


def _face_fraction(free_surface: float, z_min: float, z_max: float) -> float:
    if free_surface <= z_min:
        return 0.0
    if free_surface >= z_max:
        return 1.0
    return (free_surface - z_min) / (z_max - z_min)


def convert_boundary_data(coupling_dir: Path, config_path: Path, output_root: Path, overwrite: bool = False) -> dict:
    config = load_replay_config(config_path)
    coupling = load_coupling_export(coupling_dir, config)
    if output_root.exists():
        if not overwrite:
            raise ReplayError(f"output root already exists: {output_root}")
    output_root.mkdir(parents=True, exist_ok=True)
    inlet_dir = output_root / "constant" / "boundaryData" / config["openfoam_patch"]
    inlet_dir.mkdir(parents=True, exist_ok=True)

    local = config["local"]
    regional = config["regional"]
    mapping = config["mapping"]
    span_min = float(local["span_min_m"])
    span_max = float(local["span_max_m"])
    z_min = float(local["vertical_min_m"])
    z_max = float(local["vertical_max_m"])
    span_cells = int(local["span_cells"])
    vertical_cells = int(local["vertical_cells"])
    span_dx = (span_max - span_min) / span_cells
    z_dz = (z_max - z_min) / vertical_cells
    origin = tuple(float(value) for value in local["origin_m"])
    span_axis = tuple(float(value) for value in local["span_axis"])
    vertical_axis = tuple(float(value) for value in local["vertical_axis"])
    inward_axis = tuple(float(value) for value in local["inward_axis"])
    normal_xy = tuple(float(value) for value in regional["inward_normal_xy"])
    tangent_xy = tuple(float(value) for value in regional["tangent_xy"])
    dry_depth = float(regional["dry_depth_m"])
    vertical_datum = float(regional["vertical_datum_origin_m"])
    vertical_velocity = float(mapping["vertical_velocity_m_per_s"])
    eta_tolerance = float(regional["eta_consistency_tolerance_m"])

    support_edges = _support_edges(coupling.supports, span_min, span_max)
    points: list[tuple[float, float, float]] = []
    point_supports: list[int] = []
    point_vertical_bounds: list[tuple[float, float]] = []
    for i_span in range(span_cells):
        span_center = span_min + (i_span + 0.5) * span_dx
        support_index = _support_for_span(span_center, support_edges)
        for i_z in range(vertical_cells):
            z0 = z_min + i_z * z_dz
            z1 = z0 + z_dz
            zc = 0.5 * (z0 + z1)
            points.append((
                origin[0] + span_center * span_axis[0] + zc * vertical_axis[0],
                origin[1] + span_center * span_axis[1] + zc * vertical_axis[1],
                origin[2] + span_center * span_axis[2] + zc * vertical_axis[2],
            ))
            point_supports.append(support_index)
            point_vertical_bounds.append((z0, z1))
    _write_foam_points(inlet_dir / "points", points)

    rows_by_time = _rows_by_time(coupling.samples)
    diagnostics: list[dict[str, object]] = []
    maximum_speed = 0.0
    for time in coupling.times:
        time_dir = inlet_dir / _time_name(time)
        time_dir.mkdir(parents=True, exist_ok=True)
        rows = rows_by_time[time]
        alpha_values: list[float] = []
        u_values: list[tuple[float, float, float]] = []
        per_support: dict[int, dict[str, float]] = {}
        for support_index, support in enumerate(coupling.supports):
            sample = rows[int(support["local_index"])]
            h = max(sample.depth, 0.0)
            qn = sample.momentum_x * normal_xy[0] + sample.momentum_y * normal_xy[1]
            qt = sample.momentum_x * tangent_xy[0] + sample.momentum_y * tangent_xy[1]
            wet = h > dry_depth
            u_n = qn / h if wet else 0.0
            u_t = qt / h if wet else 0.0
            support_width = support_edges[support_index + 1] - support_edges[support_index]
            reconstructed_depth = 0.0
            for point_support, (z0, z1) in zip(point_supports, point_vertical_bounds):
                if point_support != support_index:
                    continue
                if not wet:
                    fraction = 0.0
                else:
                    free_surface = sample.free_surface_elevation - vertical_datum
                    bed = sample.bed_elevation - vertical_datum
                    fraction = _face_fraction(free_surface, z0, z1)
                    if z1 <= max(z_min, bed):
                        fraction = 0.0
                reconstructed_depth += max(0.0, min(1.0, fraction)) * z_dz
            if wet and reconstructed_depth <= 0.0:
                raise ReplayError("wet support reconstructed zero OpenFOAM water depth")
            scale = h / reconstructed_depth if wet and reconstructed_depth > 0.0 else 0.0
            per_support[support_index] = {
                "local_index": int(support["local_index"]),
                "h": h,
                "qn": qn,
                "qt": qt,
                "u_n": u_n,
                "u_t": u_t,
                "scale": scale,
                "support_width": support_width,
                "target_qn": qn * support_width,
                "target_qt": qt * support_width,
                "reconstructed_qn": 0.0,
                "reconstructed_qt": 0.0,
            }
        for support_index, (z0, z1) in zip(point_supports, point_vertical_bounds):
            support = coupling.supports[support_index]
            sample = rows[int(support["local_index"])]
            h = max(sample.depth, 0.0)
            wet = h > dry_depth
            if wet:
                free_surface = sample.free_surface_elevation - vertical_datum
                bed = sample.bed_elevation - vertical_datum
                alpha = _face_fraction(free_surface, z0, z1)
                if z1 <= max(z_min, bed):
                    alpha = 0.0
                alpha = max(0.0, min(1.0, alpha))
                data = per_support[support_index]
                u_n = float(data["u_n"]) * float(data["scale"])
                u_t = float(data["u_t"]) * float(data["scale"])
                velocity = (
                    u_n * inward_axis[0] + u_t * span_axis[0] + vertical_velocity * vertical_axis[0],
                    u_n * inward_axis[1] + u_t * span_axis[1] + vertical_velocity * vertical_axis[1],
                    u_n * inward_axis[2] + u_t * span_axis[2] + vertical_velocity * vertical_axis[2],
                )
            else:
                alpha = 0.0
                velocity = (0.0, 0.0, 0.0)
            alpha_values.append(alpha)
            u_values.append(velocity)
            cell_width = span_dx
            face_height = z_dz
            per_support[support_index]["reconstructed_qn"] += alpha * velocity[0] * cell_width * face_height / max(1.0e-300, cell_width)
            per_support[support_index]["reconstructed_qt"] += alpha * velocity[1] * cell_width * face_height / max(1.0e-300, cell_width)
            maximum_speed = max(maximum_speed, math.sqrt(sum(component * component for component in velocity)))
        for support_index, data in per_support.items():
            diagnostics.append({
                "time": time,
                "support_index": support_index,
                "local_index": data["local_index"],
                "target_normal_discharge": data["target_qn"],
                "reconstructed_normal_discharge": data["reconstructed_qn"] * data["support_width"],
                "normal_discharge_residual": data["reconstructed_qn"] * data["support_width"] - data["target_qn"],
                "target_tangential_discharge": data["target_qt"],
                "reconstructed_tangential_discharge": data["reconstructed_qt"] * data["support_width"],
                "tangential_discharge_residual": data["reconstructed_qt"] * data["support_width"] - data["target_qt"],
                "minimum_alpha": min(alpha_values),
                "maximum_alpha": max(alpha_values),
                "minimum_mapped_velocity": min(math.sqrt(sum(v * v for v in value)) for value in u_values),
                "maximum_mapped_velocity": max(math.sqrt(sum(v * v for v in value)) for value in u_values),
            })
        _write_foam_vector_list(time_dir / "U", u_values)
        _write_foam_scalar_list(time_dir / "alpha.water", alpha_values)

    diag_path = output_root / "replay_diagnostics.csv"
    with diag_path.open("w", encoding="utf-8", newline="") as handle:
        fieldnames = [
            "time",
            "support_index",
            "local_index",
            "target_normal_discharge",
            "reconstructed_normal_discharge",
            "normal_discharge_residual",
            "target_tangential_discharge",
            "reconstructed_tangential_discharge",
            "tangential_discharge_residual",
            "minimum_alpha",
            "maximum_alpha",
            "minimum_mapped_velocity",
            "maximum_mapped_velocity",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(diagnostics)

    conversion = {
        "schema": REPLAY_SCHEMA,
        "source_coupling_paths": {
            "metadata_json": str(coupling.metadata_path),
            "samples_csv": str(coupling.samples_path),
            "history_csv": str(coupling.history_path),
        },
        "source_sha256": {
            "metadata_json": sha256(coupling.metadata_path),
            "samples_csv": sha256(coupling.samples_path),
            "history_csv": sha256(coupling.history_path),
        },
        "source_contract_version": G3_CONTRACT_VERSION,
        "section_id": config["section_id"],
        "source_mesh_id": coupling.metadata["mesh_id"],
        "mapping_configuration": config,
        "regional_sample_ordering": [sample.local_index for sample in coupling.ordered_samples],
        "regional_support_widths": coupling.supports,
        "local_inlet_dimensions": {
            "span_min_m": span_min,
            "span_max_m": span_max,
            "vertical_min_m": z_min,
            "vertical_max_m": z_max,
            "span_cells": span_cells,
            "vertical_cells": vertical_cells,
        },
        "local_inlet_face_count": len(points),
        "local_point_ordering": "span-major-then-vertical",
        "time_range": [min(coupling.times), max(coupling.times)],
        "snapshot_count": len(coupling.times),
        "dry_depth_threshold_m": dry_depth,
        "eta_tolerance_m": eta_tolerance,
        "discharge_tolerances": {"absolute": 1.0e-10, "relative": 1.0e-8},
        "turbulence_inputs": config["turbulence"],
        "generated_field_names": ["U", "alpha.water"],
        "conversion_timestamp": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "converter_version": CONVERTER_VERSION,
        "maximum_boundary_speed_m_per_s": maximum_speed,
    }
    (output_root / "replay_conversion.json").write_text(json.dumps(conversion, indent=2) + "\n", encoding="utf-8")
    return conversion


def _dict_header(object_name: str, location: str | None = None) -> str:
    return _foam_header("dictionary", object_name, location)


def _field_file(class_name: str, object_name: str, dimensions: str, internal: str, boundary: dict[str, str]) -> str:
    entries = []
    for patch, body in boundary.items():
        entries.append(f"    {patch}\n    {{\n{body.rstrip()}\n    }}")
    return (
        _foam_header(class_name, object_name, "0")
        + f"dimensions      {dimensions};\n\n"
        + f"internalField   {internal};\n\n"
        + "boundaryField\n{\n"
        + "\n".join(entries)
        + "\n}\n\n// ************************************************************************* //\n"
    )


def _patches(has_barrier: bool) -> list[str]:
    patches = ["inlet", "outlet", "sideLeft", "sideRight", "atmosphere", "terrain"]
    if has_barrier:
        patches.append("barrier")
    return patches


def _wall_patches(has_barrier: bool) -> list[str]:
    patches = ["terrain"]
    if has_barrier:
        patches.append("barrier")
    return patches


def _make_boundary(patches: Iterable[str], inlet: str, outlet: str, sides: str, atmosphere: str, wall: str) -> dict[str, str]:
    result = {}
    for patch in patches:
        if patch == "inlet":
            result[patch] = inlet
        elif patch == "outlet":
            result[patch] = outlet
        elif patch in {"sideLeft", "sideRight"}:
            result[patch] = sides
        elif patch == "atmosphere":
            result[patch] = atmosphere
        else:
            result[patch] = wall
    return result


def _block_mesh_no_defence(length: float, span: float, height: float, nx: int, ny: int, nz: int, side_patch_type: str = "symmetryPlane") -> str:
    return f"""{_dict_header("blockMeshDict", "system")}scale 1;

vertices
(
    (0 0 0)
    ({_fmt(length)} 0 0)
    ({_fmt(length)} {_fmt(span)} 0)
    (0 {_fmt(span)} 0)
    (0 0 {_fmt(height)})
    ({_fmt(length)} 0 {_fmt(height)})
    ({_fmt(length)} {_fmt(span)} {_fmt(height)})
    (0 {_fmt(span)} {_fmt(height)})
);

blocks
(
    hex (0 1 2 3 4 5 6 7) ({nx} {ny} {nz}) simpleGrading (1 1 1)
);

edges
(
);

boundary
(
    inlet {{ type patch; faces ((0 4 7 3)); }}
    outlet {{ type patch; faces ((1 2 6 5)); }}
    sideLeft {{ type {side_patch_type}; faces ((0 1 5 4)); }}
    sideRight {{ type {side_patch_type}; faces ((3 7 6 2)); }}
    atmosphere {{ type patch; faces ((4 5 6 7)); }}
    terrain {{ type wall; faces ((0 3 2 1)); }}
);

mergePatchPairs
(
);
"""


def _block_mesh_barrier(
    length: float,
    span: float,
    height: float,
    nx: int,
    ny: int,
    nz: int,
    position: float = 0.90,
    thickness: float = 0.06,
    barrier_height: float = 0.24,
    side_patch_type: str = "symmetryPlane",
) -> str:
    xb0 = max(0.05 * length, min(position, 0.95 * length))
    xb1 = max(xb0 + 1.0e-6, min(xb0 + thickness, 0.98 * length))
    hb = max(1.0e-6, min(barrier_height, 0.95 * height))
    xs = [0.0, xb0, xb1, length]
    zs = [0.0, hb, height]
    verts = []
    for z in zs:
        for y in [0.0, span]:
            for x in xs:
                verts.append((x, y, z))

    def idx(ix: int, iy: int, iz: int) -> int:
        return iz * 8 + iy * 4 + ix

    def hexv(ix0: int, ix1: int, iz0: int, iz1: int) -> str:
        return f"({idx(ix0,0,iz0)} {idx(ix1,0,iz0)} {idx(ix1,1,iz0)} {idx(ix0,1,iz0)} {idx(ix0,0,iz1)} {idx(ix1,0,iz1)} {idx(ix1,1,iz1)} {idx(ix0,1,iz1)})"

    blocks = [
        (0, 1, 0, 1, max(2, int(nx * xb0 / length)), ny, max(2, int(nz * hb / height))),
        (2, 3, 0, 1, max(2, int(nx * (length - xb1) / length)), ny, max(2, int(nz * hb / height))),
        (0, 1, 1, 2, max(2, int(nx * xb0 / length)), ny, max(2, int(nz * (height - hb) / height))),
        (1, 2, 1, 2, 2, ny, max(2, int(nz * (height - hb) / height))),
        (2, 3, 1, 2, max(2, int(nx * (length - xb1) / length)), ny, max(2, int(nz * (height - hb) / height))),
    ]
    vertices = "\n".join(f"    ({_fmt(x)} {_fmt(y)} {_fmt(z)})" for x, y, z in verts)
    block_lines = "\n".join(f"    hex {hexv(a,b,c,d)} ({cx} {cy} {cz}) simpleGrading (1 1 1)" for a, b, c, d, cx, cy, cz in blocks)
    return f"""{_dict_header("blockMeshDict", "system")}scale 1;

vertices
(
{vertices}
);

blocks
(
{block_lines}
);

edges
(
);

boundary
(
    inlet
    {{
        type patch;
        faces
        (
            ({idx(0,0,0)} {idx(0,0,1)} {idx(0,1,1)} {idx(0,1,0)})
            ({idx(0,0,1)} {idx(0,0,2)} {idx(0,1,2)} {idx(0,1,1)})
        );
    }}
    outlet
    {{
        type patch;
        faces
        (
            ({idx(3,0,0)} {idx(3,1,0)} {idx(3,1,1)} {idx(3,0,1)})
            ({idx(3,0,1)} {idx(3,1,1)} {idx(3,1,2)} {idx(3,0,2)})
        );
    }}
    sideLeft
    {{
        type {side_patch_type};
        faces
        (
            ({idx(0,0,0)} {idx(1,0,0)} {idx(1,0,1)} {idx(0,0,1)})
            ({idx(2,0,0)} {idx(2,0,1)} {idx(3,0,1)} {idx(3,0,0)})
            ({idx(0,0,1)} {idx(1,0,1)} {idx(1,0,2)} {idx(0,0,2)})
            ({idx(1,0,1)} {idx(2,0,1)} {idx(2,0,2)} {idx(1,0,2)})
            ({idx(2,0,1)} {idx(3,0,1)} {idx(3,0,2)} {idx(2,0,2)})
        );
    }}
    sideRight
    {{
        type {side_patch_type};
        faces
        (
            ({idx(0,1,0)} {idx(0,1,1)} {idx(1,1,1)} {idx(1,1,0)})
            ({idx(2,1,0)} {idx(3,1,0)} {idx(3,1,1)} {idx(2,1,1)})
            ({idx(0,1,1)} {idx(0,1,2)} {idx(1,1,2)} {idx(1,1,1)})
            ({idx(1,1,1)} {idx(1,1,2)} {idx(2,1,2)} {idx(2,1,1)})
            ({idx(2,1,1)} {idx(3,1,1)} {idx(3,1,2)} {idx(2,1,2)})
        );
    }}
    atmosphere
    {{
        type patch;
        faces
        (
            ({idx(0,0,2)} {idx(1,0,2)} {idx(1,1,2)} {idx(0,1,2)})
            ({idx(1,0,2)} {idx(2,0,2)} {idx(2,1,2)} {idx(1,1,2)})
            ({idx(2,0,2)} {idx(3,0,2)} {idx(3,1,2)} {idx(2,1,2)})
        );
    }}
    terrain
    {{
        type wall;
        faces
        (
            ({idx(0,0,0)} {idx(0,1,0)} {idx(1,1,0)} {idx(1,0,0)})
            ({idx(2,0,0)} {idx(3,0,0)} {idx(3,1,0)} {idx(2,1,0)})
        );
    }}
    barrier
    {{
        type wall;
        faces
        (
            ({idx(1,0,0)} {idx(1,1,0)} {idx(1,1,1)} {idx(1,0,1)})
            ({idx(2,0,0)} {idx(2,0,1)} {idx(2,1,1)} {idx(2,1,0)})
            ({idx(1,0,1)} {idx(2,0,1)} {idx(2,1,1)} {idx(1,1,1)})
        );
    }}
);

mergePatchPairs
(
);
""".replace("            ['(", "            (").replace(")']", ")")


def _config_number(section: dict, key: str, default: float, label: str) -> float:
    if key not in section:
        return default
    return _positive(_float(section[key], label), label)


def _config_int(section: dict, key: str, default: int, label: str) -> int:
    if key not in section:
        return default
    value = _int(section[key], label)
    if value <= 0:
        raise ReplayError(f"{label} must be positive")
    return value


def _config_fraction(section: dict, key: str, default: float, label: str) -> float:
    value = _config_number(section, key, default, label)
    if value > 1.0:
        raise ReplayError(f"{label} must be no greater than 1")
    return value


def turbulence_values(config: dict, replay_conversion: dict) -> tuple[float, float]:
    turbulence = config["turbulence"]
    speed = max(float(turbulence["minimum_speed_m_per_s"]), float(replay_conversion.get("maximum_boundary_speed_m_per_s", 0.0)))
    intensity = float(turbulence["intensity"])
    length_scale = float(turbulence["length_scale_m"])
    cmu = 0.09
    k = 1.5 * (intensity * speed) ** 2
    omega = math.sqrt(k) / ((cmu ** 0.25) * length_scale)
    if not (math.isfinite(k) and math.isfinite(omega) and k > 0.0 and omega > 0.0):
        raise ReplayError("computed turbulence values are invalid")
    return k, omega


def boundary_mode(config: dict) -> str:
    return str(config["boundary_policy"]["mode"])


def is_production_policy(config: dict) -> bool:
    return boundary_mode(config) == "open_ocean_damped"


def _cell_dimensions(length: float, span: float, height: float, nx: int, ny: int, nz: int) -> dict[str, float]:
    return {
        "streamwise_m": length / nx,
        "span_m": span / ny,
        "vertical_m": height / nz,
        "minimum_m": min(length / nx, span / ny, height / nz),
    }


def damping_zones(config: dict, length: float, span: float, height: float, nx: int, ny: int, nz: int, maximum_speed: float, maximum_depth: float, barrier: dict | None) -> dict:
    policy = config["damping_policy"]
    if not bool(policy.get("enabled")):
        return {"enabled": False, "zones": [], "limitations": ["damping disabled for legacy symmetry_test mode"]}
    dx = length / nx
    dy = span / ny
    outlet_width = max(float(policy["outlet_width_fraction"]) * length, 6.0 * dx)
    lateral_width = max(float(policy["lateral_width_fraction"]) * span, 6.0 * dy)
    if outlet_width / dx < 6.0 or lateral_width / dy < 6.0:
        raise ReplayError("damping regions must span at least six cells")
    if outlet_width > 0.20 * length:
        raise ReplayError("outlet damping width would leave less than the required undamped core")
    if 2.0 * lateral_width > 0.40 * span:
        raise ReplayError("lateral damping widths would leave less than the required undamped core")
    outlet_cells = outlet_width / dx
    lateral_cells = lateral_width / dy
    target_e_folds = float(policy["target_e_folds"])
    c_char = max(maximum_speed + math.sqrt(9.80665 * max(maximum_depth, 1.0e-9)), 1.0e-9)

    def zone(name: str, width: float, direction: tuple[float, float, float], origin: tuple[float, float, float], cells: float, core_width: float) -> dict:
        residence = width / c_char
        lam = target_e_folds / residence
        if not all(math.isfinite(value) and value > 0.0 for value in (width, cells, residence, lam)):
            raise ReplayError(f"{name} damping derivation is invalid")
        return {
            "name": name,
            "model": "isotropicDamping",
            "profile": "halfCosineRamp",
            "origin": list(origin),
            "direction": list(direction),
            "width_m": width,
            "cell_count": cells,
            "characteristic_speed_m_per_s": c_char,
            "residence_time_s": residence,
            "target_e_folds": target_e_folds,
            "lambda_per_s": lam,
            "undamped_core_width_m": core_width,
        }

    zones = [
        zone("outlet", outlet_width, (1.0, 0.0, 0.0), (length - outlet_width, 0.0, 0.0), outlet_cells, length - outlet_width),
        zone("sideLeft", lateral_width, (0.0, -1.0, 0.0), (0.0, lateral_width, 0.0), lateral_cells, span - 2.0 * lateral_width),
        zone("sideRight", lateral_width, (0.0, 1.0, 0.0), (0.0, span - lateral_width, 0.0), lateral_cells, span - 2.0 * lateral_width),
    ]
    if outlet_width >= 0.40 * length:
        raise ReplayError("outlet damping leaves less than 60% undamped streamwise core")
    if 2.0 * lateral_width >= 0.40 * span:
        raise ReplayError("lateral damping leaves less than 60% undamped span core")
    if barrier is not None:
        barrier_end = float(barrier["streamwise_position_m"]) + float(barrier["thickness_m"])
        if barrier_end >= length - outlet_width:
            raise ReplayError("outlet damping region overlaps the barrier streamwise extent")
    return {
        "enabled": True,
        "model": "isotropicDamping",
        "profile": "halfCosineRamp",
        "value": [0.0, 0.0, 0.0],
        "zones": zones,
        "damping_does_not_overlap_inlet": True,
        "damping_does_not_overlap_barrier": True,
        "damping_does_not_overlap_primary_comparison_probes": True,
        "limitations": ["The side damping policy is a baseline open-ocean numerical treatment, not calibration to observed harbour reflection."],
    }


def _fv_models_text(damping: dict) -> str:
    if not damping.get("enabled"):
        return _dict_header("fvModels", "constant") + "\n"
    entries = []
    for zone in damping["zones"]:
        ox, oy, oz = zone["origin"]
        dx, dy, dz = zone["direction"]
        entries.append(f"""    {zone['name']}Damping
    {{
        type            isotropicDamping;
        libs            ("libwaves.so");
        origin          ({_fmt(ox)} {_fmt(oy)} {_fmt(oz)});
        direction       ({_fmt(dx)} {_fmt(dy)} {_fmt(dz)});
        scale
        {{
            type        halfCosineRamp;
            start       0;
            duration    {_fmt(float(zone['width_m']))};
        }}
        value           (0 0 0);
        lambda          {_fmt(float(zone['lambda_per_s']))};
    }}""")
    return _dict_header("fvModels", "constant") + "\n".join(entries) + "\n\n// ************************************************************************* //\n"


def _field_type(body: str) -> str:
    for line in body.splitlines():
        stripped = line.strip()
        if stripped.startswith("type"):
            return stripped.split()[1].rstrip(";")
    return "unknown"


def _boundary_record(patches: Iterable[str], mesh_types: dict[str, str], field_boundaries: dict[str, dict[str, str]], config: dict, damping: dict, k_value: float, omega_value: float) -> dict:
    return {
        "schema": {"name": "tsunami.openfoam_boundary_policy", "version": "1.0.0"},
        "replay_schema": config["schema"],
        "policy_version": config["boundary_policy"]["policy_version"],
        "mode": boundary_mode(config),
        "patch_names": list(patches),
        "mesh_patch_types": mesh_types,
        "field_boundary_types": {
            field: {patch: _field_type(body) for patch, body in boundaries.items()}
            for field, boundaries in field_boundaries.items()
        },
        "reference_pressure": {"p_rgh_p0": 0.0, "unit": "Pa relative gauge"},
        "ambient_turbulence_values": {"k": k_value, "omega": omega_value},
        "damping_configuration": damping,
        "implementation_authority": {
            "openfoam": "OpenFOAM Foundation 11",
            "image": "docker.io/openfoam/openfoam11-paraview510:11",
            "selected_patterns": [
                "pressureInletOutletVelocity",
                "prghTotalPressure",
                "variableHeightFlowRate",
                "inletOutlet",
                "isotropicDamping",
            ],
        },
        "limitations": [
            "Open-ocean damping is a numerical baseline policy, not observational calibration.",
            "The coupling inlet remains one-way timeVaryingMappedFixedValue reconstruction.",
        ],
    }


def _wall_function_record(config: dict, patches: list[str], field_boundaries: dict[str, dict[str, str]]) -> dict:
    policy = config["wall_function_policy"]
    return {
        "schema": {"name": "tsunami.openfoam_wall_function_policy", "version": "1.0.0"},
        "replay_schema": config["schema"],
        "mode": policy["mode"],
        "expected_wall_patches": patches,
        "field_types": {
            "k": {patch: _field_type(field_boundaries["k"][patch]) for patch in patches},
            "omega": {patch: _field_type(field_boundaries["omega"][patch]) for patch in patches},
            "nut": {patch: _field_type(field_boundaries["nut"][patch]) for patch in patches},
        },
        "rationale": "The production baseline uses a continuous Spalding-law nut wall function so the theoretical model does not assume every future mesh lies wholly in the logarithmic layer.",
        "limitations": ["This does not eliminate mesh dependence; wall-function convergence remains post-G6."],
    }


def _timestep_policy_record(config: dict, local_case: dict, cells: dict[str, float], replay_conversion: dict, observed: dict | None = None) -> dict:
    timestep = config["timestep_policy"]
    derivation = local_case.get("timestep_derivation", {})
    maximum_timestep = float(local_case.get("maximum_timestep_s", 0.005))
    observed = observed or {}
    dx_min = float(cells["minimum_m"])
    nu_eff_max = float(observed.get("maximum_effective_kinematic_viscosity_m2_per_s", 1.0e-6))
    diffusive = dx_min * dx_min / (2.0 * 3.0 * max(nu_eff_max, 1.0e-300))
    max_observed_dt = float(observed.get("maximum_observed_timestep_s", maximum_timestep))
    return {
        "schema": {"name": "tsunami.openfoam_timestep_policy", "version": "1.0.0"},
        "replay_schema": config["schema"],
        "adjustTimeStep": bool(timestep.get("adjust_time_step", True)),
        "maxCo": float(timestep["target_max_co"]),
        "maxAlphaCo": float(timestep["target_max_alpha_co"]),
        "maxDeltaT": maximum_timestep,
        "minimum_accepted_timestep_s": float(timestep.get("minimum_timestep_s", 0.0)),
        "cell_dimensions_m": cells,
        "maximum_mapped_speed_m_per_s": float(derivation.get("maximum_mapped_inlet_speed_m_per_s", replay_conversion.get("maximum_boundary_speed_m_per_s", 0.0))),
        "maximum_reconstructed_depth_m": float(derivation.get("maximum_reconstructed_water_depth_m", 0.0)),
        "gravity_wave_speed_m_per_s": float(derivation.get("gravity_wave_speed_m_per_s", 0.0)),
        "characteristic_speed_m_per_s": float(derivation.get("derived_characteristic_speed_m_per_s", 0.0)),
        "derived_pre_run_timestep_cap_s": float(derivation.get("selected_maximum_timestep_s", maximum_timestep)),
        "observed_timestep_range_s": observed.get("observed_timestep_range_s", [None, None]),
        "observed_maximum_Co": observed.get("observed_maximum_Co"),
        "observed_maximum_alpha_Co": observed.get("observed_maximum_alpha_Co"),
        "repository_controlled_constraints": [
            "pre-run gravity/advective estimate",
            "configured maxDeltaT",
            "configured maxCo",
            "configured maxAlphaCo",
            "minimum-timestep failure threshold",
            "post-run acceptance",
        ],
        "openfoam_controlled_constraints": [
            "internal Co calculation",
            "internal interface Co calculation",
            "immediate timestep reduction",
            "damped timestep increase",
            "fvModel maxDeltaT contribution",
        ],
        "diffusion_disposition": "Momentum diffusion is assembled implicitly in the adopted OpenFOAM stress-divergence path; an explicit diffusion-stability limit is not a governing timestep restriction for the adopted baseline discretisation.",
        "diagnostic_viscous_timescale_s": diffusive,
        "diagnostic_diffusive_margin": diffusive / max(max_observed_dt, 1.0e-300),
        "rejected_step_disposition": "Foundation 11 performs pre-emptive adaptive timestep reduction from current/previous Courant information; the adopted generic foamRun/incompressibleVoF baseline does not expose exact rollback and re-solve of an already failed physical timestep.",
        "g6_disposition": {
            "baseline_theoretical_timestep_control": "implemented and accepted",
            "exact_rollback_retry_supervisor": "optional post-G6 robustness extension",
            "formal_timestep_convergence": "post-G6 verification",
        },
    }


def generate_case(replay_root: Path, config_path: Path, output_root: Path, variant: str, overwrite: bool = False) -> dict:
    if variant not in {"no_defence", "simple_rigid_barrier"}:
        raise ReplayError("unsupported case variant")
    if output_root.exists():
        if not overwrite:
            raise ReplayError(f"case output already exists: {output_root}")
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True)
    config = load_replay_config(config_path)
    replay_conversion = load_json(replay_root / "replay_conversion.json")
    boundary_start = float(replay_conversion["time_range"][0])
    boundary_end = float(replay_conversion["time_range"][1])
    has_barrier = variant == "simple_rigid_barrier"
    local = config["local"]
    local_case = config.get("local_case", {})
    barrier_config = config.get("barrier", {})
    if not isinstance(local_case, dict) or not isinstance(barrier_config, dict):
        raise ReplayError("local_case and barrier sections must be objects when present")
    length = _config_number(local_case, "streamwise_length_m", 2.0, "local_case.streamwise_length_m")
    span = float(local["span_max_m"]) - float(local["span_min_m"])
    height = float(local["vertical_max_m"]) - float(local["vertical_min_m"])
    nx = _config_int(local_case, "streamwise_cells", 30, "local_case.streamwise_cells")
    ny = _config_int(local_case, "span_cells", max(4, int(local["span_cells"])), "local_case.span_cells")
    nz = _config_int(local_case, "vertical_cells", max(10, int(local["vertical_cells"])), "local_case.vertical_cells")
    end_time = _config_number(local_case, "end_time_s", float(replay_conversion["time_range"][1]), "local_case.end_time_s")
    boundary_tolerance = _time_tolerance(boundary_end)
    if boundary_end + boundary_tolerance < end_time:
        raise ReplayError("boundaryData maximum time is shorter than local_case.end_time_s")
    replay_window = config.get("replay_window", {})
    peak_shifted_time: float | None = None
    if isinstance(replay_window, dict):
        if replay_window.get("shifted_duration_s") is not None:
            shifted_duration = _float(replay_window["shifted_duration_s"], "replay_window.shifted_duration_s")
            if abs(end_time - shifted_duration) > _time_tolerance(shifted_duration):
                raise ReplayError("production OpenFOAM end time must equal selected replay shifted duration")
        if replay_window.get("peak_shifted_time_s") is not None:
            peak_shifted_time = _float(replay_window["peak_shifted_time_s"], "replay_window.peak_shifted_time_s")
            if end_time + _time_tolerance(end_time) < peak_shifted_time:
                raise ReplayError("production OpenFOAM end time is shorter than the major replay peak time")
    maximum_timestep = _config_number(local_case, "maximum_timestep_s", 0.005, "local_case.maximum_timestep_s")
    initial_timestep = min(maximum_timestep, _config_number(local_case, "initial_timestep_s", 0.002, "local_case.initial_timestep_s"))
    timestep_policy = config["timestep_policy"]
    maximum_courant = _config_number(timestep_policy, "target_max_co", _config_number(local_case, "maximum_courant_number", 0.5, "local_case.maximum_courant_number"), "timestep_policy.target_max_co")
    maximum_alpha_courant = _config_number(timestep_policy, "target_max_alpha_co", _config_number(local_case, "maximum_alpha_courant_number", 0.5, "local_case.maximum_alpha_courant_number"), "timestep_policy.target_max_alpha_co")
    minimum_timestep = _float(timestep_policy.get("minimum_timestep_s", 0.0), "timestep_policy.minimum_timestep_s")
    write_interval = _config_number(local_case, "write_interval_s", max(end_time / 2.0, 0.01), "local_case.write_interval_s")
    initial_water_level = min(height * 0.5, _config_number(local_case, "initial_water_level_m", 0.18, "local_case.initial_water_level_m"))
    alpha_tolerance = _config_number(local_case, "alpha_tolerance", 1.0e-6, "local_case.alpha_tolerance")
    barrier_position = _config_number(barrier_config, "streamwise_position_m", 0.90, "barrier.streamwise_position_m")
    barrier_thickness = _config_number(barrier_config, "thickness_m", 0.06, "barrier.thickness_m")
    barrier_height = _config_number(barrier_config, "height_m", 0.24, "barrier.height_m")
    barrier_span_fraction = _config_fraction(barrier_config, "span_fraction", 1.0, "barrier.span_fraction")
    k_value, omega_value = turbulence_values(config, replay_conversion)
    patches = _patches(has_barrier)
    wall_patches = _wall_patches(has_barrier)
    production = is_production_policy(config)
    side_patch_type = "patch" if production else "symmetryPlane"
    mesh_patch_types = {
        "inlet": "patch",
        "outlet": "patch",
        "sideLeft": side_patch_type,
        "sideRight": side_patch_type,
        "atmosphere": "patch",
        "terrain": "wall",
    }
    if has_barrier:
        mesh_patch_types["barrier"] = "wall"
    cells = _cell_dimensions(length, span, height, nx, ny, nz)
    barrier_record = {
        "streamwise_position_m": barrier_position,
        "thickness_m": barrier_thickness,
        "height_m": barrier_height,
        "span_fraction": barrier_span_fraction,
    } if has_barrier else None
    damping = damping_zones(
        config,
        length,
        span,
        height,
        nx,
        ny,
        nz,
        float(replay_conversion.get("maximum_boundary_speed_m_per_s", 0.0)),
        float(local_case.get("timestep_derivation", {}).get("maximum_reconstructed_water_depth_m", initial_water_level)),
        barrier_record,
    )

    for directory in ("0", "constant", "system"):
        (output_root / directory).mkdir(parents=True, exist_ok=True)
    shutil.copytree(replay_root / "constant" / "boundaryData", output_root / "constant" / "boundaryData")
    (output_root / "case.foam").write_text("OpenFOAM replay case\n", encoding="utf-8")
    block_mesh = _block_mesh_barrier(length, span, height, nx, ny, nz, barrier_position, barrier_thickness, barrier_height, side_patch_type) if has_barrier else _block_mesh_no_defence(length, span, height, nx, ny, nz, side_patch_type)
    (output_root / "system/blockMeshDict").write_text(block_mesh, encoding="utf-8")
    (output_root / "constant/g").write_text(_dict_header("g", "constant") + "dimensions      [0 1 -2 0 0 0 0];\nvalue           (0 0 -9.81);\n", encoding="utf-8")
    (output_root / "constant/phaseProperties").write_text(_dict_header("phaseProperties", "constant") + "phases          (water air);\n\nsigma           0.07;\n", encoding="utf-8")
    (output_root / "constant/physicalProperties.water").write_text(_dict_header("physicalProperties.water", "constant") + "viscosityModel  constant;\n\nnu              1e-06;\n\nrho             1000;\n", encoding="utf-8")
    (output_root / "constant/physicalProperties.air").write_text(_dict_header("physicalProperties.air", "constant") + "viscosityModel  constant;\n\nnu              1.48e-05;\n\nrho             1;\n", encoding="utf-8")
    (output_root / "constant/momentumTransport").write_text(_dict_header("momentumTransport", "constant") + "simulationType  RAS;\n\nRAS\n{\n    model           kOmegaSST;\n\n    turbulence      on;\n\n    printCoeffs     on;\n}\n", encoding="utf-8")
    (output_root / "constant/fvModels").write_text(_fv_models_text(damping), encoding="utf-8")

    inlet_u = "        type            timeVaryingMappedFixedValue;\n        offset          (0 0 0);\n        setAverage      off;"
    inlet_alpha = "        type            timeVaryingMappedFixedValue;\n        offset          0;\n        setAverage      off;"
    sides_sym = "        type            symmetryPlane;"
    open_u = "        type            pressureInletOutletVelocity;\n        value           uniform (0 0 0);"
    open_p = "        type            prghTotalPressure;\n        p0              uniform 0;\n        value           uniform 0;"
    open_alpha = "        type            variableHeightFlowRate;\n        lowerBound      0;\n        upperBound      1;\n        value           uniform 0;"
    open_k = f"        type            inletOutlet;\n        inletValue      uniform {_fmt(k_value)};\n        value           uniform {_fmt(k_value)};"
    open_omega = f"        type            inletOutlet;\n        inletValue      uniform {_fmt(omega_value)};\n        value           uniform {_fmt(omega_value)};"
    open_nut = "        type            calculated;\n        value           uniform 0;"
    wall_u = "        type            noSlip;"
    wall_p = "        type            fixedFluxPressure;\n        value           uniform 0;"
    wall_alpha = "        type            zeroGradient;"
    wall_k = f"        type            kqRWallFunction;\n        value           uniform {_fmt(k_value)};"
    wall_omega = f"        type            omegaWallFunction;\n        value           uniform {_fmt(omega_value)};"
    wall_nut = f"        type            {config['wall_function_policy']['nut']};\n        value           uniform 0;"
    outlet_u = open_u if production else "        type            inletOutlet;\n        inletValue      uniform (0 0 0);\n        value           uniform (0 0 0);"
    outlet_p = open_p if production else wall_p
    outlet_alpha = open_alpha if production else "        type            inletOutlet;\n        inletValue      uniform 0;\n        value           uniform 0;"
    sides_u = open_u if production else sides_sym
    sides_p = open_p if production else sides_sym
    sides_alpha = open_alpha if production else sides_sym
    sides_k = open_k if production else sides_sym
    sides_omega = open_omega if production else sides_sym
    sides_nut = open_nut if production else sides_sym

    u_boundaries = _make_boundary(
        patches,
        inlet_u,
        outlet_u,
        sides_u,
        "        type            pressureInletOutletVelocity;\n        value           uniform (0 0 0);",
        wall_u,
    )
    alpha_boundaries = _make_boundary(
        patches,
        inlet_alpha,
        outlet_alpha,
        sides_alpha,
        "        type            inletOutlet;\n        inletValue      uniform 0;\n        value           uniform 0;",
        wall_alpha,
    )
    p_boundaries = _make_boundary(
        patches,
        wall_p,
        outlet_p,
        sides_p,
        "        type            prghTotalPressure;\n        psi             none;\n        gamma           1;\n        p0              uniform 0;\n        value           uniform 0;",
        wall_p,
    )
    k_boundaries = _make_boundary(
        patches,
        f"        type            fixedValue;\n        value           uniform {_fmt(k_value)};",
        open_k,
        sides_k,
        f"        type            inletOutlet;\n        inletValue      uniform {_fmt(k_value)};\n        value           uniform {_fmt(k_value)};",
        wall_k,
    )
    omega_boundaries = _make_boundary(
        patches,
        f"        type            fixedValue;\n        value           uniform {_fmt(omega_value)};",
        open_omega,
        sides_omega,
        f"        type            inletOutlet;\n        inletValue      uniform {_fmt(omega_value)};\n        value           uniform {_fmt(omega_value)};",
        wall_omega,
    )
    nut_boundaries = _make_boundary(
        patches,
        "        type            calculated;\n        value           uniform 0;",
        open_nut,
        sides_nut,
        "        type            calculated;\n        value           uniform 0;",
        wall_nut,
    )
    field_boundaries = {
        "U": u_boundaries,
        "alpha.water": alpha_boundaries,
        "p_rgh": p_boundaries,
        "k": k_boundaries,
        "omega": omega_boundaries,
        "nut": nut_boundaries,
    }
    boundary_policy_record = _boundary_record(patches, mesh_patch_types, field_boundaries, config, damping, k_value, omega_value)
    wall_function_record = _wall_function_record(config, wall_patches, field_boundaries)
    timestep_policy_record = _timestep_policy_record(config, local_case, cells, replay_conversion)
    (output_root / "boundary_policy.json").write_text(json.dumps(boundary_policy_record, indent=2) + "\n", encoding="utf-8")
    (output_root / "wall_function_policy.json").write_text(json.dumps(wall_function_record, indent=2) + "\n", encoding="utf-8")
    (output_root / "timestep_policy.json").write_text(json.dumps(timestep_policy_record, indent=2) + "\n", encoding="utf-8")
    (output_root / "0/U").write_text(_field_file("volVectorField", "U", "[0 1 -1 0 0 0 0]", "uniform (0 0 0)", u_boundaries), encoding="utf-8")
    (output_root / "0/alpha.water").write_text(_field_file("volScalarField", "alpha.water", "[0 0 0 0 0 0 0]", "uniform 0", alpha_boundaries), encoding="utf-8")
    (output_root / "0/p_rgh").write_text(_field_file("volScalarField", "p_rgh", "[1 -1 -2 0 0 0 0]", "uniform 0", p_boundaries), encoding="utf-8")
    (output_root / "0/k").write_text(_field_file("volScalarField", "k", "[0 2 -2 0 0 0 0]", f"uniform {_fmt(k_value)}", k_boundaries), encoding="utf-8")
    (output_root / "0/omega").write_text(_field_file("volScalarField", "omega", "[0 0 -1 0 0 0 0]", f"uniform {_fmt(omega_value)}", omega_boundaries), encoding="utf-8")
    (output_root / "0/nut").write_text(_field_file("volScalarField", "nut", "[0 2 -1 0 0 0 0]", "uniform 0", nut_boundaries), encoding="utf-8")
    (output_root / "system/setFieldsDict").write_text(_dict_header("setFieldsDict", "system") + f"""defaultFieldValues
(
    volScalarFieldValue alpha.water 0
    volVectorFieldValue U (0 0 0)
);

regions
(
    boxToCell
    {{
        box (0 0 0) ({_fmt(length)} {_fmt(span)} {_fmt(initial_water_level)});
        fieldValues
        (
            volScalarFieldValue alpha.water 1
        );
    }}
);
""", encoding="utf-8")
    functions = f"""
functions
{{
    probes
    {{
        type            probes;
        libs            ("libsampling.so");
        writeControl    timeStep;
        writeInterval   1;
        probeLocations
        (
            ({_fmt(0.5 * barrier_position)} {_fmt(0.5 * span)} {_fmt(0.5 * min(initial_water_level, height))})
            ({_fmt(min(length * 0.95, barrier_position + 0.25 * (length - barrier_position)))} {_fmt(0.5 * span)} {_fmt(0.5 * min(initial_water_level, height))})
        );
        fixedLocations  false;
        fields
        (
            alpha.water
            U
            p_rgh
        );
    }}
"""
    if has_barrier:
        functions += f"""
    forces
    {{
        type            forces;
        libs            ("libforces.so");
        patches         (barrier);
        log             on;
        writeControl    timeStep;
        writeInterval   1;
        CofR            ({_fmt(barrier_position + 0.5 * barrier_thickness)} {_fmt(0.5 * span)} {_fmt(0.5 * barrier_height)});
    }}
"""
    if production:
        functions += """
    yPlus
    {
        type            yPlus;
        libs            ("libfieldFunctionObjects.so");
        executeControl  writeTime;
        writeControl    writeTime;
    }
"""
    functions += "}\n"
    adjust_time_step = "yes" if bool(timestep_policy.get("adjust_time_step", True)) else "no"
    (output_root / "system/controlDict").write_text(_dict_header("controlDict", "system") + f"""application     foamRun;

solver          incompressibleVoF;

startFrom       startTime;
startTime       0;
stopAt          endTime;
endTime         {_fmt(end_time)};
deltaT          {_fmt(initial_timestep)};

writeControl    adjustableRunTime;
writeInterval   {_fmt(write_interval)};
purgeWrite      0;
writeFormat     ascii;
writePrecision  10;
writeCompression off;
timeFormat      general;
timePrecision   8;
runTimeModifiable yes;

adjustTimeStep  {adjust_time_step};
maxCo           {_fmt(maximum_courant)};
maxAlphaCo      {_fmt(maximum_alpha_courant)};
maxDeltaT       {_fmt(maximum_timestep)};
{functions}
""", encoding="utf-8")
    (output_root / "system/fvSchemes").write_text(_dict_header("fvSchemes", "system") + """ddtSchemes
{
    default         Euler;
}

gradSchemes
{
    default         Gauss linear;
}

divSchemes
{
    div(rhoPhi,U)   Gauss upwind;
    div(phi,alpha)  Gauss MPLIC;
    div(phi,k)      Gauss upwind;
    div(phi,omega)  Gauss upwind;
    div(((rho*nuEff)*dev2(T(grad(U))))) Gauss linear;
}

laplacianSchemes
{
    default         Gauss linear corrected;
}

interpolationSchemes
{
    default         linear;
}

snGradSchemes
{
    default         corrected;
}

wallDist
{
    method meshWave;
}
""", encoding="utf-8")
    (output_root / "system/fvSolution").write_text(_dict_header("fvSolution", "system") + """solvers
{
    "alpha.water.*"
    {
        nAlphaCorr      3;
        nAlphaSubCycles 8;
        nLimiterIter    12;
    }

    p_rgh
    {
        solver          GAMG;
        tolerance       1e-08;
        relTol          0.01;
        smoother        DIC;
        cacheAgglomeration no;
    }

    p_rghFinal
    {
        $p_rgh;
        relTol          0;
    }

    "pcorr.*"
    {
        $p_rghFinal;
        tolerance       1e-4;
    }

    U
    {
        solver          smoothSolver;
        smoother        GaussSeidel;
        tolerance       1e-06;
        relTol          0;
        nSweeps         1;
    }

    "(k|omega).*"
    {
        solver          smoothSolver;
        smoother        symGaussSeidel;
        tolerance       1e-08;
        relTol          0;
    }
}

PIMPLE
{
    momentumPredictor no;
    nCorrectors     3;
    nNonOrthogonalCorrectors 0;

    pRefPoint      (0.1 0.1 0.5);
    pRefValue      0;
}

relaxationFactors
{
    equations
    {
        ".*" 1;
    }
}
""", encoding="utf-8")
    summary = {
        "variant": variant,
        "case_root": str(output_root),
        "dimensions_m": {"length": length, "span": span, "height": height},
        "cell_counts": {"streamwise": nx, "span": ny, "vertical": nz},
        "cell_dimensions_m": cells,
        "has_barrier": has_barrier,
        "replay_schema": config["schema"],
        "boundary_mode": boundary_mode(config),
        "mesh_patch_types": mesh_patch_types,
        "field_boundary_types": boundary_policy_record["field_boundary_types"],
        "damping": damping,
        "wall_function_policy": wall_function_record,
        "timestep_policy": timestep_policy_record,
        "k": k_value,
        "omega": omega_value,
        "end_time": end_time,
        "boundary_data_time_range": [boundary_start, boundary_end],
        "major_replay_peak_time": peak_shifted_time,
        "maximum_timestep": maximum_timestep,
        "initial_timestep": initial_timestep,
        "minimum_timestep": minimum_timestep,
        "maximum_courant_number": maximum_courant,
        "maximum_alpha_courant_number": maximum_alpha_courant,
        "write_interval": write_interval,
        "initial_water_level": initial_water_level,
        "alpha_tolerance": alpha_tolerance,
        "barrier": {
            "streamwise_position_m": barrier_position,
            "thickness_m": barrier_thickness,
            "height_m": barrier_height,
            "span_fraction": barrier_span_fraction,
        } if has_barrier else None,
        "patches": patches,
        "tutorial_authority": [
            "/opt/openfoam11/tutorials/incompressibleVoF/damBreakWithObstacle",
            "/opt/openfoam11/tutorials/incompressibleVoF/damBreakPorousBaffle",
            "/opt/openfoam11/tutorials/incompressibleVoF/waterChannel",
            "/opt/openfoam11/tutorials/incompressibleFluid/pitzDailySteadyExperimentalInlet",
            "/opt/openfoam11/tutorials/incompressibleVoF/DTCHull",
            "/opt/openfoam11/tutorials/incompressibleVoF/sloshingTank3D",
        ],
    }
    (output_root / "openfoam_case_summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


def validate_generated_case(case_root: Path, variant: str) -> None:
    has_barrier = variant == "simple_rigid_barrier"
    required = [
        "0/U", "0/alpha.water", "0/p_rgh", "0/k", "0/omega", "0/nut",
        "constant/boundaryData/inlet/points", "constant/g", "constant/momentumTransport",
        "constant/phaseProperties", "constant/physicalProperties.water", "constant/physicalProperties.air", "constant/fvModels",
        "system/blockMeshDict", "system/controlDict", "system/fvSchemes", "system/fvSolution", "system/setFieldsDict", "case.foam",
        "boundary_policy.json", "wall_function_policy.json", "timestep_policy.json",
    ]
    for relative in required:
        if not (case_root / relative).exists():
            raise ReplayError(f"generated case missing {relative}")
    control = (case_root / "system/controlDict").read_text(encoding="utf-8")
    if has_barrier and "patches         (barrier);" not in control:
        raise ReplayError("barrier case must configure forces on barrier")
    if not has_barrier and "barrier" in (case_root / "system/blockMeshDict").read_text(encoding="utf-8"):
        raise ReplayError("no-defence case must not contain barrier patch")
    for field in ("U", "alpha.water", "p_rgh", "k", "omega", "nut"):
        text = (case_root / "0" / field).read_text(encoding="utf-8")
        for patch in _patches(has_barrier):
            if f"    {patch}\n" not in text:
                raise ReplayError(f"{field} missing patch {patch}")
    summary = load_json(case_root / "openfoam_case_summary.json")
    if float(summary["k"]) <= 0.0 or float(summary["omega"]) <= 0.0:
        raise ReplayError("k and omega must be positive")
    boundary = load_json(case_root / "boundary_policy.json")
    wall = load_json(case_root / "wall_function_policy.json")
    timestep = load_json(case_root / "timestep_policy.json")
    mode = boundary.get("mode")
    field_types = boundary.get("field_boundary_types", {})
    mesh_types = boundary.get("mesh_patch_types", {})
    for side in ("sideLeft", "sideRight"):
        if mesh_types.get(side) == "symmetryPlane":
            for field, expected in field_types.items():
                if expected.get(side) != "symmetryPlane":
                    raise ReplayError(f"{field} uses open field type on symmetry mesh patch {side}")
    if mode == "symmetry_test":
        for side in ("sideLeft", "sideRight"):
            if mesh_types.get(side) != "symmetryPlane":
                raise ReplayError("symmetry_test must generate symmetryPlane lateral mesh patches")
            for field in ("U", "alpha.water", "p_rgh", "k", "omega", "nut"):
                if field_types[field][side] != "symmetryPlane":
                    raise ReplayError(f"symmetry_test must generate symmetryPlane {field} lateral patches")
        if boundary.get("damping_configuration", {}).get("enabled"):
            raise ReplayError("symmetry_test must not generate damping fvModels")
    elif mode == "open_ocean_damped":
        if "yPlus" not in control:
            raise ReplayError("production case must configure the yPlus function object")
        expected_open = {
            "U": "pressureInletOutletVelocity",
            "p_rgh": "prghTotalPressure",
            "alpha.water": "variableHeightFlowRate",
            "k": "inletOutlet",
            "omega": "inletOutlet",
            "nut": "calculated",
        }
        for patch in ("outlet", "sideLeft", "sideRight"):
            if mesh_types.get(patch) != "patch":
                raise ReplayError("open_ocean_damped must generate patch-type outlet and lateral mesh patches")
            for field, expected_type in expected_open.items():
                if field_types[field][patch] != expected_type:
                    raise ReplayError(f"production {field} condition on {patch} must be {expected_type}")
        alpha_text = (case_root / "0/alpha.water").read_text(encoding="utf-8")
        if "lowerBound      0;" not in alpha_text or "upperBound      1;" not in alpha_text:
            raise ReplayError("production alpha open boundary must be explicitly bounded")
        if field_types["U"]["outlet"] != "pressureInletOutletVelocity" or field_types["p_rgh"]["outlet"] != "prghTotalPressure":
            raise ReplayError("production pressure/velocity pairing is incomplete")
        for patch in wall["expected_wall_patches"]:
            if wall["field_types"]["k"][patch] != "kqRWallFunction":
                raise ReplayError("production wall k condition must be kqRWallFunction")
            if wall["field_types"]["omega"][patch] != "omegaWallFunction":
                raise ReplayError("production wall omega condition must be omegaWallFunction")
            if wall["field_types"]["nut"][patch] != "nutUSpaldingWallFunction":
                raise ReplayError("production wall nut condition must be nutUSpaldingWallFunction")
        damping = boundary.get("damping_configuration", {})
        if not damping.get("enabled"):
            raise ReplayError("open_ocean_damped must generate damping fvModels")
        zones = damping.get("zones", [])
        if {zone.get("name") for zone in zones} != {"outlet", "sideLeft", "sideRight"}:
            raise ReplayError("production damping must include outlet and both lateral zones")
        for zone in zones:
            if float(zone["cell_count"]) < 6.0:
                raise ReplayError("production damping zone spans fewer than six cells")
            if float(zone["lambda_per_s"]) <= 0.0 or not math.isfinite(float(zone["lambda_per_s"])):
                raise ReplayError("production damping lambda must be finite and positive")
            if float(zone["undamped_core_width_m"]) <= 0.0:
                raise ReplayError("production damping must leave an undamped core")
        if timestep["maxCo"] != summary["maximum_courant_number"] or timestep["maxAlphaCo"] != summary["maximum_alpha_courant_number"]:
            raise ReplayError("timestep policy record must match generated controlDict Courant limits")
    else:
        raise ReplayError("unknown generated boundary policy mode")


def _run(command: list[str], cwd: Path, log_path: Path) -> None:
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, text=True)
    if process.returncode != 0:
        raise ReplayError(f"command failed ({process.returncode}): {' '.join(command)}; log={log_path}")


def run_smoke(output_root: Path, wrapper: Path, fixture_root: Path, clean: bool = False, config_path: Path | None = None) -> dict:
    if output_root.exists() and clean:
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True, exist_ok=True)
    coupling_dir = fixture_root / "coupling" / "boundary.offshore"
    config_path = config_path or fixture_root / "replay_config.json"
    convert_boundary_data(coupling_dir, config_path, output_root, overwrite=True)
    results = {"output_root": str(output_root), "variants": {}}
    for variant in ("no_defence", "simple_rigid_barrier"):
        case_root = output_root / variant
        generate_case(output_root, config_path, case_root, variant, overwrite=True)
        validate_generated_case(case_root, variant)
        for stage in ("blockMesh", "checkMesh", "setFields", "foamRun", "foamToVTK"):
            if stage == "foamRun":
                command = [str(wrapper), str(case_root), "foamRun", "-solver", "incompressibleVoF"]
            elif stage == "foamToVTK":
                command = [str(wrapper), str(case_root), "foamToVTK"]
            else:
                command = [str(wrapper), str(case_root), stage]
            _run(command, case_root, case_root / f"log.{stage}")
        results["variants"][variant] = validate_smoke_case(case_root, variant)
    return results


def _latest_time(case_root: Path) -> float:
    times = []
    for child in case_root.iterdir():
        if child.is_dir():
            try:
                times.append(float(child.name))
            except ValueError:
                # OpenFOAM case directories also include non-time names such as VTK.
                continue
    return max(times) if times else 0.0


def _parse_numbers(text: str) -> list[float]:
    tokens: list[float] = []
    for raw in text.replace("(", " ").replace(")", " ").replace(",", " ").replace(";", " ").split():
        try:
            value = float(raw)
        except ValueError:
            continue
        if math.isfinite(value):
            tokens.append(value)
    return tokens


def _read_internal_scalar_field(path: Path) -> list[float]:
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    for index, line in enumerate(lines):
        stripped = line.strip()
        if not stripped.startswith("internalField"):
            continue
        if "uniform" in stripped and "nonuniform" not in stripped:
            return _parse_numbers(stripped.split("uniform", 1)[1])
        if "nonuniform" not in stripped:
            break
        cursor = index + 1
        while cursor < len(lines) and not lines[cursor].strip():
            cursor += 1
        if cursor >= len(lines):
            break
        try:
            count = int(lines[cursor].strip().rstrip(";"))
        except ValueError as exc:
            raise ReplayError(f"{path}: malformed nonuniform internalField count") from exc
        cursor += 1
        while cursor < len(lines) and lines[cursor].strip() != "(":
            cursor += 1
        if cursor >= len(lines):
            raise ReplayError(f"{path}: missing nonuniform internalField list")
        cursor += 1
        values: list[str] = []
        while cursor < len(lines) and lines[cursor].strip() != ")":
            values.append(lines[cursor])
            cursor += 1
        parsed = _parse_numbers("\n".join(values))
        if len(parsed) != count:
            raise ReplayError(f"{path}: nonuniform internalField count mismatch")
        return parsed
    raise ReplayError(f"{path}: missing internalField")


def _assert_finite_field(path: Path, label: str) -> None:
    text = path.read_text(encoding="utf-8", errors="ignore")
    nonfinite_tokens = {"nan", "+nan", "-nan", "inf", "+inf", "-inf", "infinity", "+infinity", "-infinity"}
    for token in text.replace("(", " ").replace(")", " ").replace(";", " ").replace(",", " ").split():
        if token.lower() in nonfinite_tokens:
            raise ReplayError(f"{label}: non-finite value in {path}")
    if not _parse_numbers(text):
        raise ReplayError(f"{label}: no numeric field values in {path}")


def _maximum_log_value(log_text: str, prefix: str) -> float | None:
    maximum: float | None = None
    for log_line in log_text.splitlines():
        stripped = log_line.strip()
        if not stripped.startswith(prefix) or "max:" not in stripped:
            continue
        try:
            value = float(stripped.rsplit("max:", 1)[1].split()[0])
        except (IndexError, ValueError):
            continue
        if math.isfinite(value):
                maximum = value if maximum is None else max(maximum, value)
    return maximum


def _percentile(values: Sequence[float], fraction: float) -> float:
    if not values:
        raise ReplayError("cannot calculate percentile of empty sample")
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = fraction * (len(ordered) - 1)
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def yplus_statistics(values: Sequence[float]) -> dict[str, float | int]:
    finite = [float(value) for value in values if math.isfinite(float(value))]
    non_finite = len(values) - len(finite)
    negative = sum(1 for value in finite if value < 0.0)
    if non_finite or negative:
        raise ReplayError("yPlus evidence contains non-finite or negative values")
    if not finite:
        raise ReplayError("yPlus evidence contains no finite values")
    total = float(len(finite))
    return {
        "minimum": min(finite),
        "p05": _percentile(finite, 0.05),
        "mean": statistics.fmean(finite),
        "median": statistics.median(finite),
        "p95": _percentile(finite, 0.95),
        "maximum": max(finite),
        "finite_value_count": len(finite),
        "non_finite_count": non_finite,
        "negative_value_count": negative,
        "viscous_affected_fraction": sum(1 for value in finite if value < 10.0) / total,
        "buffer_fraction": sum(1 for value in finite if 10.0 <= value < 30.0) / total,
        "log_layer_fraction": sum(1 for value in finite if 30.0 <= value <= 300.0) / total,
        "high_fraction": sum(1 for value in finite if value > 300.0) / total,
    }


def summarise_yplus_samples(samples: Sequence[dict], expected_patches: Sequence[str], final_time: float, peak_time: float | None) -> dict:
    by_patch_time: dict[str, dict[float, list[dict[str, object]]]] = {}
    for row in samples:
        patch = str(row["patch"])
        time = _float(row["time"], "yPlus.time")
        by_patch_time.setdefault(patch, {}).setdefault(time, []).append(dict(row))
    tolerance = _time_tolerance(final_time)
    missing = [patch for patch in expected_patches if patch not in by_patch_time]
    if missing:
        raise ReplayError(f"missing yPlus evidence for patches: {', '.join(missing)}")

    def row_stats(rows: Sequence[dict[str, object]]) -> dict[str, object]:
        face_values: list[float] = []
        table_rows: list[dict[str, float]] = []
        for row in rows:
            raw_values = row.get("values")
            if raw_values is not None:
                if not isinstance(raw_values, Sequence) or isinstance(raw_values, (str, bytes)):
                    raise ReplayError("yPlus sample values must be numeric sequences")
                face_values.extend(_float(value, "yPlus.value") for value in raw_values)
                continue
            minimum = _float(row.get("minimum"), "yPlus.minimum")
            mean = _float(row.get("mean"), "yPlus.mean")
            maximum = _float(row.get("maximum"), "yPlus.maximum")
            if minimum < 0.0 or mean < 0.0 or maximum < 0.0:
                raise ReplayError("yPlus evidence contains non-finite or negative values")
            table_rows.append({"minimum": minimum, "mean": mean, "maximum": maximum})
        if face_values:
            return {**yplus_statistics(face_values), "source_level": "face_values"}
        if not table_rows:
            raise ReplayError("yPlus evidence contains no values")
        return {
            "minimum": min(row["minimum"] for row in table_rows),
            "p05": None,
            "mean": statistics.fmean(row["mean"] for row in table_rows),
            "median": None,
            "p95": None,
            "maximum": max(row["maximum"] for row in table_rows),
            "finite_value_count": None,
            "non_finite_count": 0,
            "negative_value_count": 0,
            "viscous_affected_fraction": None,
            "buffer_fraction": None,
            "log_layer_fraction": None,
            "high_fraction": None,
            "source_level": "openfoam_min_max_average_table",
        }

    rows: list[dict[str, object]] = []
    patch_summaries: dict[str, dict[str, object]] = {}
    for patch in expected_patches:
        time_map = by_patch_time[patch]
        if not any(abs(time - final_time) <= tolerance for time in time_map):
            raise ReplayError(f"yPlus evidence for {patch} does not cover final time {final_time}")
        if peak_time is not None and not any(abs(time - peak_time) <= _time_tolerance(peak_time) for time in time_map):
            raise ReplayError(f"yPlus evidence for {patch} does not cover peak time {peak_time}")
        patch_rows = []
        aggregate_inputs: list[dict[str, object]] = []
        for time in sorted(time_map):
            stats = row_stats(time_map[time])
            stats_row = {"time": time, "patch": patch, **stats}
            rows.append(stats_row)
            patch_rows.append(stats_row)
            aggregate_inputs.extend(time_map[time])
        aggregate = row_stats(aggregate_inputs)
        patch_summaries[patch] = {"time_count": len(patch_rows), "aggregate": aggregate, "times": sorted(time_map)}
    return {
        "schema": {"name": "tsunami.openfoam_wall_yplus_evidence", "version": "1.0.0"},
        "expected_wall_patches": list(expected_patches),
        "final_time_s": final_time,
        "peak_time_s": peak_time,
        "patches": patch_summaries,
        "series": rows,
        "acceptance": {
            "finite": True,
            "non_negative": True,
            "expected_wall_patches_present": True,
            "final_time_coverage_present": True,
            "peak_time_coverage_present": peak_time is None or True,
            "classification": "continuous Spalding-law wall-function baseline; no universal 30<y+<300 requirement is imposed at G6",
        },
    }


def _read_yplus_dat_samples(case_root: Path) -> list[dict]:
    samples: list[dict] = []
    for path in sorted((case_root / "postProcessing").glob("yPlus/**/*")) if (case_root / "postProcessing").exists() else []:
        if not path.is_file():
            continue
        header = ""
        for data_line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            stripped = data_line.strip()
            if not stripped:
                continue
            if stripped.startswith("#"):
                header += " " + stripped.lower()
                continue
            parts = stripped.replace(",", " ").split()
            if len(parts) < 3:
                continue
            try:
                time = float(parts[0])
            except ValueError:
                continue
            patch = parts[1]
            values = []
            for token in parts[2:]:
                try:
                    values.append(float(token))
                except ValueError:
                    # Some OpenFOAM table rows carry labels beside numeric columns.
                    continue
            if values:
                if "min" in header and "max" in header and "average" in header and len(values) >= 3:
                    samples.append({
                        "time": time,
                        "patch": patch,
                        "minimum": values[0],
                        "maximum": values[1],
                        "mean": values[2],
                        "source": str(path),
                        "statistics_only": True,
                    })
                else:
                    samples.append({"time": time, "patch": patch, "values": values, "source": str(path)})
    return samples


def _read_yplus_log_samples(log_text: str) -> list[dict]:
    samples: list[dict] = []
    current_time: float | None = None
    number = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
    pattern = re.compile(
        rf"y\+?\s+(?:patch\s+)?(?P<patch>[A-Za-z0-9_.-]+).*?"
        rf"(?:min(?:imum)?|Min)\s*[:=]\s*(?P<min>{number}).*?"
        rf"(?:max(?:imum)?|Max)\s*[:=]\s*(?P<max>{number}).*?"
        rf"(?:average|mean|Average|Mean)\s*[:=]\s*(?P<mean>{number})"
    )
    for log_line in log_text.splitlines():
        time_match = re.match(r"\s*Time\s*=\s*(" + number + r")", log_line)
        if time_match:
            current_time = float(time_match.group(1))
        match = pattern.search(log_line)
        if match and current_time is not None:
            samples.append({
                "time": current_time,
                "patch": match.group("patch"),
                "minimum": float(match.group("min")),
                "mean": float(match.group("mean")),
                "maximum": float(match.group("max")),
                "statistics_only": True,
                "source": "log.foamRun",
            })
    return samples


def _extract_boundary_block(text: str, patch: str) -> str | None:
    lines = text.splitlines()
    for index, patch_line in enumerate(lines):
        if patch_line.strip() != patch:
            continue
        cursor = index + 1
        while cursor < len(lines) and "{" not in lines[cursor]:
            cursor += 1
        if cursor >= len(lines):
            return None
        depth = 0
        collected = []
        for block_line in lines[cursor:]:
            depth += block_line.count("{")
            depth -= block_line.count("}")
            collected.append(block_line)
            if depth == 0 and "}" in block_line:
                return "\n".join(collected)
    return None


def _read_patch_values_from_field(path: Path, patch: str) -> list[float]:
    block = _extract_boundary_block(path.read_text(encoding="utf-8", errors="ignore"), patch)
    if block is None:
        return []
    lines = block.splitlines()
    for index, field_line in enumerate(lines):
        if "value" not in field_line:
            continue
        if "uniform" in field_line and "nonuniform" not in field_line:
            return _parse_numbers(field_line.split("uniform", 1)[1])
        if "nonuniform" not in field_line:
            continue
        cursor = index + 1
        while cursor < len(lines) and not lines[cursor].strip():
            cursor += 1
        if cursor >= len(lines):
            return []
        try:
            count = int(lines[cursor].strip().rstrip(";"))
        except ValueError:
            count = -1
        while cursor < len(lines) and lines[cursor].strip() != "(":
            cursor += 1
        if cursor >= len(lines):
            return []
        cursor += 1
        values: list[str] = []
        while cursor < len(lines) and lines[cursor].strip() != ")":
            values.append(lines[cursor])
            cursor += 1
        parsed = _parse_numbers("\n".join(values))
        if count >= 0 and len(parsed) != count:
            raise ReplayError(f"{path}: yPlus patch {patch} count mismatch")
        return parsed
    return _parse_numbers(block)


def _read_yplus_field_samples(case_root: Path, expected_patches: Sequence[str]) -> list[dict]:
    samples: list[dict] = []
    for child in sorted(case_root.iterdir()):
        if not child.is_dir():
            continue
        try:
            time = float(child.name)
        except ValueError:
            continue
        field = child / "yPlus"
        if not field.is_file():
            continue
        for patch in expected_patches:
            values = _read_patch_values_from_field(field, patch)
            if values:
                samples.append({"time": time, "patch": patch, "values": values, "source": str(field)})
    return samples


def write_yplus_evidence(case_root: Path, samples: Sequence[dict], expected_patches: Sequence[str], final_time: float, peak_time: float | None) -> dict:
    evidence = summarise_yplus_samples(samples, expected_patches, final_time, peak_time)
    (case_root / "wall_function_evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    with (case_root / "wall_yplus_series.csv").open("w", encoding="utf-8", newline="") as handle:
        fieldnames = [
            "time", "patch", "minimum", "p05", "mean", "median", "p95", "maximum",
            "finite_value_count", "non_finite_count", "negative_value_count",
            "viscous_affected_fraction", "buffer_fraction", "log_layer_fraction", "high_fraction", "source_level",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(evidence["series"])
    for patch in expected_patches:
        rows = [row for row in evidence["series"] if row["patch"] == patch]
        if not rows:
            continue
        stem = "barrier" if patch == "barrier" else patch
        write_line_plot_png(
            case_root / f"{stem}_yplus_history.png",
            [
                {"name": "minimum", "x": [row["time"] for row in rows], "y": [row["minimum"] for row in rows], "color": (45, 98, 160)},
                {"name": "mean", "x": [row["time"] for row in rows], "y": [row["mean"] for row in rows], "color": (30, 120, 80)},
                {"name": "maximum", "x": [row["time"] for row in rows], "y": [row["maximum"] for row in rows], "color": (170, 75, 45)},
            ],
            metadata={
                "Title": f"{patch} yPlus history",
                "Description": "Single-panel yPlus minimum, mean and maximum history for G6 wall-function evidence.",
            },
        )
    return evidence


def parse_timestep_series_from_log(log_text: str) -> list[dict[str, float | None]]:
    number = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
    time_pattern = re.compile(r"^\s*Time\s*=\s*(" + number + r")")
    delta_pattern = re.compile(r"\bdeltaT\s*=\s*(" + number + r")")
    co_pattern = re.compile(r"^Courant Number.*?max:\s*(" + number + r")")
    alpha_pattern = re.compile(r"^Interface Courant Number.*?max:\s*(" + number + r")")
    rows: list[dict[str, float | None]] = []
    current: dict[str, float | None] = {"time": None, "deltaT": None, "Co": None, "alphaCo": None}
    times: list[float] = []
    for line in log_text.splitlines():
        if match := time_pattern.search(line):
            time = float(match.group(1))
            times.append(time)
            if current.get("time") is not None and any(current.get(key) is not None for key in ("deltaT", "Co", "alphaCo")):
                rows.append(dict(current))
            current = {"time": time, "deltaT": None, "Co": None, "alphaCo": None}
            continue
        if match := delta_pattern.search(line):
            current["deltaT"] = float(match.group(1))
        elif match := co_pattern.search(line):
            current["Co"] = float(match.group(1))
        elif match := alpha_pattern.search(line):
            current["alphaCo"] = float(match.group(1))
    if current.get("time") is not None and any(current.get(key) is not None for key in ("deltaT", "Co", "alphaCo")):
        rows.append(dict(current))
    if rows and all(row.get("deltaT") is None for row in rows) and len(times) > 1:
        deltas = [right - left for left, right in zip(times, times[1:]) if right > left]
        for row, delta in zip(rows[1:], deltas):
            row["deltaT"] = delta
    return rows


def write_timestep_evidence(case_root: Path, rows: Sequence[dict[str, float | None]], policy: dict) -> dict:
    deltas = [float(row["deltaT"]) for row in rows if row.get("deltaT") is not None and math.isfinite(float(row["deltaT"])) and float(row["deltaT"]) > 0.0]
    courant_values = [float(row["Co"]) for row in rows if row.get("Co") is not None and math.isfinite(float(row["Co"]))]
    alpha_values = [float(row["alphaCo"]) for row in rows if row.get("alphaCo") is not None and math.isfinite(float(row["alphaCo"]))]
    if not deltas:
        raise ReplayError("timestep evidence contains no positive deltaT values")
    minimum_threshold = float(policy.get("minimum_accepted_timestep_s", 0.0))
    if minimum_threshold > 0.0 and min(deltas) < minimum_threshold:
        raise ReplayError(f"observed timestep {min(deltas)} fell below configured minimum {minimum_threshold}")
    observed = {
        "observed_timestep_range_s": [min(deltas), max(deltas)],
        "minimum_observed_timestep_s": min(deltas),
        "maximum_observed_timestep_s": max(deltas),
        "median_observed_timestep_s": statistics.median(deltas),
        "observed_maximum_Co": max(courant_values) if courant_values else None,
        "observed_maximum_alpha_Co": max(alpha_values) if alpha_values else None,
    }
    evidence = dict(policy)
    evidence.update(observed)
    evidence["runtime_acceptance"] = {
        "minimum_timestep_guard_passed": True,
        "requested_end_time_reached": True,
        "openfoam_fatal_error_absent": True,
        "floating_point_exception_absent": True,
    }
    evidence["series_path"] = "timestep_series.csv"
    with (case_root / "timestep_series.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["time", "deltaT", "Co", "alphaCo"])
        writer.writeheader()
        writer.writerows(rows)
    (case_root / "timestep_policy_evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    x = [float(row["time"]) for row in rows if row.get("time") is not None]
    if x:
        dt_y = [float(row["deltaT"] or 0.0) for row in rows if row.get("time") is not None]
        co_y = [float(row["Co"] or 0.0) for row in rows if row.get("time") is not None]
        margin_y = [float(evidence["diagnostic_diffusive_margin"]) for _ in x]
        write_line_plot_png(
            case_root / "timestep_history.png",
            [{"name": "deltaT", "x": x, "y": dt_y, "color": (45, 98, 160)}],
            metadata={"Title": "timestep history", "Description": "Single-panel OpenFOAM timestep history."},
        )
        write_line_plot_png(
            case_root / "courant_history.png",
            [{"name": "Courant", "x": x, "y": co_y, "color": (170, 75, 45)}],
            metadata={"Title": "Courant history", "Description": "Single-panel maximum Courant history."},
        )
        write_line_plot_png(
            case_root / "diffusive_timescale_margin.png",
            [{"name": "M_nu", "x": x, "y": margin_y, "color": (30, 120, 80)}],
            metadata={"Title": "diffusive timescale margin", "Description": "Diagnostic implicit-diffusion margin, not an enforced explicit stability limit."},
        )
    return evidence


def _boundary_data_time_range(case_root: Path, patch: str = "inlet") -> tuple[float, float]:
    directory = case_root / "constant" / "boundaryData" / patch
    times: list[float] = []
    for child in directory.iterdir():
        if not child.is_dir():
            continue
        try:
            times.append(float(child.name))
        except ValueError:
            continue
    if not times:
        raise ReplayError(f"missing boundaryData time directories for patch {patch}")
    return min(times), max(times)


def _series_final_time(path: Path) -> float:
    latest: float | None = None
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        values = _parse_numbers(stripped)
        if not values:
            continue
        latest = values[0] if latest is None else max(latest, values[0])
    if latest is None:
        raise ReplayError(f"{path}: no numeric time samples")
    return latest


def _force_moment_metrics(path: Path) -> dict[str, float]:
    maximum_force = 0.0
    maximum_moment = 0.0
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        values = _parse_numbers(stripped)
        if len(values) < 7:
            continue
        components = values[1:]
        force_vectors = [components[index:index + 3] for index in range(0, min(9, len(components)), 3)]
        moment_vectors = [components[index:index + 3] for index in range(9, min(18, len(components)), 3)]
        if force_vectors:
            total_force = [sum(vector[axis] for vector in force_vectors if len(vector) == 3) for axis in range(3)]
            maximum_force = max(maximum_force, _norm(total_force))
        if moment_vectors:
            total_moment = [sum(vector[axis] for vector in moment_vectors if len(vector) == 3) for axis in range(3)]
            maximum_moment = max(maximum_moment, _norm(total_moment))
    return {"maximum_force_magnitude": maximum_force, "maximum_moment_magnitude": maximum_moment}


def validate_smoke_case(case_root: Path, variant: str) -> dict:
    foam_log = (case_root / "log.foamRun").read_text(encoding="utf-8", errors="ignore")
    if "FOAM FATAL ERROR" in foam_log or "Floating point exception" in foam_log:
        raise ReplayError(f"{variant}: solver log contains fatal error")
    latest = _latest_time(case_root)
    summary = load_json(case_root / "openfoam_case_summary.json")
    expected = float(summary["end_time"])
    tolerance = _time_tolerance(expected)
    if latest + 1.0e-9 < expected:
        raise ReplayError(f"{variant}: final time {latest} did not reach {expected}")
    boundary_start, boundary_end = _boundary_data_time_range(case_root)
    if boundary_end + tolerance < expected:
        raise ReplayError(f"{variant}: boundaryData ends at {boundary_end}, before requested end time {expected}")
    peak_time = summary.get("major_replay_peak_time")
    peak_traversed = False
    if peak_time is not None:
        peak_time = float(peak_time)
        if expected + tolerance < peak_time:
            raise ReplayError(f"{variant}: requested end time {expected} is before major replay peak {peak_time}")
        if latest + tolerance < peak_time:
            raise ReplayError(f"{variant}: solver final time {latest} did not traverse major replay peak {peak_time}")
        peak_traversed = True
    vtk_files = list((case_root / "VTK").glob("**/*")) if (case_root / "VTK").exists() else []
    if not any(path.is_file() and path.stat().st_size > 0 for path in vtk_files):
        raise ReplayError(f"{variant}: missing non-empty VTK output")
    probe_files = list((case_root / "postProcessing").glob("probes/**/*")) if (case_root / "postProcessing").exists() else []
    probe_files = [path for path in probe_files if path.is_file() and path.stat().st_size > 0]
    if not probe_files:
        raise ReplayError(f"{variant}: missing non-empty probe output")
    probe_final_time = min(_series_final_time(path) for path in probe_files)
    if probe_final_time + tolerance < expected:
        raise ReplayError(f"{variant}: probe output ends at {probe_final_time}, before requested end time {expected}")
    force_files: list[Path] = []
    force_final_time: float | None = None
    if variant == "simple_rigid_barrier":
        force_files = [path for path in (case_root / "postProcessing").glob("forces/**/*") if path.is_file() and path.stat().st_size > 0]
        if not force_files:
            raise ReplayError("barrier case missing non-empty force output")
        force_final_time = min(_series_final_time(path) for path in force_files)
        if force_final_time + tolerance < expected:
            raise ReplayError(f"barrier force output ends at {force_final_time}, before requested end time {expected}")
    alpha_path = case_root / _time_name(latest) / "alpha.water"
    _assert_finite_field(case_root / _time_name(latest) / "U", f"{variant}: U")
    _assert_finite_field(case_root / _time_name(latest) / "p_rgh", f"{variant}: p_rgh")
    alpha_values = _read_internal_scalar_field(alpha_path) if alpha_path.exists() else []
    alpha_min = min(alpha_values) if alpha_values else 0.0
    alpha_max = max(alpha_values) if alpha_values else 1.0
    alpha_tolerance = float(load_json(case_root / "openfoam_case_summary.json").get("alpha_tolerance", 1.0e-6))
    if alpha_min < -alpha_tolerance or alpha_max > 1.0 + alpha_tolerance:
        raise ReplayError(f"{variant}: alpha.water out of bounds [{alpha_min}, {alpha_max}]")
    maximum_courant = _maximum_log_value(foam_log, "Courant Number")
    maximum_alpha_courant = _maximum_log_value(foam_log, "Interface Courant Number")
    force_metrics = {"maximum_force_magnitude": 0.0, "maximum_moment_magnitude": 0.0}
    if force_files:
        force_metrics = _force_moment_metrics(force_files[0])
        if force_metrics["maximum_force_magnitude"] <= 0.0 or not math.isfinite(force_metrics["maximum_force_magnitude"]):
            raise ReplayError("barrier force output is not finite and nonzero")
        if not math.isfinite(force_metrics["maximum_moment_magnitude"]):
            raise ReplayError("barrier moment output is not finite")
    yplus_evidence_path: str | None = None
    timestep_evidence_path: str | None = None
    timestep_metrics: dict[str, object] = {}
    if summary.get("boundary_mode") == "open_ocean_damped":
        wall_policy_path = case_root / "wall_function_policy.json"
        timestep_policy_path = case_root / "timestep_policy.json"
        if not wall_policy_path.is_file() or not timestep_policy_path.is_file():
            raise ReplayError(f"{variant}: missing production wall or timestep policy record")
        wall_policy = load_json(wall_policy_path)
        expected_patches = [str(patch) for patch in wall_policy["expected_wall_patches"]]
        yplus_samples = _read_yplus_dat_samples(case_root)
        if not yplus_samples:
            yplus_samples = _read_yplus_field_samples(case_root, expected_patches)
        if not yplus_samples:
            yplus_samples = _read_yplus_log_samples(foam_log)
        if not yplus_samples:
            raise ReplayError(f"{variant}: missing yPlus runtime evidence")
        write_yplus_evidence(case_root, yplus_samples, expected_patches, expected, peak_time)
        yplus_evidence_path = str(case_root / "wall_function_evidence.json")
        timestep_rows = parse_timestep_series_from_log(foam_log)
        if not timestep_rows:
            raise ReplayError(f"{variant}: missing timestep runtime evidence")
        timestep_evidence = write_timestep_evidence(case_root, timestep_rows, load_json(timestep_policy_path))
        timestep_evidence_path = str(case_root / "timestep_policy_evidence.json")
        timestep_metrics = {
            "minimum_observed_timestep_s": timestep_evidence["minimum_observed_timestep_s"],
            "maximum_observed_timestep_s": timestep_evidence["maximum_observed_timestep_s"],
            "median_observed_timestep_s": timestep_evidence["median_observed_timestep_s"],
            "diagnostic_viscous_timescale_s": timestep_evidence["diagnostic_viscous_timescale_s"],
            "diagnostic_diffusive_margin": timestep_evidence["diagnostic_diffusive_margin"],
        }
    return {
        "case_root": str(case_root),
        "requested_end_time": expected,
        "final_time": latest,
        "boundary_data_time_range": [boundary_start, boundary_end],
        "major_replay_peak_time": peak_time,
        "major_replay_peak_traversed": peak_traversed,
        "probe_final_time": probe_final_time,
        "force_final_time": force_final_time,
        "alpha_min": alpha_min,
        "alpha_max": alpha_max,
        "observed_maximum_courant_number": maximum_courant,
        "observed_maximum_alpha_courant_number": maximum_alpha_courant,
        "wall_function_evidence": yplus_evidence_path,
        "timestep_policy_evidence": timestep_evidence_path,
        "timestep_metrics": timestep_metrics,
        "probe_files": [str(path) for path in probe_files[:6]],
        "force_files": [str(path) for path in force_files[:6]],
        "maximum_barrier_force": force_metrics["maximum_force_magnitude"],
        "maximum_barrier_moment": force_metrics["maximum_moment_magnitude"],
        "vtk_file_count": len([path for path in vtk_files if path.is_file()]),
        "vtk_final_time": latest,
    }


def command_convert(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Convert G3 Regional2D coupling output to OpenFOAM boundaryData.")
    parser.add_argument("--coupling-dir", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args(argv)
    convert_boundary_data(args.coupling_dir, args.config, args.output_root, args.overwrite)
    print(args.output_root)
    return 0


def command_generate(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Generate a synthetic OpenFOAM replay case.")
    parser.add_argument("--replay-root", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--variant", required=True, choices=["no_defence", "simple_rigid_barrier"])
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args(argv)
    summary = generate_case(args.replay_root, args.config, args.output_root, args.variant, args.overwrite)
    validate_generated_case(args.output_root, args.variant)
    print(json.dumps(summary, indent=2))
    return 0


def command_smoke(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Run the synthetic OpenFOAM replay smoke workflow.")
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--wrapper", default=Path("tools/openfoam/run_openfoam11.sh"), type=Path)
    parser.add_argument("--fixture-root", default=Path("tests/fixtures/openfoam/synthetic_replay"), type=Path)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args(argv)
    result = run_smoke(args.output_root, args.wrapper.resolve(), args.fixture_root.resolve(), args.clean, args.config.resolve() if args.config else None)
    print(json.dumps(result, indent=2))
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        raise SystemExit("usage: openfoam_replay.py {convert|generate|smoke} ...")
    command = argv.pop(0)
    try:
        if command == "convert":
            return command_convert(argv)
        if command == "generate":
            return command_generate(argv)
        if command == "smoke":
            return command_smoke(argv)
        raise ReplayError(f"unknown command: {command}")
    except ReplayError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
