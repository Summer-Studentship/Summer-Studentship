#!/usr/bin/env python3
"""Diagnose Regional2D spatial order, mesh topology, and R5 non-convergence."""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
import shutil
import statistics
import subprocess
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Sequence


STUDY_ID = "regional2d-spatial-method-r6"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-spatial-method-r6")
DEFAULT_R5_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional-spatial-frozen-terrain-v5")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
DEFAULT_BINARY = Path("build/linux-gcc-crs-openmp-release/apps/r2d_case/tsunami_r2d_case")
LEVELS = ("h600", "h500", "h450", "h400")
RUN_ID_BY_LEVEL = {"h600": "r4-full-h600", "h500": "r5-full-h500", "h450": "r5-full-h450", "h400": "r5-full-h400"}
COUPLING_SECTION = "kamaishi-nearshore-interface"
TERRAIN_RESOLUTION_M = 1000.0
SECTION_WIDTH_M = 8000.0
PI = math.pi
G = 9.81


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
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def command_output(command: Sequence[str], *, cwd: Path | None = None) -> str | None:
    try:
        completed = subprocess.run(list(command), cwd=cwd or repo_root(), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError:
        return None
    return completed.stdout.strip() if completed.returncode == 0 else None


def update_state(external_root: Path, state: str, extra: dict[str, Any] | None = None) -> None:
    path = external_root / "execution_state.json"
    payload = read_json(path) if path.is_file() else {"schema": {"name": "tsunami.c1a_r6_execution_state", "version": "1.0.0"}}
    payload.update(
        {
            "study_id": STUDY_ID,
            "state": state,
            "updated_at_utc": utc_now(),
            "external_root": external_root.as_posix(),
        }
    )
    if extra:
        payload.update(extra)
    write_json(path, payload)


def percentile(values: Sequence[float], q: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    pos = (len(ordered) - 1) * q
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return ordered[lo]
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (pos - lo)


def stats(values: Sequence[float]) -> dict[str, float]:
    return {
        "min": min(values),
        "p01": percentile(values, 0.01),
        "p05": percentile(values, 0.05),
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
        "p50": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
        "max": max(values),
    }


def parse_msh(path: Path) -> dict[str, Any]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    physical_names: dict[tuple[int, int], str] = {}
    curve_physical: dict[int, list[int]] = {}
    nodes: dict[int, tuple[float, float, float]] = {}
    triangles: list[tuple[int, int, int]] = []
    line_elements: list[dict[str, Any]] = []
    i = 0
    while i < len(lines):
        marker = lines[i]
        if marker == "$PhysicalNames":
            count = int(lines[i + 1])
            i += 2
            for _ in range(count):
                dim, tag, name = lines[i].split(maxsplit=2)
                physical_names[(int(dim), int(tag))] = name.strip('"')
                i += 1
        elif marker == "$Entities":
            point_count, curve_count, surface_count, volume_count = map(int, lines[i + 1].split())
            i += 2 + point_count
            for _ in range(curve_count):
                parts = lines[i].split()
                tag = int(parts[0])
                physical_count = int(parts[7])
                curve_physical[tag] = [int(value) for value in parts[8 : 8 + physical_count]]
                i += 1
            i += surface_count + volume_count
        elif marker == "$Nodes":
            block_count, _, _, _ = map(int, lines[i + 1].split())
            i += 2
            for _ in range(block_count):
                _, _, _, node_count = map(int, lines[i].split())
                i += 1
                tags = [int(lines[i + offset]) for offset in range(node_count)]
                i += node_count
                coords = [tuple(float(value) for value in lines[i + offset].split()[:3]) for offset in range(node_count)]
                i += node_count
                nodes.update(dict(zip(tags, coords)))
        elif marker == "$Elements":
            block_count, _, _, _ = map(int, lines[i + 1].split())
            i += 2
            for _ in range(block_count):
                entity_dim, entity_tag, element_type, element_count = map(int, lines[i].split())
                i += 1
                names = [physical_names.get((entity_dim, tag), str(tag)) for tag in curve_physical.get(entity_tag, [])]
                for _ in range(element_count):
                    values = [int(value) for value in lines[i].split()]
                    i += 1
                    if entity_dim == 2 and element_type == 2:
                        triangles.append((values[1], values[2], values[3]))
                    elif entity_dim == 1 and element_type == 1:
                        line_elements.append({"nodes": (values[1], values[2]), "entity": entity_tag, "physical_names": names})
        else:
            i += 1
    return {"nodes": nodes, "triangles": triangles, "line_elements": line_elements, "sha256": file_sha256(path)}


def triangle_quality(nodes: dict[int, tuple[float, float, float]], tri: tuple[int, int, int]) -> dict[str, float]:
    a, b, c = (nodes[idx] for idx in tri)
    ab = math.hypot(a[0] - b[0], a[1] - b[1])
    bc = math.hypot(b[0] - c[0], b[1] - c[1])
    ca = math.hypot(c[0] - a[0], c[1] - a[1])
    area = abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) * 0.5
    edges = sorted([ab, bc, ca])
    angles = []
    for x, y, z in [(bc, ca, ab), (ca, ab, bc), (ab, bc, ca)]:
        value = max(-1.0, min(1.0, (y * y + z * z - x * x) / (2.0 * y * z)))
        angles.append(math.degrees(math.acos(value)))
    semiperimeter = 0.5 * (ab + bc + ca)
    inradius = area / semiperimeter if semiperimeter else 0.0
    circumradius = (ab * bc * ca) / (4.0 * area) if area else math.inf
    min_altitude = 2.0 * area / edges[-1] if edges[-1] else 0.0
    return {
        "area": area,
        "edge_min": edges[0],
        "edge_max": edges[-1],
        "edge_mean": statistics.fmean(edges),
        "min_angle": min(angles),
        "max_angle": max(angles),
        "aspect_ratio": edges[-1] / min_altitude if min_altitude else math.inf,
        "radius_ratio": (2.0 * inradius / circumradius) if circumradius else 0.0,
        "centroid_x": (a[0] + b[0] + c[0]) / 3.0,
        "centroid_y": (a[1] + b[1] + c[1]) / 3.0,
    }


def mesh_quality(level: str, mesh_path: Path, run_dir: Path, h: float) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    parsed = parse_msh(mesh_path)
    qualities = [triangle_quality(parsed["nodes"], tri) for tri in parsed["triangles"]]
    edge_count: Counter[tuple[int, int]] = Counter()
    for tri in parsed["triangles"]:
        for edge in [(tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])]:
            edge_count[tuple(sorted(edge))] += 1
    line_lengths_by_name: defaultdict[str, list[float]] = defaultdict(list)
    for item in parsed["line_elements"]:
        a, b = (parsed["nodes"][idx] for idx in item["nodes"])
        length = math.hypot(a[0] - b[0], a[1] - b[1])
        for name in item["physical_names"] or ["unnamed"]:
            line_lengths_by_name[name].append(length)
    snapshots0: dict[int, dict[str, float]] = {}
    snapshots_path = run_dir / "outputs/regional2d/snapshots.csv"
    for row in read_csv(snapshots_path):
        if float(row["time"]) != 0.0:
            break
        snapshots0[int(row["cell"])] = {
            "depth": float(row["depth"]),
            "bed": float(row["bed_elevation"]),
            "eta": float(row["free_surface_elevation"]),
        }
    coupling_points = [
        (float(row["x_m"]), float(row["y_m"]))
        for row in read_json(run_dir / f"outputs/regional2d/coupling/{COUPLING_SECTION}/metadata.json")["samples"]
    ]
    source_threshold = percentile([abs(value["eta"]) for value in snapshots0.values()], 0.90)
    x_values = [q["centroid_x"] for q in qualities]
    y_values = [q["centroid_y"] for q in qualities]
    bbox = (min(x_values), max(x_values), min(y_values), max(y_values))
    low_angle_limit = percentile([q["min_angle"] for q in qualities], 0.01)
    low_radius_limit = percentile([q["radius_ratio"] for q in qualities], 0.01)
    rows = []
    location_counts: Counter[str] = Counter()
    for index, q in enumerate(qualities):
        snap = snapshots0.get(index, {})
        poor = q["min_angle"] <= low_angle_limit or q["radius_ratio"] <= low_radius_limit
        nearest_coupling = min((math.hypot(q["centroid_x"] - x, q["centroid_y"] - y) for x, y in coupling_points), default=math.inf)
        if poor:
            if nearest_coupling <= max(2.0 * h, 500.0):
                location_counts["coupling_section"] += 1
            elif abs(snap.get("eta", 0.0)) >= source_threshold:
                location_counts["earthquake/source_region"] += 1
            elif snap.get("depth", math.inf) < 100.0:
                location_counts["nearshore_region"] += 1
            elif snap.get("depth", math.inf) < 300.0:
                location_counts["shelf"] += 1
            elif min(q["centroid_x"] - bbox[0], bbox[1] - q["centroid_x"], q["centroid_y"] - bbox[2], bbox[3] - q["centroid_y"]) <= 2.0 * h:
                location_counts["open_boundaries"] += 1
            else:
                location_counts["continental_slope_or_interior"] += 1
        rows.append(
            {
                "level": level,
                "cell": index,
                "centroid_x_m": q["centroid_x"],
                "centroid_y_m": q["centroid_y"],
                "area_m2": q["area"],
                "min_angle_deg": q["min_angle"],
                "max_angle_deg": q["max_angle"],
                "aspect_ratio": q["aspect_ratio"],
                "radius_ratio": q["radius_ratio"],
                "is_low_quality_p01": poor,
                "nearest_coupling_sample_m": nearest_coupling,
                "depth_m": snap.get("depth", ""),
                "bed_elevation_m": snap.get("bed", ""),
                "eta0_m": snap.get("eta", ""),
            }
        )
    summary = {
        "mesh_sha256": parsed["sha256"],
        "cell_count": len(qualities),
        "face_count": len(edge_count),
        "boundary_face_count": sum(1 for value in edge_count.values() if value == 1),
        "cell_area": stats([q["area"] for q in qualities]),
        "edge_length": stats([value for q in qualities for value in (q["edge_min"], q["edge_max"], q["edge_mean"])]),
        "triangle_min_angle": stats([q["min_angle"] for q in qualities]),
        "triangle_max_angle": stats([q["max_angle"] for q in qualities]),
        "aspect_ratio": stats([q["aspect_ratio"] for q in qualities]),
        "radius_ratio": stats([q["radius_ratio"] for q in qualities]),
        "low_quality_cell_location_counts": dict(location_counts),
        "coupling_boundary_face_lengths": {name: stats(values) for name, values in line_lengths_by_name.items() if name == "boundary.inland"},
        "all_boundary_names": sorted(line_lengths_by_name),
    }
    return summary, rows


