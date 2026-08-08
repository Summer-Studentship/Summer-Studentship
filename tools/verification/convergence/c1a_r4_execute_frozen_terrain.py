#!/usr/bin/env python3
"""Execute C1A-R4 frozen-terrain Regional2D mesh-convergence runs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
import os
import resource
import shutil
import statistics
import subprocess
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

import c1a_convergence as c1a


STUDY_ID = c1a.R4_FROZEN_TERRAIN_STUDY_ID
SECTION_ID = "kamaishi-nearshore-interface"
COUPLING_PATCH = "boundary.inland"
EXPECTED_TERRAIN_SHA256 = "45e5ab63a69e77ec11b293c39cbb93dd0df30a4f24d1a4f4d9515267a01f1363"
EXPECTED_SOURCE_SHA256 = "88f58bb256c8e5ff7baa8ec662118572b1e6a1b38b0fdd9b85a0541ddf6f6498"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional-spatial-frozen-terrain-v4")
DEFAULT_G6_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi")
DEFAULT_R2D_BINARY = Path("build/linux-gcc-crs-release/apps/r2d_case/tsunami_r2d_case")
FORCING_WINDOW_S = (245.0, 545.0)
PILOT_TIME_S = 60.0
FULL_TIME_S = 600.0
SECTION_WIDTH_M = 8000.0
COMMON_SUPPORT_COUNT = 401
QUALIFICATION_THRESHOLD = 0.02
COLORS = {
    "h1000": "#4c78a8",
    "h800": "#f58518",
    "h600": "#54a24b",
    "eta": "#4c78a8",
    "qn": "#f58518",
    "Qn": "#54a24b",
    "qbar": "#9467bd",
}


class R4ExecutionError(RuntimeError):
    """Raised when the frozen-family adapter must fail closed."""


@dataclass(frozen=True)
class CommandResult:
    command: list[str]
    returncode: int
    stdout: str
    wall_clock_s: float
    cpu_time_s: float
    peak_memory_kb: int


@dataclass(frozen=True)
class LevelData:
    level_id: str
    target_m: float
    case_root: Path
    run_id: str
    output_dir: Path
    mesh_path: Path
    samples: list[dict[str, str]]
    metadata: dict[str, Any]
    corridor: dict[str, Any]
    face_lengths_m: dict[int, float]
    offset_by_index_m: dict[int, float]
    baseline_by_index: dict[int, dict[str, str]]
    series: list[dict[str, float]]


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


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def file_sha256(path: Path) -> str:
    return c1a.file_sha256(path)


def run_command(command: Sequence[str], *, cwd: Path, log_path: Path | None = None) -> CommandResult:
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.monotonic()
    completed = subprocess.run(list(command), cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    elapsed = time.monotonic() - started
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    result = CommandResult(
        list(command),
        completed.returncode,
        completed.stdout,
        elapsed,
        (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime),
        int(after.ru_maxrss),
    )
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(" ".join(result.command) + "\n" + result.stdout, encoding="utf-8")
    return result


def canonical_hash(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")).hexdigest()


def copy_required_g6_case_inputs(g6_case: Path, case_root: Path) -> None:
    required_files = [
        "case.json",
        "manifests/datasets.json",
        "manifests/corridors/tohoku-kamaishi-centreline.json",
        "manifests/corridors/kamaishi-delivery-corridor.json",
        "manifests/corridors/kamaishi-delivery-corridor-evidence.json",
        "manifests/terrain/conditioned-terrain.json",
        "outputs/terrain/conditioned-terrain.tif",
        "outputs/terrain/conditioned-terrain.coverage.tif",
        "outputs/terrain/conditioned-terrain.lineage.tif",
        "inputs/data/terrain/conditioned-terrain.tif",
        "inputs/data/earthquake/tohoku_vertical_displacement.tif",
        "inputs/data/earthquake/tohoku_vertical_displacement.json",
        "inputs/data/source/terrain/ETOPO_2022_v1_15s_N45E135_surface.tif",
        "inputs/data/source/earthquake/usgs_usp000hvnu_1539808472261_basic_inversion.param",
        "inputs/data/points/tohoku-epicentre-source.json",
        "inputs/data/points/kamaishi-nearshore-interface-source.json",
    ]
    for relative in required_files:
        source = g6_case / relative
        if not source.is_file():
            raise R4ExecutionError(f"required G6 case input is missing: {source}")
        destination = case_root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


def set_case_final_time(case_root: Path, final_time_s: float) -> None:
    case_path = case_root / "case.json"
    case = read_json(case_path)
    case["regional_2d"]["numerics"]["final_time_s"] = float(final_time_s)
    case_path.write_text(json.dumps(case, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def frozen_authority_for_case(case_root: Path) -> dict[str, Any]:
    terrain_path = case_root / "outputs/terrain/conditioned-terrain.tif"
    terrain_record_path = case_root / "manifests/terrain/conditioned-terrain.json"
    source_path = case_root / "inputs/data/earthquake/tohoku_vertical_displacement.tif"
    source_metadata_path = case_root / "inputs/data/earthquake/tohoku_vertical_displacement.json"
    terrain_record = read_json(terrain_record_path)
    source_metadata = read_json(source_metadata_path)
    return {
        "terrain": {
            "path": terrain_path.as_posix(),
            "record_path": terrain_record_path.as_posix(),
            "sha256": file_sha256(terrain_path),
            "record_sha256": file_sha256(terrain_record_path),
            "metadata_sha256": canonical_hash(terrain_record),
            "processing_resolution_m": float(terrain_record["grid"]["spacing_m"]),
            "raster_dimensions": {"width": int(terrain_record["grid"]["width"]), "height": int(terrain_record["grid"]["height"])},
        },
        "source": {
            "path": source_path.as_posix(),
            "metadata_path": source_metadata_path.as_posix(),
            "sha256": file_sha256(source_path),
            "metadata_sha256": file_sha256(source_metadata_path),
            "representation_policy": "fixed G6 coseismic displacement raster projected to each solver mesh",
            "event_id": source_metadata.get("event_id"),
            "model_id": source_metadata.get("model_id"),
        },
    }


def fail_closed_invariance(case_root: Path) -> dict[str, Any]:
    authority = frozen_authority_for_case(case_root)
    failures: list[dict[str, Any]] = []
    expected = {
        "terrain.sha256": EXPECTED_TERRAIN_SHA256,
        "source.sha256": EXPECTED_SOURCE_SHA256,
        "terrain.processing_resolution_m": 1000.0,
    }
    actual = {
        "terrain.sha256": authority["terrain"]["sha256"],
        "source.sha256": authority["source"]["sha256"],
        "terrain.processing_resolution_m": authority["terrain"]["processing_resolution_m"],
    }
    for key, expected_value in expected.items():
        if actual[key] != expected_value:
            failures.append({"field": key, "expected": expected_value, "actual": actual[key]})
    case = read_json(case_root / "case.json")
    corridor = read_json(case_root / "manifests/corridors/kamaishi-delivery-corridor.json")
    corridor_evidence = read_json(case_root / "manifests/corridors/kamaishi-delivery-corridor-evidence.json")
    record = {
        **authority,
        "physical_configuration_sha256": canonical_hash(
            {
                "physics": case["regional_2d"]["physics"],
                "boundaries": case["regional_2d"]["boundaries"],
                "numerics_without_time_horizon": {
                    key: value for key, value in case["regional_2d"]["numerics"].items() if key != "final_time_s"
                },
            }
        ),
        "domain_sha256": canonical_hash(case["regional_2d"]["corridor"]),
        "coupling_section_sha256": canonical_hash(
            {
                "section_id": SECTION_ID,
                "patch": COUPLING_PATCH,
                "corridor": corridor,
                "evidence_basis": corridor_evidence.get("selected_nearshore_interface"),
                "basis": corridor_evidence.get("basis"),
            }
        ),
        "case_final_time_s": case["regional_2d"]["numerics"]["final_time_s"],
    }
    if failures:
        write_json(case_root / "r4_invariance_failure.json", {"status": "failed", "failures": failures, "record": record})
        raise R4ExecutionError("frozen-family invariance failed before solver launch: " + ", ".join(item["field"] for item in failures))
    return record


def adapter_level_record(case_record: dict[str, Any], requested_mesh_target_m: float, mesh_path: Path | None = None) -> dict[str, Any]:
    authority = {"terrain": case_record["terrain"], "source": case_record["source"]}
    contract = c1a.regional_frozen_terrain_resolution_contract(
        requested_solver_mesh_size_m=float(requested_mesh_target_m),
        frozen_authority=authority,
        profile_name=f"r4-frozen-g6-terrain-h{requested_mesh_target_m:g}",
    )
    contract["physical_configuration_sha256"] = case_record["physical_configuration_sha256"]
    contract["domain_sha256"] = case_record["domain_sha256"]
    contract["coupling_section_sha256"] = case_record["coupling_section_sha256"]
    if mesh_path is not None:
        contract["mesh_path"] = mesh_path.as_posix()
    return contract


def write_mesh_geo(case_root: Path, requested_mesh_target_m: float) -> Path:
    template = (case_root / "meshes/kamaishi-regional.geo").read_text(encoding="utf-8")
    lines = []
    replaced = False
    for line in template.splitlines():
        if line.startswith("lc = "):
            lines.append(f"lc = {float(requested_mesh_target_m):.17g};")
            replaced = True
        else:
            lines.append(line)
    if not replaced:
        raise R4ExecutionError("G6 mesh template does not define lc")
    output = case_root / f"meshes/r4-h{requested_mesh_target_m:g}.geo"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return output


def generate_mesh(case_root: Path, requested_mesh_target_m: float, logs_root: Path) -> Path:
    geo_path = write_mesh_geo(case_root, requested_mesh_target_m)
    mesh_path = case_root / f"meshes/r4-h{requested_mesh_target_m:g}.msh"
    result = run_command(
        ["gmsh", "-2", str(geo_path), "-format", "msh4", "-o", str(mesh_path)],
        cwd=repo_root(),
        log_path=logs_root / f"gmsh-h{requested_mesh_target_m:g}.log",
    )
    if result.returncode != 0:
        raise R4ExecutionError(f"gmsh failed for h{requested_mesh_target_m:g}: {result.stdout[-1000:]}")
    return mesh_path


def parse_msh_triangles(mesh_path: Path) -> dict[str, Any]:
    lines = mesh_path.read_text(encoding="utf-8", errors="replace").splitlines()
    nodes: dict[int, tuple[float, float, float]] = {}
    triangles: list[tuple[int, int, int]] = []
    index = 0
    while index < len(lines):
        marker = lines[index]
        if marker == "$Nodes":
            block_count, _, _, _ = map(int, lines[index + 1].split())
            index += 2
            for _ in range(block_count):
                _, _, _, node_count = map(int, lines[index].split())
                index += 1
                tags = [int(lines[index + offset]) for offset in range(node_count)]
                index += node_count
                coords = [tuple(float(value) for value in lines[index + offset].split()[:3]) for offset in range(node_count)]
                index += node_count
                nodes.update(dict(zip(tags, coords)))
        elif marker == "$Elements":
            block_count, _, _, _ = map(int, lines[index + 1].split())
            index += 2
            for _ in range(block_count):
                entity_dim, _, element_type, element_count = map(int, lines[index].split())
                index += 1
                for _ in range(element_count):
                    values = [int(value) for value in lines[index].split()]
                    index += 1
                    if entity_dim == 2 and element_type == 2:
                        triangles.append((values[1], values[2], values[3]))
        else:
            index += 1
    areas = []
    for a_id, b_id, c_id in triangles:
        a = nodes[a_id]
        b = nodes[b_id]
        c = nodes[c_id]
        areas.append(abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) * 0.5)
    if not areas:
        raise R4ExecutionError(f"no triangular cells found in {mesh_path}")
    domain_area = sum(areas)
    return {
        "mesh_path": mesh_path.as_posix(),
        "mesh_sha256": file_sha256(mesh_path),
        "active_cells": len(areas),
        "total_cells": len(areas),
        "domain_area_m2": domain_area,
        "minimum_cell_area_m2": min(areas),
        "maximum_cell_area_m2": max(areas),
        "mean_cell_area_m2": statistics.fmean(areas),
        "actual_characteristic_h_m": math.sqrt(domain_area / len(areas)),
    }


def parse_msh_surface_cells(mesh_path: Path) -> list[dict[str, float]]:
    lines = mesh_path.read_text(encoding="utf-8", errors="replace").splitlines()
    nodes: dict[int, tuple[float, float, float]] = {}
    triangles: list[tuple[int, int, int]] = []
    index = 0
    while index < len(lines):
        marker = lines[index]
        if marker == "$Nodes":
            block_count, _, _, _ = map(int, lines[index + 1].split())
            index += 2
            for _ in range(block_count):
                _, _, _, node_count = map(int, lines[index].split())
                index += 1
                tags = [int(lines[index + offset]) for offset in range(node_count)]
                index += node_count
                coords = [tuple(float(value) for value in lines[index + offset].split()[:3]) for offset in range(node_count)]
                index += node_count
                nodes.update(dict(zip(tags, coords)))
        elif marker == "$Elements":
            block_count, _, _, _ = map(int, lines[index + 1].split())
            index += 2
            for _ in range(block_count):
                entity_dim, _, element_type, element_count = map(int, lines[index].split())
                index += 1
                for _ in range(element_count):
                    values = [int(value) for value in lines[index].split()]
                    index += 1
                    if entity_dim == 2 and element_type == 2:
                        triangles.append((values[1], values[2], values[3]))
        else:
            index += 1
    cells: list[dict[str, float]] = []
    for cell_index, (a_id, b_id, c_id) in enumerate(triangles):
        a = nodes[a_id]
        b = nodes[b_id]
        c = nodes[c_id]
        area = abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) * 0.5
        cells.append(
            {
                "cell": float(cell_index),
                "area_m2": area,
                "centroid_x_m": (a[0] + b[0] + c[0]) / 3.0,
                "centroid_y_m": (a[1] + b[1] + c[1]) / 3.0,
            }
        )
    return cells


def parse_boundary_face_lengths(mesh_path: Path, boundary_name: str, samples: Sequence[dict[str, Any]]) -> dict[int, float]:
    lines = mesh_path.read_text(encoding="utf-8", errors="replace").splitlines()
    physical_tags: dict[str, int] = {}
    boundary_entities: set[int] = set()
    nodes: dict[int, tuple[float, float, float]] = {}
    elements: list[tuple[int, int, int, int, list[int]]] = []
    index = 0
    while index < len(lines):
        marker = lines[index]
        if marker == "$PhysicalNames":
            count = int(lines[index + 1])
            index += 2
            for _ in range(count):
                parts = lines[index].split(maxsplit=2)
                physical_tags[parts[2].strip('"')] = int(parts[1])
                index += 1
        elif marker == "$Entities":
            point_count, curve_count, surface_count, volume_count = map(int, lines[index + 1].split())
            index += 2 + point_count
            target_physical = physical_tags.get(boundary_name)
            for _ in range(curve_count):
                parts = lines[index].split()
                entity_tag = int(parts[0])
                physical_count = int(parts[7])
                physicals = [int(value) for value in parts[8 : 8 + physical_count]]
                if target_physical in physicals:
                    boundary_entities.add(entity_tag)
                index += 1
            index += surface_count + volume_count
        elif marker == "$Nodes":
            block_count, _, _, _ = map(int, lines[index + 1].split())
            index += 2
            for _ in range(block_count):
                _, _, _, node_count = map(int, lines[index].split())
                index += 1
                tags = [int(lines[index + offset]) for offset in range(node_count)]
                index += node_count
                coords = [tuple(float(value) for value in lines[index + offset].split()[:3]) for offset in range(node_count)]
                index += node_count
                nodes.update(dict(zip(tags, coords)))
        elif marker == "$Elements":
            block_count, _, _, _ = map(int, lines[index + 1].split())
            index += 2
            for _ in range(block_count):
                entity_dim, entity_tag, element_type, element_count = map(int, lines[index].split())
                index += 1
                for _ in range(element_count):
                    values = [int(value) for value in lines[index].split()]
                    index += 1
                    elements.append((entity_dim, entity_tag, element_type, values[0], values[1:]))
        else:
            index += 1
    if not boundary_entities:
        raise R4ExecutionError(f"could not find physical boundary {boundary_name!r} in {mesh_path}")
    line_elements: list[tuple[float, float, float]] = []
    for entity_dim, entity_tag, element_type, _, node_tags in elements:
        if entity_dim == 1 and entity_tag in boundary_entities and element_type == 1:
            a = nodes[node_tags[0]]
            b = nodes[node_tags[1]]
            line_elements.append(((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, math.hypot(a[0] - b[0], a[1] - b[1])))
    if len(line_elements) != len(samples):
        raise R4ExecutionError(f"{mesh_path} has {len(line_elements)} boundary faces, but metadata has {len(samples)} samples")
    matched: dict[int, float] = {}
    available = list(line_elements)
    for sample in samples:
        sx = float(sample["x_m"])
        sy = float(sample["y_m"])
        best_index = min(range(len(available)), key=lambda i: math.hypot(available[i][0] - sx, available[i][1] - sy))
        cx, cy, length = available.pop(best_index)
        if math.hypot(cx - sx, cy - sy) > 1.0e-6:
            raise R4ExecutionError(f"could not match boundary face centre within tolerance for {mesh_path}")
        matched[int(sample["local_index"])] = length
    return matched


def read_diagnostics(path: Path) -> list[dict[str, float]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        rows = []
        for row in csv.DictReader(handle):
            converted: dict[str, float] = {}
            for key, value in row.items():
                try:
                    converted[key] = float(value)
                except (TypeError, ValueError):
                    pass
            rows.append(converted)
        return rows


def timestep_stats(output_dir: Path) -> dict[str, Any]:
    diagnostics = read_diagnostics(output_dir / "diagnostics.csv")
    dt_values = [
        row.get("dt", row.get("timestep"))
        for row in diagnostics
        if row.get("dt", row.get("timestep")) is not None and row.get("dt", row.get("timestep")) > 0.0
    ]
    if not dt_values:
        return {"status": "missing_dt"}
    return {
        "minimum_dt_s": min(dt_values),
        "maximum_dt_s": max(dt_values),
        "mean_dt_s": statistics.fmean(dt_values),
        "median_dt_s": statistics.median(dt_values),
        "step_count": len(dt_values),
        "diagnostics_rows": len(diagnostics),
        "inferred_active_timestep_limiter": "explicit_stability_or_source_restriction",
    }


def run_regional(
    case_root: Path,
    mesh_path: Path,
    run_id: str,
    r2d_binary: Path,
    logs_root: Path,
) -> dict[str, Any]:
    rel_mesh = mesh_path.relative_to(case_root)
    command = [
        str(r2d_binary),
        "--case-root",
        str(case_root),
        "--terrain-record",
        "manifests/terrain/conditioned-terrain.json",
        "--mesh",
        rel_mesh.as_posix(),
        "--run-id",
        run_id,
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
    result = run_command(command, cwd=repo_root(), log_path=logs_root / f"{run_id}.log")
    if result.returncode != 0:
        return {
            "status": "failed",
            "run_id": run_id,
            "returncode": result.returncode,
            "log_tail": result.stdout[-4000:],
            "resource_usage": {
                "wall_clock_s": result.wall_clock_s,
                "cpu_time_s": result.cpu_time_s,
                "peak_memory_kb": result.peak_memory_kb,
            },
        }
    output_dir = case_root / "runs" / run_id / "outputs/regional2d"
    return {
        "status": "passed",
        "run_id": run_id,
        "output_dir": output_dir.as_posix(),
        "resource_usage": {
            "wall_clock_s": result.wall_clock_s,
            "cpu_time_s": result.cpu_time_s,
            "peak_memory_kb": result.peak_memory_kb,
        },
        "timestep": timestep_stats(output_dir),
        "stdout": result.stdout.strip(),
    }


def prepare_shared_case(external_root: Path, g6_root: Path) -> tuple[Path, dict[str, Any]]:
    case_root = external_root / "case"
    copy_required_g6_case_inputs(g6_root / "case", case_root)
    (case_root / "meshes").mkdir(parents=True, exist_ok=True)
    shutil.copy2(g6_root / "case/meshes/kamaishi-regional.geo", case_root / "meshes/kamaishi-regional.geo")
    set_case_final_time(case_root, FULL_TIME_S)
    case_record = fail_closed_invariance(case_root)
    return case_root, case_record


def command_preflight(args: argparse.Namespace) -> int:
    external_root = args.external_root
    case_root, case_record = prepare_shared_case(external_root, args.g6_root)
    logs_root = external_root / "logs"
    rows = []
    records = []
    for target in args.targets:
        mesh_path = generate_mesh(case_root, float(target), logs_root)
        mesh = parse_msh_triangles(mesh_path)
        record = adapter_level_record(case_record, float(target), mesh_path)
        record["mesh"] = mesh
        record["actual_characteristic_mesh_size"]["value_m"] = mesh["actual_characteristic_h_m"]
        records.append(record)
        rows.append(
            {
                "requested_solver_target_m": float(target),
                "actual_characteristic_h_m": mesh["actual_characteristic_h_m"],
                "active_cells": mesh["active_cells"],
                "total_cells": mesh["total_cells"],
                "domain_area_m2": mesh["domain_area_m2"],
                "minimum_cell_area_m2": mesh["minimum_cell_area_m2"],
                "maximum_cell_area_m2": mesh["maximum_cell_area_m2"],
                "mean_cell_area_m2": mesh["mean_cell_area_m2"],
                "mesh_sha256": mesh["mesh_sha256"],
                "terrain_sha256": case_record["terrain"]["sha256"],
                "source_sha256": case_record["source"]["sha256"],
            }
        )
    c1a.assert_regional_frozen_family_invariance(records)
    summary = {
        "schema": {"name": "tsunami.c1a_r4_frozen_mesh_preflight", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "case_root": case_root.as_posix(),
        "frozen_family_invariance": "passed",
        "case_record": case_record,
        "candidate_meshes": rows,
    }
    write_json(external_root / "mesh_preflight.json", summary)
    write_csv(
        external_root / "mesh_preflight.csv",
        [
            "requested_solver_target_m",
            "actual_characteristic_h_m",
            "active_cells",
            "total_cells",
            "domain_area_m2",
            "minimum_cell_area_m2",
            "maximum_cell_area_m2",
            "mean_cell_area_m2",
            "mesh_sha256",
            "terrain_sha256",
            "source_sha256",
        ],
        rows,
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


def command_pilot(args: argparse.Namespace) -> int:
    external_root = args.external_root
    case_root, case_record = prepare_shared_case(external_root, args.g6_root)
    logs_root = external_root / "logs"
    preflight = read_json(external_root / "mesh_preflight.json") if (external_root / "mesh_preflight.json").is_file() else None
    set_case_final_time(case_root, PILOT_TIME_S)
    pilots = []
    for target in args.targets:
        mesh_path = case_root / f"meshes/r4-h{float(target):g}.msh"
        if not mesh_path.is_file():
            mesh_path = generate_mesh(case_root, float(target), logs_root)
        run_id = f"r4-pilot-h{float(target):g}"
        result = run_regional(case_root, mesh_path, run_id, args.r2d_binary, logs_root)
        simulated = PILOT_TIME_S if result["status"] == "passed" else None
        wall = result["resource_usage"]["wall_clock_s"]
        result.update(
            {
                "requested_solver_target_m": float(target),
                "mesh": parse_msh_triangles(mesh_path),
                "projected_600s_wall_s": wall * FULL_TIME_S / simulated if simulated and wall else None,
                "case_invariance": {
                    "terrain_sha256": case_record["terrain"]["sha256"],
                    "source_sha256": case_record["source"]["sha256"],
                },
            }
        )
        pilots.append(result)
        write_json(external_root / "pilots" / f"h{float(target):g}" / "pilot.json", result)
    set_case_final_time(case_root, FULL_TIME_S)
    summary = {
        "schema": {"name": "tsunami.c1a_r4_frozen_pilots", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "preflight_source": str(external_root / "mesh_preflight.json") if preflight else None,
        "pilots": pilots,
    }
    write_json(external_root / "pilot_summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


def command_run_full(args: argparse.Namespace) -> int:
    external_root = args.external_root
    case_root, case_record = prepare_shared_case(external_root, args.g6_root)
    logs_root = external_root / "logs"
    set_case_final_time(case_root, FULL_TIME_S)
    pilot_summary = read_json(external_root / "pilot_summary.json") if (external_root / "pilot_summary.json").is_file() else None
    pilot_by_target = {
        float(pilot["requested_solver_target_m"]): pilot
        for pilot in (pilot_summary or {}).get("pilots", [])
        if pilot.get("status") == "passed"
    }
    runs = []
    for target in args.targets:
        target = float(target)
        pilot = pilot_by_target.get(target)
        if pilot is not None and pilot.get("projected_600s_wall_s") is not None:
            projected = float(pilot["projected_600s_wall_s"])
            if projected > args.max_projected_wall_s:
                runs.append(
                    {
                        "status": "resource_limited_not_started",
                        "requested_solver_target_m": target,
                        "projected_600s_wall_s": projected,
                        "max_projected_wall_s": args.max_projected_wall_s,
                    }
                )
                continue
        mesh_path = case_root / f"meshes/r4-h{target:g}.msh"
        if not mesh_path.is_file():
            mesh_path = generate_mesh(case_root, target, logs_root)
        run_id = f"r4-full-h{target:g}"
        result = run_regional(case_root, mesh_path, run_id, args.r2d_binary, logs_root)
        result.update(
            {
                "requested_solver_target_m": target,
                "mesh": parse_msh_triangles(mesh_path),
                "case_invariance": {
                    "terrain_sha256": case_record["terrain"]["sha256"],
                    "source_sha256": case_record["source"]["sha256"],
                    "physical_configuration_sha256": case_record["physical_configuration_sha256"],
                    "domain_sha256": case_record["domain_sha256"],
                    "coupling_section_sha256": case_record["coupling_section_sha256"],
                },
            }
        )
        write_json(external_root / "spatial" / f"h{target:g}" / "run.json", result)
        runs.append(result)
    summary = {
        "schema": {"name": "tsunami.c1a_r4_frozen_spatial_runs", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "resource_gate": {
            "max_projected_wall_s": args.max_projected_wall_s,
            "pilot_summary": str(external_root / "pilot_summary.json") if pilot_summary else None,
        },
        "runs": runs,
    }
    write_json(external_root / "spatial_run_summary.json", summary)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


def coupling_dir(case_root: Path, run_id: str) -> Path:
    return case_root / "runs" / run_id / "outputs/regional2d/coupling" / SECTION_ID


def common_support() -> list[float]:
    start = -SECTION_WIDTH_M * 0.5
    step = SECTION_WIDTH_M / float(COMMON_SUPPORT_COUNT - 1)
    return [start + step * index for index in range(COMMON_SUPPORT_COUNT)]


def derive_level_data(level_id: str, target_m: float, case_root: Path, run: dict[str, Any]) -> LevelData:
    run_id = str(run["run_id"])
    output_dir = Path(run["output_dir"])
    mesh_path = Path(run["mesh"]["mesh_path"])
    cdir = coupling_dir(case_root, run_id)
    samples = read_csv(cdir / "samples.csv")
    metadata = read_json(cdir / "metadata.json")
    corridor = read_json(case_root / "manifests/corridors/kamaishi-delivery-corridor-evidence.json")
    normal = (
        float(corridor["basis"]["centreline_unit"]["x"]),
        float(corridor["basis"]["centreline_unit"]["y"]),
    )
    cross = (
        float(corridor["basis"]["left_normal_unit"]["x"]),
        float(corridor["basis"]["left_normal_unit"]["y"]),
    )
    origin = corridor["selected_nearshore_interface"]["projected_m"]
    offsets = {
        int(sample["local_index"]): (float(sample["x_m"]) - float(origin["x"])) * cross[0]
        + (float(sample["y_m"]) - float(origin["y"])) * cross[1]
        for sample in metadata["samples"]
    }
    face_lengths = parse_boundary_face_lengths(mesh_path, COUPLING_PATCH, metadata["samples"])
    by_time: dict[float, list[dict[str, str]]] = {}
    for row in samples:
        by_time.setdefault(float(row["time"]), []).append(row)
    baseline_time = min(by_time)
    baseline = {int(row["local_index"]): row for row in by_time[baseline_time]}
    series: list[dict[str, float]] = []
    for time_s in sorted(by_time):
        rows = by_time[time_s]
        eta_values: list[float] = []
        qn_values: list[float] = []
        Qn = 0.0
        for row in rows:
            local_index = int(row["local_index"])
            base = baseline[local_index]
            eta = float(row["free_surface_elevation"]) - float(base["free_surface_elevation"])
            qn = float(row["momentum_x"]) * normal[0] + float(row["momentum_y"]) * normal[1]
            base_qn = float(base["momentum_x"]) * normal[0] + float(base["momentum_y"]) * normal[1]
            delta_qn = qn - base_qn
            eta_values.append(eta)
            qn_values.append(delta_qn)
            Qn += delta_qn * face_lengths[local_index]
        series.append(
            {
                "time_s": time_s,
                "eta_m": max(eta_values, key=lambda value: abs(value)),
                "qn_m2_per_s": max(qn_values, key=lambda value: abs(value)),
                "Qn_m3_per_s": Qn,
                "qbar_m2_per_s": Qn / SECTION_WIDTH_M,
            }
        )
    return LevelData(level_id, target_m, case_root, run_id, output_dir, mesh_path, samples, metadata, corridor, face_lengths, offsets, baseline, series)


def rows_in_window(series: Sequence[dict[str, float]]) -> list[dict[str, float]]:
    start, end = FORCING_WINDOW_S
    return [row for row in series if start - 1.0e-9 <= row["time_s"] <= end + 1.0e-9]


def values_on_common_times(
    candidate: Sequence[dict[str, float]], reference: Sequence[dict[str, float]], quantity: str
) -> tuple[list[float], list[float], list[float]]:
    candidate_by_time = {round(row["time_s"], 9): row for row in rows_in_window(candidate)}
    reference_by_time = {round(row["time_s"], 9): row for row in rows_in_window(reference)}
    times = sorted(set(candidate_by_time).intersection(reference_by_time))
    if len(times) < 3:
        raise R4ExecutionError(f"insufficient common time samples for {quantity}")
    return (
        [candidate_by_time[time][quantity] for time in times],
        [reference_by_time[time][quantity] for time in times],
        [float(time) for time in times],
    )


def rmse(candidate: Sequence[float], reference: Sequence[float]) -> float:
    if len(candidate) != len(reference) or not candidate:
        raise R4ExecutionError("RMSE requires non-empty equal-length series")
    return math.sqrt(sum((c - r) ** 2 for c, r in zip(candidate, reference)) / len(candidate))


def waveform_metric(candidate: Sequence[dict[str, float]], reference: Sequence[dict[str, float]], quantity: str) -> dict[str, Any]:
    candidate_values, reference_values, times = values_on_common_times(candidate, reference, quantity)
    peak_candidate_index = max(range(len(candidate_values)), key=lambda index: abs(candidate_values[index]))
    peak_reference_index = max(range(len(reference_values)), key=lambda index: abs(reference_values[index]))
    dt_s = statistics.median([b - a for a, b in zip(times, times[1:])]) if len(times) > 1 else 0.0
    return {
        "sample_count": len(times),
        "time_window_s": list(FORCING_WINDOW_S),
        "rmse": rmse(candidate_values, reference_values),
        "nrmse": c1a.nrmse(candidate_values, reference_values),
        "max_abs_difference": max(abs(c - r) for c, r in zip(candidate_values, reference_values)),
        "candidate_peak_abs": abs(candidate_values[peak_candidate_index]),
        "candidate_peak_time_s": times[peak_candidate_index],
        "reference_peak_abs": abs(reference_values[peak_reference_index]),
        "reference_peak_time_s": times[peak_reference_index],
        "correlation": c1a._pearson(candidate_values, reference_values),
        "phase_alignment": c1a.phase_alignment_diagnostic(candidate_values, reference_values, dt_s, max_lag_steps=20),
        "formal_metric_shifted": False,
    }


def qoi_summary(series: Sequence[dict[str, float]]) -> dict[str, Any]:
    rows = rows_in_window(series)

    def peak(quantity: str) -> tuple[float, float, float]:
        index = max(range(len(rows)), key=lambda item: abs(rows[item][quantity]))
        return abs(rows[index][quantity]), rows[index]["time_s"], rows[index][quantity]

    eta_abs, eta_time, eta_signed = peak("eta_m")
    qn_abs, qn_time, qn_signed = peak("qn_m2_per_s")
    Qn_abs, Qn_time, Qn_signed = peak("Qn_m3_per_s")
    qbar_abs, qbar_time, qbar_signed = peak("qbar_m2_per_s")
    arrival_threshold = 0.05 * Qn_abs
    arrival = next((row["time_s"] for row in rows if abs(row["Qn_m3_per_s"]) >= arrival_threshold), None)
    return {
        "time_window_s": list(FORCING_WINDOW_S),
        "peak_eta_abs_m": eta_abs,
        "peak_eta_signed_m": eta_signed,
        "peak_eta_time_s": eta_time,
        "peak_qn_abs_m2_per_s": qn_abs,
        "peak_qn_signed_m2_per_s": qn_signed,
        "peak_qn_time_s": qn_time,
        "peak_Qn_abs_m3_per_s": Qn_abs,
        "peak_Qn_signed_m3_per_s": Qn_signed,
        "peak_Qn_time_s": Qn_time,
        "peak_qbar_abs_m2_per_s": qbar_abs,
        "peak_qbar_signed_m2_per_s": qbar_signed,
        "peak_qbar_time_s": qbar_time,
        "arrival_time_proxy_s": arrival,
        "arrival_time_proxy_definition": "first formal-window sample where abs(Qn) reaches 5% of its own formal-window peak",
    }


def interpolate_profile(points: Sequence[tuple[float, float]], xs: Sequence[float]) -> list[float]:
    ordered = sorted(points)
    result: list[float] = []
    for x in xs:
        if x <= ordered[0][0]:
            result.append(ordered[0][1])
            continue
        if x >= ordered[-1][0]:
            result.append(ordered[-1][1])
            continue
        for (x0, y0), (x1, y1) in zip(ordered, ordered[1:]):
            if x0 <= x <= x1:
                fraction = 0.0 if x1 == x0 else (x - x0) / (x1 - x0)
                result.append(y0 + fraction * (y1 - y0))
                break
    return result


def profile_at_time(level: LevelData, time_s: float, quantity: str) -> list[tuple[float, float]]:
    normal = (
        float(level.corridor["basis"]["centreline_unit"]["x"]),
        float(level.corridor["basis"]["centreline_unit"]["y"]),
    )
    rows = [row for row in level.samples if abs(float(row["time"]) - time_s) < 1.0e-9]
    points: list[tuple[float, float]] = []
    for row in rows:
        local_index = int(row["local_index"])
        base = level.baseline_by_index[local_index]
        if quantity == "qn":
            value = (
                float(row["momentum_x"]) * normal[0]
                + float(row["momentum_y"]) * normal[1]
                - float(base["momentum_x"]) * normal[0]
                - float(base["momentum_y"]) * normal[1]
            )
        elif quantity == "eta":
            value = float(row["free_surface_elevation"]) - float(base["free_surface_elevation"])
        elif quantity == "bed":
            value = float(row["bed_elevation"])
        else:
            raise ValueError(quantity)
        points.append((level.offset_by_index_m[local_index], value))
    return points


def distributed_metric(candidate: LevelData, reference: LevelData, quantity: str) -> dict[str, Any]:
    support = common_support()
    candidate_times = {round(row["time_s"], 9) for row in rows_in_window(candidate.series)}
    reference_times = {round(row["time_s"], 9) for row in rows_in_window(reference.series)}
    times = sorted(candidate_times.intersection(reference_times))
    candidate_values: list[float] = []
    reference_values: list[float] = []
    for time_s in times:
        candidate_values.extend(interpolate_profile(profile_at_time(candidate, time_s, quantity), support))
        reference_values.extend(interpolate_profile(profile_at_time(reference, time_s, quantity), support))
    return {
        "quantity": quantity,
        "time_sample_count": len(times),
        "support_point_count": len(support),
        "common_support_width_m": SECTION_WIDTH_M,
        "rmse": rmse(candidate_values, reference_values),
        "nrmse": c1a.nrmse(candidate_values, reference_values),
        "max_abs_difference": max(abs(c - r) for c, r in zip(candidate_values, reference_values)),
        "formal_metric_shifted": False,
    }


def bed_projection_metric(candidate: LevelData, reference: LevelData) -> dict[str, Any]:
    support = common_support()
    candidate_values = interpolate_profile(profile_at_time(candidate, 0.0, "bed"), support)
    reference_values = interpolate_profile(profile_at_time(reference, 0.0, "bed"), support)
    differences = [c - r for c, r in zip(candidate_values, reference_values)]
    return {
        "support_point_count": len(support),
        "common_support_width_m": SECTION_WIDTH_M,
        "rmse_m": rmse(candidate_values, reference_values),
        "nrmse": c1a.nrmse(candidate_values, reference_values),
        "bias_m": statistics.fmean(differences),
        "max_abs_difference_m": max(abs(value) for value in differences),
    }


def source_projection(level: LevelData) -> dict[str, Any]:
    rows = read_csv(level.output_dir / "snapshots.csv")
    initial_rows = [row for row in rows if abs(float(row["time"])) < 1.0e-12]
    cells = parse_msh_surface_cells(level.mesh_path)
    if len(initial_rows) != len(cells):
        raise R4ExecutionError(f"{level.level_id} snapshot cell count does not match mesh cells")
    signed_weight = 0.0
    signed_x = 0.0
    signed_y = 0.0
    absolute_weight = 0.0
    absolute_x = 0.0
    absolute_y = 0.0
    for row, cell in zip(sorted(initial_rows, key=lambda item: int(item["cell"])), cells):
        perturbation = float(row["free_surface_elevation"])
        area = cell["area_m2"]
        signed = perturbation * area
        absolute = abs(perturbation) * area
        signed_weight += signed
        signed_x += signed * cell["centroid_x_m"]
        signed_y += signed * cell["centroid_y_m"]
        absolute_weight += absolute
        absolute_x += absolute * cell["centroid_x_m"]
        absolute_y += absolute * cell["centroid_y_m"]
    earthquake = read_csv(level.output_dir / "earthquake_initialisation.csv")[0]
    return {
        "integrated_surface_perturbation_m3": float(earthquake["integrated_surface_perturbation"]),
        "maximum_absolute_surface_perturbation_m": float(earthquake["maximum_absolute_surface_perturbation"]),
        "maximum_absolute_bathymetry_change_m": float(earthquake["maximum_absolute_bathymetry_change"]),
        "cell_count": int(float(earthquake["cell_count"])),
        "signed_centroid_m": {
            "x": signed_x / signed_weight if abs(signed_weight) > 1.0e-300 else None,
            "y": signed_y / signed_weight if abs(signed_weight) > 1.0e-300 else None,
        },
        "absolute_centroid_m": {
            "x": absolute_x / absolute_weight if absolute_weight > 1.0e-300 else None,
            "y": absolute_y / absolute_weight if absolute_weight > 1.0e-300 else None,
        },
        "centroid_definition": "time-zero free-surface perturbation integrated over mesh cell areas from snapshots.csv",
    }


def source_comparison(candidate: dict[str, Any], reference: dict[str, Any]) -> dict[str, Any]:
    def distance(left: dict[str, float | None], right: dict[str, float | None]) -> float | None:
        if left["x"] is None or left["y"] is None or right["x"] is None or right["y"] is None:
            return None
        return math.hypot(float(left["x"]) - float(right["x"]), float(left["y"]) - float(right["y"]))

    return {
        "integrated_surface_perturbation_relative_change": c1a.relative_change(
            reference["integrated_surface_perturbation_m3"], candidate["integrated_surface_perturbation_m3"]
        ),
        "maximum_absolute_surface_perturbation_relative_change": c1a.relative_change(
            reference["maximum_absolute_surface_perturbation_m"], candidate["maximum_absolute_surface_perturbation_m"]
        ),
        "signed_centroid_shift_m": distance(candidate["signed_centroid_m"], reference["signed_centroid_m"]),
        "absolute_centroid_shift_m": distance(candidate["absolute_centroid_m"], reference["absolute_centroid_m"]),
    }


def timestep_stats_from_diagnostics(output_dir: Path) -> dict[str, Any]:
    rows = read_diagnostics(output_dir / "diagnostics.csv")
    raw_rows = read_csv(output_dir / "diagnostics.csv")
    dt_values = [row["timestep"] for row in rows if row.get("timestep", 0.0) > 0.0]
    wet = [row["wet_cells"] for row in rows if "wet_cells" in row]
    dry = [row["dry_cells"] for row in rows if "dry_cells" in row]
    rejected = [row.get("rejected_attempts", 0.0) for row in rows]
    source_restriction_rows = sum(1 for row in raw_rows if row.get("source_restriction") == "present")
    return {
        **timestep_stats(output_dir),
        "rejected_attempts_total": int(sum(rejected)),
        "source_restriction_rows": source_restriction_rows,
        "wet_cell_min": int(min(wet)) if wet else None,
        "wet_cell_max": int(max(wet)) if wet else None,
        "dry_cell_min": int(min(dry)) if dry else None,
        "dry_cell_max": int(max(dry)) if dry else None,
        "final_diagnostic_time_s": max((row.get("end_time", 0.0) for row in rows), default=None),
    }


def refresh_pilot_timesteps(external_root: Path) -> dict[str, Any]:
    summary_path = external_root / "pilot_summary.json"
    summary = read_json(summary_path)
    refreshed = []
    for pilot in summary.get("pilots", []):
        if pilot.get("status") != "passed" or not pilot.get("output_dir"):
            refreshed.append(pilot)
            continue
        pilot["timestep"] = timestep_stats_from_diagnostics(Path(pilot["output_dir"]))
        pilot_path = external_root / "pilots" / f"h{float(pilot['requested_solver_target_m']):g}" / "pilot.json"
        if pilot_path.is_file():
            individual = read_json(pilot_path)
            individual["timestep"] = pilot["timestep"]
            write_json(pilot_path, individual)
        refreshed.append(pilot)
    summary["generated_at_utc"] = utc_now()
    summary["pilots"] = refreshed
    write_json(summary_path, summary)
    return summary


def path_points(
    series_by_level: dict[str, Sequence[dict[str, float]]],
    x_key: str,
    y_key: str,
    width: int,
    height: int,
    margin: tuple[int, int, int, int],
) -> tuple[dict[str, str], tuple[float, float, float, float]]:
    left, top, right, bottom = margin
    xs = [row[x_key] for series in series_by_level.values() for row in series]
    ys = [row[y_key] for series in series_by_level.values() for row in series]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    if ymin == ymax:
        ymin -= 1.0
        ymax += 1.0
    ypad = 0.08 * (ymax - ymin)
    ymin -= ypad
    ymax += ypad
    xspan = xmax - xmin or 1.0
    yspan = ymax - ymin or 1.0

    def map_x(x: float) -> float:
        return left + (x - xmin) / xspan * (width - left - right)

    def map_y(y: float) -> float:
        return height - bottom - (y - ymin) / yspan * (height - top - bottom)

    return (
        {
            level: " ".join(f"{map_x(row[x_key]):.2f},{map_y(row[y_key]):.2f}" for row in series)
            for level, series in series_by_level.items()
        },
        (xmin, xmax, ymin, ymax),
    )


def tick_values(minimum: float, maximum: float, count: int = 6) -> list[float]:
    if minimum == maximum:
        return [minimum]
    step = (maximum - minimum) / (count - 1)
    return [minimum + step * index for index in range(count)]


def format_tick(value: float) -> str:
    if abs(value) >= 1000.0:
        return f"{value:.0f}"
    if abs(value) >= 10.0:
        return f"{value:.1f}".rstrip("0").rstrip(".")
    return f"{value:.2f}".rstrip("0").rstrip(".")


def svg_line_plot(
    output: Path,
    title: str,
    desc: str,
    x_label: str,
    y_label: str,
    series_by_level: dict[str, Sequence[dict[str, float]]],
    y_key: str,
    colors: dict[str, str],
    x_key: str = "time_s",
    extra_note: str = "",
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    width, height = 960, 540
    margin = (82, 58, 34, 76)
    left, top, right, bottom = margin
    points, bounds = path_points(series_by_level, x_key, y_key, width, height, margin)
    xmin, xmax, ymin, ymax = bounds
    xspan = xmax - xmin or 1.0
    yspan = ymax - ymin or 1.0

    def map_x(x: float) -> float:
        return left + (x - xmin) / xspan * (width - left - right)

    def map_y(y: float) -> float:
        return height - bottom - (y - ymin) / yspan * (height - top - bottom)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
        f'<title id="title">{html.escape(title)}</title>',
        f'<desc id="desc">{html.escape(desc)}</desc>',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="{left}" y="31" font-family="Arial, sans-serif" font-size="22" fill="#222">{html.escape(title)}</text>',
    ]
    for value in tick_values(xmin, xmax, 7):
        x = map_x(value)
        parts.append(f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{height-bottom}" stroke="#e4e4e4" stroke-width="1"/>')
        parts.append(f'<text x="{x:.2f}" y="{height-bottom+24}" text-anchor="middle" font-family="Arial, sans-serif" font-size="13" fill="#555">{format_tick(value)}</text>')
    for value in tick_values(ymin, ymax, 6):
        y = map_y(value)
        parts.append(f'<line x1="{left}" y1="{y:.2f}" x2="{width-right}" y2="{y:.2f}" stroke="#e8e8e8" stroke-width="1"/>')
        parts.append(f'<text x="{left-10}" y="{y+4:.2f}" text-anchor="end" font-family="Arial, sans-serif" font-size="13" fill="#555">{format_tick(value)}</text>')
    parts.append(f'<line x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}" stroke="#333" stroke-width="1.4"/>')
    parts.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}" stroke="#333" stroke-width="1.4"/>')
    for level, point_text in points.items():
        parts.append(f'<polyline fill="none" stroke="{colors[level]}" stroke-width="3" stroke-linejoin="round" stroke-linecap="round" points="{point_text}"/>')
    for offset, level in enumerate(series_by_level):
        y = 76 + offset * 24
        parts.append(f'<line x1="102" y1="{y}" x2="130" y2="{y}" stroke="{colors[level]}" stroke-width="4"/>')
        parts.append(f'<text x="140" y="{y+5}" font-family="Arial, sans-serif" font-size="14" fill="#222">{html.escape(level)}</text>')
    parts.append(f'<text x="{(left+width-right)/2:.1f}" y="{height-28}" text-anchor="middle" font-family="Arial, sans-serif" font-size="15" fill="#333">{html.escape(x_label)}</text>')
    parts.append(f'<text transform="translate(25 {(top+height-bottom)/2:.1f}) rotate(-90)" text-anchor="middle" font-family="Arial, sans-serif" font-size="15" fill="#333">{html.escape(y_label)}</text>')
    if extra_note:
        parts.append(f'<text x="{left}" y="{height-8}" font-family="Arial, sans-serif" font-size="12" fill="#666">{html.escape(extra_note)}</text>')
    parts.append("</svg>")
    output.write_text("\n".join(parts) + "\n", encoding="utf-8")


def repo_relative(path: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root()).as_posix()
    except ValueError:
        return str(path)


def write_figure_provenance(figure: Path, metrics_path: Path, quantity: str, levels: dict[str, LevelData], extra: dict[str, Any] | None = None) -> dict[str, str]:
    provenance = figure.with_suffix(".provenance.json")
    payload = {
        "schema": {"name": "tsunami.figure_provenance", "version": "1.0.0"},
        "figure": repo_relative(figure),
        "generated_at_utc": utc_now(),
        "renderer": "stdlib_svg",
        "study_id": STUDY_ID,
        "quantity": quantity,
        "forcing_window_s": list(FORCING_WINDOW_S),
        "common_support_point_count": COMMON_SUPPORT_COUNT,
        "source_metrics": str(metrics_path),
        "source_inputs": {
            level_id: {
                "run_id": level.run_id,
                "samples_csv_sha256": file_sha256(coupling_dir(level.case_root, level.run_id) / "samples.csv"),
                "metadata_json_sha256": file_sha256(coupling_dir(level.case_root, level.run_id) / "metadata.json"),
                "mesh_msh_sha256": file_sha256(level.mesh_path),
            }
            for level_id, level in levels.items()
        },
    }
    if extra:
        payload.update(extra)
    write_json(provenance, payload)
    return {"figure": repo_relative(figure), "provenance": repo_relative(provenance)}


def generate_r4_figures(figure_root: Path, metrics_path: Path, metrics: dict[str, Any], levels: dict[str, LevelData]) -> dict[str, Any]:
    outputs: list[dict[str, str]] = []
    note = f"Classification: {metrics['qualification']['status']}; formal window 245-545 s."
    for filename, title, desc, ylabel, key, quantity in [
        ("c1a_r4_eta_waveform.svg", "C1A-R4 eta Waveform Convergence", "Maximum signed free-surface perturbation over the frozen-terrain coupling section.", "eta perturbation (m)", "eta_m", "eta waveform"),
        ("c1a_r4_qn_waveform.svg", "C1A-R4 qn Waveform Convergence", "Maximum signed normal-momentum perturbation over the frozen-terrain coupling section.", "qn perturbation (m^2/s)", "qn_m2_per_s", "qn waveform"),
        ("c1a_r4_Qn_waveform.svg", "C1A-R4 Qn Waveform Convergence", "Integrated normal discharge over the frozen-terrain coupling section.", "Qn (m^3/s)", "Qn_m3_per_s", "Qn waveform"),
    ]:
        figure = figure_root / filename
        svg_line_plot(
            figure,
            title,
            desc,
            "Time since event start (s)",
            ylabel,
            {level_id: rows_in_window(level.series) for level_id, level in levels.items()},
            key,
            {level_id: COLORS[level_id] for level_id in levels},
            extra_note=note,
        )
        outputs.append(write_figure_provenance(figure, metrics_path, quantity, levels))
    crest_time = metrics["levels"]["h600"]["forcing_window_qoi"]["peak_Qn_time_s"]
    support = common_support()
    profile_series = {
        level_id: [
            {"offset_m": x, "qn_m2_per_s": y}
            for x, y in zip(support, interpolate_profile(profile_at_time(level, crest_time, "qn"), support))
        ]
        for level_id, level in levels.items()
    }
    bed_series = {
        level_id: [{"offset_m": x, "bed_m": y} for x, y in zip(support, interpolate_profile(profile_at_time(level, 0.0, "bed"), support))]
        for level_id, level in levels.items()
    }
    for filename, title, desc, ylabel, key, series, quantity in [
        ("c1a_r4_section_profile_principal_crest.svg", "C1A-R4 Section Profile at Principal Crest", f"Normal-momentum perturbation interpolated onto the fixed 8 km support at t={crest_time:g} s.", "qn perturbation (m^2/s)", "qn_m2_per_s", profile_series, "section qn profile"),
        ("c1a_r4_bed_profile.svg", "C1A-R4 Bed-Profile Projection", "Frozen G6 bed projected onto each solver mesh and interpolated onto the fixed 8 km support.", "bed elevation (m)", "bed_m", bed_series, "bed profile"),
    ]:
        figure = figure_root / filename
        svg_line_plot(
            figure,
            title,
            desc,
            "Offset across coupling section (m)",
            ylabel,
            series,
            key,
            {level_id: COLORS[level_id] for level_id in levels},
            x_key="offset_m",
            extra_note="Common support: 401 points over the fixed 8000 m coupling section.",
        )
        outputs.append(write_figure_provenance(figure, metrics_path, quantity, levels, {"profile_time_s": crest_time if "section_profile" in filename else 0.0}))
    runtime_series = {
        quantity: [
            {
                "runtime_s": metrics["levels"]["h800"]["runtime_wall_clock_s"],
                "nrmse_percent": metrics["comparisons"]["h800_vs_h1000"][quantity + "_waveform"]["nrmse"] * 100.0,
            },
            {
                "runtime_s": metrics["levels"]["h600"]["runtime_wall_clock_s"],
                "nrmse_percent": metrics["comparisons"]["h600_vs_h800"][quantity + "_waveform"]["nrmse"] * 100.0,
            },
        ]
        for quantity in ("eta", "qn", "Qn")
    }
    figure = figure_root / "c1a_r4_runtime_vs_error.svg"
    svg_line_plot(
        figure,
        "C1A-R4 Runtime vs Forcing Error",
        "Waveform NRMSE against the next coarser frozen-terrain Regional2D level plotted against full-run wall time.",
        "Fine-level wall clock (s)",
        "Waveform NRMSE (%)",
        runtime_series,
        "nrmse_percent",
        COLORS,
        x_key="runtime_s",
        extra_note="Formal threshold: 2% NRMSE for medium-to-fine spatial qualification.",
    )
    outputs.append(write_figure_provenance(figure, metrics_path, "runtime vs forcing waveform error", levels, {"threshold_percent": 2.0}))
    manifest = {
        "schema": {"name": "tsunami.c1a_r4_figure_manifest", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "classification": metrics["qualification"]["status"],
        "source_metrics": str(metrics_path),
        "outputs": outputs,
    }
    write_json(figure_root / "c1a_r4_figure_manifest.json", manifest)
    return manifest


def build_metrics(external_root: Path) -> tuple[dict[str, Any], dict[str, LevelData]]:
    refresh_pilot_timesteps(external_root)
    preflight = read_json(external_root / "mesh_preflight.json")
    run_summary = read_json(external_root / "spatial_run_summary.json")
    case_root = Path(preflight["case_root"])
    passed_runs = [run for run in run_summary["runs"] if run.get("status") == "passed"]
    if len(passed_runs) < 3:
        raise R4ExecutionError("R4 analysis requires three passed spatial runs")
    by_target = {float(run["requested_solver_target_m"]): run for run in passed_runs}
    targets = sorted(by_target, reverse=True)
    levels = {
        f"h{target:g}": derive_level_data(f"h{target:g}", target, case_root, by_target[target])
        for target in targets
    }
    level_metrics: dict[str, Any] = {}
    for level_id, level in levels.items():
        run = by_target[level.target_m]
        mesh = run["mesh"]
        timestep = timestep_stats_from_diagnostics(level.output_dir)
        level_metrics[level_id] = {
            "requested_solver_target_m": level.target_m,
            "active_cells": mesh["active_cells"],
            "total_cells": mesh["total_cells"],
            "actual_characteristic_mesh_size_m": mesh["actual_characteristic_h_m"],
            "mesh_sha256": mesh["mesh_sha256"],
            "runtime_wall_clock_s": run["resource_usage"]["wall_clock_s"],
            "runtime_cpu_time_s": run["resource_usage"]["cpu_time_s"],
            "peak_memory_kb": run["resource_usage"]["peak_memory_kb"],
            "timestep": timestep,
            "wetdry": {
                "wet_cell_min": timestep["wet_cell_min"],
                "wet_cell_max": timestep["wet_cell_max"],
                "dry_cell_min": timestep["dry_cell_min"],
                "dry_cell_max": timestep["dry_cell_max"],
            },
            "forcing_window_qoi": qoi_summary(level.series),
            "source_projection": source_projection(level),
            "coupling_sample_count": int(level.metadata["sample_count"]),
        }
    comparisons: dict[str, Any] = {}
    ordered_ids = list(levels)
    for coarse_id, fine_id in zip(ordered_ids, ordered_ids[1:]):
        coarse = levels[coarse_id]
        fine = levels[fine_id]
        fine_source = level_metrics[fine_id]["source_projection"]
        coarse_source = level_metrics[coarse_id]["source_projection"]
        comparisons[f"{fine_id}_vs_{coarse_id}"] = {
            "coarse_level": coarse_id,
            "fine_level": fine_id,
            "refinement_ratio_actual_h": level_metrics[coarse_id]["actual_characteristic_mesh_size_m"]
            / level_metrics[fine_id]["actual_characteristic_mesh_size_m"],
            "eta_waveform": waveform_metric(fine.series, coarse.series, "eta_m"),
            "qn_waveform": waveform_metric(fine.series, coarse.series, "qn_m2_per_s"),
            "Qn_waveform": waveform_metric(fine.series, coarse.series, "Qn_m3_per_s"),
            "qbar_waveform": waveform_metric(fine.series, coarse.series, "qbar_m2_per_s"),
            "eta_distributed_common_support": distributed_metric(fine, coarse, "eta"),
            "qn_distributed_common_support": distributed_metric(fine, coarse, "qn"),
            "bed_projection_common_support": bed_projection_metric(fine, coarse),
            "source_projection": source_comparison(fine_source, coarse_source),
            "arrival_time_proxy_difference_s": level_metrics[fine_id]["forcing_window_qoi"]["arrival_time_proxy_s"]
            - level_metrics[coarse_id]["forcing_window_qoi"]["arrival_time_proxy_s"],
        }
    fine_pair_id = f"{ordered_ids[-1]}_vs_{ordered_ids[-2]}"
    fine_pair = comparisons[fine_pair_id]
    formal_keys = [
        ("eta_waveform", "nrmse"),
        ("qn_waveform", "nrmse"),
        ("Qn_waveform", "nrmse"),
        ("qbar_waveform", "nrmse"),
        ("eta_distributed_common_support", "nrmse"),
        ("qn_distributed_common_support", "nrmse"),
    ]
    failing = [
        f"{name}.{field}"
        for name, field in formal_keys
        if fine_pair[name][field] > QUALIFICATION_THRESHOLD
    ]
    richardson_inputs = {
        "h_fine_to_coarse_m": [
            level_metrics[ordered_ids[2]]["actual_characteristic_mesh_size_m"],
            level_metrics[ordered_ids[1]]["actual_characteristic_mesh_size_m"],
            level_metrics[ordered_ids[0]]["actual_characteristic_mesh_size_m"],
        ],
    }
    richardson = {}
    for quantity, qoi_name in [
        ("peak_eta_abs_m", "peak_eta_abs_m"),
        ("peak_qn_abs_m2_per_s", "peak_qn_abs_m2_per_s"),
        ("peak_Qn_abs_m3_per_s", "peak_Qn_abs_m3_per_s"),
        ("peak_qbar_abs_m2_per_s", "peak_qbar_abs_m2_per_s"),
    ]:
        values = [
            level_metrics[level_id]["forcing_window_qoi"][qoi_name]
            for level_id in (ordered_ids[2], ordered_ids[1], ordered_ids[0])
        ]
        richardson[quantity] = {
            **richardson_inputs,
            "values_fine_to_coarse": values,
            "result": c1a.richardson_gci(values, richardson_inputs["h_fine_to_coarse_m"]),
        }
    qualification_status = "spatially_qualified" if not failing else "not_spatially_qualified"
    metrics = {
        "schema": {"name": "tsunami.c1a_r4_frozen_terrain_metrics", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "external_root": str(external_root),
        "case_root": str(case_root),
        "selected_ladder": ordered_ids,
        "forcing_window_s": list(FORCING_WINDOW_S),
        "qualification": {
            "status": qualification_status,
            "threshold_nrmse": QUALIFICATION_THRESHOLD,
            "medium_to_fine_pair": fine_pair_id,
            "failing_metrics": failing,
            "temporal_gate": "open" if qualification_status == "spatially_qualified" else "closed",
            "temporal_runs_started": False,
        },
        "frozen_family_invariance": {
            "terrain_path": preflight["case_record"]["terrain"]["path"],
            "terrain_sha256": preflight["case_record"]["terrain"]["sha256"],
            "source_path": preflight["case_record"]["source"]["path"],
            "source_sha256": preflight["case_record"]["source"]["sha256"],
            "physical_configuration_sha256": preflight["case_record"]["physical_configuration_sha256"],
            "domain_sha256": preflight["case_record"]["domain_sha256"],
            "coupling_section_sha256": preflight["case_record"]["coupling_section_sha256"],
        },
        "section_integration": {
            "method": "sum((q_n(t)-q_n(t0)) * face_length) over boundary.inland faces",
            "section_width_m": SECTION_WIDTH_M,
            "normal_source": "manifests/corridors/kamaishi-delivery-corridor-evidence.json basis.centreline_unit",
            "common_support": {"width_m": SECTION_WIDTH_M, "point_count": COMMON_SUPPORT_COUNT},
        },
        "levels": level_metrics,
        "comparisons": comparisons,
        "richardson_gci": richardson,
        "dominant_residual_error_mechanism": [
            "mesh-dependent projection of frozen bed/source onto solver cells",
            "section-normal momentum waveform differences on the fixed coupling support",
        ],
        "temporal_gate": {
            "status": "not_started_spatial_gate_closed" if failing else "ready_not_started",
            "reason": "Temporal convergence is gated on frozen-terrain spatial qualification.",
        },
        "no_observations_used": True,
        "no_calibration_performed": True,
        "local3d_not_started": True,
        "fabricated_results": False,
    }
    return metrics, levels


def update_r4_docs(docs_root: Path, external_root: Path, figure_manifest: dict[str, Any], metrics: dict[str, Any]) -> None:
    docs_root.mkdir(parents=True, exist_ok=True)
    write_json(docs_root / "regional_frozen_terrain_v4_metrics.json", metrics)
    summary = {
        "schema": {"name": "tsunami.c1a_regional_convergence_summary", "version": "1.3.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "external_root": str(external_root),
        "spatial": {
            "classification": metrics["qualification"]["status"],
            "qualification": metrics["qualification"],
            "selected_ladder": metrics["selected_ladder"],
            "section_integration": metrics["section_integration"],
            "levels": metrics["levels"],
            "comparisons": metrics["comparisons"],
            "richardson_gci": metrics["richardson_gci"],
            "figures": figure_manifest["outputs"],
        },
        "frozen_family_invariance": metrics["frozen_family_invariance"],
        "production_discretisation": {
            "selected": metrics["qualification"]["status"] == "spatially_qualified",
            "reason": "No Regional2D production mesh is selected because the frozen-terrain medium-to-fine spatial qualification did not pass."
            if metrics["qualification"]["status"] != "spatially_qualified"
            else "Frozen-terrain spatial qualification passed; production selection requires downstream review.",
        },
        "temporal": metrics["temporal_gate"],
        "physical_parameter_invariance": {
            "no_observations_used": True,
            "no_calibration_performed": True,
            "local3d_not_started": True,
        },
        "fabricated_results": False,
    }
    write_json(docs_root / "regional_convergence_summary.json", summary)
    rows = []
    for level_id, level in metrics["levels"].items():
        qoi = level["forcing_window_qoi"]
        timestep = level["timestep"]
        rows.append(
            {
                "level_id": level_id,
                "status": "passed",
                "study_id": STUDY_ID,
                "requested_solver_target_m": level["requested_solver_target_m"],
                "active_cells": level["active_cells"],
                "total_cells": level["total_cells"],
                "actual_characteristic_mesh_size_m": level["actual_characteristic_mesh_size_m"],
                "achieved_final_time_s": 600.0,
                "wall_clock_s": level["runtime_wall_clock_s"],
                "peak_memory_kb": level["peak_memory_kb"],
                "diagnostics_rows": timestep["diagnostics_rows"],
                "step_count": timestep["step_count"],
                "min_dt_s": timestep["minimum_dt_s"],
                "mean_dt_s": timestep["mean_dt_s"],
                "median_dt_s": timestep["median_dt_s"],
                "max_dt_s": timestep["maximum_dt_s"],
                "rejected_attempts_total": timestep["rejected_attempts_total"],
                "source_restriction_rows": timestep["source_restriction_rows"],
                "forcing_window_peak_eta_m": qoi["peak_eta_abs_m"],
                "peak_eta_time_s": qoi["peak_eta_time_s"],
                "forcing_window_peak_qn_m2_per_s": qoi["peak_qn_abs_m2_per_s"],
                "peak_qn_time_s": qoi["peak_qn_time_s"],
                "forcing_window_peak_Qn_m3_per_s": qoi["peak_Qn_abs_m3_per_s"],
                "peak_Qn_time_s": qoi["peak_Qn_time_s"],
                "forcing_window_peak_qbar_m2_per_s": qoi["peak_qbar_abs_m2_per_s"],
                "peak_qbar_time_s": qoi["peak_qbar_time_s"],
                "arrival_time_proxy_s": qoi["arrival_time_proxy_s"],
                "wet_cell_min": level["wetdry"]["wet_cell_min"],
                "wet_cell_max": level["wetdry"]["wet_cell_max"],
                "classification_note": metrics["qualification"]["status"],
            }
        )
    fields = [
        "level_id",
        "status",
        "study_id",
        "requested_solver_target_m",
        "active_cells",
        "total_cells",
        "actual_characteristic_mesh_size_m",
        "achieved_final_time_s",
        "wall_clock_s",
        "peak_memory_kb",
        "diagnostics_rows",
        "step_count",
        "min_dt_s",
        "mean_dt_s",
        "median_dt_s",
        "max_dt_s",
        "rejected_attempts_total",
        "source_restriction_rows",
        "forcing_window_peak_eta_m",
        "peak_eta_time_s",
        "forcing_window_peak_qn_m2_per_s",
        "peak_qn_time_s",
        "forcing_window_peak_Qn_m3_per_s",
        "peak_Qn_time_s",
        "forcing_window_peak_qbar_m2_per_s",
        "peak_qbar_time_s",
        "arrival_time_proxy_s",
        "wet_cell_min",
        "wet_cell_max",
        "classification_note",
    ]
    write_csv(docs_root / "regional_spatial_convergence.csv", fields, rows)
    write_csv(docs_root / "regional_spatial_convergence_frozen_terrain_v4.csv", fields, rows)
    write_csv(
        docs_root / "regional_temporal_convergence.csv",
        ["level_id", "status", "cfl_target", "maximum_timestep_s", "reason"],
        [
            {
                "level_id": level_id,
                "status": "not_started",
                "cfl_target": "",
                "maximum_timestep_s": "",
                "reason": "Temporal convergence remains gated by C1A-R4 frozen-terrain spatial qualification.",
            }
            for level_id in ("T0", "T1", "T2")
        ],
    )
    production = f"""# C1A-R4 Regional2D Frozen-Terrain Production Discretisation Decision

