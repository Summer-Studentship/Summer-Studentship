#!/usr/bin/env python3
"""Regional2D-to-OpenFOAM replay conversion and synthetic case generation."""

from __future__ import annotations

import argparse
import csv
import hashlib
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


CONVERTER_VERSION = "0.1.0"
REPLAY_SCHEMA = {"name": "tsunami.openfoam_replay_conversion", "version": "1.0.0"}
CONFIG_SCHEMA_NAME = "tsunami.openfoam_replay_configuration"
CONFIG_SCHEMA_VERSION = "1.0.0"
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


def load_replay_config(path: Path) -> dict:
    config = load_json(path)
    schema = config.get("schema")
    if schema != {"name": CONFIG_SCHEMA_NAME, "version": CONFIG_SCHEMA_VERSION}:
        raise ReplayError("unsupported replay configuration schema")
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


def _block_mesh_no_defence(length: float, span: float, height: float, nx: int, ny: int, nz: int) -> str:
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
    sideLeft {{ type symmetryPlane; faces ((0 1 5 4)); }}
    sideRight {{ type symmetryPlane; faces ((3 7 6 2)); }}
    atmosphere {{ type patch; faces ((4 5 6 7)); }}
    terrain {{ type wall; faces ((0 3 2 1)); }}
);

mergePatchPairs
(
);
"""


def _block_mesh_barrier(length: float, span: float, height: float, nx: int, ny: int, nz: int) -> str:
    xb0 = 0.90
    xb1 = 0.96
    hb = 0.24
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
        type symmetryPlane;
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
        type symmetryPlane;
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
    has_barrier = variant == "simple_rigid_barrier"
    local = config["local"]
    length = 2.0
    span = float(local["span_max_m"]) - float(local["span_min_m"])
    height = float(local["vertical_max_m"]) - float(local["vertical_min_m"])
    nx, ny, nz = 30, max(4, int(local["span_cells"])), max(10, int(local["vertical_cells"]))
    end_time = float(replay_conversion["time_range"][1])
    first_level = 0.18
    k_value, omega_value = turbulence_values(config, replay_conversion)
    patches = _patches(has_barrier)

    for directory in ("0", "constant", "system"):
        (output_root / directory).mkdir(parents=True, exist_ok=True)
    shutil.copytree(replay_root / "constant" / "boundaryData", output_root / "constant" / "boundaryData")
    (output_root / "case.foam").write_text("OpenFOAM replay case\n", encoding="utf-8")
    block_mesh = _block_mesh_barrier(length, span, height, nx, ny, nz) if has_barrier else _block_mesh_no_defence(length, span, height, nx, ny, nz)
    (output_root / "system/blockMeshDict").write_text(block_mesh, encoding="utf-8")
    (output_root / "constant/g").write_text(_dict_header("g", "constant") + "dimensions      [0 1 -2 0 0 0 0];\nvalue           (0 0 -9.81);\n", encoding="utf-8")
    (output_root / "constant/phaseProperties").write_text(_dict_header("phaseProperties", "constant") + "phases          (water air);\n\nsigma           0.07;\n", encoding="utf-8")
    (output_root / "constant/physicalProperties.water").write_text(_dict_header("physicalProperties.water", "constant") + "viscosityModel  constant;\n\nnu              1e-06;\n\nrho             1000;\n", encoding="utf-8")
    (output_root / "constant/physicalProperties.air").write_text(_dict_header("physicalProperties.air", "constant") + "viscosityModel  constant;\n\nnu              1.48e-05;\n\nrho             1;\n", encoding="utf-8")
    (output_root / "constant/momentumTransport").write_text(_dict_header("momentumTransport", "constant") + "simulationType  RAS;\n\nRAS\n{\n    model           kOmegaSST;\n\n    turbulence      on;\n\n    printCoeffs     on;\n}\n", encoding="utf-8")

    inlet_u = "        type            timeVaryingMappedFixedValue;\n        offset          (0 0 0);\n        setAverage      off;"
    inlet_alpha = "        type            timeVaryingMappedFixedValue;\n        offset          0;\n        setAverage      off;"
    sides_sym = "        type            symmetryPlane;"
    wall_u = "        type            noSlip;"
    wall_p = "        type            fixedFluxPressure;\n        value           uniform 0;"
    wall_alpha = "        type            zeroGradient;"
    wall_k = f"        type            kqRWallFunction;\n        value           uniform {_fmt(k_value)};"
    wall_omega = f"        type            omegaWallFunction;\n        value           uniform {_fmt(omega_value)};"
    wall_nut = "        type            nutkWallFunction;\n        value           uniform 0;"

    (output_root / "0/U").write_text(_field_file("volVectorField", "U", "[0 1 -1 0 0 0 0]", "uniform (0 0 0)", _make_boundary(
        patches,
        inlet_u,
        "        type            inletOutlet;\n        inletValue      uniform (0 0 0);\n        value           uniform (0 0 0);",
        sides_sym,
        "        type            pressureInletOutletVelocity;\n        value           uniform (0 0 0);",
        wall_u,
    )), encoding="utf-8")
    (output_root / "0/alpha.water").write_text(_field_file("volScalarField", "alpha.water", "[0 0 0 0 0 0 0]", "uniform 0", _make_boundary(
        patches,
        inlet_alpha,
        "        type            inletOutlet;\n        inletValue      uniform 0;\n        value           uniform 0;",
        sides_sym,
        "        type            inletOutlet;\n        inletValue      uniform 0;\n        value           uniform 0;",
        wall_alpha,
    )), encoding="utf-8")
    (output_root / "0/p_rgh").write_text(_field_file("volScalarField", "p_rgh", "[1 -1 -2 0 0 0 0]", "uniform 0", _make_boundary(
        patches,
        wall_p,
        wall_p,
        sides_sym,
        "        type            prghTotalPressure;\n        psi             none;\n        gamma           1;\n        p0              uniform 0;\n        value           uniform 0;",
        wall_p,
    )), encoding="utf-8")
    (output_root / "0/k").write_text(_field_file("volScalarField", "k", "[0 2 -2 0 0 0 0]", f"uniform {_fmt(k_value)}", _make_boundary(
        patches,
        f"        type            fixedValue;\n        value           uniform {_fmt(k_value)};",
        f"        type            inletOutlet;\n        inletValue      uniform {_fmt(k_value)};\n        value           uniform {_fmt(k_value)};",
        sides_sym,
        f"        type            inletOutlet;\n        inletValue      uniform {_fmt(k_value)};\n        value           uniform {_fmt(k_value)};",
        wall_k,
    )), encoding="utf-8")
    (output_root / "0/omega").write_text(_field_file("volScalarField", "omega", "[0 0 -1 0 0 0 0]", f"uniform {_fmt(omega_value)}", _make_boundary(
        patches,
        f"        type            fixedValue;\n        value           uniform {_fmt(omega_value)};",
        f"        type            inletOutlet;\n        inletValue      uniform {_fmt(omega_value)};\n        value           uniform {_fmt(omega_value)};",
        sides_sym,
        f"        type            inletOutlet;\n        inletValue      uniform {_fmt(omega_value)};\n        value           uniform {_fmt(omega_value)};",
        wall_omega,
    )), encoding="utf-8")
    (output_root / "0/nut").write_text(_field_file("volScalarField", "nut", "[0 2 -1 0 0 0 0]", "uniform 0", _make_boundary(
        patches,
        "        type            calculated;\n        value           uniform 0;",
        "        type            calculated;\n        value           uniform 0;",
        sides_sym,
        "        type            calculated;\n        value           uniform 0;",
        wall_nut,
    )), encoding="utf-8")
    (output_root / "system/setFieldsDict").write_text(_dict_header("setFieldsDict", "system") + f"""defaultFieldValues