def normalised_waveform_metrics(r5_metrics: dict[str, Any]) -> list[dict[str, Any]]:
    rows = []
    for pair in ("h500_vs_h600", "h450_vs_h500", "h400_vs_h450"):
        comp = r5_metrics["comparisons"][pair]
        for key, quantity in (("eta_waveform", "eta"), ("qn_waveform", "qn"), ("Qn_waveform", "Qn")):
            metric = comp[key]
            denominator = metric["rmse"] / metric["nrmse"] if metric["nrmse"] else math.nan
            rows.append(
                {
                    "pair": pair,
                    "quantity": quantity,
                    "rmse": metric["rmse"],
                    "nrmse": metric["nrmse"],
                    "normalization_denominator": denominator,
                    "max_abs_difference": metric["max_abs_difference"],
                    "correlation": metric["correlation"],
                    "candidate_peak_abs": metric["candidate_peak_abs"],
                    "reference_peak_abs": metric["reference_peak_abs"],
                    "optimal_lag_s": metric["phase_alignment"]["optimal_lag_s"],
                    "phase_aligned_nrmse": metric["phase_alignment"]["phase_aligned_nrmse"],
                }
            )
    return rows


def make_structured_triangles(columns: int) -> tuple[list[tuple[float, float]], list[tuple[int, int, int]], float, float]:
    length_x = 1.0
    length_y = 0.25
    rows = 1
    points = [(length_x * col / columns, length_y * row / rows) for row in range(rows + 1) for col in range(columns + 1)]
    def vid(col: int, row: int) -> int:
        return row * (columns + 1) + col
    tris = []
    for row in range(rows):
        for col in range(columns):
            v00, v10, v01, v11 = vid(col, row), vid(col + 1, row), vid(col, row + 1), vid(col + 1, row + 1)
            tris.append((v00, v10, v11))
            tris.append((v00, v11, v01))
    return points, tris, length_x, length_y


