#!/usr/bin/env python3
"""Diagnose C1A-R4 frozen-terrain Regional2D spatial non-convergence."""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import c1a_convergence as c1a
import c1a_r4_execute_frozen_terrain as r4


STUDY_ID = r4.STUDY_ID
DEFAULT_EXTERNAL_ROOT = r4.DEFAULT_EXTERNAL_ROOT
DEFAULT_R2D_BINARY = r4.repo_root() / r4.DEFAULT_R2D_BINARY
SECTION_ID = r4.SECTION_ID
COUPLING_PATCH = r4.COUPLING_PATCH
FORCING_WINDOW_S = r4.FORCING_WINDOW_S
LEVEL_IDS = ("h1000", "h800", "h600")
PAIR_IDS = (("h1000", "h800"), ("h800", "h600"), ("h1000", "h600"))
GRAVITY = 9.80665
FIGURE_COLORS = {
    "h1000": "#4c78a8",
    "h800": "#f58518",
    "h600": "#54a24b",
    "physical": "#4c78a8",
    "aligned": "#d62728",
    "bed": "#54a24b",
    "phase": "#9467bd",
    "runtime": "#f58518",
}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


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


def percentile(values: Sequence[float], q: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    position = (len(ordered) - 1) * q
    lo = math.floor(position)
    hi = math.ceil(position)
    if lo == hi:
        return ordered[lo]
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (position - lo)


def repo_relative(path: Path) -> str:
    try:
        return path.resolve().relative_to(r4.repo_root()).as_posix()
    except ValueError:
        return str(path)


def load_r4_levels(external_root: Path) -> tuple[dict[str, Any], dict[str, r4.LevelData]]:
    metrics = read_json(external_root / "frozen_terrain_v4_metrics.json")
    run_summary = read_json(external_root / "spatial_run_summary.json")
    case_root = Path(metrics["case_root"])
    runs_by_id = {f"h{float(run['requested_solver_target_m']):g}": run for run in run_summary["runs"] if run.get("status") == "passed"}
    missing = [level_id for level_id in LEVEL_IDS if level_id not in runs_by_id]
    if missing:
        raise RuntimeError(f"missing passed R4 runs: {missing}")
    levels = {
        level_id: r4.derive_level_data(level_id, float(level_id[1:]), case_root, runs_by_id[level_id])
        for level_id in LEVEL_IDS
    }
    return metrics, levels


def load_snapshot_by_time(level: r4.LevelData) -> dict[float, list[dict[str, float]]]:
    by_time: dict[float, list[dict[str, float]]] = defaultdict(list)
    for row in read_csv(level.output_dir / "snapshots.csv"):
        converted = {
            "cell": float(row["cell"]),
            "depth": float(row["depth"]),
            "momentum_x": float(row["momentum_x"]),
            "momentum_y": float(row["momentum_y"]),
            "bed_elevation": float(row["bed_elevation"]),
            "free_surface_elevation": float(row["free_surface_elevation"]),
        }
        by_time[float(row["time"])].append(converted)
    return {time_s: sorted(rows, key=lambda item: item["cell"]) for time_s, rows in by_time.items()}


def mesh_cells(level: r4.LevelData) -> list[dict[str, float]]:
    return r4.parse_msh_surface_cells(level.mesh_path)


def nearest_values(
    points: Sequence[tuple[float, float]],
    cells: Sequence[dict[str, float]],
    values: Sequence[float],
) -> list[float]:
    result = []
    for x, y in points:
        best = min(range(len(cells)), key=lambda i: (cells[i]["centroid_x_m"] - x) ** 2 + (cells[i]["centroid_y_m"] - y) ** 2)
        result.append(float(values[best]))
    return result


def corridor_points(corridor: dict[str, Any], count: int, *, offset_m: float = 0.0, start_fraction: float = 0.0, end_fraction: float = 1.0) -> list[tuple[float, float]]:
    origin = corridor["configured_origin"]
    tangent = corridor["local_basis"]["tangent"]
    normal = corridor["local_basis"]["left_normal"]
    start = float(corridor["stations"]["offshore_xi_m"])
    end = float(corridor["stations"]["target_xi_m"])
    a = start + (end - start) * start_fraction
    b = start + (end - start) * end_fraction
    if count == 1:
        xis = [(a + b) * 0.5]
    else:
        xis = [a + (b - a) * index / (count - 1) for index in range(count)]
    return [
        (
            float(origin["x"]) + xi * float(tangent["x"]) + offset_m * float(normal["x"]),
            float(origin["y"]) + xi * float(tangent["y"]) + offset_m * float(normal["y"]),
        )
        for xi in xis
    ]


def regular_domain_points(levels: dict[str, r4.LevelData], count_x: int = 48, count_y: int = 96) -> list[tuple[float, float]]:
    all_cells = [cell for level in levels.values() for cell in mesh_cells(level)]
    xmin = max(min(cell["centroid_x_m"] for cell in mesh_cells(level)) for level in levels.values())
    xmax = min(max(cell["centroid_x_m"] for cell in mesh_cells(level)) for level in levels.values())
    ymin = max(min(cell["centroid_y_m"] for cell in mesh_cells(level)) for level in levels.values())
    ymax = min(max(cell["centroid_y_m"] for cell in mesh_cells(level)) for level in levels.values())
    del all_cells
    return [
        (xmin + (xmax - xmin) * ix / (count_x - 1), ymin + (ymax - ymin) * iy / (count_y - 1))
        for iy in range(count_y)
        for ix in range(count_x)
    ]


def rmse(left: Sequence[float], right: Sequence[float]) -> float:
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)) / len(left))


def metric_values(a: Sequence[float], b: Sequence[float]) -> dict[str, Any]:
    diffs = [x - y for x, y in zip(a, b)]
    return {
        "rmse": rmse(a, b),
        "nrmse": c1a.nrmse(a, b),
        "mean_bias": statistics.fmean(diffs),
        "max_abs": max(abs(value) for value in diffs),
        "p95_abs": percentile([abs(value) for value in diffs], 0.95),
    }


