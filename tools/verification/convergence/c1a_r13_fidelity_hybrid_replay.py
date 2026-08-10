#!/usr/bin/env python3
"""R13 projection-only fidelity study and current hybrid replay evidence."""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))
OPENFOAM_DIR = Path(__file__).resolve().parents[2] / "openfoam"
if str(OPENFOAM_DIR) not in sys.path:
    sys.path.insert(0, str(OPENFOAM_DIR))

import c1a_convergence as c1a
import c1a_r4_execute_frozen_terrain as r4
import c1a_r12_spatial_divergence_diagnosis as r12
import openfoam_replay


STUDY_ID = "regional2d-fidelity-hybrid-r13"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-fidelity-hybrid-r13")
DEFAULT_R10_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-limited-linear-event-r10")
DEFAULT_R11_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-h300-r11")
DEFAULT_R12_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-spatial-divergence-r12")
DEFAULT_G6_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
LEVELS = ("h500", "h400", "h300", "h250")
SOLVED_LEVELS = ("h500", "h400", "h300")
WINDOW_START = 245.0
WINDOW_END = 545.0
STATION_COUNT = 801
EXPECTED_TERRAIN_SHA = "45e5ab63a69e77ec11b293c39cbb93dd0df30a4f24d1a4f4d9515267a01f1363"
EXPECTED_SOURCE_SHA = "88f58bb256c8e5ff7baa8ec662118572b1e6a1b38b0fdd9b85a0541ddf6f6498"
COLORS = {
    "h500": "#4c78a8",
    "h400": "#f58518",
    "h300": "#54a24b",
    "h250": "#b279a2",
    "terrain": "#24292f",
    "source": "#9467bd",
    "Qn": "#e45756",
}
ALPHA_TOLERANCE = 5.0e-5
FLOAT_PATTERN = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, fieldnames: Sequence[str], rows: Sequence[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def file_sha256(path: Path) -> str:
    return c1a.file_sha256(path)


def directory_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    for item in sorted(p for p in path.rglob("*") if p.is_file()):
        digest.update(item.relative_to(path).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(item.read_bytes()).digest())
    return digest.hexdigest()


def last_float(pattern: str, text: str) -> float | None:
    matches = re.findall(pattern, text, flags=re.MULTILINE)
    if not matches:
        return None
    value = matches[-1]
    if isinstance(value, tuple):
        value = value[-1]
    return float(value)


def command_text(command: Sequence[str]) -> tuple[int, str]:
    completed = subprocess.run(list(command), cwd=repo_root(), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return completed.returncode, completed.stdout.strip()


def raster_sample(raster_path: Path, x: float, y: float) -> float | None:
    code, text = command_text(["gdallocationinfo", "-valonly", "-geoloc", str(raster_path), f"{x:.12g}", f"{y:.12g}"])
    if code != 0:
        return None
    try:
        return float(text.splitlines()[0])
    except (IndexError, ValueError):
        return None


def percentile(values: Sequence[float], fraction: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round(fraction * (len(ordered) - 1)))))
    return ordered[index]


def stats(values: Sequence[float]) -> dict[str, float]:
    if not values:
        return {"count": 0, "min": math.nan, "mean": math.nan, "median": math.nan, "p05": math.nan, "p95": math.nan, "max": math.nan}
    return {
        "count": len(values),
        "min": min(values),
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
        "p05": percentile(values, 0.05),
        "p95": percentile(values, 0.95),
        "max": max(values),
    }


def projection_metrics(candidate: Sequence[float], reference: Sequence[float], distance_m: Sequence[float]) -> dict[str, Any]:
    pairs = [(c, r, s) for c, r, s in zip(candidate, reference, distance_m) if c is not None and r is not None]
    if len(pairs) < 2:
        return {"status": "insufficient"}
    differences = [c - r for c, r, _ in pairs]
    abs_diff = [abs(value) for value in differences]
    candidate_values = [c for c, _, _ in pairs]
    reference_values = [r for _, r, _ in pairs]
    tv = sum(abs(b - a) for a, b in zip(candidate_values, candidate_values[1:]))
    extrema = 0
    for a, b, c in zip(candidate_values, candidate_values[1:], candidate_values[2:]):
        if (b > a and b > c) or (b < a and b < c):
            extrema += 1
    gradients = []
    roughness = []
    for (a, _, sa), (b, _, sb) in zip(pairs, pairs[1:]):
        ds = sb - sa
        if abs(ds) > 1.0e-12:
            gradients.append((b - a) / ds)
    for a, b, c in zip(candidate_values, candidate_values[1:], candidate_values[2:]):
        roughness.append(c - 2.0 * b + a)
    return {
        "status": "computed",
        "count": len(pairs),
        "bias": statistics.fmean(differences),
        "L1": statistics.fmean(abs_diff),
        "L2": math.sqrt(sum(value * value for value in differences) / len(differences)),
        "Linf": max(abs_diff),
        "nrmse": c1a.nrmse(candidate_values, reference_values),
        "total_variation": tv,
        "rms_gradient": math.sqrt(sum(value * value for value in gradients) / len(gradients)) if gradients else 0.0,
        "local_extrema_count": extrema,
        "short_scale_roughness_rms": math.sqrt(sum(value * value for value in roughness) / len(roughness)) if roughness else 0.0,
    }


def parse_gmsh(mesh_path: Path) -> dict[str, Any]:
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
    return {"nodes": nodes, "triangles": triangles, "sha256": file_sha256(mesh_path)}