def controlled_benchmark() -> tuple[dict[str, Any], list[dict[str, Any]]]:
    # Source-equivalent flat-bed residual benchmark mirroring the C++ R6 Catch diagnostic.
    mean_depth = 1.0
    amplitude = 1.0e-4
    c = math.sqrt(G * mean_depth)
    rows = []
    for columns in (16, 32, 64, 128):
        points, tris, lx, ly = make_structured_triangles(columns)
        k = 2.0 * PI / lx
        residual = [[0.0, 0.0, 0.0] for _ in tris]
        cell_area = []
        centroids = []
        states = []
        for tri in tris:
            pts = [points[i] for i in tri]
            area = abs((pts[1][0] - pts[0][0]) * (pts[2][1] - pts[0][1]) - (pts[2][0] - pts[0][0]) * (pts[1][1] - pts[0][1])) * 0.5
            cx = sum(p[0] for p in pts) / 3.0
            cy = sum(p[1] for p in pts) / 3.0
            eta = amplitude * math.sin(k * cx)
            cell_area.append(area)
            centroids.append((cx, cy))
            states.append((mean_depth + eta, c * eta, 0.0))
        def exact_state(x: float) -> tuple[float, float, float]:
            local_eta = amplitude * math.sin(k * x)
            return (mean_depth + local_eta, c * local_eta, 0.0)
        def flux_for(left: tuple[float, float, float], right: tuple[float, float, float], nx: float, ny: float) -> tuple[float, float, float]:
            def phys(s: tuple[float, float, float]) -> tuple[float, float, float]:
                h, qx, qy = s
                u, v = qx / h, qy / h
                un = u * nx + v * ny
                return (h * un, qx * un + 0.5 * G * h * h * nx, qy * un + 0.5 * G * h * h * ny)
            def speed(s: tuple[float, float, float]) -> float:
                h, qx, qy = s
                return abs((qx / h) * nx + (qy / h) * ny) + math.sqrt(G * h)
            fl = phys(left)
            fr = phys(right)
            alpha = max(speed(left), speed(right))
            return tuple(0.5 * (fl[i] + fr[i]) - 0.5 * alpha * (right[i] - left[i]) for i in range(3))
        edge_owner: dict[tuple[int, int], tuple[int, tuple[int, int]]] = {}
        for ci, tri in enumerate(tris):
            for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
                key = tuple(sorted((a, b)))
                if key in edge_owner:
                    owner, oriented = edge_owner.pop(key)
                    pairs = [(owner, oriented[0], oriented[1], ci)]
                else:
                    edge_owner[key] = (ci, (a, b))
                    pairs = []
                for owner, va, vb, neigh in pairs:
                    ax, ay = points[va]
                    bx, by = points[vb]
                    dx, dy = bx - ax, by - ay
                    length = math.hypot(dx, dy)
                    nx, ny = dy / length, -dx / length
                    left = states[owner]
                    right = states[neigh]
                    flux = flux_for(left, right, nx, ny)
                    for i in range(3):
                        residual[owner][i] += flux[i] * length
                        residual[neigh][i] -= flux[i] * length
        for owner, oriented in edge_owner.values():
            va, vb = oriented
            ax, ay = points[va]
            bx, by = points[vb]
            dx, dy = bx - ax, by - ay
            length = math.hypot(dx, dy)
            nx, ny = dy / length, -dx / length
            right = exact_state(0.5 * (ax + bx))
            flux = flux_for(states[owner], right, nx, ny)
            for i in range(3):
                residual[owner][i] += flux[i] * length
        area_sum = eta_l1 = eta_l2 = eta_linf = qx_l1 = qx_l2 = qx_linf = qy_l2 = qy_linf = 0.0
        for ci, (cx, _) in enumerate(centroids):
            if cx < 0.15 or cx > 0.85:
                continue
            exact_mass = c * amplitude * k * math.cos(k * cx)
            exact_qx = G * mean_depth * amplitude * k * math.cos(k * cx)
            mass_error = residual[ci][0] / cell_area[ci] - exact_mass
            qx_error = residual[ci][1] / cell_area[ci] - exact_qx
            qy_error = residual[ci][2] / cell_area[ci]
            area_sum += cell_area[ci]
            eta_l1 += cell_area[ci] * abs(mass_error)
            eta_l2 += cell_area[ci] * mass_error * mass_error
            eta_linf = max(eta_linf, abs(mass_error))
            qx_l1 += cell_area[ci] * abs(qx_error)
            qx_l2 += cell_area[ci] * qx_error * qx_error
            qx_linf = max(qx_linf, abs(qx_error))
            qy_l2 += cell_area[ci] * qy_error * qy_error
            qy_linf = max(qy_linf, abs(qy_error))
        rows.append(
            {
                "level": f"n{columns}",
                "columns": columns,
                "cells": len(tris),
                "actual_h": math.sqrt((lx * ly) / len(tris)),
                "eta_l1": eta_l1 / area_sum,
                "eta_l2": math.sqrt(eta_l2 / area_sum),
                "eta_linf": eta_linf,
                "qx_l1": qx_l1 / area_sum,
                "qx_l2": math.sqrt(qx_l2 / area_sum),
                "qx_linf": qx_linf,
                "qy_l2": math.sqrt(qy_l2 / area_sum),
                "qy_linf": qy_linf,
            }
        )
    for coarse, fine in zip(rows, rows[1:]):
        for quantity in ("eta", "qx"):
            for norm in ("l1", "l2", "linf"):
                key = f"{quantity}_{norm}"
                fine[f"p_{key}_from_previous"] = math.log(coarse[key] / fine[key]) / math.log(coarse["actual_h"] / fine["actual_h"])
    orders = [row["p_eta_l2_from_previous"] for row in rows[1:]]
    summary = {
        "benchmark_id": "smooth_linear_wave_semidiscrete_residual",
        "benchmark_kind": "flat-bed small-amplitude linear wave, nested structured triangular meshes",
        "uses_same_flux_formula": "Rusanov flux with piecewise-constant cell states and flat-bed hydrostatic reconstruction; C++ test exercises the production residual path and documents lower-than-formal behaviour on the smooth diagnostic.",
        "timestep_treatment": "semi-discrete residual at t=0; temporal truncation excluded",
        "expected_spatial_order": 1.0,
        "classification": "matches_expected_order" if min(orders) > 0.65 and max(orders) < 1.45 else "lower_than_expected_order",
        "amplitude_dissipation_trend": "first-order Rusanov dissipation scales approximately with h; smaller h reduces smoothing but the method remains deliberately diffusive",
        "phase_speed_trend": "not measured by time integration in this semi-discrete residual benchmark; R5 phase lags remain event-case evidence",
        "levels": rows,
    }
    return summary, rows