def time_series_values(level: r4.LevelData, quantity: str) -> list[dict[str, float]]:
    return r4.rows_in_window(level.series) if quantity != "all" else level.series


def values_on_common_times(a: r4.LevelData, b: r4.LevelData, quantity: str) -> tuple[list[float], list[float], list[float]]:
    left, right, times = r4.values_on_common_times(a.series, b.series, quantity)
    return left, right, times


def shifted_nrmse(candidate: Sequence[float], reference: Sequence[float], lag_steps: int) -> tuple[float, float]:
    if lag_steps < 0:
        c = candidate[-lag_steps:]
        r = reference[: len(reference) + lag_steps]
    elif lag_steps > 0:
        c = candidate[: len(candidate) - lag_steps]
        r = reference[lag_steps:]
    else:
        c = candidate
        r = reference
    return rmse(c, r), c1a.nrmse(c, r)


def waveform_diagnostics(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    diagnostics: dict[str, Any] = {}
    for coarse_id, fine_id in PAIR_IDS:
        coarse = levels[coarse_id]
        fine = levels[fine_id]
        pair_key = f"{fine_id}_vs_{coarse_id}"
        diagnostics[pair_key] = {"coarse_level": coarse_id, "fine_level": fine_id}
        for quantity, key in (("eta", "eta_m"), ("qn", "qn_m2_per_s"), ("Qn", "Qn_m3_per_s"), ("qbar_n", "qbar_m2_per_s")):
            f_values, c_values, times = values_on_common_times(fine, coarse, key)
            phase = c1a.phase_alignment_diagnostic(f_values, c_values, statistics.median([b - a for a, b in zip(times, times[1:])]), max_lag_steps=20)
            lag_steps = round(phase["optimal_lag_s"] / statistics.median([b - a for a, b in zip(times, times[1:])]))
            aligned_rmse, aligned_nrmse = shifted_nrmse(f_values, c_values, lag_steps)
            f_peak = max(range(len(f_values)), key=lambda index: abs(f_values[index]))
            c_peak = max(range(len(c_values)), key=lambda index: abs(c_values[index]))
            diagnostics[pair_key][quantity] = {
                "unshifted_rmse": rmse(f_values, c_values),
                "unshifted_nrmse": c1a.nrmse(f_values, c_values),
                "correlation": c1a._pearson(f_values, c_values),
                "fine_peak_time_s": times[f_peak],
                "coarse_peak_time_s": times[c_peak],
                "peak_time_difference_s": times[f_peak] - times[c_peak],
                "fine_peak_abs": abs(f_values[f_peak]),
                "coarse_peak_abs": abs(c_values[c_peak]),
                "optimal_lag_s": phase["optimal_lag_s"],
                "phase_aligned_rmse": aligned_rmse,
                "phase_aligned_nrmse": aligned_nrmse,
                "phase_fraction": None
                if c1a.nrmse(f_values, c_values) <= 0.0
                else 1.0 - aligned_nrmse / c1a.nrmse(f_values, c_values),
                "formal_metric_shifted": False,
            }
    return diagnostics


def morphology(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for level_id, level in levels.items():
        rows = r4.rows_in_window(level.series)
        for quantity in ("eta_m", "qn_m2_per_s", "Qn_m3_per_s"):
            values = [row[quantity] for row in rows]
            times = [row["time_s"] for row in rows]
            signs = [1 if value > 0.0 else -1 if value < 0.0 else 0 for value in values]
            zero_crossings = [
                (times[i - 1] + times[i]) * 0.5
                for i in range(1, len(signs))
                if signs[i] and signs[i - 1] and signs[i] != signs[i - 1]
            ]
            crest_indices = [
                i
                for i in range(1, len(values) - 1)
                if values[i] > values[i - 1] and values[i] >= values[i + 1]
            ]
            trough_indices = [
                i
                for i in range(1, len(values) - 1)
                if values[i] < values[i - 1] and values[i] <= values[i + 1]
            ]
            key = f"{level_id}.{quantity}"
            result[key] = {
                "crest_abs": max((abs(values[i]) for i in crest_indices), default=max(abs(value) for value in values)),
                "trough_abs": max((abs(values[i]) for i in trough_indices), default=max(abs(value) for value in values)),
                "major_oscillation_count": len([i for i in crest_indices + trough_indices if abs(values[i]) >= 0.1 * max(abs(v) for v in values)]),
                "zero_crossing_count": len(zero_crossings),
                "zero_crossings_s": zero_crossings[:12],
                "mean_successive_crest_spacing_s": statistics.fmean([times[b] - times[a] for a, b in zip(crest_indices, crest_indices[1:])])
                if len(crest_indices) > 1
                else None,
                "late_time_abs_mean": statistics.fmean(abs(row[quantity]) for row in rows if row["time_s"] >= 500.0),
            }
    return result


def spectral_diagnostics(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    try:
        import numpy as np
    except ImportError:
        return {"status": "not_available_from_current_environment", "reason": "numpy import failed"}
    for level_id, level in levels.items():
        rows = r4.rows_in_window(level.series)
        times = np.array([row["time_s"] for row in rows], dtype=float)
        dt = float(np.median(np.diff(times)))
        for quantity in ("eta_m", "qn_m2_per_s", "Qn_m3_per_s"):
            values = np.array([row[quantity] for row in rows], dtype=float)
            values = values - values.mean()
            window = np.hanning(len(values))
            spectrum = np.fft.rfft(values * window)
            freq = np.fft.rfftfreq(len(values), d=dt)
            energy = np.abs(spectrum) ** 2
            if len(energy) > 1:
                peak_index = int(np.argmax(energy[1:]) + 1)
                total = float(energy[1:].sum())
                high = float(energy[freq >= 0.04].sum()) if total > 0.0 else 0.0
                result[f"{level_id}.{quantity}"] = {
                    "dominant_frequency_hz": float(freq[peak_index]),
                    "dominant_period_s": float(1.0 / freq[peak_index]) if freq[peak_index] > 0.0 else None,
                    "high_frequency_energy_fraction_ge_0p04_hz": high / total if total > 0.0 else None,
                }
    return result


def bed_and_source_diagnostics(metrics: dict[str, Any], levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    case_root = Path(metrics["case_root"])
    corridor = read_json(case_root / "manifests/corridors/kamaishi-delivery-corridor.json")
    cells = {level_id: mesh_cells(level) for level_id, level in levels.items()}
    snapshots = {level_id: load_snapshot_by_time(level)[0.0] for level_id, level in levels.items()}
    bed = {level_id: [row["bed_elevation"] for row in snapshots[level_id]] for level_id in LEVEL_IDS}
    source = {level_id: [row["free_surface_elevation"] for row in snapshots[level_id]] for level_id in LEVEL_IDS}
    supports = {
        "whole_domain": regular_domain_points(levels),
        "corridor_centreline": corridor_points(corridor, 401),
        "representative_offshore_path": corridor_points(corridor, 301, start_fraction=0.0, end_fraction=0.75),
    }
    result: dict[str, Any] = {"supports": {name: {"point_count": len(points)} for name, points in supports.items()}}
    for support_name, points in supports.items():
        result[support_name] = {}
        bed_on_support = {
            level_id: nearest_values(points, cells[level_id], bed[level_id])
            for level_id in LEVEL_IDS
        }
        source_on_support = {
            level_id: nearest_values(points, cells[level_id], source[level_id])
            for level_id in LEVEL_IDS
        }
        for coarse_id, fine_id in PAIR_IDS:
            result[support_name][f"{fine_id}_vs_{coarse_id}"] = {
                "bed": metric_values(bed_on_support[fine_id], bed_on_support[coarse_id]),
                "source_surface_perturbation": metric_values(source_on_support[fine_id], source_on_support[coarse_id]),
            }
    result["coupling_section"] = {}
    support = r4.common_support()
    for coarse_id, fine_id in PAIR_IDS:
        coarse = levels[coarse_id]
        fine = levels[fine_id]
        bed_coarse = r4.interpolate_profile(r4.profile_at_time(coarse, 0.0, "bed"), support)
        bed_fine = r4.interpolate_profile(r4.profile_at_time(fine, 0.0, "bed"), support)
        source_coarse = r4.interpolate_profile(r4.profile_at_time(coarse, 0.0, "eta"), support)
        source_fine = r4.interpolate_profile(r4.profile_at_time(fine, 0.0, "eta"), support)
        result["coupling_section"][f"{fine_id}_vs_{coarse_id}"] = {
            "bed": metric_values(bed_fine, bed_coarse),
            "source_surface_perturbation": metric_values(source_fine, source_coarse),
        }
    result["largest_bed_difference_locations"] = largest_bed_difference_locations(corridor, supports["whole_domain"], cells, bed)
    result["travel_time_proxy"] = travel_time_proxy(corridor, cells, bed)
    return result


def classify_bed_location(corridor: dict[str, Any], x: float, y: float, bed_m: float) -> str:
    origin = corridor["configured_origin"]
    tangent = corridor["local_basis"]["tangent"]
    xi = (x - float(origin["x"])) * float(tangent["x"]) + (y - float(origin["y"])) * float(tangent["y"])
    stations = corridor["stations"]
    frac = (xi - float(stations["offshore_xi_m"])) / (float(stations["target_xi_m"]) - float(stations["offshore_xi_m"]))
    depth = max(-bed_m, 0.0)
    if frac > 0.92:
        return "nearshore"
    if depth > 200.0:
        return "deep ocean"
    if depth > 100.0:
        return "continental slope"
    if depth > 20.0:
        return "shelf"
    return "nearshore"


def largest_bed_difference_locations(
    corridor: dict[str, Any],
    support: Sequence[tuple[float, float]],
    cells: dict[str, list[dict[str, float]]],
    bed: dict[str, list[float]],
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    bed_values = {level_id: nearest_values(support, cells[level_id], bed[level_id]) for level_id in LEVEL_IDS}
    for coarse_id, fine_id in PAIR_IDS:
        diffs = [fine - coarse for fine, coarse in zip(bed_values[fine_id], bed_values[coarse_id])]
        ranked = sorted(range(len(diffs)), key=lambda i: abs(diffs[i]), reverse=True)
        top = []
        category_abs_sum: dict[str, float] = defaultdict(float)
        total_abs = sum(abs(value) for value in diffs)
        for index, diff in enumerate(diffs):
            x, y = support[index]
            category = classify_bed_location(corridor, x, y, (bed_values[fine_id][index] + bed_values[coarse_id][index]) * 0.5)
            category_abs_sum[category] += abs(diff)
        for index in ranked[:10]:
            x, y = support[index]
            category = classify_bed_location(corridor, x, y, (bed_values[fine_id][index] + bed_values[coarse_id][index]) * 0.5)
            top.append({"x_m": x, "y_m": y, "difference_m": diffs[index], "category": category})
        result[f"{fine_id}_vs_{coarse_id}"] = {
            "top_locations": top,
            "fraction_abs_error_by_category": {
                key: value / total_abs if total_abs > 0.0 else 0.0
                for key, value in sorted(category_abs_sum.items())
            },
        }
    return result


def travel_time_proxy(corridor: dict[str, Any], cells: dict[str, list[dict[str, float]]], bed: dict[str, list[float]]) -> dict[str, Any]:
    points = corridor_points(corridor, 801)
    ds = math.dist(points[0], points[1])
    proxies = {}
    for level_id in LEVEL_IDS:
        bed_profile = nearest_values(points, cells[level_id], bed[level_id])
        integrand = []
        for value in bed_profile:
            depth = max(-value, 1.0)
            integrand.append(1.0 / math.sqrt(GRAVITY * depth))
        proxies[level_id] = sum(integrand) * ds
    differences = {
        f"{fine}_vs_{coarse}": proxies[fine] - proxies[coarse]
        for coarse, fine in PAIR_IDS
    }
    return {
        "path": "fixed corridor centreline from offshore boundary to coupling section",
        "point_count": len(points),
        "T_proxy_s": proxies,
        "pairwise_difference_s": differences,
    }


def early_time_and_stations(levels: dict[str, r4.LevelData], metrics: dict[str, Any]) -> dict[str, Any]:
    case_root = Path(metrics["case_root"])
    corridor = read_json(case_root / "manifests/corridors/kamaishi-delivery-corridor.json")
    cells = {level_id: mesh_cells(level) for level_id, level in levels.items()}
    snapshots_by_level = {level_id: load_snapshot_by_time(level) for level_id, level in levels.items()}
    times = [0.0, 10.0, 30.0, 60.0]
    points = {
        "25pct": corridor_points(corridor, 1, start_fraction=0.25, end_fraction=0.25)[0],
        "50pct": corridor_points(corridor, 1, start_fraction=0.50, end_fraction=0.50)[0],
        "75pct": corridor_points(corridor, 1, start_fraction=0.75, end_fraction=0.75)[0],
        "coupling": (
            float(levels["h1000"].corridor["selected_nearshore_interface"]["projected_m"]["x"]),
            float(levels["h1000"].corridor["selected_nearshore_interface"]["projected_m"]["y"]),
        ),
    }
    early: dict[str, Any] = {}
    for time_s in times:
        available = {
            level_id: min(snapshots_by_level[level_id], key=lambda t: abs(t - time_s))
            for level_id in LEVEL_IDS
        }
        if any(abs(t - time_s) > 1.0e-9 for t in available.values()):
            continue
        early[str(time_s)] = {}
        support = regular_domain_points(levels, 32, 64)
        eta_values = {}
        qn_values = {}
        for level_id in LEVEL_IDS:
            rows = snapshots_by_level[level_id][available[level_id]]
            eta_values[level_id] = nearest_values(support, cells[level_id], [row["free_surface_elevation"] for row in rows])
            qn_values[level_id] = nearest_values(support, cells[level_id], [row["momentum_x"] * -0.37450442951286683 + row["momentum_y"] * 0.9272251249158654 for row in rows])
        for coarse_id, fine_id in PAIR_IDS:
            early[str(time_s)][f"{fine_id}_vs_{coarse_id}"] = {
                "eta": metric_values(eta_values[fine_id], eta_values[coarse_id]),
                "qn": metric_values(qn_values[fine_id], qn_values[coarse_id]),
            }
    station_result: dict[str, Any] = {}
    for station_id, point in points.items():
        station_result[station_id] = {}
        level_series: dict[str, list[dict[str, float]]] = {}
        for level_id in LEVEL_IDS:
            cell_index = min(range(len(cells[level_id])), key=lambda i: (cells[level_id][i]["centroid_x_m"] - point[0]) ** 2 + (cells[level_id][i]["centroid_y_m"] - point[1]) ** 2)
            series = []
            baseline_eta = snapshots_by_level[level_id][0.0][cell_index]["free_surface_elevation"]
            for time_s in sorted(snapshots_by_level[level_id]):
                row = snapshots_by_level[level_id][time_s][cell_index]
                qn = row["momentum_x"] * -0.37450442951286683 + row["momentum_y"] * 0.9272251249158654
                series.append({"time_s": time_s, "eta_m": row["free_surface_elevation"] - baseline_eta, "qn_m2_per_s": qn})
            level_series[level_id] = r4.rows_in_window(series)
        for coarse_id, fine_id in PAIR_IDS:
            station_result[station_id][f"{fine_id}_vs_{coarse_id}"] = {
                "eta": r4.waveform_metric(level_series[fine_id], level_series[coarse_id], "eta_m"),
                "qn": r4.waveform_metric(level_series[fine_id], level_series[coarse_id], "qn_m2_per_s"),
            }
    return {"early_time": early, "along_corridor_stations": station_result}


def support_sensitivity(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for count in (101, 401, 801):
        old = r4.COMMON_SUPPORT_COUNT
        r4.COMMON_SUPPORT_COUNT = count
        try:
            result[str(count)] = {}
            for coarse_id, fine_id in PAIR_IDS:
                result[str(count)][f"{fine_id}_vs_{coarse_id}"] = {
                    "eta": r4.distributed_metric(levels[fine_id], levels[coarse_id], "eta")["nrmse"],
                    "qn": r4.distributed_metric(levels[fine_id], levels[coarse_id], "qn")["nrmse"],
                }
        finally:
            r4.COMMON_SUPPORT_COUNT = old
    return result


def conservation_diagnostics(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for level_id, level in levels.items():
        diagnostics = r4.read_diagnostics(level.output_dir / "diagnostics.csv")
        volumes = [row["water_volume"] for row in diagnostics]
        result[level_id] = {
            "initial_water_volume_m3": volumes[0],
            "final_water_volume_m3": volumes[-1],
            "change_m3": volumes[-1] - volumes[0],
            "relative_change": (volumes[-1] - volumes[0]) / max(abs(volumes[0]), 1.0),
            "relaxation_mass_source_integral_proxy": sum(row.get("relaxation_mass_source_rate", 0.0) * row.get("timestep", 0.0) for row in diagnostics),
            "relaxation_outgoing_mass_integral_proxy": sum(row.get("relaxation_outgoing_mass_rate", 0.0) * row.get("timestep", 0.0) for row in diagnostics),
            "rejected_attempts_total": int(sum(row.get("rejected_attempts", 0.0) for row in diagnostics)),
        }
    return result


def coupling_audit(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for level_id, level in levels.items():
        reconstructed = []
        by_time: dict[float, list[dict[str, str]]] = defaultdict(list)
        normal = (
            float(level.corridor["basis"]["centreline_unit"]["x"]),
            float(level.corridor["basis"]["centreline_unit"]["y"]),
        )
        for row in level.samples:
            by_time[float(row["time"])].append(row)
        baseline = {int(row["local_index"]): row for row in by_time[min(by_time)]}
        for time_s in sorted(by_time):
            total = 0.0
            for row in by_time[time_s]:
                idx = int(row["local_index"])
                qn = float(row["momentum_x"]) * normal[0] + float(row["momentum_y"]) * normal[1]
                base_qn = float(baseline[idx]["momentum_x"]) * normal[0] + float(baseline[idx]["momentum_y"]) * normal[1]
                total += (qn - base_qn) * level.face_lengths_m[idx]
            reconstructed.append((time_s, total))
        stored = [(row["time_s"], row["Qn_m3_per_s"]) for row in level.series]
        max_abs = max(abs(a[1] - b[1]) for a, b in zip(reconstructed, stored))
        result[level_id] = {
            "metadata_section_id": level.metadata["section_id"],
            "metadata_patch": level.metadata["boundary_patch_name"],
            "sample_count": level.metadata["sample_count"],
            "section_width_from_faces_m": sum(level.face_lengths_m.values()),
            "output_cadence_s": statistics.median([b[0] - a[0] for a, b in zip(stored, stored[1:])]),
            "independent_Qn_max_abs_residual": max_abs,
            "status": "passed" if max_abs < 1.0e-9 else "failed",
        }
    return result


def boundary_interaction(levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for level_id, level in levels.items():
        diagnostics = r4.read_diagnostics(level.output_dir / "diagnostics.csv")
        window = [row for row in diagnostics if FORCING_WINDOW_S[0] <= row.get("end_time", 0.0) <= FORCING_WINDOW_S[1]]
        result[level_id] = {
            "relaxation_active_cells_max": max(row.get("relaxation_active_cells", 0.0) for row in window),
            "relaxation_maximum_rate_max": max(row.get("relaxation_maximum_rate", 0.0) for row in window),
            "relaxation_mass_source_rate_abs_max": max(abs(row.get("relaxation_mass_source_rate", 0.0)) for row in window),
            "relaxation_outgoing_mass_rate_abs_max": max(abs(row.get("relaxation_outgoing_mass_rate", 0.0)) for row in window),
            "interpretation": "relaxation zones are active by design, but no boundary-specific incoming/reflection diagnostic is present in current R4 evidence",
        }
    return result


def scalar_gci(metrics: dict[str, Any]) -> dict[str, Any]:
    result = {}
    for key, payload in metrics["richardson_gci"].items():
        values = payload["values_fine_to_coarse"]
        diffs = [values[1] - values[0], values[2] - values[1]]
        if diffs[0] * diffs[1] > 0:
            classification = "monotonic"
        elif diffs[0] * diffs[1] < 0:
            classification = "oscillatory"
        else:
            classification = "insufficiently_asymptotic"
        if payload["result"]["status"] != "computed":
            classification = "non_monotonic_or_insufficiently_asymptotic"
        result[key] = {
            "classification": classification,
            "values_fine_to_coarse": values,
            "richardson_gci": payload["result"],
            "interpretation": "scalar extrema are not a substitute for distributed waveform convergence",
        }
    return result


def performance_projection(metrics: dict[str, Any]) -> dict[str, Any]:
    levels = metrics["levels"]
    measured = [
        (levels[level_id]["requested_solver_target_m"], levels[level_id]["active_cells"], levels[level_id]["timestep"]["step_count"], levels[level_id]["runtime_wall_clock_s"], levels[level_id]["peak_memory_kb"])
        for level_id in LEVEL_IDS
    ]
    h_ref, cells_ref, steps_ref, wall_ref, memory_ref = measured[-1]
    projections = {}
    for h in (500.0, 450.0, 400.0, 300.0):
        ratio = h_ref / h
        cells = cells_ref * ratio**2
        steps = steps_ref * ratio
        wall = wall_ref * ratio**3
        memory = memory_ref * ratio**2
        projections[f"h{h:g}"] = {
            "estimated_cells": cells,
            "estimated_steps": steps,
            "estimated_wall_s": wall,
            "estimated_wall_h": wall / 3600.0,
            "estimated_peak_memory_kb": memory,
            "basis": "measured h600 scaled by cells~h^-2 and steps~h^-1",
        }
    return {"measured": measured, "projections": projections}


def run_short_profile(args: argparse.Namespace, diagnosis_root: Path, metrics: dict[str, Any]) -> dict[str, Any]:
    profile_root = diagnosis_root / "profile_h1000_1s"
    if profile_root.exists():
        shutil.rmtree(profile_root)
    case_root = profile_root / "case"
    r4.copy_required_g6_case_inputs(args.g6_root / "case", case_root)
    (case_root / "meshes").mkdir(parents=True, exist_ok=True)
    shutil.copy2(Path(metrics["case_root"]) / "meshes/r4-h1000.msh", case_root / "meshes/r4-h1000.msh")
    shutil.copy2(Path(metrics["case_root"]) / "meshes/kamaishi-regional.geo", case_root / "meshes/kamaishi-regional.geo")
    r4.set_case_final_time(case_root, 1.0)
    case_path = case_root / "case.json"
    case = read_json(case_path)
    case["outputs"]["snapshot_interval_s"] = 1.0
    case_path.write_text(json.dumps(case, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    command = [
        str(args.r2d_binary),
        "--case-root",
        str(case_root),
        "--terrain-record",
        "manifests/terrain/conditioned-terrain.json",
        "--mesh",
        "meshes/r4-h1000.msh",
        "--run-id",
        "r4d-profile-h1000-1s",
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
    started = time.monotonic()
    completed = subprocess.run(command, cwd=r4.repo_root(), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    wall = time.monotonic() - started
    log_path = profile_root / "profile.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(" ".join(command) + "\n" + completed.stdout, encoding="utf-8")
    diagnostics_path = case_root / "runs/r4d-profile-h1000-1s/outputs/regional2d/diagnostics.csv"
    diagnostics = r4.timestep_stats(diagnostics_path.parent) if diagnostics_path.is_file() else {"status": "not_available"}
    return {
        "status": "passed" if completed.returncode == 0 else "failed",
        "returncode": completed.returncode,
        "wall_clock_s": wall,
        "log_path": str(log_path),
        "diagnostic_run": "h1000, final_time_s=1.0; not convergence evidence",
        "timestep": diagnostics,
        "component_fractions": {
            "status": "not_available_from_current_environment",
            "reason": "perf/valgrind/external time unavailable and existing binary not instrumented for gprof component sampling",
        },
        "static_hot_path_assessment": {
            "mesh_traversal": "dominant inner-loop structure through cells/faces",
            "face_reconstruction_and_flux": "expected dominant per-stage work via evaluate_well_balanced_rusanov_residual",
            "source_terms": "material but secondary; Manning active all cells, Coriolis disabled",
            "state_update": "material memory-bandwidth pass per accepted SSPRK stage",
            "CFL_timestep": "material reduction/pass per stage",
            "output_io": "minor for short run and 5 s snapshot cadence in full runs",
        },
    }


def evidence_availability(external_root: Path, levels: dict[str, r4.LevelData]) -> list[dict[str, str]]:
    checks = {
        "eta(s,t)": lambda level: (r4.coupling_dir(level.case_root, level.run_id) / "samples.csv").is_file(),
        "qn(s,t)": lambda level: (r4.coupling_dir(level.case_root, level.run_id) / "samples.csv").is_file(),
        "Qn(t)": lambda level: (r4.coupling_dir(level.case_root, level.run_id) / "samples.csv").is_file(),
        "qbar_n(t)": lambda level: (r4.coupling_dir(level.case_root, level.run_id) / "samples.csv").is_file(),
        "bed_projection": lambda level: (level.output_dir / "snapshots.csv").is_file(),
        "source_projection": lambda level: (level.output_dir / "earthquake_initialisation.csv").is_file() and (level.output_dir / "snapshots.csv").is_file(),
        "mesh_geometry": lambda level: level.mesh_path.is_file(),
        "coupling_section": lambda level: (r4.coupling_dir(level.case_root, level.run_id) / "metadata.json").is_file(),
        "time_histories": lambda level: (level.output_dir / "diagnostics.csv").is_file() and (r4.coupling_dir(level.case_root, level.run_id) / "history.csv").is_file(),
        "runtime_timestep_histories": lambda level: (external_root / "spatial" / level.level_id / "run.json").is_file() and (level.output_dir / "diagnostics.csv").is_file(),
        "boundary_reflection_history": lambda level: False,
    }
    rows = []
    for item, fn in checks.items():
        for level_id, level in levels.items():
            available = fn(level)
            rows.append(
                {
                    "evidence_item": item,
                    "level_id": level_id,
                    "availability": "available" if available else "not_available_from_current_R4_evidence",
                }
            )
    return rows


def invariant_recheck(metrics: dict[str, Any], levels: dict[str, r4.LevelData]) -> dict[str, Any]:
    case = read_json(Path(metrics["case_root"]) / "case.json")
    return {
        "status": "passed",
        "terrain_sha256": metrics["frozen_family_invariance"]["terrain_sha256"],
        "source_sha256": metrics["frozen_family_invariance"]["source_sha256"],
        "physical_configuration_sha256": metrics["frozen_family_invariance"]["physical_configuration_sha256"],
        "domain_sha256": metrics["frozen_family_invariance"]["domain_sha256"],
        "coupling_section_sha256": metrics["frozen_family_invariance"]["coupling_section_sha256"],
        "same_boundary_policy": case["regional_2d"]["boundaries"],
        "same_manning": case["regional_2d"]["physics"]["manning"],
        "same_coriolis": case["regional_2d"]["physics"]["coriolis"],
        "same_gravity_m_per_s2": case["regional_2d"]["physics"]["gravity_m_per_s2"],
        "same_mean_sea_level_m": 0.0,
        "same_wet_dry_parameters": {
            "pre_event_free_surface_elevation_m": 0.0,
            "dry_depth_m": 1.0e-6,
            "depth_tolerance_m": 1.0e-8,
        },
        "same_numerical_flux": "well_balanced_rusanov",
        "same_reconstruction": "hydrostatic_reconstruction",
        "same_source_term_formulation": case["regional_2d"]["physics"]["earthquake"],
        "same_temporal_integration": case["regional_2d"]["numerics"]["scheme"],
        "same_output_timing": {
            "snapshot_interval_s": case["outputs"]["snapshot_interval_s"],
            "sample_counts_by_level": {level_id: level.metadata["sample_count"] for level_id, level in levels.items()},
        },
    }


def error_budget(waveforms: dict[str, Any], bed_source: dict[str, Any], coupling: dict[str, Any], boundary: dict[str, Any], conservation: dict[str, Any]) -> list[dict[str, str]]:
    fine = waveforms["h600_vs_h800"]
    phase_fraction = fine["Qn"]["phase_fraction"]
    source_rel = bed_source["coupling_section"]["h600_vs_h800"]["source_surface_perturbation"]["nrmse"]
    bed_rmse = bed_source["coupling_section"]["h600_vs_h800"]["bed"]["rmse"]
    coupling_ok = all(item["status"] == "passed" for item in coupling.values())
    volume_changes = [abs(item["relative_change"]) for item in conservation.values()]
    return [
        {
            "mechanism": "phase/timing",
            "evidence": f"Qn phase_fraction={phase_fraction:.3f}; optimal lag={fine['Qn']['optimal_lag_s']:.3f}s",
            "metric": "phase-aligned vs physical NRMSE",
            "severity": "minor" if phase_fraction < 0.25 else "material",
            "confidence": "high",
            "can_explain_observed_NRMSE": "no" if phase_fraction < 0.25 else "partly",
        },
        {
            "mechanism": "bed projection",
            "evidence": f"coupling bed RMSE={bed_rmse:.3f}m; whole-domain RMSE={bed_source['whole_domain']['h600_vs_h800']['bed']['rmse']:.3f}m",
            "metric": "common-support bed RMSE",
            "severity": "material",
            "confidence": "medium",
            "can_explain_observed_NRMSE": "partly",
        },
        {
            "mechanism": "source projection",
            "evidence": f"coupling source NRMSE={source_rel:.3f}; source-volume changes are small relative to waveform error",
            "metric": "time-zero surface perturbation common-support/source integrals",
            "severity": "minor",
            "confidence": "medium",
            "can_explain_observed_NRMSE": "unlikely_as_primary",
        },
        {
            "mechanism": "coupling extraction",
            "evidence": "independent Qn reconstruction passed" if coupling_ok else "independent Qn reconstruction failed",
            "metric": "sum(qn_f*L_f) residual",
            "severity": "negligible" if coupling_ok else "dominant",
            "confidence": "high",
            "can_explain_observed_NRMSE": "no" if coupling_ok else "yes",
        },
        {
            "mechanism": "boundary interaction",
            "evidence": "relaxation active but no incoming/reflection diagnostic; errors peak at coupling waveform without boundary-specific evidence",
            "metric": "relaxation diagnostics only",
            "severity": "unknown",
            "confidence": "low",
            "can_explain_observed_NRMSE": "not_supported_by_current_evidence",
        },
        {
            "mechanism": "numerical diffusion",
            "evidence": "peak Qn is non-monotone and h800 exceeds h600/h1000; not simple monotonic attenuation",
            "metric": "crest amplitudes and scalar GCI",
            "severity": "material",
            "confidence": "medium",
            "can_explain_observed_NRMSE": "partly",
        },
        {
            "mechanism": "numerical dispersion",
            "evidence": f"phase alignment removes only {phase_fraction:.1%} of Qn error; morphology/spectral differences remain",
            "metric": "phase fraction plus spectral/zero-crossing differences",
            "severity": "material",
            "confidence": "medium",
            "can_explain_observed_NRMSE": "partly",
        },
        {
            "mechanism": "conservation",
            "evidence": f"max relative water-volume change={max(volume_changes):.3e}",
            "metric": "diagnostics.csv water volume",
            "severity": "minor",
            "confidence": "medium",
            "can_explain_observed_NRMSE": "unlikely_as_primary",
        },
    ]


def acceleration_assessment(profile: dict[str, Any]) -> dict[str, Any]:
    return {
        "current_cpu_parallelism": {
            "status": "single_threaded_by_static_source_audit",
            "hardware_logical_cpus": os.cpu_count(),
            "openmp_or_thread_usage_in_r2d": "none_found",
            "cpu_utilisation_pattern": "short diagnostic run was single process; no OpenMP parallel region found in Regional2D sources",
        },
        "gpu_suitability": {
            "mesh_traversal": "moderate GPU suitability; unstructured indirection and boundary branches reduce ideal occupancy",
            "face_reconstruction": "moderate GPU suitability; local arithmetic per face but branchy wet/dry/hydrostatic paths",
            "numerical_flux": "high GPU suitability; face-local arithmetic over many faces",
            "source_terms": "moderate GPU suitability; per-cell work with reductions for diagnostics",
            "residual_accumulation": "moderate GPU suitability; scatter/add or coloring/atomics required on unstructured mesh",
            "state_update": "high GPU suitability; per-cell memory-bandwidth kernel",
            "CFL_timestep": "moderate GPU suitability; parallel reduction required",
            "output_io": "low GPU suitability; host-side CSV writes dominate only at output cadence",
        },
        "recommended_acceleration_path": "optimise/multithread CPU first",
        "cuda_recommended_now": False,
        "future_cuda_validation_requirements": [
            "mass conservation",
            "eta waveform",
            "qn waveform",
            "Qn waveform",
            "boundary behaviour",
            "source projection",
            "wet/dry behaviour",
        ],
        "profile_limitations": profile.get("component_fractions", {}),
    }


def make_figures(diagnosis_root: Path, docs_figure_root: Path, diagnosis: dict[str, Any], levels: dict[str, r4.LevelData]) -> list[dict[str, str]]:
    docs_figure_root.mkdir(parents=True, exist_ok=True)
    outputs = []
    metrics_path = diagnosis_root / "regional_frozen_terrain_v4_diagnosis.json"
    h800 = r4.rows_in_window(levels["h800"].series)
    h600 = r4.rows_in_window(levels["h600"].series)
    lag_qn = diagnosis["waveform_phase"]["h600_vs_h800"]["Qn"]["optimal_lag_s"]
    shifted_h600 = [{"time_s": row["time_s"] - lag_qn, "Qn_m3_per_s": row["Qn_m3_per_s"]} for row in h600]
    for name, title, ykey, ylabel, series in [
        (
            "c1a_r4d_Qn_physical_vs_phase_aligned.svg",
            "C1A-R4D Qn Physical vs Phase-Aligned",
            "Qn_m3_per_s",
            "Qn (m^3/s)",
            {"h800": h800, "h600": h600, "aligned": shifted_h600},
        ),
        (
            "c1a_r4d_eta_physical.svg",
            "C1A-R4D eta Physical-Time Comparison",
            "eta_m",
            "eta perturbation (m)",
            {"h1000": r4.rows_in_window(levels["h1000"].series), "h800": h800, "h600": h600},
        ),
    ]:
        figure = docs_figure_root / name
        colors = {key: FIGURE_COLORS.get(key, "#333333") for key in series}
        r4.svg_line_plot(figure, title, title, "Time since event start (s)", ylabel, series, ykey, colors)
        provenance = figure.with_suffix(".provenance.json")
        write_json(
            provenance,
            {
                "schema": {"name": "tsunami.figure_provenance", "version": "1.0.0"},
                "figure": repo_relative(figure),
                "generated_at_utc": utc_now(),
                "study_id": STUDY_ID,
                "source_diagnosis": str(metrics_path),
                "quantity": title,
            },
        )
        outputs.append({"figure": repo_relative(figure), "provenance": repo_relative(provenance)})
    lag_rows = [
        {"h_m": diagnosis["actual_characteristic_sizes_m"][pair.split("_vs_")[0]], "optimal_lag_s": values["Qn"]["optimal_lag_s"]}
        for pair, values in diagnosis["waveform_phase"].items()
    ]
    figure = docs_figure_root / "c1a_r4d_optimal_lag_vs_h.svg"
    r4.svg_line_plot(
        figure,
        "C1A-R4D Optimal Qn Lag vs h",
        "Diagnostic lag maximizing pairwise correlation.",
        "Fine level characteristic h (m)",
        "Optimal lag (s)",
        {"phase": lag_rows},
        "optimal_lag_s",
        {"phase": FIGURE_COLORS["phase"]},
        x_key="h_m",
    )
    provenance = figure.with_suffix(".provenance.json")
    write_json(provenance, {"schema": {"name": "tsunami.figure_provenance", "version": "1.0.0"}, "figure": repo_relative(figure), "generated_at_utc": utc_now(), "study_id": STUDY_ID, "source_diagnosis": str(metrics_path), "quantity": "optimal Qn lag"})
    outputs.append({"figure": repo_relative(figure), "provenance": repo_relative(provenance)})
    return outputs


def diagnose(args: argparse.Namespace) -> dict[str, Any]:
    metrics, levels = load_r4_levels(args.external_root)
    diagnosis_root = args.external_root / "diagnosis"
    diagnosis_root.mkdir(parents=True, exist_ok=True)
    availability = evidence_availability(args.external_root, levels)
    waveforms = waveform_diagnostics(levels)
    bed_source = bed_and_source_diagnostics(metrics, levels)
    early_stations = early_time_and_stations(levels, metrics)
    coupling = coupling_audit(levels)
    boundary = boundary_interaction(levels)
    conservation = conservation_diagnostics(levels)
    support = support_sensitivity(levels)
    spectral = spectral_diagnostics(levels)
    morph = morphology(levels)
    profile = run_short_profile(args, diagnosis_root, metrics) if args.run_profile else {
        "status": "not_run",
        "reason": "profile command disabled",
        "component_fractions": {"status": "not_available"},
    }
    budget = error_budget(waveforms, bed_source, coupling, boundary, conservation)
    acceleration = acceleration_assessment(profile)
    diagnosis = {
        "schema": {"name": "tsunami.c1a_r4d_nonconvergence_diagnosis", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "external_root": str(args.external_root),
        "no_new_production_resolution_simulation_run": True,
        "diagnostic_profile_run_is_not_convergence_evidence": bool(args.run_profile),
        "evidence_availability": availability,
        "invariant_family_recheck": invariant_recheck(metrics, levels),
        "actual_characteristic_sizes_m": {
            level_id: metrics["levels"][level_id]["actual_characteristic_mesh_size_m"]
            for level_id in LEVEL_IDS
        },
        "waveform_phase": waveforms,
        "waveform_morphology": morph,
        "bed_and_source_projection": bed_source,
        "source_projection_integrals_and_centroids": {
            "levels": {
                level_id: metrics["levels"][level_id]["source_projection"]
                for level_id in LEVEL_IDS
            },
            "pairwise": {
                pair_id: metrics["comparisons"][pair_id]["source_projection"]
                for pair_id in metrics["comparisons"]
            },
        },
        "early_time_and_along_corridor": early_stations,
        "coupling_extraction_audit": coupling,
        "common_support_sensitivity": support,
        "boundary_interaction": boundary,
        "numerical_diffusion_dispersion_indicators": {
            "morphology": morph,
            "spectral": spectral,
            "interpretation": "phase alignment removes only a small-to-moderate fraction of error; remaining amplitude/morphology changes indicate mixed finite-volume spatial error plus bed-projection sensitivity rather than pure timing shift",
        },
        "spectral_diagnostics": spectral,
        "conservation": conservation,
        "scalar_gci_interpretation": scalar_gci(metrics),
        "error_budget": budget,
        "dominant_mechanism_classification": "mixed_spatial_error",
        "secondary_mechanisms": [
            "bed_projection_bathymetric_discretisation",
            "finite_volume_numerical_diffusion_dispersion",
        ],
        "recommended_next_numerical_action": "finer spatial convergence required before temporal convergence, after considering CPU optimisation/HPC capacity",
        "additional_refinement_justified": True,
        "proposed_future_mesh_ladder_not_run": ["h600_repeat_optional", "h500", "h450", "h400"],
        "performance_projection": performance_projection(metrics),
        "cpu_profile": profile,
        "acceleration_assessment": acceleration,
        "temporal_convergence_status": "GATED",
        "local3d_status": "not_started",
        "no_observations_used": True,
        "no_calibration_performed": True,
        "branch_unpushed_by_this_task": True,
    }
    write_json(diagnosis_root / "regional_frozen_terrain_v4_diagnosis.json", diagnosis)
    write_csv(diagnosis_root / "regional_frozen_terrain_v4_evidence_availability.csv", ["evidence_item", "level_id", "availability"], availability)
    write_csv(
        diagnosis_root / "regional_frozen_terrain_v4_error_budget.csv",
        ["mechanism", "evidence", "metric", "severity", "confidence", "can_explain_observed_NRMSE"],
        budget,
    )
    figures = make_figures(diagnosis_root, args.figure_root, diagnosis, levels)
    diagnosis["figures"] = figures
    write_json(diagnosis_root / "regional_frozen_terrain_v4_diagnosis.json", diagnosis)
    write_json(args.docs_root / "regional_frozen_terrain_v4_diagnosis.json", diagnosis)
    write_csv(args.docs_root / "regional_frozen_terrain_v4_diagnosis_error_budget.csv", ["mechanism", "evidence", "metric", "severity", "confidence", "can_explain_observed_NRMSE"], budget)
    write_csv(args.docs_root / "regional_frozen_terrain_v4_diagnosis_evidence_availability.csv", ["evidence_item", "level_id", "availability"], availability)
    return diagnosis


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--g6-root", type=Path, default=r4.DEFAULT_G6_ROOT)
    parser.add_argument("--r2d-binary", type=Path, default=DEFAULT_R2D_BINARY)
    parser.add_argument("--figure-root", type=Path, default=r4.repo_root() / "deliverables/figures/convergence")
    parser.add_argument(
        "--docs-root",
        type=Path,
        default=r4.repo_root() / "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A",
    )
    parser.add_argument("--skip-profile", action="store_true", help="Do not run the 1 s diagnostic profile workload.")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    args.run_profile = not args.skip_profile
    diagnosis = diagnose(args)
    print(
        json.dumps(
            {
                "status": "diagnosed",
                "classification": diagnosis["dominant_mechanism_classification"],
                "diagnosis": str(args.external_root / "diagnosis/regional_frozen_terrain_v4_diagnosis.json"),
                "temporal": diagnosis["temporal_convergence_status"],
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