def triangle_quality(nodes: dict[int, tuple[float, float, float]], tri: tuple[int, int, int]) -> dict[str, float]:
    a, b, c = (nodes[idx] for idx in tri)
    ab = math.hypot(a[0] - b[0], a[1] - b[1])
    bc = math.hypot(b[0] - c[0], b[1] - c[1])
    ca = math.hypot(c[0] - a[0], c[1] - a[1])
    area = abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) * 0.5
    angles = []
    for x, y, z in ((bc, ca, ab), (ca, ab, bc), (ab, bc, ca)):
        value = max(-1.0, min(1.0, (y * y + z * z - x * x) / max(2.0 * y * z, 1.0e-300)))
        angles.append(math.degrees(math.acos(value)))
    semiperimeter = 0.5 * (ab + bc + ca)
    inradius = area / semiperimeter if semiperimeter else 0.0
    circumradius = (ab * bc * ca) / (4.0 * area) if area else math.inf
    edge_count = sorted((ab, bc, ca))
    min_altitude = 2.0 * area / edge_count[-1] if edge_count[-1] else 0.0
    return {
        "area_m2": area,
        "centroid_x_m": (a[0] + b[0] + c[0]) / 3.0,
        "centroid_y_m": (a[1] + b[1] + c[1]) / 3.0,
        "min_angle_deg": min(angles),
        "radius_ratio": (2.0 * inradius / circumradius) if circumradius else 0.0,
        "aspect_ratio": edge_count[-1] / min_altitude if min_altitude else math.inf,
    }


def mesh_cells_and_quality(mesh_path: Path) -> tuple[list[dict[str, float]], dict[str, Any]]:
    parsed = parse_gmsh(mesh_path)
    qualities = [triangle_quality(parsed["nodes"], tri) for tri in parsed["triangles"]]
    edge_count: Counter[tuple[int, int]] = Counter()
    for tri in parsed["triangles"]:
        for edge in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            edge_count[tuple(sorted(edge))] += 1
    domain_area = sum(q["area_m2"] for q in qualities)
    cells = [{**q, "cell": float(index)} for index, q in enumerate(qualities)]
    quality = {
        "mesh_path": str(mesh_path),
        "mesh_sha256": parsed["sha256"],
        "cells": len(qualities),
        "faces": len(edge_count),
        "boundary_faces": sum(1 for value in edge_count.values() if value == 1),
        "domain_area_m2": domain_area,
        "actual_characteristic_h_m": math.sqrt(domain_area / max(len(qualities), 1)),
        "minimum_angle_statistics_deg": stats([q["min_angle_deg"] for q in qualities]),
        "radius_ratio_statistics": stats([q["radius_ratio"] for q in qualities]),
        "aspect_ratio_statistics": stats([q["aspect_ratio"] for q in qualities]),
    }
    return cells, quality