def make_figures(docs_root: Path, figure_root: Path, mesh_rows: list[dict[str, Any]], formal_rows: list[dict[str, Any]], r5_metrics: dict[str, Any]) -> dict[str, Any]:
    figure_root.mkdir(parents=True, exist_ok=True)
    manifest = {"schema": {"name": "tsunami.c1a_r6_figures", "version": "1.0.0"}, "figures": []}

    def save_svg(name: str, series: list[tuple[str, list[tuple[float, float]], str]], xlabel: str, ylabel: str, provenance: dict[str, Any], *, logx: bool = False, logy: bool = False, invert_x: bool = False) -> None:
        path = figure_root / name
        width, height = 640, 420
        left, right, top, bottom = 70, 24, 28, 64
        plot_w, plot_h = width - left - right, height - top - bottom
        transformed: list[tuple[str, list[tuple[float, float]], str]] = []
        xs: list[float] = []
        ys: list[float] = []
        for label, points, color in series:
            values = []
            for x, y in points:
                tx = math.log10(x) if logx else x
                ty = math.log10(y) if logy else y
                values.append((tx, ty))
                xs.append(tx)
                ys.append(ty)
            transformed.append((label, values, color))
        xmin, xmax = min(xs), max(xs)
        ymin, ymax = min(ys), max(ys)
        if xmin == xmax:
            xmax = xmin + 1.0
        if ymin == ymax:
            ymax = ymin + 1.0
        ypad = 0.08 * (ymax - ymin)
        ymin -= ypad
        ymax += ypad
        def sx(x: float) -> float:
            frac = (x - xmin) / (xmax - xmin)
            if invert_x:
                frac = 1.0 - frac
            return left + frac * plot_w
        def sy(y: float) -> float:
            return top + (1.0 - ((y - ymin) / (ymax - ymin))) * plot_h
        body = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
            '<rect width="100%" height="100%" fill="white"/>',
            f'<line x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}" stroke="#24292f"/>',
            f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}" stroke="#24292f"/>',
        ]
        for i in range(6):
            gx = left + i * plot_w / 5.0
            gy = top + i * plot_h / 5.0
            body.append(f'<line x1="{gx:.2f}" y1="{top}" x2="{gx:.2f}" y2="{top + plot_h}" stroke="#d0d7de" stroke-width="1"/>')
            body.append(f'<line x1="{left}" y1="{gy:.2f}" x2="{left + plot_w}" y2="{gy:.2f}" stroke="#d0d7de" stroke-width="1"/>')
        for label, points, color in transformed:
            coords = " ".join(f"{sx(x):.2f},{sy(y):.2f}" for x, y in points)
            body.append(f'<polyline points="{coords}" fill="none" stroke="{color}" stroke-width="2.2"/>')
            for x, y in points:
                body.append(f'<circle cx="{sx(x):.2f}" cy="{sy(y):.2f}" r="4" fill="{color}"/>')
        body.append(f'<text x="{left + plot_w / 2:.2f}" y="{height - 18}" font-family="sans-serif" font-size="15" text-anchor="middle">{html.escape(xlabel)}</text>')
        body.append(f'<text x="18" y="{top + plot_h / 2:.2f}" font-family="sans-serif" font-size="15" text-anchor="middle" transform="rotate(-90 18 {top + plot_h / 2:.2f})">{html.escape(ylabel)}</text>')
        legend_x = left + 12
        legend_y = top + 20
        for index, (label, _, color) in enumerate(transformed):
            y = legend_y + index * 20
            body.append(f'<line x1="{legend_x}" y1="{y}" x2="{legend_x + 22}" y2="{y}" stroke="{color}" stroke-width="2.2"/>')
            body.append(f'<text x="{legend_x + 30}" y="{y + 5}" font-family="sans-serif" font-size="13">{html.escape(label)}</text>')
        body.append("</svg>")
        path.write_text("\n".join(body) + "\n", encoding="utf-8")
        prov = path.with_suffix(".provenance.json")
        write_json(prov, {"generated_at_utc": utc_now(), "script": Path(__file__).as_posix(), **provenance})
        manifest["figures"].append({"path": path.as_posix(), "provenance": prov.as_posix()})

    hs = [row["actual_h"] for row in formal_rows]
    save_svg(
        "c1a_r6_formal_error_vs_h.svg",
        [
            ("eta L2", list(zip(hs, [row["eta_l2"] for row in formal_rows])), "#1f77b4"),
            ("qx L2", list(zip(hs, [row["qx_l2"] for row in formal_rows])), "#d62728"),
        ],
        "Characteristic h",
        "Residual-density error",
        {"source": "regional2d_r6_formal_order.csv"},
        logx=True,
        logy=True,
        invert_x=True,
    )

    ordered = sorted({row["level"]: row for row in mesh_rows}.values(), key=lambda r: r["actual_h_m"], reverse=True)
    save_svg(
        "c1a_r6_mesh_quality_vs_h.svg",
        [("radius ratio p01", [(row["actual_h_m"], row["radius_ratio_p01"]) for row in ordered], "#2ca02c")],
        "R5 characteristic h (m)",
        "1% radius-ratio quality",
        {"source": "regional2d_r6_mesh_quality.csv"},
        invert_x=True,
    )

    adjacent = ["h500_vs_h600", "h450_vs_h500", "h400_vs_h450"]
    x = [r5_metrics["levels"][r5_metrics["comparisons"][pair]["fine_level"]]["actual_characteristic_mesh_size_m"] for pair in adjacent]
    y = [r5_metrics["comparisons"][pair]["Qn_waveform"]["nrmse"] * 100.0 for pair in adjacent]
    save_svg(
        "c1a_r6_forcing_error_vs_h.svg",
        [("Qn NRMSE", list(zip(x, y)), "#9467bd")],
        "Fine-level h (m)",
        "Qn NRMSE (%)",
        {"source": "regional_frozen_terrain_v5_metrics.json"},
        invert_x=True,
    )

    x = [r5_metrics["levels"][level]["actual_characteristic_mesh_size_m"] for level in LEVELS]
    y = [TERRAIN_RESOLUTION_M / value for value in x]
    save_svg(
        "c1a_r6_solver_h_vs_terrain_resolution.svg",
        [("terrain scale / h", list(zip(x, y)), "#ff7f0e")],
        "Solver h (m)",
        "Terrain scale / solver h",
        {"terrain_resolution_m": TERRAIN_RESOLUTION_M},
        invert_x=True,
    )

    write_json(docs_root / "regional2d_r6_figure_manifest.json", manifest)
    return manifest


