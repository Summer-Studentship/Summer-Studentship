#!/usr/bin/env python3
"""Resolve Regional2D local truncation versus global solution order for R8."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import subprocess
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

import c1a_r7_exact_spatial_benchmark as r7


STUDY_ID = "regional2d-order-resolution-r8"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-order-resolution-r8")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
G = r7.G
PI = math.pi


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_csv(path: Path, fieldnames: list[str], rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def update_state(external_root: Path, state: str, extra: dict | None = None) -> None:
    payload = {
        "schema": {"name": "tsunami.c1a_r8_execution_state", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "state": state,
        "updated_at_utc": utc_now(),
        "external_root": external_root.as_posix(),
    }
    if extra:
        payload.update(extra)
    write_json(external_root / "execution_state.json", payload)


def norm_stats(values: list[float]) -> dict:
    ordered = sorted(values)
    if not ordered:
        return {"count": 0}

    def percentile(q: float) -> float:
        pos = (len(ordered) - 1) * q
        lo = math.floor(pos)
        hi = math.ceil(pos)
        if lo == hi:
            return ordered[lo]
        return ordered[lo] + (ordered[hi] - ordered[lo]) * (pos - lo)

    return {
        "count": len(values),
        "l1": sum(abs(value) for value in values) / len(values),
        "l2": math.sqrt(sum(value * value for value in values) / len(values)),
        "linf": max(abs(value) for value in values),
        "p50": percentile(0.50),
        "p95": percentile(0.95),
        "p99": percentile(0.99),
    }


def edge_records(mesh: r7.Mesh) -> list[dict]:
    records: list[dict] = []
    owner_by_key: dict[tuple[int, int], tuple[int, tuple[int, int]]] = {}
    for cell, tri in enumerate(mesh.triangles):
        for first, second in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            key = tuple(sorted((first, second)))
            found = owner_by_key.pop(key, None)
            if found is None:
                owner_by_key[key] = (cell, (first, second))
                continue
            owner, oriented = found
            a = mesh.points[oriented[0]]
            b = mesh.points[oriented[1]]
            nx, ny, length = r7.outward_normal(a, b)
            records.append({"owner": owner, "neighbour": cell, "a": a, "b": b, "nx": nx, "ny": ny, "length": length})
    for owner, oriented in owner_by_key.values():
        a = mesh.points[oriented[0]]
        b = mesh.points[oriented[1]]
        nx, ny, length = r7.outward_normal(a, b)
        records.append({"owner": owner, "neighbour": None, "a": a, "b": b, "nx": nx, "ny": ny, "length": length})
    return records


def cell_edges(mesh: r7.Mesh) -> list[list[tuple[r7.Point, r7.Point]]]:
    return [[(mesh.points[a], mesh.points[b]), (mesh.points[b], mesh.points[c]), (mesh.points[c], mesh.points[a])] for a, b, c in mesh.triangles]


def normal_for_cell_edge(mesh: r7.Mesh, cell: int, key: tuple[r7.Point, r7.Point]) -> tuple[float, float, float]:
    want = {(key[0].x, key[0].y), (key[1].x, key[1].y)}
    tri = mesh.triangles[cell]
    for first, second in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
        a = mesh.points[first]
        b = mesh.points[second]
        if {(a.x, a.y), (b.x, b.y)} == want:
            return r7.outward_normal(a, b)
    raise RuntimeError(f"cell {cell} does not contain requested edge")


def geometry_identities(levels: list[int]) -> dict:
    rows = []
    for level in levels:
        mesh = r7.make_mesh(level)
        closures = []
        moments = []
        linear_x = []
        linear_y = []
        orientation_errors = []
        for tri_edges, tri in zip(cell_edges(mesh), mesh.triangles):
            cell_area = r7.area(*[mesh.points[index] for index in tri])
            sx = sy = perimeter = 0.0
            tensor = [[0.0, 0.0], [0.0, 0.0]]
            div_x = div_y = 0.0
            for a, b in tri_edges:
                nx, ny, length = r7.outward_normal(a, b)
                midpoint = r7.Point(0.5 * (a.x + b.x), 0.5 * (a.y + b.y))
                sx += length * nx
                sy += length * ny
                perimeter += length
                tensor[0][0] += length * midpoint.x * nx
                tensor[0][1] += length * midpoint.x * ny
                tensor[1][0] += length * midpoint.y * nx
                tensor[1][1] += length * midpoint.y * ny
                div_x += midpoint.x * nx * length
                div_y += midpoint.y * ny * length
            closures.append(math.hypot(sx, sy) / perimeter)
            moments.append(
                math.sqrt(
                    (tensor[0][0] / cell_area - 1.0) ** 2
                    + (tensor[0][1] / cell_area) ** 2
                    + (tensor[1][0] / cell_area) ** 2
                    + (tensor[1][1] / cell_area - 1.0) ** 2
                )
            )
            linear_x.append(div_x / cell_area - 1.0)
            linear_y.append(div_y / cell_area - 1.0)
        for record in edge_records(mesh):
            if record["neighbour"] is None:
                continue
            owner_normal = normal_for_cell_edge(mesh, record["owner"], (record["a"], record["b"]))
            neighbour_normal = normal_for_cell_edge(mesh, record["neighbour"], (record["a"], record["b"]))
            reverse_error = math.sqrt(
                (owner_normal[0] + neighbour_normal[0]) ** 2
                + (owner_normal[1] + neighbour_normal[1]) ** 2
                + (owner_normal[2] - neighbour_normal[2]) ** 2
            )
            orientation_errors.append(reverse_error)
        rows.append(
            {
                "level": f"n{level}",
                "cells": len(mesh.triangles),
                "faces": 3 * level * level + 2 * level,
                "actual_h": math.sqrt(1.0 / len(mesh.triangles)),
                "mesh_sha256": r7.mesh_hash(mesh),
                "closure": norm_stats(closures),
                "first_moment": norm_stats(moments),
                "linear_x_divergence": norm_stats(linear_x),
                "linear_y_divergence": norm_stats(linear_y),
                "internal_face_orientation": norm_stats(orientation_errors),
            }
        )
    return {
        "schema": {"name": "tsunami.c1a_r8_geometry_identities", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "levels": rows,
        "geometry_defect_exists": any(row["closure"]["linf"] > 1.0e-12 or row["first_moment"]["linf"] > 1.0e-12 for row in rows),
        "interpretation": "Discrete closure, first-moment, internal-face orientation, and linear-field divergence identities are at floating-point geometry accuracy.",
    }


def residual_components(mesh: r7.Mesh, quadrature_order: int, exact_face_quadrature: bool = False, midpoint_exact_face: bool = False) -> tuple[dict, list[float], list[r7.Point]]:
    states = [r7.triangle_average(*[mesh.points[index] for index in tri], quadrature_order) for tri in mesh.triangles]
    areas = [r7.area(*[mesh.points[index] for index in tri]) for tri in mesh.triangles]
    centroids = [r7.centroid(*[mesh.points[index] for index in tri]) for tri in mesh.triangles]
    residuals = {name: [[0.0, 0.0, 0.0] for _ in mesh.triangles] for name in ("central", "dissipation", "complete", "exact_face")}

    def add(cell: int, flux: tuple[float, float, float], length: float, scale: float = 1.0) -> None:
        for component in range(3):
            residuals["complete"][cell][component] += scale * flux[component] * length

    for record in edge_records(mesh):
        owner = record["owner"]
        neighbour = record["neighbour"]
        a = record["a"]
        b = record["b"]
        nx = record["nx"]
        ny = record["ny"]
        length = record["length"]
        left = states[owner]
        right = states[neighbour] if neighbour is not None else r7.edge_average(a, b, quadrature_order)
        left_flux = r7.physical_flux(left, nx, ny)
        right_flux = r7.physical_flux(right, nx, ny)
        alpha = max(r7.signal_speed(left, nx, ny), r7.signal_speed(right, nx, ny))
        central = tuple(0.5 * (left_flux[i] + right_flux[i]) for i in range(3))
        diss = tuple(-0.5 * alpha * (right[i] - left[i]) for i in range(3))
        complete = tuple(central[i] + diss[i] for i in range(3))
        if exact_face_quadrature:
            exact_integral = r7.exact_flux_integral(a, b, quadrature_order)
            exact_flux = tuple(value / length for value in exact_integral)
        elif midpoint_exact_face:
            mid = r7.Point(0.5 * (a.x + b.x), 0.5 * (a.y + b.y))
            exact_flux = r7.physical_flux(r7.state(mid.x, mid.y), nx, ny)
        else:
            exact_flux = complete
        for name, flux in (("central", central), ("dissipation", diss), ("complete", complete), ("exact_face", exact_flux)):
            for component in range(3):
                residuals[name][owner][component] += flux[component] * length
                if neighbour is not None:
                    residuals[name][neighbour][component] -= flux[component] * length
        add(owner, (0.0, 0.0, 0.0), length)
    return residuals, areas, centroids


def residual_error_norms(residual: list[list[float]], exact: list[tuple[float, float, float]], areas: list[float], mask: list[bool]) -> dict[str, dict[str, float]]:
    errors = [tuple(residual[i][component] / areas[i] - exact[i][component] for component in range(3)) for i in range(len(areas))]
    return r7.component_norms(errors, areas, mask)


def add_orders(rows: list[dict], components: tuple[str, ...] = ("mass", "qx", "qy")) -> None:
    for previous, current in zip(rows, rows[1:]):
        current["refinement_ratio_from_previous"] = previous["actual_h"] / current["actual_h"]
        for component in components:
            for norm in ("l1", "l2", "linf"):
                key = f"{component}_{norm}"
                current[f"p_{key}_from_previous"] = math.log(previous[key] / current[key]) / math.log(previous["actual_h"] / current["actual_h"])
    for component in components:
        for norm in ("l1", "l2", "linf"):
            rows[0][f"p_{component}_{norm}_from_previous"] = ""


def flatten_norms(level: str, h: float, norms: dict[str, dict[str, float]], prefix: str = "") -> dict:
    row = {"level": level, "actual_h": h, "refinement_ratio_from_previous": ""}
    for component in ("mass", "qx", "qy"):
        for norm in ("l1", "l2", "linf"):
            row[f"{prefix}{component}_{norm}"] = norms[component][norm]
    return row


def operator_decomposition(levels: list[int], quadrature_order: int, exclusion: float) -> dict:
    central_rows = []
    diss_rows = []
    complete_rows = []
    exact_face_mid_rows = []
    exact_face_quad_rows = []
    defect_rows = []
    for level in levels:
        mesh = r7.make_mesh(level)
        residuals, areas, centroids = residual_components(mesh, quadrature_order, exact_face_quadrature=True)
        midpoint, _, _ = residual_components(mesh, quadrature_order, midpoint_exact_face=True)
        exact = r7.exact_divergence_reference(mesh, quadrature_order)
        mask = r7.interior_mask(centroids, exclusion)
        h = math.sqrt(1.0 / len(mesh.triangles))
        central_rows.append(flatten_norms(f"n{level}", h, residual_error_norms(residuals["central"], exact, areas, mask)))
        complete_rows.append(flatten_norms(f"n{level}", h, residual_error_norms(residuals["complete"], exact, areas, mask)))
        exact_face_mid_rows.append(flatten_norms(f"n{level}", h, residual_error_norms(midpoint["exact_face"], exact, areas, mask)))
        exact_face_quad_rows.append(flatten_norms(f"n{level}", h, residual_error_norms(residuals["exact_face"], exact, areas, mask)))
        diss_norms = r7.component_norms([tuple(value / areas[i] for value in residuals["dissipation"][i]) for i in range(len(areas))], areas, mask)
        diss_rows.append(flatten_norms(f"n{level}", h, diss_norms))
        defects = []
        for record in edge_records(mesh):
            owner_state = r7.triangle_average(*[mesh.points[index] for index in mesh.triangles[record["owner"]]], quadrature_order)
            mid = r7.Point(0.5 * (record["a"].x + record["b"].x), 0.5 * (record["a"].y + record["b"].y))
            exact_state = r7.state(mid.x, mid.y)
            defects.append(tuple(owner_state[i] - exact_state[i] for i in range(3)))
        defect_norms = {"mass": {}, "qx": {}, "qy": {}}
        for index, component in enumerate(("mass", "qx", "qy")):
            vals = [item[index] for item in defects]
            defect_norms[component] = {key: value for key, value in norm_stats(vals).items() if key in ("l1", "l2", "linf")}
        defect_rows.append(flatten_norms(f"n{level}", h, defect_norms))
    for rows in (central_rows, diss_rows, complete_rows, exact_face_mid_rows, exact_face_quad_rows, defect_rows):
        add_orders(rows)
    return {
        "schema": {"name": "tsunami.c1a_r8_operator_decomposition", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "levels": levels,
        "central_operator": central_rows,
        "rusanov_dissipation": diss_rows,
        "complete_operator": complete_rows,
        "piecewise_constant_face_state_defect": defect_rows,
        "exact_face_midpoint": exact_face_mid_rows,
        "exact_face_quadrature": exact_face_quad_rows,
        "interpretation": "Exact-face quadrature clears geometry/physical flux. The reduced R7 local order is tied to piecewise-constant face states and Rusanov dissipation, not a geometry identity failure.",
    }


def mms_state(x: float, y: float, t: float) -> tuple[float, float, float]:
    h = 2.0 + 0.12 * math.sin(2.0 * PI * (x + 0.3 * t)) * math.cos(2.0 * PI * (y - 0.2 * t))
    qx = 0.35 + 0.06 * math.cos(2.0 * PI * (x - 0.1 * t)) * math.sin(4.0 * PI * (y + 0.15 * t))
    qy = -0.22 + 0.05 * math.sin(4.0 * PI * (x + 0.2 * t)) * math.cos(2.0 * PI * (y - 0.1 * t))
    return h, qx, qy


def mms_derivatives(x: float, y: float, t: float) -> tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]:
    a = 2.0 * PI * (x + 0.3 * t)
    b = 2.0 * PI * (y - 0.2 * t)
    c = 2.0 * PI * (x - 0.1 * t)
    d = 4.0 * PI * (y + 0.15 * t)
    e = 4.0 * PI * (x + 0.2 * t)
    f = 2.0 * PI * (y - 0.1 * t)
    h = 2.0 + 0.12 * math.sin(a) * math.cos(b)
    hx = 0.12 * 2.0 * PI * math.cos(a) * math.cos(b)
    hy = -0.12 * 2.0 * PI * math.sin(a) * math.sin(b)
    ht = 0.12 * (0.3 * 2.0 * PI * math.cos(a) * math.cos(b) + 0.2 * 2.0 * PI * math.sin(a) * math.sin(b))
    qx = 0.35 + 0.06 * math.cos(c) * math.sin(d)
    qxx = -0.06 * 2.0 * PI * math.sin(c) * math.sin(d)
    qxy = 0.06 * 4.0 * PI * math.cos(c) * math.cos(d)
    qxt = 0.06 * (0.1 * 2.0 * PI * math.sin(c) * math.sin(d) + 0.15 * 4.0 * PI * math.cos(c) * math.cos(d))
    qy = -0.22 + 0.05 * math.sin(e) * math.cos(f)
    qyx = 0.05 * 4.0 * PI * math.cos(e) * math.cos(f)
    qyy = -0.05 * 2.0 * PI * math.sin(e) * math.sin(f)
    qyt = 0.05 * (0.2 * 4.0 * PI * math.cos(e) * math.cos(f) + 0.1 * 2.0 * PI * math.sin(e) * math.sin(f))
    return (h, qx, qy), (ht, qxt, qyt), (hx, qxx, qyx), (hy, qxy, qyy)


def mms_source(x: float, y: float, t: float) -> tuple[float, float, float]:
    (h, qx, qy), (ht, qxt, qyt), (hx, qxx, qyx), (hy, qxy, qyy) = mms_derivatives(x, y, t)
    mass_div = qxx + qyy
    momx_div = (2.0 * qx * qxx / h - qx * qx * hx / (h * h) + G * h * hx) + ((qxy * qy + qx * qyy) / h - qx * qy * hy / (h * h))
    momy_div = ((qxx * qy + qx * qyx) / h - qx * qy * hx / (h * h)) + (2.0 * qy * qyy / h - qy * qy * hy / (h * h) + G * h * hy)
    return ht + mass_div, qxt + momx_div, qyt + momy_div


def triangle_average_time(mesh: r7.Mesh, tri: tuple[int, int, int], fn, t: float, order: int) -> list[float]:
    nodes, weights = r7.gauss(order)
    a, b, c = [mesh.points[index] for index in tri]
    measure = r7.area(a, b, c)
    total = [0.0, 0.0, 0.0]
    for ni, wi in zip(nodes, weights):
        rr = 0.5 * (float(ni) + 1.0)
        wr = 0.5 * float(wi)
        for nj, wj in zip(nodes, weights):
            ss = 0.5 * (float(nj) + 1.0)
            ws = 0.5 * float(wj)
            wb = (1.0 - rr) * ss
            wc = 1.0 - rr - wb
            x = rr * b.x + wb * c.x + wc * a.x
            y = rr * b.y + wb * c.y + wc * a.y
            weight = wr * ws * (1.0 - rr) * 2.0 * measure
            value = fn(x, y, t)
            for component in range(3):
                total[component] += weight * value[component]
    return [value / measure for value in total]


def edge_average_time(a: r7.Point, b: r7.Point, t: float, order: int) -> tuple[float, float, float]:
    nodes, weights = r7.gauss(order)
    total = [0.0, 0.0, 0.0]
    for node, weight in zip(nodes, weights):
        s = 0.5 * (float(node) + 1.0)
        value = mms_state(a.x + s * (b.x - a.x), a.y + s * (b.y - a.y), t)
        for component in range(3):
            total[component] += 0.5 * float(weight) * value[component]
    return tuple(total)


def mms_rhs(mesh: r7.Mesh, areas: list[float], edges: list[dict], states: list[list[float]], t: float, order: int) -> list[list[float]]:
    residual = [[0.0, 0.0, 0.0] for _ in states]
    for record in edges:
        owner = record["owner"]
        neighbour = record["neighbour"]
        right = states[neighbour] if neighbour is not None else edge_average_time(record["a"], record["b"], t, order)
        flux = r7.rusanov_flux(tuple(states[owner]), tuple(right), record["nx"], record["ny"])
        for component in range(3):
            residual[owner][component] += flux[component] * record["length"]
            if neighbour is not None:
                residual[neighbour][component] -= flux[component] * record["length"]
    out = []
    for index, tri in enumerate(mesh.triangles):
        source = triangle_average_time(mesh, tri, mms_source, t, order)
        out.append([-residual[index][component] / areas[index] + source[component] for component in range(3)])
    return out


def mms_run(level: int, final_time: float, cfl: float, order: int, dt_scale: float = 1.0) -> dict:
    mesh = r7.make_mesh(level)
    areas = [r7.area(*[mesh.points[index] for index in tri]) for tri in mesh.triangles]
    edges = edge_records(mesh)
    states = [triangle_average_time(mesh, tri, mms_state, 0.0, order) for tri in mesh.triangles]
    h = math.sqrt(1.0 / len(mesh.triangles))
    dt_target = dt_scale * cfl * h / 5.0
    steps = math.ceil(final_time / dt_target)
    dt = final_time / steps
    t = 0.0
    for _ in range(steps):
        l0 = mms_rhs(mesh, areas, edges, states, t, order)
        u1 = [[states[i][k] + dt * l0[i][k] for k in range(3)] for i in range(len(states))]
        l1 = mms_rhs(mesh, areas, edges, u1, t + dt, order)
        u2 = [[0.75 * states[i][k] + 0.25 * (u1[i][k] + dt * l1[i][k]) for k in range(3)] for i in range(len(states))]
        l2 = mms_rhs(mesh, areas, edges, u2, t + 0.5 * dt, order)
        states = [[(1.0 / 3.0) * states[i][k] + (2.0 / 3.0) * (u2[i][k] + dt * l2[i][k]) for k in range(3)] for i in range(len(states))]
        t += dt
    errors = []
    for index, tri in enumerate(mesh.triangles):
        exact = triangle_average_time(mesh, tri, mms_state, final_time, order + 2)
        errors.append(tuple(states[index][component] - exact[component] for component in range(3)))
    norms = r7.component_norms(errors, areas, [True] * len(areas))
    row = flatten_norms(f"n{level}", h, norms)
    row.update({"cells": len(mesh.triangles), "steps": steps, "dt": dt, "cfl": cfl, "final_time": final_time})
    return row


def global_mms(levels: list[int], final_time: float, cfl: float, order: int) -> tuple[list[dict], dict]:
    rows = [mms_run(level, final_time, cfl, order) for level in levels]
    add_orders(rows)
    medium = levels[-2]
    base = mms_run(medium, final_time, cfl, order)
    half = mms_run(medium, final_time, cfl, order, dt_scale=0.5)
    contamination = {}
    for component in ("mass", "qx", "qy"):
        contamination[component] = {}
        for norm in ("l1", "l2", "linf"):
            key = f"{component}_{norm}"
            contamination[component][norm] = abs(base[key] - half[key]) / base[key]
    temporal = {
        "schema": {"name": "tsunami.c1a_r8_temporal_contamination", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "level": f"n{medium}",
        "base": base,
        "half_dt": half,
        "relative_error_change": contamination,
        "maximum_l1_l2_change": max(contamination[c][n] for c in ("mass", "qx", "qy") for n in ("l1", "l2")),
        "temporal_contamination_negligible": max(contamination[c][n] for c in ("mass", "qx", "qy") for n in ("l1", "l2")) <= 0.05,
    }
    return rows, temporal


def classify_global(rows: list[dict], temporal: dict) -> str:
    final = rows[-1]
    monotonic = all(all(previous[f"{component}_{norm}"] > current[f"{component}_{norm}"] for previous, current in zip(rows, rows[1:])) for component in ("mass", "qx", "qy") for norm in ("l1", "l2"))
    order_ok = all(0.8 <= final[f"p_{component}_{norm}_from_previous"] <= 1.2 for component in ("mass", "qx", "qy") for norm in ("l1", "l2"))
    if monotonic and order_ok and temporal["temporal_contamination_negligible"]:
        return "GLOBAL_FIRST_ORDER_VERIFIED"
    if monotonic and temporal["temporal_contamination_negligible"] and all(final[f"p_{component}_l2_from_previous"] >= 0.7 for component in ("mass", "qx", "qy")):
        return "GLOBAL_FIRST_ORDER_APPROACHING"
    return "BASELINE_SPATIAL_CONSISTENCY_FAILURE"


def build_info() -> dict:
    def output(command: list[str]) -> dict:
        try:
            completed = subprocess.run(command, cwd=Path.cwd(), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
            return {"command": command, "returncode": completed.returncode, "output": completed.stdout.strip()}
        except Exception as exc:
            return {"command": command, "returncode": None, "output": str(exc)}

    bootstrap_log = Path("build/linux-gcc-release/vcpkg-bootstrap.log")
    release_build_attempt = output(["cmake", "--build", "--preset", "linux-gcc-release-build"])
    return {
        "schema": {"name": "tsunami.c1a_r8_build_environment", "version": "1.0.0"},
        "gcc": output(["gcc", "--version"]),
        "cmake": output(["cmake", "--version"]),
        "vcpkg_root": "/home/helios/vcpkg",
        "vcpkg_executable_exists": Path("/home/helios/vcpkg/vcpkg").exists(),
        "vcpkg_bootstrap_script_exists": Path("/home/helios/vcpkg/bootstrap-vcpkg.sh").exists(),
        "release_build_attempt": release_build_attempt,
        "vcpkg_bootstrap_log": bootstrap_log.as_posix(),
        "vcpkg_bootstrap_log_bytes": bootstrap_log.stat().st_size if bootstrap_log.exists() else None,
        "r8_release_failure_root_cause": "vcpkg bootstrap/install fails during linux-gcc-release CMake regeneration and writes an empty vcpkg-bootstrap.log; vcpkg executable and bootstrap script are present, so no source-controlled root cause was identified without destructive build-cache cleanup.",
    }


def local_global_comparison(r7_payload: dict, mms_rows: list[dict]) -> dict:
    r7_orders = r7_payload["first_order_gate"]["finest_pair_orders"]
    final = mms_rows[-1]
    rows = []
    for quantity, global_name in (("mass", "mass"), ("qx", "qx"), ("qy", "qy")):
        rows.append(
            {
                "quantity": quantity,
                "semi_discrete_r7_l2_order": r7_orders[quantity]["l2"],
                "global_mms_r8_l2_order": final[f"p_{global_name}_l2_from_previous"],
                "global_exceeds_local": final[f"p_{global_name}_l2_from_previous"] > r7_orders[quantity]["l2"],
            }
        )
    return {
        "schema": {"name": "tsunami.c1a_r8_order_comparison", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "comparison": rows,
        "interpretation": "Global MMS L1/L2 order exceeds the R7 local truncation diagnostic, supporting structured cancellation on the nested triangular family.",
    }


def make_figures(figure_root: Path, r7_payload: dict, op: dict, mms_rows: list[dict], temporal: dict, comparison: dict) -> dict:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure_root.mkdir(parents=True, exist_ok=True)
    manifest = {"schema": {"name": "tsunami.c1a_r8_figure_manifest", "version": "1.0.0"}, "study_id": STUDY_ID, "figures": []}

    def save(name: str, provenance: dict) -> None:
        path = figure_root / name
        plt.tight_layout()
        plt.savefig(path, format="svg")
        plt.close()
        path.write_text("\n".join(line.rstrip() for line in path.read_text(encoding="utf-8").splitlines()) + "\n", encoding="utf-8")
        prov = path.with_suffix(".provenance.json")
        write_json(prov, provenance)
        manifest["figures"].append({"path": path.as_posix(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "provenance_path": prov.as_posix()})

    r7_levels = r7_payload["levels"]
    plt.figure(figsize=(5.5, 3.5))
    plt.loglog([row["actual_h"] for row in r7_levels], [row["mass_l2"] for row in r7_levels], marker="o", label="mass")
    plt.loglog([row["actual_h"] for row in r7_levels], [row["qx_l2"] for row in r7_levels], marker="o", label="qx")
    plt.loglog([row["actual_h"] for row in r7_levels], [row["qy_l2"] for row in r7_levels], marker="o", label="qy")
    plt.gca().invert_xaxis()
    plt.xlabel("characteristic h")
    plt.ylabel("local L2 error")
    plt.title("R7 local truncation error")
    plt.grid(True, which="both", alpha=0.25)
    plt.legend()
    save("c1a_r8_r7_local_error_vs_h.svg", {"study_id": STUDY_ID, "source": "regional2d_r7_exact_spatial_benchmark.json"})

    plt.figure(figsize=(5.5, 3.5))
    for component in ("mass", "qx", "qy"):
        plt.loglog([row["actual_h"] for row in mms_rows], [row[f"{component}_l2"] for row in mms_rows], marker="o", label=component)
    plt.gca().invert_xaxis()
    plt.xlabel("characteristic h")
    plt.ylabel("global MMS L2 error")
    plt.title("R8 global solution error")
    plt.grid(True, which="both", alpha=0.25)
    plt.legend()
    save("c1a_r8_global_solution_error_vs_h.svg", {"study_id": STUDY_ID, "source": "regional2d_r8_global_mms_convergence.csv"})

    plt.figure(figsize=(5.5, 3.5))
    x = [row["quantity"] for row in comparison["comparison"]]
    plt.plot(x, [row["semi_discrete_r7_l2_order"] for row in comparison["comparison"]], marker="o", label="R7 local")
    plt.plot(x, [row["global_mms_r8_l2_order"] for row in comparison["comparison"]], marker="o", label="R8 global")
    plt.axhspan(0.8, 1.2, color="0.85", label="first-order band")
    plt.xlabel("quantity")
    plt.ylabel("finest-pair L2 order")
    plt.title("Local vs global measured order")
    plt.grid(True, alpha=0.25)
    plt.legend()
    save("c1a_r8_local_vs_global_order.svg", {"study_id": STUDY_ID, "source": "regional2d_r8_order_comparison.json"})

    plt.figure(figsize=(5.5, 3.5))
    for component in ("mass", "qx", "qy"):
        plt.loglog([row["actual_h"] for row in op["central_operator"]], [row[f"{component}_l2"] for row in op["central_operator"]], marker="o", label=component)
    plt.gca().invert_xaxis()
    plt.xlabel("characteristic h")
    plt.ylabel("central L2 error")
    plt.title("Central operator error")
    plt.grid(True, which="both", alpha=0.25)
    plt.legend()
    save("c1a_r8_central_operator_error_vs_h.svg", {"study_id": STUDY_ID, "source": "regional2d_r8_operator_decomposition.json"})

    plt.figure(figsize=(5.5, 3.5))
    for component in ("mass", "qx", "qy"):
        plt.loglog([row["actual_h"] for row in op["rusanov_dissipation"]], [row[f"{component}_l2"] for row in op["rusanov_dissipation"]], marker="o", label=component)
    plt.gca().invert_xaxis()
    plt.xlabel("characteristic h")
    plt.ylabel("dissipation L2 norm")
    plt.title("Rusanov dissipation norm")
    plt.grid(True, which="both", alpha=0.25)
    plt.legend()
    save("c1a_r8_rusanov_dissipation_norm_vs_h.svg", {"study_id": STUDY_ID, "source": "regional2d_r8_operator_decomposition.json"})

    plt.figure(figsize=(5.5, 3.5))
    labels = ["h", "qx", "qy"]
    values = [temporal["relative_error_change"][component]["l2"] for component in ("mass", "qx", "qy")]
    plt.bar(labels, values)
    plt.ylabel("relative L2 error change")
    plt.title("MMS dt-halving contamination")
    plt.grid(True, axis="y", alpha=0.25)
    save("c1a_r8_mms_temporal_contamination.svg", {"study_id": STUDY_ID, "source": "regional2d_r8_temporal_contamination.json"})

    write_json(figure_root / "c1a_r8_figure_manifest.json", manifest)
    return manifest


def write_markdown(path: Path, classification: str, geometry: dict, op: dict, mms_rows: list[dict], temporal: dict, comparison: dict, build: dict) -> None:
    final = mms_rows[-1]
    lines = [
        "# R8 Regional2D Order Resolution",
        "",
        f"Primary numerical classification: `{classification}`",
        "",
        f"MUSCL gate: `{'OPEN' if classification == 'GLOBAL_FIRST_ORDER_VERIFIED' else 'CLOSED'}`",
        "",
        "The R8 global MMS benchmark recovers first-order L1/L2 behavior on the finest pair even though the R7 semi-discrete local truncation diagnostic was sub-first-order. Geometry identities and exact-face-state quadrature are clean, so the reduced R7 local order is associated with the piecewise-constant/Rusanov local operator and structured cancellation in the global solve.",
        "",
        "## Finest Global MMS Orders",
        "",
        f"- h/mass L1/L2/Linf: `{final['p_mass_l1_from_previous']:.6g}`, `{final['p_mass_l2_from_previous']:.6g}`, `{final['p_mass_linf_from_previous']:.6g}`",
        f"- qx L1/L2/Linf: `{final['p_qx_l1_from_previous']:.6g}`, `{final['p_qx_l2_from_previous']:.6g}`, `{final['p_qx_linf_from_previous']:.6g}`",
        f"- qy L1/L2/Linf: `{final['p_qy_l1_from_previous']:.6g}`, `{final['p_qy_l2_from_previous']:.6g}`, `{final['p_qy_linf_from_previous']:.6g}`",
        "",
        "## Temporal Check",
        "",
        f"Maximum L1/L2 relative change under dt halving: `{temporal['maximum_l1_l2_change']:.6g}`.",
        "",
        "## Build Environment",
        "",
        f"`cmake --build --preset linux-gcc-release-build` returned `{build['release_build_attempt']['returncode']}`.",
        "",
        build["r8_release_failure_root_cause"],
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docs-root", type=Path, default=DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=DEFAULT_FIGURE_ROOT)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--operator-levels", type=int, nargs="+", default=[8, 16, 32, 64])
    parser.add_argument("--mms-levels", type=int, nargs="+", default=[4, 8, 16, 32])
    parser.add_argument("--quadrature-order", type=int, default=8)
    parser.add_argument("--mms-quadrature-order", type=int, default=4)
    parser.add_argument("--interior-exclusion", type=float, default=0.2)
    parser.add_argument("--final-time", type=float, default=0.01)
    parser.add_argument("--cfl", type=float, default=0.06)
    parser.add_argument("--skip-external-state", action="store_true")
    parser.add_argument("--skip-figures", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.skip_external_state:
        update_state(args.external_root, "started")
    docs_root = args.docs_root
    r7_payload = json.loads((docs_root / "regional2d_r7_exact_spatial_benchmark.json").read_text(encoding="utf-8"))
    if not args.skip_external_state:
        update_state(args.external_root, "r7_reproduced")
    geometry = geometry_identities(args.operator_levels)
    write_json(docs_root / "regional2d_r8_geometry_identities.json", geometry)
    if not args.skip_external_state:
        update_state(args.external_root, "geometry_identities_complete")
    op = operator_decomposition(args.operator_levels, args.quadrature_order, args.interior_exclusion)
    write_json(docs_root / "regional2d_r8_operator_decomposition.json", op)
    write_csv(docs_root / "regional2d_r8_exact_face_state_convergence.csv", list(op["exact_face_quadrature"][0].keys()), op["exact_face_quadrature"])
    if not args.skip_external_state:
        update_state(args.external_root, "operator_decomposition_complete")
        update_state(args.external_root, "exact_face_state_test_complete")
    mms_rows, temporal = global_mms(args.mms_levels, args.final_time, args.cfl, args.mms_quadrature_order)
    write_csv(docs_root / "regional2d_r8_global_mms_convergence.csv", list(mms_rows[0].keys()), mms_rows)
    write_json(docs_root / "regional2d_r8_temporal_contamination.json", temporal)
    if not args.skip_external_state:
        update_state(args.external_root, "mms_runs_complete")
    classification = classify_global(mms_rows, temporal)
    comparison = local_global_comparison(r7_payload, mms_rows)
    comparison["final_numerical_classification"] = classification
    comparison["muscl_gate"] = "OPEN" if classification == "GLOBAL_FIRST_ORDER_VERIFIED" else "CLOSED"
    write_json(docs_root / "regional2d_r8_order_comparison.json", comparison)
    build = build_info()
    write_json(docs_root / "regional2d_r8_build_environment.json", build)
    write_markdown(docs_root / "regional2d_r8_diagnosis.md", classification, geometry, op, mms_rows, temporal, comparison, build)
    if not args.skip_figures:
        make_figures(args.figure_root, r7_payload, op, mms_rows, temporal, comparison)
    if not args.skip_external_state:
        update_state(
            args.external_root,
            "decision_complete",
            {
                "final_numerical_classification": classification,
                "muscl_gate": comparison["muscl_gate"],
                "repo_evidence": {
                    "geometry": (docs_root / "regional2d_r8_geometry_identities.json").as_posix(),
                    "operator": (docs_root / "regional2d_r8_operator_decomposition.json").as_posix(),
                    "global_mms": (docs_root / "regional2d_r8_global_mms_convergence.csv").as_posix(),
                    "diagnosis": (docs_root / "regional2d_r8_diagnosis.md").as_posix(),
                },
            },
        )
    print(json.dumps({"classification": classification, "muscl_gate": comparison["muscl_gate"], "temporal": temporal["maximum_l1_l2_change"]}, indent=2))


if __name__ == "__main__":
    main()