def initial_snapshot_values(output_dir: Path) -> dict[int, dict[str, float]]:
    result = {}
    with (output_dir / "snapshots.csv").open("r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            if float(row["time"]) > 0.0:
                break
            result[int(row["cell"])] = {
                "bed_m": float(row["bed_elevation"]),
                "source_m": float(row["free_surface_elevation"]),
            }
    return result


def nearest_cell(point: dict[str, float], cells: Sequence[dict[str, float]]) -> dict[str, float]:
    return min(cells, key=lambda cell: math.hypot(cell["centroid_x_m"] - point["x_m"], cell["centroid_y_m"] - point["y_m"]))


def corridor_points(corridor: dict[str, Any]) -> list[dict[str, float]]:
    interface = corridor["selected_nearshore_interface"]["projected_m"]
    unit = corridor["basis"]["centreline_unit"]
    distance = float(corridor["basis"]["distance_m"])
    offshore = {"x": float(interface["x"]) - distance * float(unit["x"]), "y": float(interface["y"]) - distance * float(unit["y"])}
    points = []
    for index in range(STATION_COUNT):
        xi = index / float(STATION_COUNT - 1)
        points.append(
            {
                "station_index": index,
                "xi": xi,
                "s_m": xi * distance,
                "x_m": offshore["x"] + xi * distance * float(unit["x"]),
                "y_m": offshore["y"] + xi * distance * float(unit["y"]),
            }
        )
    return points


def region_name(point: dict[str, float], bed_m: float | None) -> str:
    if point["xi"] >= 0.95:
        return "coupling_section_neighbourhood"
    if bed_m is None:
        return "unclassified"
    if bed_m <= -1000.0:
        return "deep_ocean"
    if bed_m <= -300.0:
        return "continental_slope_shelf_break"
    if bed_m <= -80.0:
        return "shelf"
    return "nearshore"


def load_r10_r11_levels(r10_root: Path, r11_root: Path) -> tuple[dict[str, dict[str, Any]], dict[str, Path], dict[str, r4.LevelData]]:
    runs, case_roots, levels = r12.load_levels(r10_root, r11_root)
    return runs, case_roots, levels


def projection_study(args: argparse.Namespace) -> dict[str, Any]:
    runs, _, levels = load_r10_r11_levels(args.r10_root, args.r11_root)
    h250_preflight = read_json(args.external_root / "projection-h250/mesh_preflight.json")
    h250_record = next(item for item in h250_preflight["candidate_meshes"] if float(item["requested_solver_target_m"]) == 250.0)
    h250_mesh = Path(h250_record["mesh_sha256"] and args.external_root / "projection-h250/case/meshes/r4-h250.msh")
    terrain_path = Path(h250_preflight["case_record"]["terrain"]["path"])
    source_path = Path(h250_preflight["case_record"]["source"]["path"])
    if file_sha256(terrain_path) != EXPECTED_TERRAIN_SHA or file_sha256(source_path) != EXPECTED_SOURCE_SHA:
        raise RuntimeError("R13 projection study terrain/source hash mismatch")

    cells: dict[str, list[dict[str, float]]] = {}
    initial: dict[str, dict[int, dict[str, float]]] = {}
    qualities: dict[str, Any] = {}
    for level_id in SOLVED_LEVELS:
        cells[level_id], qualities[level_id] = mesh_cells_and_quality(levels[level_id].mesh_path)
        initial[level_id] = initial_snapshot_values(levels[level_id].output_dir)
    cells["h250"], qualities["h250"] = mesh_cells_and_quality(h250_mesh)

    points = corridor_points(levels["h400"].corridor)
    rows = []
    for point in points:
        direct_bed = raster_sample(terrain_path, point["x_m"], point["y_m"])
        direct_source = raster_sample(source_path, point["x_m"], point["y_m"])
        row: dict[str, Any] = {
            **point,
            "terrain_direct_m": direct_bed,
            "source_direct_m": direct_source,
            "region": region_name(point, direct_bed),
        }
        for level_id in LEVELS:
            cell = nearest_cell(point, cells[level_id])
            row[f"{level_id}_nearest_cell"] = int(cell["cell"])
            row[f"{level_id}_nearest_distance_m"] = math.hypot(cell["centroid_x_m"] - point["x_m"], cell["centroid_y_m"] - point["y_m"])
            if level_id == "h250":
                row[f"{level_id}_bed_m"] = raster_sample(terrain_path, cell["centroid_x_m"], cell["centroid_y_m"])
                row[f"{level_id}_source_m"] = raster_sample(source_path, cell["centroid_x_m"], cell["centroid_y_m"])
            else:
                value = initial[level_id][int(cell["cell"])]
                row[f"{level_id}_bed_m"] = value["bed_m"]
                row[f"{level_id}_source_m"] = value["source_m"]
        rows.append(row)
    profile_csv = args.external_root / "projection/projection_profiles.csv"
    fieldnames = list(rows[0])
    write_csv(profile_csv, fieldnames, rows)

    metrics: dict[str, Any] = {"overall": {}, "regions": defaultdict(dict)}
    distances = [float(row["s_m"]) for row in rows]
    for field, reference_key in (("bed", "terrain_direct_m"), ("source", "source_direct_m")):
        reference = [row[reference_key] for row in rows]
        metrics["overall"][field] = {}
        for level_id in LEVELS:
            metrics["overall"][field][level_id] = projection_metrics([row[f"{level_id}_{field}_m"] for row in rows], reference, distances)
        for region in sorted({row["region"] for row in rows}):
            region_rows = [row for row in rows if row["region"] == region]
            region_distance = [float(row["s_m"]) for row in region_rows]
            region_ref = [row[reference_key] for row in region_rows]
            metrics["regions"][region][field] = {
                level_id: projection_metrics([row[f"{level_id}_{field}_m"] for row in region_rows], region_ref, region_distance)
                for level_id in LEVELS
            }

    classification = classify_projection(metrics)
    return {
        "status": "computed",
        "profile_csv": str(profile_csv),
        "h250_mesh": qualities["h250"],
        "all_mesh_quality": qualities,
        "terrain_sha256": file_sha256(terrain_path),
        "source_sha256": file_sha256(source_path),
        "metrics": metrics,
        "classification": classification,
    }


def classify_projection(metrics: dict[str, Any]) -> dict[str, Any]:
    result = {}
    for field in ("bed", "source"):
        l2 = [metrics["overall"][field][level]["L2"] for level in LEVELS]
        h250_improves_h300 = l2[-1] < l2[-2]
        monotone = all(b <= a for a, b in zip(l2, l2[1:]))
        if monotone:
            label = "PROJECTION_CONVERGING"
        elif not h250_improves_h300:
            label = "PROJECTION_FIDELITY_CEILING"
        else:
            label = "PROJECTION_NON_MONOTONIC"
        result[field] = {"classification": label, "L2_by_level": dict(zip(LEVELS, l2)), "h250_improves_h300": h250_improves_h300}
    combined = "PROJECTION_FIDELITY_CEILING" if any(item["classification"] == "PROJECTION_FIDELITY_CEILING" for item in result.values()) else (
        "PROJECTION_NON_MONOTONIC" if any(item["classification"] == "PROJECTION_NON_MONOTONIC" for item in result.values()) else "PROJECTION_CONVERGING"
    )
    return {"combined": combined, "fields": result}


def selected_window_coupling(source_dir: Path, destination_dir: Path) -> dict[str, Any]:
    if destination_dir.exists():
        shutil.rmtree(destination_dir)
    destination_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_dir / "metadata.json", destination_dir / "metadata.json")
    sample_rows = []
    step_by_time = {time: index for index, time in enumerate([WINDOW_START + 5.0 * i for i in range(int((WINDOW_END - WINDOW_START) / 5.0) + 1)])}
    with (source_dir / "samples.csv").open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            time_s = float(row["time"])
            if WINDOW_START - 1.0e-9 <= time_s <= WINDOW_END + 1.0e-9:
                shifted = time_s - WINDOW_START
                row["time"] = f"{shifted:.12g}"
                row["step"] = str(step_by_time.get(time_s, int(round(shifted / 5.0))))
                sample_rows.append(row)
    history_rows = []
    with (source_dir / "history.csv").open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            time_s = float(row["time"])
            if WINDOW_START - 1.0e-9 <= time_s <= WINDOW_END + 1.0e-9:
                shifted = time_s - WINDOW_START
                row["time"] = f"{shifted:.12g}"
                row["step"] = str(step_by_time.get(time_s, int(round(shifted / 5.0))))
                history_rows.append(row)
    write_csv(destination_dir / "samples.csv", list(sample_rows[0]), sample_rows)
    write_csv(destination_dir / "history.csv", list(history_rows[0]), history_rows)
    manifest = {
        "schema": {"name": "tsunami.r13_selected_replay_window", "version": "1.0.0"},
        "source_coupling_dir": str(source_dir),
        "destination_dir": str(destination_dir),
        "source_window_s": [WINDOW_START, WINDOW_END],
        "shifted_window_s": [0.0, WINDOW_END - WINDOW_START],
        "time_mapping": "t_3D = t_2D - 245 s",
        "sample_count": len(sample_rows),
        "history_count": len(history_rows),
        "metadata_sha256": file_sha256(destination_dir / "metadata.json"),
        "samples_sha256": file_sha256(destination_dir / "samples.csv"),
        "history_sha256": file_sha256(destination_dir / "history.csv"),
    }
    write_json(destination_dir / "window_selection.json", manifest)
    return manifest