(
    volScalarFieldValue alpha.water 0
    volVectorFieldValue U (0 0 0)
);

regions
(
    boxToCell
    {{
        box (0 0 0) ({_fmt(length)} {_fmt(span)} {_fmt(first_level)});
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
            (0.55 0.30 0.12)
            (1.45 0.30 0.12)
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
        functions += """
    forces
    {
        type            forces;
        libs            ("libforces.so");
        patches         (barrier);
        log             on;
        writeControl    timeStep;
        writeInterval   1;
        CofR            (0.93 0.30 0.12);
    }
"""
    functions += "}\n"
    (output_root / "system/controlDict").write_text(_dict_header("controlDict", "system") + f"""application     foamRun;

solver          incompressibleVoF;

startFrom       startTime;
startTime       0;
stopAt          endTime;
endTime         {_fmt(end_time)};
deltaT          0.002;

writeControl    adjustableRunTime;
writeInterval   {_fmt(max(end_time / 2.0, 0.01))};
purgeWrite      0;
writeFormat     ascii;
writePrecision  10;
writeCompression off;
timeFormat      general;
timePrecision   8;
runTimeModifiable yes;

adjustTimeStep  yes;
maxCo           0.5;
maxAlphaCo      0.5;
maxDeltaT       0.005;
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
        nAlphaCorr      1;
        nAlphaSubCycles 2;
        nLimiterIter    5;
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
        "has_barrier": has_barrier,
        "k": k_value,
        "omega": omega_value,
        "end_time": end_time,
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
        "constant/phaseProperties", "constant/physicalProperties.water", "constant/physicalProperties.air",
        "system/blockMeshDict", "system/controlDict", "system/fvSchemes", "system/fvSolution", "system/setFieldsDict", "case.foam",
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


def _run(command: list[str], cwd: Path, log_path: Path) -> None:
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, text=True)
    if process.returncode != 0:
        raise ReplayError(f"command failed ({process.returncode}): {' '.join(command)}; log={log_path}")


def run_smoke(output_root: Path, wrapper: Path, fixture_root: Path, clean: bool = False) -> dict:
    if output_root.exists() and clean:
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True, exist_ok=True)
    coupling_dir = fixture_root / "coupling" / "boundary.offshore"
    config_path = fixture_root / "replay_config.json"
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
                pass
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


def validate_smoke_case(case_root: Path, variant: str) -> dict:
    foam_log = (case_root / "log.foamRun").read_text(encoding="utf-8", errors="ignore")
    if "FOAM FATAL ERROR" in foam_log or "Floating point exception" in foam_log:
        raise ReplayError(f"{variant}: solver log contains fatal error")
    latest = _latest_time(case_root)
    expected = float(load_json(case_root / "openfoam_case_summary.json")["end_time"])
    if latest + 1.0e-9 < expected:
        raise ReplayError(f"{variant}: final time {latest} did not reach {expected}")
    vtk_files = list((case_root / "VTK").glob("**/*")) if (case_root / "VTK").exists() else []
    if not any(path.is_file() and path.stat().st_size > 0 for path in vtk_files):
        raise ReplayError(f"{variant}: missing non-empty VTK output")
    probe_files = list((case_root / "postProcessing").glob("probes/**/*")) if (case_root / "postProcessing").exists() else []
    probe_files = [path for path in probe_files if path.is_file() and path.stat().st_size > 0]
    if not probe_files:
        raise ReplayError(f"{variant}: missing non-empty probe output")
    force_files: list[Path] = []
    if variant == "simple_rigid_barrier":
        force_files = [path for path in (case_root / "postProcessing").glob("forces/**/*") if path.is_file() and path.stat().st_size > 0]
        if not force_files:
            raise ReplayError("barrier case missing non-empty force output")
    alpha_path = case_root / _time_name(latest) / "alpha.water"
    alpha_values = _read_internal_scalar_field(alpha_path) if alpha_path.exists() else []
    alpha_min = min(alpha_values) if alpha_values else 0.0
    alpha_max = max(alpha_values) if alpha_values else 1.0
    alpha_tolerance = 1.0e-6
    if alpha_min < -alpha_tolerance or alpha_max > 1.0 + alpha_tolerance:
        raise ReplayError(f"{variant}: alpha.water out of bounds [{alpha_min}, {alpha_max}]")
    return {
        "case_root": str(case_root),
        "final_time": latest,
        "alpha_min": alpha_min,
        "alpha_max": alpha_max,
        "probe_files": [str(path) for path in probe_files[:6]],
        "force_files": [str(path) for path in force_files[:6]],
        "vtk_file_count": len([path for path in vtk_files if path.is_file()]),
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
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args(argv)
    result = run_smoke(args.output_root, args.wrapper.resolve(), args.fixture_root.resolve(), args.clean)
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
