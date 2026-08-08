#!/usr/bin/env python3
"""Execute C1A-R4 frozen-terrain Regional2D mesh-convergence runs."""

from __future__ import annotations

import argparse
import csv
import hashlib
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
    dt_values = [row["dt"] for row in diagnostics if "dt" in row and row["dt"] > 0.0]
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


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--g6-root", type=Path, default=DEFAULT_G6_ROOT)
    parser.add_argument("--r2d-binary", type=Path, default=repo_root() / DEFAULT_R2D_BINARY)
    parser.add_argument("--targets", type=float, nargs="+", default=[1000.0, 800.0, 600.0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("preflight").set_defaults(func=command_preflight)
    sub.add_parser("pilot").set_defaults(func=command_pilot)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return int(args.func(args))
    except R4ExecutionError as exc:
        print(f"ERROR: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