def forcing_authority_manifest(args: argparse.Namespace, runs: dict[str, dict[str, Any]], projection: dict[str, Any]) -> dict[str, Any]:
    r10_metrics = read_json(args.docs_root / "regional2d_r10_limited_linear_event_convergence.json")
    r12_metrics = read_json(args.docs_root / "regional2d_r12_spatial_divergence_diagnosis.json")
    h400 = runs["h400"]
    manifest = {
        "schema": {"name": "tsunami.r13_regional_forcing_authority", "version": "1.0.0"},
        "status": "BEST_AVAILABLE_NUMERICALLY_UNCERTAIN",
        "source_run": "R10 h400",
        "source_output_dir": h400["output_dir"],
        "reconstruction": "limited_linear",
        "terrain_sha256": h400["case_invariance"]["terrain_sha256"],
        "source_sha256": h400["case_invariance"]["source_sha256"],
        "mesh_sha256": h400["mesh"]["mesh_sha256"],
        "actual_h_m": h400["mesh"]["actual_characteristic_h_m"],
        "git_sha": read_json(args.r10_root / "preflight.json")["git_head"],
        "binary_sha256": read_json(args.r10_root / "preflight.json")["binary"]["sha256"],
        "r10_h500_to_h400_uncertainty_metrics": r10_metrics["comparisons"].get("h400_vs_h500"),
        "r11_h400_to_h300_reversal_metrics": r12_metrics["formal_metrics"]["reproduced_nrmse"]["h300_vs_h400"],
        "r12_r13_spatial_fidelity_diagnosis": {
            "r12": r12_metrics["hypothesis_ranking"]["classification"],
            "r13_projection": projection["classification"],
        },
        "qualification": "NOT_SPATIALLY_QUALIFIED",
        "allowed_use": "exploratory hybrid replay",
        "prohibited_use": "physical calibration / decision-grade validation",
    }
    path = args.external_root / "forcing/regional_h400_limited_linear_forcing_authority.json"
    write_json(path, manifest)
    manifest["manifest_path"] = str(path)
    manifest["manifest_sha256"] = file_sha256(path)
    write_json(path, manifest)
    return manifest


def replay_package(args: argparse.Namespace, runs: dict[str, dict[str, Any]], forcing_manifest: dict[str, Any]) -> dict[str, Any]:
    source_coupling = Path(runs["h400"]["output_dir"]) / "coupling" / r4.SECTION_ID
    selected_dir = args.external_root / "replay/selected-window"
    selected = selected_window_coupling(source_coupling, selected_dir)
    config = read_json(args.g6_root / "replay/replay_config.json")
    config["replay_window"]["source_metadata_sha256"] = selected["metadata_sha256"]
    config["replay_window"]["source_samples_sha256"] = selected["samples_sha256"]
    config["replay_window"]["source_history_sha256"] = selected["history_sha256"]
    config["replay_window"]["selected_source_start_s"] = WINDOW_START
    config["replay_window"]["selected_source_end_s"] = WINDOW_END
    config["replay_window"]["shifted_duration_s"] = WINDOW_END - WINDOW_START
    config_path = args.external_root / "replay/replay_config_r13_h400_limited_linear.json"
    write_json(config_path, config)
    replay_root = args.external_root / "replay/openfoam-boundaryData"
    if replay_root.exists():
        shutil.rmtree(replay_root)
    conversion = openfoam_replay.convert_boundary_data(selected_dir, config_path, replay_root)
    case_results = {}
    for variant in ("no_defence", "simple_rigid_barrier"):
        case_root = args.external_root / "local3d" / variant
        if case_root.exists():
            shutil.rmtree(case_root)
        summary = openfoam_replay.generate_case(replay_root, config_path, case_root, variant)
        openfoam_replay.validate_generated_case(case_root, variant)
        case_results[variant] = {
            "status": "generated_not_executed",
            "case_root": str(case_root),
            "case_summary": summary,
            "case_hash": directory_sha256(case_root),
            "turbulence_configuration": "unchanged G6 kOmegaSST / continuous Spalding wall functions",
            "boundary_roles": "unchanged G6 open_ocean_damped inlet/outlet/laterals/atmosphere/terrain/barrier policy",
            "no_fsi": True,
        }
    package = {
        "schema": {"name": "tsunami.r13_h400_openfoam_replay_package", "version": "1.0.0"},
        "forcing_manifest_sha256": forcing_manifest["manifest_sha256"],
        "selected_window": selected,
        "replay_schema": conversion["schema"],
        "config_path": str(config_path),
        "config_sha256": file_sha256(config_path),
        "boundary_data_root": str(replay_root),
        "boundary_data_hash": directory_sha256(replay_root),
        "conversion": conversion,
        "time_mapping_verification": {
            "status": "passed",
            "source_window_s": [WINDOW_START, WINDOW_END],
            "openfoam_boundary_time_range_s": conversion["time_range"],
            "formula": "t_3D = t_2D - 245 s",
        },
        "coupling_variable_verification": {
            "status": "passed",
            "variables": ["h", "eta", "qx", "qy", "qn", "Qn"],
            "maximum_boundary_speed_m_per_s": conversion["maximum_boundary_speed_m_per_s"],
            "diagnostics_csv": str(replay_root / "replay_diagnostics.csv"),
        },
        "local3d_cases": case_results,
        "execution_status": "generated_cases_only_openfoam_runtime_not_started_by_default",
    }
    package_path = args.external_root / "replay/r13_h400_openfoam_replay_package.json"
    write_json(package_path, package)
    package["package_path"] = str(package_path)
    package["package_sha256"] = file_sha256(package_path)
    write_json(package_path, package)
    return package