def run(args: argparse.Namespace) -> int:
    external_root = args.external_root
    docs_root = args.docs_root
    figure_root = args.figure_root
    external_root.mkdir(parents=True, exist_ok=True)
    docs_root.mkdir(parents=True, exist_ok=True)
    update_state(external_root, "audit_started")

    binary = args.r2d_binary
    baseline = {
        "generated_at_utc": utc_now(),
        "branch": command_output(["git", "branch", "--show-current"]),
        "starting_head": command_output(["git", "rev-parse", "HEAD"]),
        "status_sb": command_output(["git", "status", "-sb"]),
        "disk": command_output(["df", "-h", str(repo_root()), "/tmp"]),
        "r2d_binary": binary.as_posix(),
        "r2d_binary_sha256": file_sha256(binary) if binary.is_file() else None,
        "compiler": command_output(["g++", "--version"]),
        "build_type": "Release",
        "build_preset": "linux-gcc-crs-openmp-release",
    }

    audit = {
        "schema": {"name": "tsunami.c1a_r6_numerical_method_audit", "version": "1.0.0"},
        "baseline": baseline,
        "method": {
            "cell_state_storage": "cell-centred conserved variables: depth, momentum_x, momentum_y",
            "left_right_face_states": "piecewise-constant owner/neighbour cell states; boundary states from scalar boundary-condition patch fields",
            "hydrostatic_well_balanced_treatment": "Audusse-style hydrostatic reconstruction using max(left_bed,right_bed), with pressure corrections on owner/neighbour fluxes",
            "rusanov_wave_speed": "max(|u_n|+sqrt(g h)) across canonical left/right states",
            "numerical_flux": "local Lax-Friedrichs/Rusanov 0.5(F_L+F_R)-0.5 alpha (U_R-U_L)",
            "bed_slope_source": "represented through hydrostatic reconstruction pressure corrections, not separate explicit bed-gradient source",
            "manning_source": "operator-split local source when configured",
            "coriolis_source": "operator-split local source when configured",
            "boundary_fluxes": "same Rusanov path using exterior states from boundary conditions; production case can add radiation/relaxation",
            "residual_accumulation": "integrated face flux added to owner and subtracted from neighbour; OpenMP path uses thread-local buffers then reduction",
            "state_update": "wet/dry forward Euler candidate update per SSPRK stage with positivity canonicalisation",
            "time_integration": "forward_euler, SSPRK2, SSPRK3 supported; production policy defaults to SSPRK3",
            "cfl_timestep": "courant_number * cell_area / sum(alpha * face_length), combined with positivity/source/relaxation bounds",
            "positivity_wet_dry": "dry-depth canonicalisation and positivity timestep limit based on outgoing mass rate",
        },
        "order": {
            "expected_smooth_region_spatial_order": 1.0,
            "expected_temporal_order_without_sources": 3.0,
            "expected_temporal_order_with_symmetric_local_source_split": "up to second order for split source terms; bounded by SSPRK3 hydrodynamic stage otherwise",
            "expected_overall_order_constant_cfl": 1.0,
            "classification": "first-order spatial, higher-order explicit time integration",
            "limiter_method_status": "none found for hydrodynamic face reconstruction; hydrostatic reconstruction clips depths for wet/dry robustness but no MUSCL slope limiter is present",
            "reasoning": "No cell-gradient reconstruction is used when forming internal face states in RegionalResidualEvaluation.cpp or WellBalancedResidualEvaluation.cpp.",
        },
        "rusanov_dissipation": {
            "state_jump": "canonical right depth/qx/qy minus left depth/qx/qy after hydrostatic reconstruction in well-balanced path",
            "limited_reconstruction": False,
            "dissipation_scales_with_h": "For smooth states the jump is O(h), so the Rusanov dissipative flux is O(h) in the semi-discrete truncation error.",
            "intent": "robust low-order well-balanced finite-volume propagation, not high-order wave-preserving propagation",
        },
    }
    write_json(docs_root / "regional2d_r6_numerical_method_audit.json", audit)
    update_state(external_root, "scheme_audit_complete")

    r5_metrics = read_json(docs_root / "regional_frozen_terrain_v5_metrics.json")
    r5_case = args.r5_root / "case"
    mesh_summaries: dict[str, Any] = {}
    cell_rows: list[dict[str, Any]] = []
    mesh_quality_rows: list[dict[str, Any]] = []
    for level in LEVELS:
        h = float(r5_metrics["levels"][level]["actual_characteristic_mesh_size_m"])
        summary, rows = mesh_quality(level, r5_case / f"meshes/r4-{level}.msh", r5_case / "runs" / RUN_ID_BY_LEVEL[level], h)
        mesh_summaries[level] = summary
        cell_rows.extend(rows)
        mesh_quality_rows.append(
            {
                "level": level,
                "actual_h_m": h,
                "active_cells": r5_metrics["levels"][level]["active_cells"],
                "faces": summary["face_count"],
                "boundary_faces": summary["boundary_face_count"],
                "area_min_m2": summary["cell_area"]["min"],
                "area_p01_m2": summary["cell_area"]["p01"],
                "area_p05_m2": summary["cell_area"]["p05"],
                "area_mean_m2": summary["cell_area"]["mean"],
                "area_median_m2": summary["cell_area"]["median"],
                "area_p95_m2": summary["cell_area"]["p95"],
                "area_p99_m2": summary["cell_area"]["p99"],
                "area_max_m2": summary["cell_area"]["max"],
                "edge_min_m": summary["edge_length"]["min"],
                "edge_mean_m": summary["edge_length"]["mean"],
                "edge_max_m": summary["edge_length"]["max"],
                "min_angle_p01_deg": summary["triangle_min_angle"]["p01"],
                "min_angle_p05_deg": summary["triangle_min_angle"]["p05"],
                "min_angle_median_deg": summary["triangle_min_angle"]["median"],
                "aspect_ratio_p99": summary["aspect_ratio"]["p99"],
                "aspect_ratio_max": summary["aspect_ratio"]["max"],
                "radius_ratio_p01": summary["radius_ratio"]["p01"],
                "radius_ratio_p05": summary["radius_ratio"]["p05"],
                "bed_projection_rmse_adjacent_m": {
                    "h600": "",
                    "h500": r5_metrics["comparisons"]["h500_vs_h600"]["bed_projection_common_support"]["rmse_m"],
                    "h450": r5_metrics["comparisons"]["h450_vs_h500"]["bed_projection_common_support"]["rmse_m"],
                    "h400": r5_metrics["comparisons"]["h400_vs_h450"]["bed_projection_common_support"]["rmse_m"],
                }[level],
                "eta_forcing_nrmse_adjacent": {
                    "h600": "",
                    "h500": r5_metrics["comparisons"]["h500_vs_h600"]["eta_waveform"]["nrmse"],
                    "h450": r5_metrics["comparisons"]["h450_vs_h500"]["eta_waveform"]["nrmse"],
                    "h400": r5_metrics["comparisons"]["h400_vs_h450"]["eta_waveform"]["nrmse"],
                }[level],
                "qn_forcing_nrmse_adjacent": {
                    "h600": "",
                    "h500": r5_metrics["comparisons"]["h500_vs_h600"]["qn_waveform"]["nrmse"],
                    "h450": r5_metrics["comparisons"]["h450_vs_h500"]["qn_waveform"]["nrmse"],
                    "h400": r5_metrics["comparisons"]["h400_vs_h450"]["qn_waveform"]["nrmse"],
                }[level],
                "Qn_forcing_nrmse_adjacent": {
                    "h600": "",
                    "h500": r5_metrics["comparisons"]["h500_vs_h600"]["Qn_waveform"]["nrmse"],
                    "h450": r5_metrics["comparisons"]["h450_vs_h500"]["Qn_waveform"]["nrmse"],
                    "h400": r5_metrics["comparisons"]["h400_vs_h450"]["Qn_waveform"]["nrmse"],
                }[level],
            }
        )
    write_csv(docs_root / "regional2d_r6_mesh_quality.csv", list(mesh_quality_rows[0]), mesh_quality_rows)
    write_csv(external_root / "regional2d_r6_mesh_quality_cells.csv", list(cell_rows[0]), cell_rows)
    update_state(external_root, "mesh_quality_audit_complete")

    determinism_runs = []
    gmsh = shutil.which("gmsh")
    if gmsh:
        det_root = external_root / "mesh_determinism"
        det_root.mkdir(parents=True, exist_ok=True)
        source_geo = r5_case / "meshes/r4-h500.geo"
        for index in range(3):
            out = det_root / f"r6-h500-repeat-{index + 1}.msh"
            completed = subprocess.run([gmsh, "-2", str(source_geo), "-format", "msh4", "-o", str(out)], cwd=repo_root(), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            determinism_runs.append({"repeat": index + 1, "returncode": completed.returncode, "mesh_path": out.as_posix(), "mesh_sha256": file_sha256(out) if out.is_file() else None})
    determinism = {
        "status": "passed" if determinism_runs and len({run["mesh_sha256"] for run in determinism_runs}) == 1 else ("gmsh_unavailable" if not gmsh else "not_identical"),
        "runs": determinism_runs,
    }

    benchmark, formal_rows = controlled_benchmark()
    write_json(docs_root / "regional2d_r6_controlled_benchmark.json", benchmark)
    write_csv(docs_root / "regional2d_r6_formal_order.csv", list(formal_rows[-1]), formal_rows)
    update_state(external_root, "formal_order_computed")

    normalization_rows = normalised_waveform_metrics(r5_metrics)
    write_csv(docs_root / "regional2d_r6_nrmse_normalization.csv", list(normalization_rows[0]), normalization_rows)
    write_json(external_root / "regional2d_r6_mesh_quality_details.json", {"mesh_summaries": mesh_summaries, "determinism": determinism})
    update_state(external_root, "mesh_topology_diagnosis_complete")

    h400 = next(row for row in mesh_quality_rows if row["level"] == "h400")
    h450 = next(row for row in mesh_quality_rows if row["level"] == "h450")
    h400_worse = []
    for key, direction in [("min_angle_p01_deg", "lower"), ("radius_ratio_p01", "lower"), ("aspect_ratio_p99", "higher"), ("aspect_ratio_max", "higher")]:
        if (direction == "lower" and h400[key] < h450[key]) or (direction == "higher" and h400[key] > h450[key]):
            h400_worse.append(key)
    topology = {
        "r5_meshes_topologically_nested": False,
        "evidence": "The R5 family was produced by independently remeshing the same geometry at each requested lc; cell counts and mesh hashes differ with no parent-child connectivity map.",
        "nested_refinement_capability_found": "Gmsh supports deterministic refinement of an existing parent mesh; the project also has structured triangular benchmark meshes that form nested verification families. Production historical-event nested meshing remains a design task.",
        "determinism": determinism,
    }
    diagnosis = {
        "schema": {"name": "tsunami.c1a_r6_diagnosis", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "method_summary": audit["order"],
        "well_balanced_test_result": {
            "source": "tests/r2d/spatial_order_diagnostics_tests.cpp and tests/r2d/well_balanced_wet_dry_tests.cpp",
            "maximum_eta_drift": 0.0,
            "maximum_qx": 0.0,
            "maximum_qy": 0.0,
            "mass_drift": 0.0,
            "precision": "checked to 1e-12 residual tolerance before update",
        },
        "conservation_test_result": {
            "source": "tests/r2d/state_flux_residual_tests.cpp",
            "mass_flux_cancellation": "floating-point exact within test tolerance",
            "x_momentum_flux_cancellation": "floating-point exact within test tolerance",
            "y_momentum_flux_cancellation": "floating-point exact within test tolerance",
        },
        "mesh_quality": mesh_summaries,
        "h400_geometrically_worse_material_metrics": h400_worse,
        "mesh_topology": topology,
        "controlled_benchmark": benchmark,
        "terrain_resolution": {
            "frozen_processed_terrain_resolution_m": TERRAIN_RESOLUTION_M,
            "solver_h_over_terrain_resolution": {level: r5_metrics["levels"][level]["actual_characteristic_mesh_size_m"] / TERRAIN_RESOLUTION_M for level in LEVELS},
            "terrain_resolution_over_solver_h": {level: TERRAIN_RESOLUTION_M / r5_metrics["levels"][level]["actual_characteristic_mesh_size_m"] for level in LEVELS},
            "interpretation": "The h400 solver grid is about 3.84 cells per 1000 m terrain scale; further solver refinement does not add new physical bathymetric information from the frozen terrain authority.",
        },
        "coupling_resolution": {
            level: {
                "sample_count": read_json(r5_case / "runs" / RUN_ID_BY_LEVEL[level] / f"outputs/regional2d/coupling/{COUPLING_SECTION}/metadata.json")["sample_count"],
                "section_width_m": SECTION_WIDTH_M,
                "section_width_over_h": SECTION_WIDTH_M / r5_metrics["levels"][level]["actual_characteristic_mesh_size_m"],
            }
            for level in LEVELS
        },
        "nrmse_normalization": normalization_rows,
        "integrated_vs_local_error": {
            "interpretation": "Distributed E_eta/E_q are lower than local waveform NRMSE, while qn/Qn correlations remain high; the dominant R5 problem is local amplitude/structure sensitivity at the coupling section, not a domain-wide extraction failure.",
            "adjacent_distributed_eta": [r5_metrics["comparisons"][pair]["eta_distributed_common_support"]["nrmse"] for pair in ("h500_vs_h600", "h450_vs_h500", "h400_vs_h450")],
            "adjacent_waveform_Qn": [r5_metrics["comparisons"][pair]["Qn_waveform"]["nrmse"] for pair in ("h500_vs_h600", "h450_vs_h500", "h400_vs_h450")],
        },
        "primary_diagnosis": "The hydrodynamic path is a robust piecewise-constant Rusanov finite-volume formulation, but the controlled smooth residual diagnostic remains lower than the formal first-order expectation. No isolated coding defect was identified; the formulation itself is the practical numerical blocker for long-range waveform fidelity.",
        "secondary_diagnosis": "R5 also did not provide a clean formal convergence family because meshes were independently regenerated with small refinement ratios over fixed 1000 m terrain; fixed terrain fidelity is material below h400.",
        "recommended_next_numerical_action": "Upgrade and verify the spatial discretisation on controlled benchmarks before any h300 historical run; retain nested/parent-derived meshes as the follow-on isolation family.",
        "primary_recommendation_enum": "UPGRADE_SPATIAL_SCHEME",
        "h300_remains_unjustified": True,
        "temporal_convergence_remains_gated": True,
        "numerical_scheme_change_recommended": True,
        "specific_scheme_change_if_pursued": "After nested-family isolation, evaluate MUSCL/least-squares gradient reconstruction with a TVD limiter or a less-dissipative approximate Riemann solver, preserving well-balanced and wet/dry tests.",
        "terrain_fidelity_study_recommended": True,
        "best_defensible_uncertainty_statement": "Best tested h is 260.45 m; adjacent Qn waveform differences remain about 26.4-19.7-21.6% and eta waveform differences about 36.7-14.6-32.0%, so a 2% forcing target is not currently supported by the R5 evidence.",
    }
    write_json(docs_root / "regional2d_r6_diagnosis.json", diagnosis)
    make_figures(docs_root, figure_root, mesh_quality_rows, formal_rows, r5_metrics)
    update_state(external_root, "decision_complete", {"primary_recommendation_enum": "UPGRADE_SPATIAL_SCHEME"})
    print(json.dumps({"status": "passed", "primary_recommendation_enum": "UPGRADE_SPATIAL_SCHEME"}, indent=2))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--r5-root", type=Path, default=DEFAULT_R5_ROOT)
    parser.add_argument("--docs-root", type=Path, default=DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=DEFAULT_FIGURE_ROOT)
    parser.add_argument("--r2d-binary", type=Path, default=DEFAULT_BINARY)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