Status: not selected.

C1A-R4 executed the frozen G6 terrain/source Regional2D spatial ladder `{', '.join(metrics['selected_ladder'])}` to 600 s under study `{STUDY_ID}`. The terrain raster, source raster, physical configuration, domain, and coupling section are invariant across all levels.

The medium-to-fine qualification pair is `{metrics['qualification']['medium_to_fine_pair']}`. Spatial qualification status is `{metrics['qualification']['status']}` with a {QUALIFICATION_THRESHOLD:.0%} NRMSE threshold. Failing formal metrics: {', '.join(metrics['qualification']['failing_metrics']) or 'none'}.

Temporal convergence remains gated and was not started. No observations were used, no calibration was performed, and Local3D convergence was not started.
"""
    (docs_root / "regional_production_discretisation.md").write_text(production, encoding="utf-8")


def command_analyze(args: argparse.Namespace) -> int:
    metrics, levels = build_metrics(args.external_root)
    metrics_path = args.external_root / "frozen_terrain_v4_metrics.json"
    write_json(metrics_path, metrics)
    rows = []
    for comparison_id, comparison in metrics["comparisons"].items():
        rows.append(
            {
                "comparison_id": comparison_id,
                "coarse_level": comparison["coarse_level"],
                "fine_level": comparison["fine_level"],
                "refinement_ratio_actual_h": comparison["refinement_ratio_actual_h"],
                "eta_waveform_nrmse": comparison["eta_waveform"]["nrmse"],
                "qn_waveform_nrmse": comparison["qn_waveform"]["nrmse"],
                "Qn_waveform_nrmse": comparison["Qn_waveform"]["nrmse"],
                "qbar_waveform_nrmse": comparison["qbar_waveform"]["nrmse"],
                "eta_distributed_nrmse": comparison["eta_distributed_common_support"]["nrmse"],
                "qn_distributed_nrmse": comparison["qn_distributed_common_support"]["nrmse"],
                "bed_projection_rmse_m": comparison["bed_projection_common_support"]["rmse_m"],
                "bed_projection_max_abs_m": comparison["bed_projection_common_support"]["max_abs_difference_m"],
                "arrival_time_proxy_difference_s": comparison["arrival_time_proxy_difference_s"],
            }
        )
    write_csv(
        args.external_root / "frozen_terrain_v4_metrics.csv",
        [
            "comparison_id",
            "coarse_level",
            "fine_level",
            "refinement_ratio_actual_h",
            "eta_waveform_nrmse",
            "qn_waveform_nrmse",
            "Qn_waveform_nrmse",
            "qbar_waveform_nrmse",
            "eta_distributed_nrmse",
            "qn_distributed_nrmse",
            "bed_projection_rmse_m",
            "bed_projection_max_abs_m",
            "arrival_time_proxy_difference_s",
        ],
        rows,
    )
    figure_manifest = generate_r4_figures(args.figure_root, metrics_path, metrics, levels)
    update_r4_docs(args.docs_root, args.external_root, figure_manifest, metrics)
    print(json.dumps({"status": "analyzed", "metrics": str(metrics_path), "qualification": metrics["qualification"]}, indent=2, sort_keys=True))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--g6-root", type=Path, default=DEFAULT_G6_ROOT)
    parser.add_argument("--r2d-binary", type=Path, default=repo_root() / DEFAULT_R2D_BINARY)
    parser.add_argument("--targets", default="1000,800,600", help="Comma-separated requested mesh targets in metres.")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("preflight").set_defaults(func=command_preflight)
    sub.add_parser("pilot").set_defaults(func=command_pilot)
    full = sub.add_parser("run-full")
    full.add_argument("--max-projected-wall-s", type=float, default=10800.0)
    full.set_defaults(func=command_run_full)
    analyze = sub.add_parser("analyze")
    analyze.add_argument("--figure-root", type=Path, default=repo_root() / "deliverables/figures/convergence")
    analyze.add_argument(
        "--docs-root",
        type=Path,
        default=repo_root() / "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A",
    )
    analyze.set_defaults(func=command_analyze)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    args.targets = [float(item) for item in str(args.targets).split(",") if item.strip()]
    try:
        return int(args.func(args))
    except R4ExecutionError as exc:
        print(f"ERROR: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