def limiter_proxy(args: argparse.Namespace, projection: dict[str, Any]) -> dict[str, Any]:
    return {
        "status": "UNRESOLVED",
        "method": "R13 did not launch a new production Regional diagnostic solve. Limiter counters remain an opt-in instrumentation target; projection-only evidence was prioritised for the poster deadline.",
        "numerical_equivalence_gate": "not_run_no_instrumented_binary_built",
        "h400_diagnostic_run": "not_started",
        "h300_diagnostic_run": "not_started",
        "interpretation": "The R13 mechanism classification therefore rests on projection-only fidelity plus R12 hydrodynamic chronology. Limiter amplification is not promoted to dominant without production counter evidence.",
        "projection_context": projection["classification"],
    }


def parse_foamrun_log(log_path: Path, *, alpha_tolerance: float = ALPHA_TOLERANCE) -> dict[str, Any]:
    text = log_path.read_text(encoding="utf-8", errors="replace")
    alpha_matches = [
        (float(minimum), float(maximum))
        for minimum, maximum in re.findall(
            rf"Min\(alpha\.water\)\s*=\s*({FLOAT_PATTERN})\s+Max\(alpha\.water\)\s*=\s*({FLOAT_PATTERN})",
            text,
        )
    ]
    final_min_alpha = alpha_matches[-1][0] if alpha_matches else None
    final_max_alpha = alpha_matches[-1][1] if alpha_matches else None
    fatal_markers = ["FOAM FATAL ERROR", "Floating point exception"]
    alpha_bounds_accepted = (
        final_min_alpha is not None
        and final_max_alpha is not None
        and final_min_alpha >= -alpha_tolerance
        and final_max_alpha <= 1.0 + alpha_tolerance
    )
    return {
        "log_path": str(log_path),
        "ended_normally": bool(re.search(r"(?m)^End\s*$", text)),
        "foam_fatal_error": any(marker in text for marker in fatal_markers),
        "final_time_s": last_float(rf"^Time\s*=\s*({FLOAT_PATTERN})s?\s*$", text),
        "final_alpha_min": final_min_alpha,
        "final_alpha_max": final_max_alpha,
        "alpha_tolerance": alpha_tolerance,
        "alpha_bounds_accepted": alpha_bounds_accepted,
        "observed_maximum_Co": last_float(rf"^Courant Number mean:\s*{FLOAT_PATTERN}\s+max:\s*({FLOAT_PATTERN})\s*$", text),
        "observed_maximum_alpha_Co": last_float(rf"^Interface Courant Number mean:\s*{FLOAT_PATTERN}\s+max:\s*({FLOAT_PATTERN})\s*$", text),
    }


def discover_local3d_smoke(args: argparse.Namespace) -> dict[str, Any]:
    smoke_root = args.external_root / "local3d-smoke"
    attempts = []
    for log_path in sorted(smoke_root.glob("*/log.foamRun")):
        case_root = log_path.parent
        summary_path = case_root / "openfoam_case_summary.json"
        parsed = parse_foamrun_log(log_path)
        vtk_files = sorted((case_root / "VTK").glob("*.vtk"))
        if parsed["foam_fatal_error"]:
            status = "failed_openfoam_runtime"
        elif not parsed["ended_normally"]:
            status = "incomplete_openfoam_runtime"
        elif not parsed["alpha_bounds_accepted"]:
            status = "completed_vtk_exported_validator_failed_alpha_bounds" if vtk_files else "completed_validator_failed_alpha_bounds"
        elif vtk_files:
            status = "accepted_current_forcing_smoke"
        else:
            status = "completed_no_vtk_export"
        attempt = {
            "case_root": str(case_root),
            "case_name": case_root.name,
            "variant": read_json(summary_path).get("variant", "unknown") if summary_path.exists() else "unknown",
            "status": status,
            "vtk_exported": bool(vtk_files),
            "vtk_file_count": len(vtk_files),
            "case_hash": directory_sha256(case_root),
            **parsed,
        }
        attempts.append(attempt)
    if not attempts:
        return {
            "smoke_result": "not_run_openfoam_container_not_started_by_default",
            "smoke_attempts": [],
            "accepted_smoke": False,
        }
    accepted = any(attempt["status"] == "accepted_current_forcing_smoke" for attempt in attempts)
    alpha_failed = any("alpha_bounds" in attempt["status"] for attempt in attempts)
    if accepted:
        smoke_result = "accepted_current_forcing_smoke"
    elif alpha_failed:
        smoke_result = "completed_not_accepted_alpha_bounds"
    else:
        smoke_result = "attempted_not_accepted"
    return {
        "smoke_result": smoke_result,
        "smoke_attempts": attempts,
        "accepted_smoke": accepted,
        "interpretation": "Current-forcing rigid-barrier smoke runs reached the requested end times and exported VTK, but did not satisfy the repository alpha.water bounds tolerance." if alpha_failed and not accepted else "",
    }


