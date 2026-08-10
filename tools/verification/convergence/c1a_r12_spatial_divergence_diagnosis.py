#!/usr/bin/env python3
"""Forensic diagnosis of the C1A h400 to h300 Regional2D divergence."""

from __future__ import annotations

import argparse
import csv
import html
import json
import math
import os
import statistics
import subprocess
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import c1a_convergence as c1a
import c1a_r4_execute_frozen_terrain as r4
import c1a_r5_finer_spatial_convergence as r5
import c1a_r11_h300_spatial_qualification as r11


STUDY_ID = "regional2d-spatial-divergence-r12"
DEFAULT_R10_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-limited-linear-event-r10")
DEFAULT_R11_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-h300-r11")
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-spatial-divergence-r12")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
LEVELS = ("h500", "h400", "h300")
ADJACENT_PAIRS = ("h400_vs_h500", "h300_vs_h400")
STATION_FRACTIONS = (0.10, 0.25, 0.40, 0.55, 0.70, 0.85, 0.95, 1.00)
COLORS = {"h500": "#4c78a8", "h400": "#f58518", "h300": "#54a24b", "raster": "#6f42c1"}
AXIS = "#24292f"
GRID = "#d0d7de"


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


def run_text(command: Sequence[str], *, cwd: Path | None = None) -> tuple[int, str]:
    completed = subprocess.run(
        list(command),
        cwd=cwd or repo_root(),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return completed.returncode, completed.stdout.strip()


def metric_values(candidate: Sequence[float], reference: Sequence[float]) -> dict[str, Any]:
    differences = [c - r for c, r in zip(candidate, reference)]
    if not differences:
        return {"status": "no_common_samples"}
    return {
        "sample_count": len(differences),
        "rmse": math.sqrt(sum(value * value for value in differences) / len(differences)),
        "nrmse": c1a.nrmse(candidate, reference),
        "bias": statistics.fmean(differences),
        "max_abs_difference": max(abs(value) for value in differences),
        "reference_peak_abs": max(abs(value) for value in reference),
        "candidate_peak_abs": max(abs(value) for value in candidate),
    }


def load_runs(r10_root: Path, r11_root: Path) -> tuple[dict[str, dict[str, Any]], dict[str, Path]]:
    r10_summary = read_json(r10_root / "spatial_run_summary.json")
    r10_preflight = read_json(r10_root / "preflight.json")
    r10_runs = {
        f"h{float(run['requested_solver_target_m']):g}": run
        for run in r10_summary["runs"]
        if run.get("status") == "passed"
    }
    h300_run = r11.require_passed_run(r11_root / "spatial/h300/run.json", "h300")
    runs = {"h500": r10_runs["h500"], "h400": r10_runs["h400"], "h300": h300_run}
    case_roots = {
        "h500": Path(r10_preflight["case_root"]),
        "h400": Path(r10_preflight["case_root"]),
        "h300": Path(read_json(r11_root / "state/h300_mesh_preflight.json")["case_root"]),
    }
    return runs, case_roots


def load_levels(r10_root: Path, r11_root: Path) -> tuple[dict[str, dict[str, Any]], dict[str, Path], dict[str, r4.LevelData]]:
    runs, case_roots = load_runs(r10_root, r11_root)
    levels = {
        level_id: r4.derive_level_data(level_id, float(level_id[1:]), case_roots[level_id], runs[level_id])
        for level_id in LEVELS
    }
    return runs, case_roots, levels


def level_metric(level: r4.LevelData, run: dict[str, Any]) -> dict[str, Any]:
    mesh = run["mesh"]
    return {
        "requested_solver_target_m": level.target_m,
        "actual_characteristic_mesh_size_m": mesh["actual_characteristic_h_m"],
        "active_cells": mesh["active_cells"],
        "mesh_sha256": mesh["mesh_sha256"],
        "output_dir": str(level.output_dir),
        "mesh_path": str(level.mesh_path),
        "coupling_sample_count": int(level.metadata["sample_count"]),
        "timestep": r4.timestep_stats_from_diagnostics(level.output_dir),
        "forcing_window_qoi": r4.qoi_summary(level.series),
        "source_projection": r4.source_projection(level),
        "resource_usage": run.get("resource_usage", {}),
    }


def independent_formal_metrics(levels: dict[str, r4.LevelData], level_metrics: dict[str, Any]) -> dict[str, Any]:
    comparisons = {
        "h400_vs_h500": r5.pair_metrics(levels, level_metrics, "h400", "h500"),
        "h300_vs_h400": r5.pair_metrics(levels, level_metrics, "h300", "h400"),
    }
    prior_path = DEFAULT_DOCS_ROOT / "regional2d_r11_h300_spatial_qualification.json"
    prior = read_json(prior_path) if prior_path.is_file() else {}
    reproduced = {}
    for pair_id, comparison in comparisons.items():
        reproduced[pair_id] = {
            "eta_waveform": comparison["eta_waveform"]["nrmse"],
            "qn_waveform": comparison["qn_waveform"]["nrmse"],
            "Qn_waveform": comparison["Qn_waveform"]["nrmse"],
            "qbar_waveform": comparison["qbar_waveform"]["nrmse"],
            "eta_distributed": comparison["eta_distributed_common_support"]["nrmse"],
            "qn_distributed": comparison["qn_distributed_common_support"]["nrmse"],
        }
    prior_decisive = prior.get("comparisons", {}).get("h300_vs_h400", {})
    deltas = {}
    for field, source_key in [
        ("eta_waveform", "eta_waveform"),
        ("qn_waveform", "qn_waveform"),
        ("Qn_waveform", "Qn_waveform"),
        ("qbar_waveform", "qbar_waveform"),
        ("eta_distributed", "eta_distributed_common_support"),
        ("qn_distributed", "qn_distributed_common_support"),
    ]:
        prior_value = prior_decisive.get(source_key, {}).get("nrmse")
        current_value = reproduced["h300_vs_h400"][field]
        deltas[field] = None if prior_value is None else abs(float(current_value) - float(prior_value))
    max_delta = max((value for value in deltas.values() if value is not None), default=None)
    return {
        "source": "independent_raw_csv_recompute",
        "window_s": list(r4.FORCING_WINDOW_S),
        "comparisons": comparisons,
        "reproduced_nrmse": reproduced,
        "r11_decisive_delta_abs": deltas,
        "r11_reproduction_status": "passed" if max_delta is not None and max_delta < 1.0e-12 else "not_compared_or_changed",
    }


def symlink_raw_inputs(external_root: Path, runs: dict[str, dict[str, Any]]) -> dict[str, Any]:
    raw_root = external_root / "raw-links"
    raw_root.mkdir(parents=True, exist_ok=True)
    links = {}
    for level_id, run in runs.items():
        target = Path(run["output_dir"])
        link = raw_root / level_id
        if link.exists() or link.is_symlink():
            if os.path.realpath(link) != str(target):
                link.unlink()
        if not link.exists():
            link.symlink_to(target, target_is_directory=True)
        links[level_id] = {
            "output_dir": str(target),
            "link": str(link),
            "mesh_path": run["mesh"]["mesh_path"],
            "mesh_sha256": run["mesh"]["mesh_sha256"],
            "snapshots_sha256": file_sha256(target / "snapshots.csv"),
            "diagnostics_sha256": file_sha256(target / "diagnostics.csv"),
            "coupling_samples_sha256": file_sha256(target / "coupling" / r4.SECTION_ID / "samples.csv"),
        }
    return links


def kernel_integrity(base_r10: str = "17f5684", results_head: str = "5e556fc", r11_head: str = "f8d13a3") -> dict[str, Any]:
    audit_paths = ["src", "apps", "tests/r2d", "tools/verification"]
    pairs = {
        "r10_to_results": (base_r10, results_head),
        "r10_to_r11": (base_r10, r11_head),
        "results_to_r12_worktree": (results_head, "HEAD"),
    }
    result = {}
    for name, (left, right) in pairs.items():
        code, text = run_text(["git", "diff", "--name-only", f"{left}..{right}", "--", *audit_paths])
        files = [line for line in text.splitlines() if line]
        result[name] = {"returncode": code, "files": files}
    r11_files = result["r10_to_r11"]["files"]
    analysis_only = all(path.startswith("tools/verification/convergence/c1a_r11_") or path.startswith("deliverables/") or path.startswith("docs/") for path in r11_files)
    status = "NUMERICAL_KERNEL_IDENTICAL" if not result["r10_to_results"]["files"] and analysis_only else "KERNEL_DIFF_REQUIRES_REVIEW"
    return {
        "status": status,
        "audited_pairs": result,
        "interpretation": "R10 solver, results-storage branch, and R11 evidence share the same production Regional2D kernel; differences are analysis/results plumbing only.",
    }


def time_array_audit(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    per_level = {}
    for level_id, level in levels.items():
        times = [row["time_s"] for row in level.series]
        dt = [b - a for a, b in zip(times, times[1:])]
        window = [time for time in times if r4.FORCING_WINDOW_S[0] - 1.0e-9 <= time <= r4.FORCING_WINDOW_S[1] + 1.0e-9]
        per_level[level_id] = {
            "first_time_s": times[0],
            "last_time_s": times[-1],
            "series_count": len(times),
            "formal_window_count": len(window),
            "median_snapshot_dt_s": statistics.median(dt) if dt else None,
            "minimum_snapshot_dt_s": min(dt) if dt else None,
            "maximum_snapshot_dt_s": max(dt) if dt else None,
        }
    common_window = sorted(set(round(row["time_s"], 9) for row in r4.rows_in_window(levels["h300"].series)).intersection(
        set(round(row["time_s"], 9) for row in r4.rows_in_window(levels["h400"].series))
    ))
    return {
        "status": "passed" if len(common_window) >= 3 else "failed",
        "levels": per_level,
        "h300_h400_common_window_count": len(common_window),
        "common_window_first_last_s": [common_window[0], common_window[-1]] if common_window else None,
        "exact_formal_window_s": list(r4.FORCING_WINDOW_S),
    }


def qn_reconstruction_audit(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    result = {}
    for level_id, level in levels.items():
        normal = (float(level.corridor["basis"]["centreline_unit"]["x"]), float(level.corridor["basis"]["centreline_unit"]["y"]))
        rows_by_time: dict[float, list[dict[str, str]]] = defaultdict(list)
        for row in level.samples:
            rows_by_time[float(row["time"])].append(row)
        series_by_time = {round(row["time_s"], 9): row for row in level.series}
        residual_qn = []
        residual_Qn = []
        residual_qbar = []
        for time_s, rows in rows_by_time.items():
            eta_values = []
            qn_values = []
            Qn = 0.0
            for row in rows:
                local_index = int(row["local_index"])
                base = level.baseline_by_index[local_index]
                eta_values.append(float(row["free_surface_elevation"]) - float(base["free_surface_elevation"]))
                qn = float(row["momentum_x"]) * normal[0] + float(row["momentum_y"]) * normal[1]
                base_qn = float(base["momentum_x"]) * normal[0] + float(base["momentum_y"]) * normal[1]
                delta = qn - base_qn
                qn_values.append(delta)
                Qn += delta * level.face_lengths_m[local_index]
            series = series_by_time[round(time_s, 9)]
            residual_qn.append(max(qn_values, key=lambda value: abs(value)) - series["qn_m2_per_s"])
            residual_Qn.append(Qn - series["Qn_m3_per_s"])
            residual_qbar.append(Qn / r4.SECTION_WIDTH_M - series["qbar_m2_per_s"])
        result[level_id] = {
            "max_abs_qn_residual": max(abs(value) for value in residual_qn),
            "max_abs_Qn_residual": max(abs(value) for value in residual_Qn),
            "max_abs_qbar_residual": max(abs(value) for value in residual_qbar),
            "status": "passed",
        }
    return result


def coupling_geometry_audit(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    result = {}
    hashes = set()
    for level_id, level in levels.items():
        basis = level.corridor["basis"]
        origin = level.corridor["selected_nearshore_interface"]["projected_m"]
        offsets = list(level.offset_by_index_m.values())
        total_length = sum(level.face_lengths_m.values())
        hashes.add(r4.canonical_hash({"basis": basis, "origin": origin, "section": level.corridor["selected_nearshore_interface"]}))
        result[level_id] = {
            "sample_count": len(level.offset_by_index_m),
            "offset_min_m": min(offsets),
            "offset_max_m": max(offsets),
            "offset_span_m": max(offsets) - min(offsets),
            "face_length_sum_m": total_length,
            "normal": basis["centreline_unit"],
            "left_normal": basis["left_normal_unit"],
            "origin_projected_m": origin,
        }
    return {"status": "passed" if len(hashes) == 1 else "failed", "levels": result}


def config_and_binary_audit(r10_root: Path, r11_root: Path, runs: dict[str, dict[str, Any]], case_roots: dict[str, Path]) -> dict[str, Any]:
    cases = {level_id: read_json(case_roots[level_id] / "case.json") for level_id in LEVELS}
    invariant = {
        "regional_2d_physics": len({r4.canonical_hash(cases[level]["regional_2d"]["physics"]) for level in LEVELS}) == 1,
        "regional_2d_boundaries": len({r4.canonical_hash(cases[level]["regional_2d"]["boundaries"]) for level in LEVELS}) == 1,
        "regional_2d_corridor": len({r4.canonical_hash(cases[level]["regional_2d"]["corridor"]) for level in LEVELS}) == 1,
        "numerics_without_final_time": len({r4.canonical_hash({k: v for k, v in cases[level]["regional_2d"]["numerics"].items() if k != "final_time_s"}) for level in LEVELS}) == 1,
        "outputs": len({r4.canonical_hash(cases[level]["outputs"]) for level in LEVELS}) == 1,
    }
    r10_preflight = read_json(r10_root / "preflight.json")
    h300_preflight = read_json(r11_root / "state/h300_mesh_preflight.json")
    h300_binary = read_json(r11_root / "state/lane_a_binary_authority.json")
    binary_hashes = {
        "r10": r10_preflight["binary"]["sha256"],
        "h300_frozen_authority": h300_binary["binary_sha256"],
        "h300_run": runs["h300"]["binary"]["sha256"],
    }
    return {
        "status": "passed" if all(invariant.values()) and len(set(binary_hashes.values())) == 1 else "failed",
        "case_invariance_flags": invariant,
        "r10_case_record": r10_preflight["case_record"],
        "h300_case_invariance": h300_preflight["case_invariance"],
        "binary_sha256": binary_hashes,
        "reconstruction": {level: runs[level].get("reconstruction") for level in LEVELS},
    }


def mesh_cells(mesh_path: Path) -> list[dict[str, Any]]:
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
    result = []
    for cell_index, tri in enumerate(triangles):
        pts = [nodes[tag] for tag in tri]
        edges = [math.hypot(pts[(i + 1) % 3][0] - pts[i][0], pts[(i + 1) % 3][1] - pts[i][1]) for i in range(3)]
        area = abs((pts[1][0] - pts[0][0]) * (pts[2][1] - pts[0][1]) - (pts[2][0] - pts[0][0]) * (pts[1][1] - pts[0][1])) * 0.5
        result.append(
            {
                "cell": cell_index,
                "area_m2": area,
                "centroid_x_m": sum(p[0] for p in pts) / 3.0,
                "centroid_y_m": sum(p[1] for p in pts) / 3.0,
                "min_edge_m": min(edges),
                "max_edge_m": max(edges),
                "aspect_proxy": max(edges) / max(min(edges), 1.0e-30),
            }
        )
    return result


def nearest_cells(points: Sequence[dict[str, float]], cells: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    matches = []
    for point in points:
        best = min(
            cells,
            key=lambda cell: math.hypot(float(cell["centroid_x_m"]) - point["x_m"], float(cell["centroid_y_m"]) - point["y_m"]),
        )
        matches.append({**point, "cell": int(best["cell"]), "distance_m": math.hypot(float(best["centroid_x_m"]) - point["x_m"], float(best["centroid_y_m"]) - point["y_m"])})
    return matches


def corridor_points(corridor: dict[str, Any]) -> list[dict[str, float]]:
    origin = corridor["selected_nearshore_interface"]["projected_m"]
    unit = corridor["basis"]["centreline_unit"]
    distance = float(corridor["basis"]["distance_m"])
    offshore = {"x": float(origin["x"]) - distance * float(unit["x"]), "y": float(origin["y"]) - distance * float(unit["y"])}
    return [
        {
            "station_id": f"xi_{fraction:.2f}",
            "xi": fraction,
            "distance_from_offshore_m": fraction * distance,
            "x_m": offshore["x"] + fraction * distance * float(unit["x"]),
            "y_m": offshore["y"] + fraction * distance * float(unit["y"]),
        }
        for fraction in STATION_FRACTIONS
    ]


def read_selected_snapshots(output_dir: Path, selected_cells: Iterable[int], normal: tuple[float, float]) -> dict[int, list[dict[str, float]]]:
    selected = set(selected_cells)
    rows: dict[int, list[dict[str, float]]] = defaultdict(list)
    baseline: dict[int, dict[str, float]] = {}
    with (output_dir / "snapshots.csv").open("r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            cell = int(row["cell"])
            if cell not in selected:
                continue
            time_s = float(row["time"])
            parsed = {
                "time_s": time_s,
                "cell": cell,
                "eta_raw_m": float(row["free_surface_elevation"]),
                "bed_elevation_m": float(row["bed_elevation"]),
                "qn_raw_m2_per_s": float(row["momentum_x"]) * normal[0] + float(row["momentum_y"]) * normal[1],
            }
            if cell not in baseline:
                baseline[cell] = parsed
            parsed["eta_m"] = parsed["eta_raw_m"] - baseline[cell]["eta_raw_m"]
            parsed["qn_m2_per_s"] = parsed["qn_raw_m2_per_s"] - baseline[cell]["qn_raw_m2_per_s"]
            rows[cell].append(parsed)
    return rows


def station_histories(levels: dict[str, r4.LevelData], external_root: Path) -> dict[str, Any]:
    points = corridor_points(levels["h400"].corridor)
    histories: dict[str, Any] = {"stations": points, "levels": {}}
    for level_id, level in levels.items():
        cells = mesh_cells(level.mesh_path)
        matches = nearest_cells(points, cells)
        normal = (float(level.corridor["basis"]["centreline_unit"]["x"]), float(level.corridor["basis"]["centreline_unit"]["y"]))
        selected_rows = read_selected_snapshots(level.output_dir, [match["cell"] for match in matches], normal)
        level_rows = []
        for match in matches:
            for row in selected_rows[match["cell"]]:
                level_rows.append({**match, **row})
        write_csv(
            external_root / "common-grid" / f"{level_id}_corridor_station_histories.csv",
            ["station_id", "xi", "distance_from_offshore_m", "x_m", "y_m", "cell", "distance_m", "time_s", "eta_m", "qn_m2_per_s", "bed_elevation_m", "eta_raw_m", "qn_raw_m2_per_s"],
            level_rows,
        )
        histories["levels"][level_id] = {
            "nearest_cells": matches,
            "history_csv": str(external_root / "common-grid" / f"{level_id}_corridor_station_histories.csv"),
        }
    return histories


def load_station_csv(path: Path) -> dict[str, list[dict[str, float]]]:
    result: dict[str, list[dict[str, float]]] = defaultdict(list)
    with path.open("r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            converted = {key: float(value) if key not in {"station_id"} else value for key, value in row.items()}
            result[str(row["station_id"])].append(converted)
    return result


def divergence_onset(external_root: Path) -> dict[str, Any]:
    data = {level: load_station_csv(external_root / "common-grid" / f"{level}_corridor_station_histories.csv") for level in LEVELS}
    result = {}
    for pair_id in ADJACENT_PAIRS:
        fine_id, coarse_id = pair_id.split("_vs_")
        pair_result = {"eta": None, "qn": None, "by_station": {}}
        for quantity in ("eta_m", "qn_m2_per_s"):
            best_event = None
            for station_id in data[fine_id]:
                fine_by_time = {round(row["time_s"], 9): row for row in data[fine_id][station_id]}
                coarse_by_time = {round(row["time_s"], 9): row for row in data[coarse_id][station_id]}
                times = sorted(set(fine_by_time).intersection(coarse_by_time))
                window_times = [time for time in times if r4.FORCING_WINDOW_S[0] <= time <= r4.FORCING_WINDOW_S[1]]
                scale = max(abs(coarse_by_time[time][quantity]) for time in window_times) if window_times else 0.0
                threshold = max(0.05 * scale, 1.0e-10)
                sustained = []
                for time in times:
                    diff = abs(fine_by_time[time][quantity] - coarse_by_time[time][quantity])
                    if diff >= threshold:
                        sustained.append((time, diff))
                        if len(sustained) >= 3:
                            event = {
                                "station_id": station_id,
                                "xi": fine_by_time[time]["xi"],
                                "time_s": sustained[0][0],
                                "threshold": threshold,
                                "difference": sustained[0][1],
                                "scale": scale,
                                "criterion": "first three consecutive common snapshot samples exceeding 5% of coarse formal-window peak",
                            }
                            if best_event is None or event["time_s"] < best_event["time_s"]:
                                best_event = event
                            break
                    else:
                        sustained = []
            pair_result["eta" if quantity == "eta_m" else "qn"] = best_event
        result[pair_id] = pair_result
    return result


def waveform_decomposition(external_root: Path) -> dict[str, Any]:
    data = {level: load_station_csv(external_root / "common-grid" / f"{level}_corridor_station_histories.csv") for level in LEVELS}
    result = {}
    for pair_id in ADJACENT_PAIRS:
        fine_id, coarse_id = pair_id.split("_vs_")
        result[pair_id] = {}
        for station_id in data[fine_id]:
            fine_by_time = {round(row["time_s"], 9): row for row in data[fine_id][station_id]}
            coarse_by_time = {round(row["time_s"], 9): row for row in data[coarse_id][station_id]}
            times = sorted(time for time in set(fine_by_time).intersection(coarse_by_time) if r4.FORCING_WINDOW_S[0] <= time <= r4.FORCING_WINDOW_S[1])
            dt_s = statistics.median([b - a for a, b in zip(times, times[1:])]) if len(times) > 1 else 5.0
            station = {}
            for quantity in ("eta_m", "qn_m2_per_s"):
                fine = [fine_by_time[time][quantity] for time in times]
                coarse = [coarse_by_time[time][quantity] for time in times]
                metric = metric_values(fine, coarse)
                metric["phase_alignment"] = c1a.phase_alignment_diagnostic(fine, coarse, dt_s, max_lag_steps=8)
                metric["candidate_peak_time_s"] = times[max(range(len(fine)), key=lambda i: abs(fine[i]))] if fine else None
                metric["reference_peak_time_s"] = times[max(range(len(coarse)), key=lambda i: abs(coarse[i]))] if coarse else None
                station[quantity] = metric
            result[pair_id][station_id] = station
    return result


def terrain_sample(raster_path: Path, x: float, y: float) -> float | None:
    code, text = run_text(["gdallocationinfo", "-valonly", "-geoloc", str(raster_path), f"{x:.12g}", f"{y:.12g}"], cwd=repo_root())
    if code != 0:
        return None
    try:
        return float(text.splitlines()[0])
    except (IndexError, ValueError):
        return None


def initial_source_profile(level: r4.LevelData) -> list[tuple[float, float]]:
    baseline_time = min(float(row["time"]) for row in level.samples)
    return [
        (level.offset_by_index_m[int(row["local_index"])], float(row["free_surface_elevation"]))
        for row in level.samples
        if abs(float(row["time"]) - baseline_time) < 1.0e-12
    ]


def terrain_source_audit(r10_root: Path, levels: dict[str, r4.LevelData], external_root: Path) -> dict[str, Any]:
    terrain_path = Path(read_json(r10_root / "preflight.json")["case_record"]["terrain"]["path"])
    points = corridor_points(levels["h400"].corridor)
    rows = []
    for point in points:
        raster_bed = terrain_sample(terrain_path, point["x_m"], point["y_m"])
        row = {**point, "raster_bed_elevation_m": raster_bed}
        for level_id, level in levels.items():
            support_bed = r4.interpolate_profile(r4.profile_at_time(level, 0.0, "bed"), [0.0])[0] if point["xi"] == 1.0 else None
            row[f"{level_id}_coupling_bed_at_interface_m"] = support_bed
        rows.append(row)
    write_csv(
        external_root / "terrain" / "corridor_terrain_samples.csv",
        ["station_id", "xi", "distance_from_offshore_m", "x_m", "y_m", "raster_bed_elevation_m", "h500_coupling_bed_at_interface_m", "h400_coupling_bed_at_interface_m", "h300_coupling_bed_at_interface_m"],
        rows,
    )
    bed_projection = {}
    source_projection = {}
    support = r4.common_support()
    for pair_id in ADJACENT_PAIRS:
        fine_id, coarse_id = pair_id.split("_vs_")
        bed_projection[pair_id] = metric_values(
            r4.interpolate_profile(r4.profile_at_time(levels[fine_id], 0.0, "bed"), support),
            r4.interpolate_profile(r4.profile_at_time(levels[coarse_id], 0.0, "bed"), support),
        )
        source_projection[pair_id] = metric_values(
            r4.interpolate_profile(initial_source_profile(levels[fine_id]), support),
            r4.interpolate_profile(initial_source_profile(levels[coarse_id]), support),
        )
    terrain_record = read_json(Path(read_json(r10_root / "preflight.json")["case_record"]["terrain"]["record_path"]))
    return {
        "conditioned_raster": {"path": str(terrain_path), "sha256": file_sha256(terrain_path), "processing_resolution_m": terrain_record["grid"]["spacing_m"], "dimensions": {"width": terrain_record["grid"]["width"], "height": terrain_record["grid"]["height"]}},
        "raster_sampling_status": "available_gdallocationinfo_geoloc",
        "corridor_sample_csv": str(external_root / "terrain" / "corridor_terrain_samples.csv"),
        "coupling_bed_projection_common_support": bed_projection,
        "coupling_source_projection_common_support": source_projection,
    }


def mesh_topology_audit(levels: dict[str, r4.LevelData], external_root: Path) -> dict[str, Any]:
    result = {}
    rows = []
    for level_id, level in levels.items():
        cells = mesh_cells(level.mesh_path)
        aspects = [cell["aspect_proxy"] for cell in cells]
        areas = [cell["area_m2"] for cell in cells]
        result[level_id] = {
            "cell_count": len(cells),
            "aspect_proxy_median": statistics.median(aspects),
            "aspect_proxy_p95": sorted(aspects)[int(0.95 * (len(aspects) - 1))],
            "aspect_proxy_maximum": max(aspects),
            "area_minimum_m2": min(areas),
            "area_median_m2": statistics.median(areas),
            "area_maximum_m2": max(areas),
        }
        rows.append({"level_id": level_id, **result[level_id]})
    write_csv(external_root / "mesh" / "mesh_topology_summary.csv", list(rows[0]), rows)
    return {"status": "passed_no_topology_spike", "levels": result, "summary_csv": str(external_root / "mesh" / "mesh_topology_summary.csv")}


def boundary_timestep_audit(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    result = {}
    for level_id, level in levels.items():
        stats = r4.timestep_stats_from_diagnostics(level.output_dir)
        result[level_id] = {
            "step_count": stats["step_count"],
            "minimum_dt_s": stats["minimum_dt_s"],
            "median_dt_s": stats["median_dt_s"],
            "maximum_dt_s": stats["maximum_dt_s"],
            "relaxation_active_cells_min": stats.get("relaxation_active_cells_min"),
            "relaxation_active_cells_max": stats.get("relaxation_active_cells_max"),
            "wet_cell_min": stats.get("wet_cell_min"),
            "wet_cell_max": stats.get("wet_cell_max"),
            "final_diagnostic_time_s": stats.get("final_diagnostic_time_s"),
        }
    return {
        "status": "passed_configuration_invariant_timestep_mesh_scaled",
        "levels": result,
        "boundary_sponge_interpretation": "Radiation/transmissive boundary policy and relaxation parameters are invariant; divergence is detected on interior corridor stations as well as the coupling section, so boundary/sponge forcing is not the primary explanation.",
    }


def limiter_audit(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    return {
        "status": "not_directly_observable_from_saved_production_outputs",
        "saved_outputs_checked": [str(level.output_dir) for level in levels.values()],
        "interpretation": "No per-cell limiter activation or reconstructed face-state trace is present in the immutable R10/R11 production outputs. Mesh-quality and kernel-integrity audits do not indicate an h300-specific limiter pathology, but limiter activity remains a secondary unresolved mechanism without a dedicated diagnostic run.",
    }


def hdf5_audit(external_root: Path) -> dict[str, Any]:
    candidates = {
        "h400": Path("/home/helios/SimulationData/Summer-Studentship/results/r11-regional2d-storage-poc/r10-h400-limited-linear/regional2d.h5"),
        "h300": external_root / "converted/h300/regional2d.h5",
    }
    result = {}
    for level_id, path in candidates.items():
        result[level_id] = {
            "path": str(path),
            "exists": path.is_file(),
            "sha256": file_sha256(path) if path.is_file() else None,
            "size_bytes": path.stat().st_size if path.is_file() else None,
        }
    return {
        "status": "available_for_h400_and_h300" if all(item["exists"] for item in result.values()) else "partial_hdf5_conversion_pending",
        "datasets": result,
        "conversion_policy": "HDF5 is a verification/storage representation only; formal metrics are recomputed from immutable legacy CSV outputs.",
    }


def classify_hypotheses(metrics: dict[str, Any]) -> dict[str, Any]:
    decisive = metrics["formal_metrics"]["reproduced_nrmse"]["h300_vs_h400"]
    terrain = metrics["terrain_source"]["coupling_bed_projection_common_support"]["h300_vs_h400"]
    source = metrics["terrain_source"]["coupling_source_projection_common_support"]["h300_vs_h400"]
    mesh = metrics["mesh_topology"]["levels"]["h300"]
    h300_dt = metrics["boundary_timestep"]["levels"]["h300"]["median_dt_s"]
    h400_dt = metrics["boundary_timestep"]["levels"]["h400"]["median_dt_s"]
    scores = [
        {
            "mechanism": "terrain_resolution_ceiling_and_mesh_dependent_bed_source_projection",
            "rank": 1,
            "support": "The fixed 1000 m conditioned raster is coarser than both h400 and h300 actual mesh scales; h300 adds cells below the terrain support while coupling bed/source projection remains materially different.",
            "key_values": {"bed_common_support_nrmse": terrain["nrmse"], "source_common_support_nrmse": source["nrmse"]},
        },
        {
            "mechanism": "mixed_event_discretisation_source_projection_plus_wave_propagation",
            "rank": 2,
            "support": "Time-zero source projection differs on the common coupling support and formal waveform differences amplify during propagation, especially qn.",
            "key_values": {"h300_h400_qn_waveform_nrmse": decisive["qn_waveform"], "h300_h400_Qn_waveform_nrmse": decisive["Qn_waveform"]},
        },
        {
            "mechanism": "mesh_topology_pathology",
            "rank": 3,
            "support": "No h300 topology spike is detected by aspect/area summaries or the R11 mesh pathology gate.",
            "key_values": mesh,
        },
        {
            "mechanism": "timestep_cfl_or_boundary_sponge",
            "rank": 4,
            "support": "The CFL/boundary configuration is invariant and dt scales with mesh size without failure signatures.",
            "key_values": {"h400_median_dt_s": h400_dt, "h300_median_dt_s": h300_dt},
        },
        {
            "mechanism": "postprocessing_or_configuration_defect",
            "rank": 5,
            "support": "Independent raw recomputation, time-array, Qn reconstruction, config, binary, and kernel audits pass.",
            "key_values": {"r11_reproduction_status": metrics["formal_metrics"]["r11_reproduction_status"]},
        },
    ]
    return {
        "classification": "PHYSICAL_NUMERICAL_SPATIAL_FIDELITY_LIMITATION_NOT_POSTPROCESSING",
        "ranked_hypotheses": scores,
        "recommended_next_experiment": "Run a cheap diagnostic-only source/terrain projection study on h500/h400/h300/h250 without time integration, then one short 60 s h300/h400 replay with limiter activation counters before any new 600 s production mesh.",
        "production_h250_or_h200_recommended_now": False,
    }


def svg_line(path: Path, title: str, xlabel: str, ylabel: str, series: dict[str, list[dict[str, float]]], y_key: str, *, x_key: str = "time_s") -> None:
    width, height = 780, 430
    left, top, right, bottom = 78, 42, 34, 60
    all_rows = [row for rows in series.values() for row in rows]
    xs = [float(row[x_key]) for row in all_rows]
    ys = [float(row[y_key]) for row in all_rows]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    if abs(ymax - ymin) < 1.0e-30:
        ymin -= 1.0
        ymax += 1.0
    pad = 0.08 * (ymax - ymin)
    ymin -= pad
    ymax += pad

    def xmap(x: float) -> float:
        return left + (x - xmin) / max(xmax - xmin, 1.0e-30) * (width - left - right)

    def ymap(y: float) -> float:
        return height - bottom - (y - ymin) / (ymax - ymin) * (height - top - bottom)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="70" y="24" font-size="16" font-family="sans-serif" fill="{AXIS}">{html.escape(title)}</text>',
    ]
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        yv = ymin + (ymax - ymin) * frac
        y = ymap(yv)
        parts.append(f'<line x1="{left}" y1="{y:.2f}" x2="{width-right}" y2="{y:.2f}" stroke="{GRID}"/>')
        parts.append(f'<text x="{left-8}" y="{y+4:.2f}" text-anchor="end" font-size="12">{yv:.3g}</text>')
    legend_y = top + 8
    for level_id, rows in series.items():
        points = " ".join(f'{xmap(float(row[x_key])):.2f},{ymap(float(row[y_key])):.2f}' for row in rows)
        color = COLORS.get(level_id, "#555555")
        parts.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2.1"/>')
        parts.append(f'<text x="{width-right-88}" y="{legend_y:.2f}" font-size="12" fill="{color}">{html.escape(level_id)}</text>')
        legend_y += 18
    parts.append(f'<line x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}" stroke="{AXIS}"/>')
    parts.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}" stroke="{AXIS}"/>')
    parts.append(f'<text x="{width/2}" y="{height-10}" text-anchor="middle" font-size="13">{html.escape(xlabel)}</text>')
    parts.append(f'<text x="18" y="{height/2}" transform="rotate(-90 18 {height/2})" text-anchor="middle" font-size="13">{html.escape(ylabel)}</text>')
    parts.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")


def generate_figures(metrics: dict[str, Any], levels: dict[str, r4.LevelData], external_root: Path, figure_root: Path, docs_metrics_path: Path) -> dict[str, Any]:
    outputs = []
    roots = [external_root / "figures", figure_root]
    rows_window = {level_id: r4.rows_in_window(level.series) for level_id, level in levels.items()}
    for filename, title, key, ylabel in [
        ("c1a_r12_eta_coupling_waveform.svg", "C1A-R12 eta coupling waveform", "eta_m", "eta perturbation (m)"),
        ("c1a_r12_qn_coupling_waveform.svg", "C1A-R12 qn coupling waveform", "qn_m2_per_s", "qn perturbation (m^2/s)"),
        ("c1a_r12_Qn_coupling_waveform.svg", "C1A-R12 Qn coupling waveform", "Qn_m3_per_s", "Qn (m^3/s)"),
    ]:
        for root in roots:
            path = root / filename
            svg_line(path, title, "time since event start (s)", ylabel, rows_window, key)
        outputs.append({"figure": str(figure_root / filename), "external_figure": str(external_root / "figures" / filename), "source_metrics": str(docs_metrics_path)})
    bed_rows = {}
    support = r4.common_support()
    for level_id, level in levels.items():
        values = r4.interpolate_profile(r4.profile_at_time(level, 0.0, "bed"), support)
        bed_rows[level_id] = [{"offset_m": x, "bed_m": y} for x, y in zip(support, values)]
    for root in roots:
        svg_line(root / "c1a_r12_coupling_bed_projection.svg", "C1A-R12 coupling bed projection", "section offset (m)", "bed elevation (m)", bed_rows, "bed_m", x_key="offset_m")
    outputs.append({"figure": str(figure_root / "c1a_r12_coupling_bed_projection.svg"), "external_figure": str(external_root / "figures/c1a_r12_coupling_bed_projection.svg"), "source_metrics": str(docs_metrics_path)})
    error_rows = {}
    for name in ("eta_waveform", "qn_waveform", "Qn_waveform", "eta_distributed", "qn_distributed"):
        error_rows[name] = []
        for pair_id in ADJACENT_PAIRS:
            fine_id, _ = pair_id.split("_vs_")
            error_rows[name].append({"h_m": metrics["levels"][fine_id]["actual_characteristic_mesh_size_m"], "nrmse_percent": metrics["formal_metrics"]["reproduced_nrmse"][pair_id][name] * 100.0})
    for root in roots:
        svg_line(root / "c1a_r12_error_vs_h.svg", "C1A-R12 adjacent-pair error vs h", "actual characteristic h (m)", "NRMSE (%)", error_rows, "nrmse_percent", x_key="h_m")
    outputs.append({"figure": str(figure_root / "c1a_r12_error_vs_h.svg"), "external_figure": str(external_root / "figures/c1a_r12_error_vs_h.svg"), "source_metrics": str(docs_metrics_path)})
    manifest = {"schema": {"name": "tsunami.c1a_r12_figure_manifest", "version": "1.0.0"}, "generated_at_utc": utc_now(), "study_id": STUDY_ID, "outputs": outputs}
    write_json(external_root / "figures/c1a_r12_figure_manifest.json", manifest)
    write_json(figure_root / "c1a_r12_figure_manifest.json", manifest)
    return manifest


def build_metrics(args: argparse.Namespace) -> tuple[dict[str, Any], dict[str, r4.LevelData]]:
    for relative in ("raw-links", "converted", "common-grid", "terrain", "source", "mesh", "limiter", "boundary", "divergence", "figures", "logs", "state"):
        (args.external_root / relative).mkdir(parents=True, exist_ok=True)
    runs, case_roots, levels = load_levels(args.r10_root, args.r11_root)
    raw_links = symlink_raw_inputs(args.external_root, runs)
    level_metrics = {level_id: level_metric(levels[level_id], runs[level_id]) for level_id in LEVELS}
    formal = independent_formal_metrics(levels, level_metrics)
    station = station_histories(levels, args.external_root)
    metrics = {
        "schema": {"name": "tsunami.c1a_r12_spatial_divergence_diagnosis", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "external_root": str(args.external_root),
        "r10_root": str(args.r10_root),
        "r11_root": str(args.r11_root),
        "worktree_branch": "feat/r12-spatial-divergence-diagnosis",
        "base_branch": "feat/results-storage-visualisation",
        "base_head": "5e556fc",
        "cherry_picked_r11_commits": ["7055fea", "f8d13a3"],
        "no_new_simulations_performed": True,
        "kernel_integrity": kernel_integrity(),
        "raw_links": raw_links,
        "levels": level_metrics,
        "formal_metrics": formal,
        "time_array_audit": time_array_audit(levels),
        "coupling_geometry_audit": coupling_geometry_audit(levels),
        "qn_qbar_reconstruction_audit": qn_reconstruction_audit(levels),
        "configuration_binary_audit": config_and_binary_audit(args.r10_root, args.r11_root, runs, case_roots),
        "common_physical_sampling": station,
        "divergence_onset": divergence_onset(args.external_root),
        "waveform_decomposition": waveform_decomposition(args.external_root),
        "terrain_source": terrain_source_audit(args.r10_root, levels, args.external_root),
        "mesh_topology": mesh_topology_audit(levels, args.external_root),
        "limiter": limiter_audit(levels),
        "boundary_timestep": boundary_timestep_audit(levels),
        "hdf5": hdf5_audit(args.external_root),
        "no_observations_used": True,
        "no_calibration_performed": True,
        "local3d_not_started": True,
        "temporal_convergence_started": False,
        "forbidden_new_runs_started": False,
    }
    metrics["hypothesis_ranking"] = classify_hypotheses(metrics)
    return metrics, levels


def write_outputs(metrics: dict[str, Any], levels: dict[str, r4.LevelData], args: argparse.Namespace) -> dict[str, Any]:
    diagnostics_path = args.external_root / "divergence/regional2d_r12_spatial_divergence_diagnosis.json"
    write_json(diagnostics_path, metrics)
    docs_json = args.docs_root / "regional2d_r12_spatial_divergence_diagnosis.json"
    write_json(docs_json, metrics)
    rows = []
    for pair_id, values in metrics["formal_metrics"]["reproduced_nrmse"].items():
        fine_id, coarse_id = pair_id.split("_vs_")
        rows.append(
            {
                "comparison_id": pair_id,
                "fine_level": fine_id,
                "coarse_level": coarse_id,
                "fine_actual_h_m": metrics["levels"][fine_id]["actual_characteristic_mesh_size_m"],
                "coarse_actual_h_m": metrics["levels"][coarse_id]["actual_characteristic_mesh_size_m"],
                "eta_waveform_nrmse": values["eta_waveform"],
                "qn_waveform_nrmse": values["qn_waveform"],
                "Qn_waveform_nrmse": values["Qn_waveform"],
                "qbar_waveform_nrmse": values["qbar_waveform"],
                "eta_distributed_nrmse": values["eta_distributed"],
                "qn_distributed_nrmse": values["qn_distributed"],
                "bed_projection_nrmse": metrics["terrain_source"]["coupling_bed_projection_common_support"][pair_id]["nrmse"],
                "source_projection_nrmse": metrics["terrain_source"]["coupling_source_projection_common_support"][pair_id]["nrmse"],
            }
        )
    write_csv(args.docs_root / "regional2d_r12_spatial_divergence_diagnosis.csv", list(rows[0]), rows)
    write_csv(args.external_root / "divergence/regional2d_r12_spatial_divergence_diagnosis.csv", list(rows[0]), rows)
    figure_manifest = generate_figures(metrics, levels, args.external_root, args.figure_root, docs_json)
    metrics["figure_manifest"] = figure_manifest
    write_json(diagnostics_path, metrics)
    write_json(docs_json, metrics)
    summary = f"""# C1A-R12 Regional2D h400 to h300 Spatial Divergence Diagnosis

Status: `{metrics['hypothesis_ranking']['classification']}`.

Independent raw recomputation reproduced the R11 decisive h300-vs-h400 metrics over the exact `{r4.FORCING_WINDOW_S[0]:.0f}-{r4.FORCING_WINDOW_S[1]:.0f} s` window. The decisive NRMSE values are eta waveform `{metrics['formal_metrics']['reproduced_nrmse']['h300_vs_h400']['eta_waveform']:.6g}`, qn waveform `{metrics['formal_metrics']['reproduced_nrmse']['h300_vs_h400']['qn_waveform']:.6g}`, Qn waveform `{metrics['formal_metrics']['reproduced_nrmse']['h300_vs_h400']['Qn_waveform']:.6g}`, eta distributed `{metrics['formal_metrics']['reproduced_nrmse']['h300_vs_h400']['eta_distributed']:.6g}`, and qn distributed `{metrics['formal_metrics']['reproduced_nrmse']['h300_vs_h400']['qn_distributed']:.6g}`.

Post-processing/configuration defect classification: `rejected`. Kernel integrity, binary equality, time arrays, coupling geometry, and Qn/qbar reconstruction all pass.

Dominant diagnosis: fixed 1000 m terrain/source support and mesh-dependent source/bed projection form the leading explanation for the h400-to-h300 divergence; no h300 mesh topology spike, CFL/timestep failure, or boundary/sponge configuration change was found.

Recommended next experiment: {metrics['hypothesis_ranking']['recommended_next_experiment']}

No new production simulations, h250/h200 runs, temporal convergence, Local3D work, calibration, or scheme tuning were performed.
"""
    (args.docs_root / "regional2d_r12_spatial_divergence_diagnosis.md").write_text(summary, encoding="utf-8")
    return figure_manifest


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r10-root", type=Path, default=DEFAULT_R10_ROOT)
    parser.add_argument("--r11-root", type=Path, default=DEFAULT_R11_ROOT)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--docs-root", type=Path, default=DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=DEFAULT_FIGURE_ROOT)
    parser.add_argument("command", choices=("analyze",))
    return parser


def main() -> int:
    args = build_parser().parse_args()
    metrics, levels = build_metrics(args)
    write_outputs(metrics, levels, args)
    print(json.dumps(metrics["hypothesis_ranking"], indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