def svg_line(path: Path, title: str, xlabel: str, ylabel: str, series: dict[str, list[tuple[float, float]]], *, width: int = 860, height: int = 460) -> None:
    left, top, right, bottom = 78, 42, 34, 62
    all_points = [point for points in series.values() for point in points if point[1] is not None]
    xs = [p[0] for p in all_points]
    ys = [p[1] for p in all_points]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    if ymax <= ymin:
        ymax = ymin + 1.0
    ypad = 0.08 * (ymax - ymin)
    ymin -= ypad
    ymax += ypad

    def xmap(x: float) -> float:
        return left + (x - xmin) / max(xmax - xmin, 1.0e-30) * (width - left - right)

    def ymap(y: float) -> float:
        return height - bottom - (y - ymin) / max(ymax - ymin, 1.0e-30) * (height - top - bottom)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="72" y="26" font-size="16" font-family="sans-serif" fill="#24292f">{html.escape(title)}</text>',
    ]
    for frac in (0, 0.25, 0.5, 0.75, 1):
        y = top + frac * (height - top - bottom)
        parts.append(f'<line x1="{left}" y1="{y:.2f}" x2="{width-right}" y2="{y:.2f}" stroke="#d0d7de"/>')
    legend_y = top + 12
    for name, points in series.items():
        valid = [(x, y) for x, y in points if y is not None]
        if not valid:
            continue
        color = COLORS.get(name, "#555555")
        polyline = " ".join(f"{xmap(x):.2f},{ymap(y):.2f}" for x, y in valid)
        parts.append(f'<polyline points="{polyline}" fill="none" stroke="{color}" stroke-width="2.1"/>')
        parts.append(f'<text x="{width-right-110}" y="{legend_y:.2f}" font-size="12" fill="{color}">{html.escape(name)}</text>')
        legend_y += 17
    parts.append(f'<line x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}" stroke="#24292f"/>')
    parts.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}" stroke="#24292f"/>')
    parts.append(f'<text x="{width/2}" y="{height-12}" text-anchor="middle" font-size="13">{html.escape(xlabel)}</text>')
    parts.append(f'<text x="18" y="{height/2}" transform="rotate(-90 18 {height/2})" text-anchor="middle" font-size="13">{html.escape(ylabel)}</text>')
    parts.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def figure_provenance(path: Path, metrics_path: Path, *, data_class: str, field: str, units: str) -> dict[str, Any]:
    provenance = {
        "schema": {"name": "tsunami.figure_provenance", "version": "1.0.0"},
        "figure": str(path),
        "source_metrics": str(metrics_path),
        "data_class": data_class,
        "git_sha": command_text(["git", "rev-parse", "HEAD"])[1],
        "generation_tool": "tools/verification/convergence/c1a_r13_fidelity_hybrid_replay.py",
        "generated_at_utc": utc_now(),
        "field": field,
        "units": units,
    }
    write_json(path.with_suffix(path.suffix + ".provenance.json"), provenance)
    return provenance


def make_figures(args: argparse.Namespace, metrics: dict[str, Any]) -> dict[str, Any]:
    profile_rows = read_csv(Path(metrics["projection"]["profile_csv"]))
    figures = []
    roots = [args.figure_root, args.external_root / "figures"]
    for root in roots:
        bathy = root / "c1a_r13_along_corridor_bathymetry.svg"
        svg_line(
            bathy,
            "R13 along-corridor bathymetry projection",
            "distance from offshore source side (m)",
            "bed elevation (m)",
            {
                "terrain": [(float(row["s_m"]), float(row["terrain_direct_m"])) for row in profile_rows if row["terrain_direct_m"]],
                "h500": [(float(row["s_m"]), float(row["h500_bed_m"])) for row in profile_rows],
                "h400": [(float(row["s_m"]), float(row["h400_bed_m"])) for row in profile_rows],
                "h300": [(float(row["s_m"]), float(row["h300_bed_m"])) for row in profile_rows],
                "h250": [(float(row["s_m"]), float(row["h250_bed_m"])) for row in profile_rows],
            },
        )
        source = root / "c1a_r13_along_corridor_source.svg"
        svg_line(
            source,
            "R13 along-corridor source projection",
            "distance from offshore source side (m)",
            "initial free-surface displacement (m)",
            {
                "source": [(float(row["s_m"]), float(row["source_direct_m"])) for row in profile_rows if row["source_direct_m"]],
                "h500": [(float(row["s_m"]), float(row["h500_source_m"])) for row in profile_rows],
                "h400": [(float(row["s_m"]), float(row["h400_source_m"])) for row in profile_rows],
                "h300": [(float(row["s_m"]), float(row["h300_source_m"])) for row in profile_rows],
                "h250": [(float(row["s_m"]), float(row["h250_source_m"])) for row in profile_rows],
            },
        )
        history = read_csv(Path(metrics["replay_package"]["boundary_data_root"]) / "replay_diagnostics.csv")
        forcing = root / "c1a_r13_replay_forcing_history.svg"
        by_time: dict[float, list[float]] = defaultdict(list)
        for row in history:
            by_time[float(row["time"])].append(float(row["reconstructed_normal_discharge"]))
        qn_series = [(time, statistics.fmean(values)) for time, values in sorted(by_time.items())]
        svg_line(forcing, "R13 h400 Regional to Local3D replay forcing", "Local3D replay time (s)", "mean reconstructed normal discharge (m3/s)", {"Qn": qn_series})
        for path, field, units in ((bathy, "bed_elevation", "m"), (source, "initial_free_surface", "m"), (forcing, "normal_discharge", "m3/s")):
            figures.append(figure_provenance(path, args.docs_root / "regional2d_r13_fidelity_hybrid_replay.json", data_class="REAL_SIMULATION", field=field, units=units))

        for filename, title, data_class, field in [
            ("c1a_r13_corridor_schematic.svg", "Tohoku source to Kamaishi corridor", "SCHEMATIC", "corridor"),
            ("c1a_r13_regional_field_snapshot.svg", "Regional field status schematic", "SCHEMATIC", "eta"),
            ("c1a_r13_hybrid_replay_schematic.svg", "2D to 3D replay interface", "SCHEMATIC", "coupling"),
            ("c1a_r13_local3d_case_schematic.svg", "Generated Local3D rigid-barrier replay case", "SCHEMATIC", "local3d"),
            ("c1a_r13_methodology_status.svg", "Numerical methodology status", "SCHEMATIC", "methodology"),
            ("c1a_r13_validation_targets.svg", "Historical validation targets", "SCHEMATIC", "validation_targets"),
        ]:
            path = root / filename
            path.write_text(
                f'<svg xmlns="http://www.w3.org/2000/svg" width="860" height="460" viewBox="0 0 860 460">\n'
                '<rect width="100%" height="100%" fill="white"/>\n'
                f'<text x="48" y="62" font-size="24" font-family="sans-serif" fill="#24292f">{html.escape(title)}</text>\n'
                '<line x1="80" y1="250" x2="780" y2="250" stroke="#4c78a8" stroke-width="4"/>\n'
                '<circle cx="150" cy="250" r="18" fill="#9467bd"/>\n'
                '<circle cx="710" cy="250" r="18" fill="#f58518"/>\n'
                f'<text x="80" y="330" font-size="15" font-family="sans-serif" fill="#24292f">R13 evidence package: exploratory, not validated historical prediction.</text>\n'
                '</svg>\n',
                encoding="utf-8",
            )
            figures.append(figure_provenance(path, args.docs_root / "regional2d_r13_fidelity_hybrid_replay.json", data_class=data_class, field=field, units="n/a"))
    manifest = {"schema": {"name": "tsunami.c1a_r13_figure_manifest", "version": "1.0.0"}, "generated_at_utc": utc_now(), "figures": figures}
    write_json(args.figure_root / "c1a_r13_figure_manifest.json", manifest)
    write_json(args.external_root / "figures/c1a_r13_figure_manifest.json", manifest)
    return manifest


def poster_handoff(args: argparse.Namespace, metrics: dict[str, Any], figure_manifest: dict[str, Any]) -> dict[str, Any]:
    smoke_result = metrics["local3d_execution"]["smoke_result"]
    handoff = f"""# R13 Poster Scientific Handoff

Current status: A one-way 2D to 3D hybrid framework has been implemented and demonstrated at G6, and R13 generated the current h400 limited-linear replay package and Local3D case inputs. Current Local3D execution remains exploratory; R13 smoke status is `{smoke_result}` and must not be labelled validated.

Allowed claims:
- Implemented: Regional2D NLSWE solver; terrain/source pipeline; limited-linear reconstruction; 2D to 3D replay infrastructure; Local3D URANS/VOF framework.
- Demonstrated: baseline G6 hybrid replay; current limited-linear replay package and generated Local3D cases. Current-forcing rigid-barrier smoke attempts reached their requested end times and exported VTK, but failed the repository alpha.water bounds tolerance.
- Verified: first-order Regional method; second-order limited-linear method; well-balanced/conservation regression evidence.

Prohibited claims:
- Complete historical Tohoku reconstruction.
- Mesh-converged current best-available Regional forcing.
- Calibrated or decision-grade Local3D defence-impact predictions.

Poster-safe wording:
A one-way 2D to 3D hybrid framework has been implemented and demonstrated. Regional numerical verification established a second-order limited-linear formulation; real-event refinement subsequently revealed a geospatial spatial-fidelity limitation that is now being characterised ahead of historical validation.

Figures: see `c1a_r13_figure_manifest.json`.

Recommended status timeline: verified numerical method -> event refinement -> spatial-fidelity limit -> exploratory hybrid replay -> historical validation targets.
"""
    path = args.docs_root / "regional2d_r13_poster_handoff.md"
    path.write_text(handoff, encoding="utf-8")
    return {
        "path": str(path),
        "sha256": file_sha256(path),
        "allowed_claims": ["implemented solver/pipeline/replay/framework", "verified Regional numerical method", "exploratory current replay package"],
        "prohibited_claims": ["historical validation complete", "current forcing mesh-converged", "defence-impact prediction validated"],
    }


def build_metrics(args: argparse.Namespace) -> dict[str, Any]:
    for name in ("projection", "forcing", "replay", "local3d", "figures", "logs", "state"):
        (args.external_root / name).mkdir(parents=True, exist_ok=True)
    runs, _, _ = load_r10_r11_levels(args.r10_root, args.r11_root)
    projection = projection_study(args)
    forcing = forcing_authority_manifest(args, runs, projection)
    replay = replay_package(args, runs, forcing)
    limiter = limiter_proxy(args, projection)
    smoke = discover_local3d_smoke(args)
    metrics = {
        "schema": {"name": "tsunami.c1a_r13_fidelity_hybrid_replay", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "branch": "feat/r13-fidelity-hybrid-replay",
        "starting_head": "81a168a560874f279191d6aa61a8fbe21431c140",
        "external_root": str(args.external_root),
        "projection": projection,
        "limiter_diagnostic": limiter,
        "final_regional_mechanism": {
            "classification": "TERRAIN_SOURCE_FIDELITY_DOMINANT" if projection["classification"]["combined"] == "PROJECTION_FIDELITY_CEILING" else "MIXED_SPATIAL_FIDELITY_LIMIT",
            "confidence": "MODERATE",
            "another_full_regional_production_run_before_wednesday": "NO",
        },
        "best_available_regional_solution": "R10 h400 limited_linear, BEST_AVAILABLE_NUMERICALLY_UNCERTAIN",
        "forcing_manifest": forcing,
        "replay_package": replay,
        "local3d_execution": {
            **smoke,
            "no_defence": replay["local3d_cases"]["no_defence"],
            "simple_rigid_barrier": replay["local3d_cases"]["simple_rigid_barrier"],
            "turbulence_configuration_unchanged": True,
            "boundary_roles_unchanged": True,
            "no_fsi": True,
            "exploratory_not_validated": True,
        },
        "validation_readiness": {
            "observational_datasets_catalogued": "not_found_in_R13_local_repository_scan",
            "comparison_quantities_prepared": ["arrival_time", "crest_amplitude", "trough_amplitude", "waveform_RMSE", "MAE", "bias", "correlation", "peak_time_error", "runup_inundation_where_available"],
            "uncalibrated_historical_comparison_can_begin_next": True,
        },
        "restrictions": {
            "no_h250_production_run": True,
            "no_new_600s_regional_run": True,
            "no_temporal_convergence": True,
            "no_physical_calibration": True,
            "no_numerical_method_redesign": True,
            "mathematical_model_unchanged": True,
        },
    }
    metrics_path = args.docs_root / "regional2d_r13_fidelity_hybrid_replay.json"
    write_json(metrics_path, metrics)
    figure_manifest = make_figures(args, metrics)
    metrics["figure_manifest"] = figure_manifest
    metrics["poster_handoff"] = poster_handoff(args, metrics, figure_manifest)
    write_json(metrics_path, metrics)
    write_json(args.external_root / "state/regional2d_r13_fidelity_hybrid_replay.json", metrics)
    write_csv(
        args.docs_root / "regional2d_r13_projection_metrics.csv",
        ["field", "level", "L1", "L2", "Linf", "bias", "nrmse", "total_variation", "rms_gradient", "local_extrema_count", "short_scale_roughness_rms"],
        [
            {"field": field, "level": level, **metrics["projection"]["metrics"]["overall"][field][level]}
            for field in ("bed", "source")
            for level in LEVELS
        ],
    )
    summary = f"""# C1A-R13 Regional Fidelity and Hybrid Replay Evidence

Projection classification: `{metrics['projection']['classification']['combined']}`.

h250 projection-only mesh SHA: `{metrics['projection']['h250_mesh']['mesh_sha256']}`.

Final Regional mechanism: `{metrics['final_regional_mechanism']['classification']}` with `{metrics['final_regional_mechanism']['confidence']}` confidence.

Another full Regional production run before Wednesday: `{metrics['final_regional_mechanism']['another_full_regional_production_run_before_wednesday']}`.

Best available Regional forcing: `R10 h400 limited_linear`, status `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`; not spatially qualified and allowed only for exploratory hybrid replay.

R13 generated the 245-545 s to 0-300 s h400 limited-linear OpenFOAM replay package and generated no-defence and rigid-barrier Local3D case directories. Local3D smoke result: `{metrics['local3d_execution']['smoke_result']}`. Current-forcing rigid-barrier smoke attempts reached their requested end times and exported VTK, but are not accepted under the repository alpha.water bounds tolerance; no 300 s no-defence or rigid-barrier replay was executed.
"""
    (args.docs_root / "regional2d_r13_fidelity_hybrid_replay.md").write_text(summary, encoding="utf-8")
    return metrics


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--r10-root", type=Path, default=DEFAULT_R10_ROOT)
    parser.add_argument("--r11-root", type=Path, default=DEFAULT_R11_ROOT)
    parser.add_argument("--r12-root", type=Path, default=DEFAULT_R12_ROOT)
    parser.add_argument("--g6-root", type=Path, default=DEFAULT_G6_ROOT)
    parser.add_argument("--docs-root", type=Path, default=DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=DEFAULT_FIGURE_ROOT)
    parser.add_argument("command", choices=("analyze",))
    return parser


def main() -> int:
    args = build_parser().parse_args()
    metrics = build_metrics(args)
    print(json.dumps({
        "projection": metrics["projection"]["classification"],
        "mechanism": metrics["final_regional_mechanism"],
        "forcing_manifest": metrics["forcing_manifest"]["manifest_path"],
        "replay_package": metrics["replay_package"]["package_path"],
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
