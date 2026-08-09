#!/usr/bin/env python3
"""Verify the R9 Regional2D limited-linear reconstruction candidate."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import time
import xml.etree.ElementTree as ET
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

import c1a_r8_order_resolution as r8


STUDY_ID = "regional2d-reconstruction-r9"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-reconstruction-r9")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")
PI = math.pi
G = r8.G


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
        "schema": {"name": "tsunami.c1a_r9_execution_state", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "state": state,
        "updated_at_utc": utc_now(),
        "external_root": external_root.as_posix(),
    }
    if extra:
        payload.update(extra)
    write_json(external_root / "execution_state.json", payload)


def add_orders(rows: list[dict]) -> None:
    rows_by_scheme: dict[str, list[dict]] = defaultdict(list)
    for row in rows:
        rows_by_scheme[row["scheme"]].append(row)
    for scheme_rows in rows_by_scheme.values():
        scheme_rows.sort(key=lambda item: item["actual_h"], reverse=True)
        for previous, current in zip(scheme_rows, scheme_rows[1:]):
            current["refinement_ratio_from_previous"] = previous["actual_h"] / current["actual_h"]
            for component in ("mass", "qx", "qy"):
                for norm in ("l1", "l2", "linf"):
                    key = f"{component}_{norm}"
                    current[f"p_{key}_from_previous"] = math.log(previous[key] / current[key]) / math.log(previous["actual_h"] / current["actual_h"])
        for component in ("mass", "qx", "qy"):
            for norm in ("l1", "l2", "linf"):
                scheme_rows[0][f"p_{component}_{norm}_from_previous"] = ""
        scheme_rows[0]["refinement_ratio_from_previous"] = ""


def face_midpoint(record: dict) -> r8.r7.Point:
    return r8.r7.Point(0.5 * (record["a"].x + record["b"].x), 0.5 * (record["a"].y + record["b"].y))


def cell_face_records(mesh: r8.r7.Mesh) -> list[list[tuple[int, dict]]]:
    records = r8.edge_records(mesh)
    per_cell: list[list[tuple[int, dict]]] = [[] for _ in mesh.triangles]
    for face_index, record in enumerate(records):
        per_cell[record["owner"]].append((face_index, record))
        if record["neighbour"] is not None:
            per_cell[record["neighbour"]].append((face_index, record))
    return per_cell


def primitive_from_state(state: tuple[float, float, float]) -> tuple[float, float, float]:
    h, qx, qy = state
    if h <= 1.0e-12:
        return h, 0.0, 0.0
    return h, qx / h, qy / h


def conserved_from_primitive(h: float, u: float, v: float) -> tuple[float, float, float]:
    h = max(0.0, h)
    return h, h * u if h > 1.0e-12 else 0.0, h * v if h > 1.0e-12 else 0.0


def gradients(mesh: r8.r7.Mesh, per_cell: list[list[tuple[int, dict]]], values: list[float], boundary_values: list[float]) -> list[tuple[float, float, bool]]:
    centroids = [r8.r7.centroid(*[mesh.points[index] for index in tri]) for tri in mesh.triangles]
    out = []
    for cell, faces in enumerate(per_cell):
        c = centroids[cell]
        center = values[cell]
        a00 = a01 = a11 = b0 = b1 = 0.0
        samples = 0
        for face_index, record in faces:
            if record["owner"] == cell and record["neighbour"] is not None:
                sample_cell = record["neighbour"]
                point = centroids[sample_cell]
                sample = values[sample_cell]
            elif record["neighbour"] == cell:
                sample_cell = record["owner"]
                point = centroids[sample_cell]
                sample = values[sample_cell]
            else:
                point = face_midpoint(record)
                sample = boundary_values[face_index]
            dx = point.x - c.x
            dy = point.y - c.y
            distance2 = dx * dx + dy * dy
            if distance2 <= 0.0:
                continue
            weight = 1.0 / max(distance2, 1.0e-24)
            delta = sample - center
            a00 += weight * dx * dx
            a01 += weight * dx * dy
            a11 += weight * dy * dy
            b0 += weight * dx * delta
            b1 += weight * dy * delta
            samples += 1
        det = a00 * a11 - a01 * a01
        scale = max(abs(a00), abs(a01), abs(a11), 1.0)
        if samples < 2 or abs(det) <= 1.0e-12 * scale * scale:
            out.append((0.0, 0.0, False))
        else:
            out.append(((a11 * b0 - a01 * b1) / det, (a00 * b1 - a01 * b0) / det, True))
    return out


def limiters(mesh: r8.r7.Mesh, per_cell: list[list[tuple[int, dict]]], values: list[float], boundary_values: list[float], grads: list[tuple[float, float, bool]]) -> list[float]:
    centroids = [r8.r7.centroid(*[mesh.points[index] for index in tri]) for tri in mesh.triangles]
    out = []
    for cell, faces in enumerate(per_cell):
        center = values[cell]
        local_min = center
        local_max = center
        for face_index, record in faces:
            if record["owner"] == cell and record["neighbour"] is not None:
                sample = values[record["neighbour"]]
            elif record["neighbour"] == cell:
                sample = values[record["owner"]]
            else:
                sample = boundary_values[face_index]
            local_min = min(local_min, sample)
            local_max = max(local_max, sample)
        gx, gy, valid = grads[cell]
        if not valid:
            out.append(0.0)
            continue
        phi = 1.0
        c = centroids[cell]
        for _, record in faces:
            f = face_midpoint(record)
            delta = gx * (f.x - c.x) + gy * (f.y - c.y)
            if delta > 0.0:
                phi = min(phi, max(0.0, min(1.0, (local_max - center) / delta)))
            elif delta < 0.0:
                phi = min(phi, max(0.0, min(1.0, (local_min - center) / delta)))
        out.append(phi)
    return out


def reconstruct_value(mesh: r8.r7.Mesh, cell: int, record: dict, values: list[float], grads: list[tuple[float, float, bool]], phis: list[float]) -> float:
    gx, gy, valid = grads[cell]
    if not valid:
        return values[cell]
    c = r8.r7.centroid(*[mesh.points[index] for index in mesh.triangles[cell]])
    f = face_midpoint(record)
    return values[cell] + phis[cell] * (gx * (f.x - c.x) + gy * (f.y - c.y))


def reconstruction_data(mesh: r8.r7.Mesh, per_cell: list[list[tuple[int, dict]]], states: list[list[float]], t: float, scheme: str) -> dict:
    if scheme == "first_order":
        return {"enabled": False}
    h = [state[0] for state in states]
    u = [primitive_from_state(tuple(state))[1] for state in states]
    v = [primitive_from_state(tuple(state))[2] for state in states]
    records = r8.edge_records(mesh)
    boundary_h = []
    boundary_u = []
    boundary_v = []
    for record in records:
        sample = r8.edge_average_time(record["a"], record["b"], t, 4) if record["neighbour"] is None else (0.0, 0.0, 0.0)
        hp, up, vp = primitive_from_state(sample)
        boundary_h.append(hp)
        boundary_u.append(up)
        boundary_v.append(vp)
    data = {"enabled": True, "h": h, "u": u, "v": v}
    for name, values, boundary in (("h", h, boundary_h), ("u", u, boundary_u), ("v", v, boundary_v)):
        g = gradients(mesh, per_cell, values, boundary)
        phi = [1.0] * len(values) if scheme == "unlimited_linear" else limiters(mesh, per_cell, values, boundary, g)
        data[f"{name}_grad"] = g
        data[f"{name}_phi"] = phi
    data["limiter_min"] = min(min(data["h_phi"]), min(data["u_phi"]), min(data["v_phi"]))
    data["limiter_active_fraction"] = sum(phi < 0.999999 for key in ("h_phi", "u_phi", "v_phi") for phi in data[key]) / (3 * len(states))
    return data


def face_state(mesh: r8.r7.Mesh, cell: int, record: dict, states: list[list[float]], recon: dict) -> tuple[float, float, float]:
    if not recon["enabled"]:
        return tuple(states[cell])
    h = reconstruct_value(mesh, cell, record, recon["h"], recon["h_grad"], recon["h_phi"])
    u = reconstruct_value(mesh, cell, record, recon["u"], recon["u_grad"], recon["u_phi"])
    v = reconstruct_value(mesh, cell, record, recon["v"], recon["v_grad"], recon["v_phi"])
    return conserved_from_primitive(h, u, v)


def mms_rhs(mesh: r8.r7.Mesh, areas: list[float], edges: list[dict], per_cell: list[list[tuple[int, dict]]], states: list[list[float]], t: float, order: int, scheme: str, counters: dict) -> list[list[float]]:
    t0 = time.perf_counter()
    recon = reconstruction_data(mesh, per_cell, states, t, scheme)
    counters["gradient_limiter_seconds"] += time.perf_counter() - t0
    if recon["enabled"]:
        counters["limiter_min"] = min(counters.get("limiter_min", 1.0), recon["limiter_min"])
        counters["limiter_active_fraction_sum"] = counters.get("limiter_active_fraction_sum", 0.0) + recon["limiter_active_fraction"]
        counters["limiter_samples"] = counters.get("limiter_samples", 0) + 1
    residual = [[0.0, 0.0, 0.0] for _ in states]
    for record in edges:
        owner = record["owner"]
        neighbour = record["neighbour"]
        left = face_state(mesh, owner, record, states, recon)
        if neighbour is not None:
            right = face_state(mesh, neighbour, record, states, recon)
        else:
            right = r8.edge_average_time(record["a"], record["b"], t, order)
        flux = r8.r7.rusanov_flux(left, right, record["nx"], record["ny"])
        for component in range(3):
            residual[owner][component] += flux[component] * record["length"]
            if neighbour is not None:
                residual[neighbour][component] -= flux[component] * record["length"]
    out = []
    for index, tri in enumerate(mesh.triangles):
        source = r8.triangle_average_time(mesh, tri, r8.mms_source, t, order)
        out.append([-residual[index][component] / areas[index] + source[component] for component in range(3)])
    return out


def mms_run(level: int, final_time: float, cfl: float, order: int, scheme: str, dt_scale: float = 1.0) -> dict:
    mesh = r8.r7.make_mesh(level)
    areas = [r8.r7.area(*[mesh.points[index] for index in tri]) for tri in mesh.triangles]
    edges = r8.edge_records(mesh)
    per_cell = cell_face_records(mesh)
    states = [r8.triangle_average_time(mesh, tri, r8.mms_state, 0.0, order) for tri in mesh.triangles]
    h = math.sqrt(1.0 / len(mesh.triangles))
    steps = math.ceil(final_time / (dt_scale * cfl * h / 5.0))
    dt = final_time / steps
    t = 0.0
    counters = {"gradient_limiter_seconds": 0.0}
    start = time.perf_counter()
    for _ in range(steps):
        l0 = mms_rhs(mesh, areas, edges, per_cell, states, t, order, scheme, counters)
        u1 = [[states[i][k] + dt * l0[i][k] for k in range(3)] for i in range(len(states))]
        l1 = mms_rhs(mesh, areas, edges, per_cell, u1, t + dt, order, scheme, counters)
        u2 = [[0.75 * states[i][k] + 0.25 * (u1[i][k] + dt * l1[i][k]) for k in range(3)] for i in range(len(states))]
        l2 = mms_rhs(mesh, areas, edges, per_cell, u2, t + 0.5 * dt, order, scheme, counters)
        states = [[(1.0 / 3.0) * states[i][k] + (2.0 / 3.0) * (u2[i][k] + dt * l2[i][k]) for k in range(3)] for i in range(len(states))]
        t += dt
    elapsed = time.perf_counter() - start
    errors = []
    for index, tri in enumerate(mesh.triangles):
        exact = r8.triangle_average_time(mesh, tri, r8.mms_state, final_time, order + 2)
        errors.append(tuple(states[index][component] - exact[component] for component in range(3)))
    norms = r8.r7.component_norms(errors, areas, [True] * len(areas))
    row = r8.flatten_norms(f"n{level}", h, norms)
    row.update(
        {
            "scheme": scheme,
            "cells": len(mesh.triangles),
            "steps": steps,
            "dt": dt,
            "cfl": cfl,
            "final_time": final_time,
            "wall_seconds": elapsed,
            "gradient_limiter_seconds": counters["gradient_limiter_seconds"],
            "limiter_min": counters.get("limiter_min", ""),
            "limiter_active_fraction": counters.get("limiter_active_fraction_sum", 0.0) / max(1, counters.get("limiter_samples", 0)),
        }
    )
    return row


def gradient_verification() -> dict:
    mesh = r8.r7.make_mesh(8)
    per_cell = cell_face_records(mesh)
    cases = {
        "constant": (lambda x, y: 2.5, (0.0, 0.0)),
        "linear_x": (lambda x, y: x - 0.2, (1.0, 0.0)),
        "linear_y": (lambda x, y: -0.3 + y, (0.0, 1.0)),
        "mixed_linear": (lambda x, y: 1.7 * x - 0.4 * y + 0.2, (1.7, -0.4)),
    }
    rows = []
    for name, (fn, exact) in cases.items():
        values = [fn(r8.r7.centroid(*[mesh.points[i] for i in tri]).x, r8.r7.centroid(*[mesh.points[i] for i in tri]).y) for tri in mesh.triangles]
        boundary = [fn(face_midpoint(record).x, face_midpoint(record).y) for record in r8.edge_records(mesh)]
        grads = gradients(mesh, per_cell, values, boundary)
        rows.append(
            {
                "case": name,
                "max_abs_gx_error": max(abs(g[0] - exact[0]) for g in grads),
                "max_abs_gy_error": max(abs(g[1] - exact[1]) for g in grads),
                "all_valid": all(g[2] for g in grads),
            }
        )
    fn = lambda x, y: math.sin(2.0 * PI * x) * math.cos(2.0 * PI * y)
    values = [fn(r8.r7.centroid(*[mesh.points[i] for i in tri]).x, r8.r7.centroid(*[mesh.points[i] for i in tri]).y) for tri in mesh.triangles]
    boundary = [fn(face_midpoint(record).x, face_midpoint(record).y) for record in r8.edge_records(mesh)]
    grads = gradients(mesh, per_cell, values, boundary)
    rows.append({"case": "nonlinear", "all_finite": all(math.isfinite(g[0]) and math.isfinite(g[1]) for g in grads), "valid_fraction": sum(g[2] for g in grads) / len(grads)})
    return {"schema": {"name": "tsunami.c1a_r9_gradient_verification", "version": "1.0.0"}, "study_id": STUDY_ID, "tests": rows}


def limiter_verification() -> dict:
    mesh = r8.r7.make_mesh(8)
    per_cell = cell_face_records(mesh)
    rows = []
    for name, fn in (
        ("constant", lambda x, y: 2.0),
        ("monotonic_linear", lambda x, y: x + y),
        ("smooth_extremum", lambda x, y: (x - 0.5) ** 2 + (y - 0.5) ** 2),
        ("sharp_gradient", lambda x, y: 0.0 if x < 0.5 else 1.0),
        ("near_dry_depth", lambda x, y: 0.02 + 0.01 * x),
    ):
        values = [fn(r8.r7.centroid(*[mesh.points[i] for i in tri]).x, r8.r7.centroid(*[mesh.points[i] for i in tri]).y) for tri in mesh.triangles]
        boundary = [fn(face_midpoint(record).x, face_midpoint(record).y) for record in r8.edge_records(mesh)]
        grads = gradients(mesh, per_cell, values, boundary)
        phis = limiters(mesh, per_cell, values, boundary, grads)
        reconstructed = [reconstruct_value(mesh, cell, record, values, grads, phis) for cell, faces in enumerate(per_cell) for _, record in faces]
        rows.append(
            {
                "case": name,
                "min_phi": min(phis),
                "active_fraction": sum(phi < 0.999999 for phi in phis) / len(phis),
                "minimum_reconstructed_value": min(reconstructed),
                "maximum_reconstructed_value": max(reconstructed),
                "constant_unchanged": name != "constant" or max(abs(value - 2.0) for value in reconstructed) < 1.0e-14,
                "no_negative_depth": name != "near_dry_depth" or min(reconstructed) >= 0.0,
            }
        )
    return {
        "schema": {"name": "tsunami.c1a_r9_limiter_verification", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "limiter": "Barth-Jespersen scalar limiter on eta/u/v with positivity floor on reconstructed depth",
        "tests": rows,
    }


def run_mms(levels: list[int], schemes: list[str], final_time: float, cfl: float, order: int) -> tuple[list[dict], dict]:
    rows = [mms_run(level, final_time, cfl, order, scheme) for scheme in schemes for level in levels]
    add_orders(rows)
    medium = levels[-2]
    base = mms_run(medium, final_time, cfl, order, "limited_linear")
    half = mms_run(medium, final_time, cfl, order, "limited_linear", dt_scale=0.5)
    changes = {
        component: {
            norm: abs(base[f"{component}_{norm}"] - half[f"{component}_{norm}"]) / base[f"{component}_{norm}"]
            for norm in ("l1", "l2", "linf")
        }
        for component in ("mass", "qx", "qy")
    }
    temporal = {
        "level": f"n{medium}",
        "base": base,
        "half_dt": half,
        "relative_error_change": changes,
        "maximum_l1_l2_change": max(changes[c][n] for c in ("mass", "qx", "qy") for n in ("l1", "l2")),
    }
    return rows, temporal


def classify(rows: list[dict], temporal: dict) -> str:
    limited = [row for row in rows if row["scheme"] == "limited_linear"]
    limited.sort(key=lambda row: row["actual_h"], reverse=True)
    final = limited[-1]
    order_ok = all(1.7 <= final[f"p_{component}_{norm}_from_previous"] <= 2.4 for component in ("mass", "qx", "qy") for norm in ("l1", "l2"))
    monotonic = all(
        previous[f"{component}_{norm}"] > current[f"{component}_{norm}"]
        for previous, current in zip(limited, limited[1:])
        for component in ("mass", "qx", "qy")
        for norm in ("l1", "l2")
    )
    temporal_ok = temporal["maximum_l1_l2_change"] <= 0.05
    if order_ok and monotonic and temporal_ok:
        return "SECOND_ORDER_VERIFIED"
    if monotonic and temporal_ok and all(final[f"p_{component}_l2_from_previous"] > 1.2 for component in ("mass", "qx", "qy")):
        return "SECOND_ORDER_PARTIALLY_VERIFIED"
    return "SECOND_ORDER_NOT_VERIFIED"


def smooth_wave_comparison() -> dict:
    first = mms_run(24, 0.006, 0.06, 4, "first_order")
    limited = mms_run(24, 0.006, 0.06, 4, "limited_linear")
    return {
        "schema": {"name": "tsunami.c1a_r9_smooth_wave_comparison", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "proxy": "inexpensive smooth fully wet MMS propagation at n24",
        "first_order_amplitude_error": first["mass_linf"],
        "limited_linear_amplitude_error": limited["mass_linf"],
        "first_order_phase_proxy_l2": first["mass_l2"],
        "limited_linear_phase_proxy_l2": limited["mass_l2"],
        "amplitude_error_reduction": first["mass_linf"] / limited["mass_linf"],
        "phase_proxy_error_reduction": first["mass_l2"] / limited["mass_l2"],
    }


def verification_summary(rows: list[dict], temporal: dict, classification: str, executable_sha: str) -> dict:
    first = {row["level"]: row for row in rows if row["scheme"] == "first_order"}
    limited = [row for row in rows if row["scheme"] == "limited_linear"]
    reductions = []
    for row in limited:
        base = first[row["level"]]
        reductions.append(
            {
                "level": row["level"],
                "mass_l2_reduction": base["mass_l2"] / row["mass_l2"],
                "qx_l2_reduction": base["qx_l2"] / row["qx_l2"],
                "qy_l2_reduction": base["qy_l2"] / row["qy_l2"],
            }
        )
    first_medium = first[limited[-2]["level"]]
    limited_medium = limited[-2]
    overhead = limited_medium["wall_seconds"] / first_medium["wall_seconds"]
    return {
        "schema": {"name": "tsunami.c1a_r9_scheme_verification", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "fresh_gcc_release_executable_sha256": executable_sha,
        "classification": classification,
        "temporal_contamination": temporal,
        "equal_mesh_error_reduction": reductions,
        "runtime_overhead_medium_level": overhead,
        "traceability": {
            "mathematical_model": "unchanged",
            "physical_source_terms": "unchanged",
            "boundary_roles": "unchanged",
            "coupling": "unchanged",
            "spatial_numerical_options": ["first_order", "limited_linear candidate"],
            "change_classification": "NUMERICAL DISCRETISATION CHANGE ONLY",
        },
    }


def make_figures(root: Path, rows: list[dict], summary: dict, smooth: dict) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    root.mkdir(parents=True, exist_ok=True)
    manifest = {"schema": {"name": "tsunami.c1a_r9_figure_manifest", "version": "1.0.0"}, "study_id": STUDY_ID, "figures": []}

    def save(name: str, provenance: dict) -> None:
        path = root / name
        plt.tight_layout()
        plt.savefig(path, format="svg")
        plt.close()
        path.write_text("\n".join(line.rstrip() for line in path.read_text(encoding="utf-8").splitlines()) + "\n", encoding="utf-8")
        prov = path.with_suffix(".provenance.json")
        write_json(prov, provenance)
        ET.parse(path)
        manifest["figures"].append({"path": path.as_posix(), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "provenance_path": prov.as_posix()})

    for component in ("mass", "qx", "qy"):
        plt.figure(figsize=(5.5, 3.5))
        for scheme in ("first_order", "limited_linear"):
            data = [row for row in rows if row["scheme"] == scheme]
            data.sort(key=lambda row: row["actual_h"], reverse=True)
            plt.loglog([row["actual_h"] for row in data], [row[f"{component}_l2"] for row in data], marker="o", label=scheme)
        plt.gca().invert_xaxis()
        plt.xlabel("characteristic h")
        plt.ylabel(f"{component} L2 error")
        plt.title(f"R9 {component} MMS error")
        plt.grid(True, which="both", alpha=0.25)
        plt.legend()
        save(f"c1a_r9_{component}_first_vs_limited_mms_error.svg", {"source": "regional2d_r9_mms_scheme_comparison.csv", "component": component})

    plt.figure(figsize=(5.5, 3.5))
    final = [row for row in rows if row["scheme"] == "limited_linear"][-1]
    plt.bar(["h", "qx", "qy"], [final["p_mass_l2_from_previous"], final["p_qx_l2_from_previous"], final["p_qy_l2_from_previous"]])
    plt.axhspan(1.7, 2.3, color="0.85")
    plt.ylabel("finest-pair L2 order")
    plt.title("R9 measured limited-linear order")
    plt.grid(True, axis="y", alpha=0.25)
    save("c1a_r9_first_vs_limited_measured_order.svg", {"source": "regional2d_r9_mms_scheme_comparison.csv"})

    plt.figure(figsize=(5.5, 3.5))
    reductions = summary["equal_mesh_error_reduction"]
    plt.plot([item["level"] for item in reductions], [item["mass_l2_reduction"] for item in reductions], marker="o", label="h")
    plt.plot([item["level"] for item in reductions], [item["qx_l2_reduction"] for item in reductions], marker="o", label="qx")
    plt.plot([item["level"] for item in reductions], [item["qy_l2_reduction"] for item in reductions], marker="o", label="qy")
    plt.ylabel("first/limited L2 error")
    plt.title("R9 equal-mesh error reduction")
    plt.grid(True, alpha=0.25)
    plt.legend()
    save("c1a_r9_error_reduction_factor_vs_h.svg", {"source": "regional2d_r9_scheme_verification.json"})

    plt.figure(figsize=(5.5, 3.5))
    plt.bar(["first", "limited"], [smooth["first_order_amplitude_error"], smooth["limited_linear_amplitude_error"]])
    plt.ylabel("amplitude error proxy")
    plt.title("R9 smooth-wave amplitude error")
    plt.grid(True, axis="y", alpha=0.25)
    save("c1a_r9_smooth_wave_amplitude_error.svg", {"source": "regional2d_r9_smooth_wave_comparison.json"})

    plt.figure(figsize=(5.5, 3.5))
    plt.bar(["first", "limited"], [smooth["first_order_phase_proxy_l2"], smooth["limited_linear_phase_proxy_l2"]])
    plt.ylabel("phase proxy L2")
    plt.title("R9 smooth-wave phase error")
    plt.grid(True, axis="y", alpha=0.25)
    save("c1a_r9_smooth_wave_phase_error.svg", {"source": "regional2d_r9_smooth_wave_comparison.json"})

    write_json(root / "c1a_r9_figure_manifest.json", manifest)


def write_diagnosis(path: Path, summary: dict, rows: list[dict], smooth: dict) -> None:
    final = [row for row in rows if row["scheme"] == "limited_linear"][-1]
    lines = [
        "# R9 Regional2D Reconstruction Verification",
        "",
        f"Second-order classification: `{summary['classification']}`",
        "",
        "The mathematical NLSWE model, source physics, boundary roles, and coupling quantities are unchanged. R9 changes only the spatial face-state reconstruction option.",
        "",
        "The production implementation preserves `first_order` as the default and adds an opt-in `limited_linear` reconstruction of `(eta,u,v)` with the existing hydrostatic bed step and Rusanov flux.",
        "",
        "## Finest Limited-Linear MMS Orders",
        "",
        f"- h/mass L1/L2/Linf: `{final['p_mass_l1_from_previous']:.6g}`, `{final['p_mass_l2_from_previous']:.6g}`, `{final['p_mass_linf_from_previous']:.6g}`",
        f"- qx L1/L2/Linf: `{final['p_qx_l1_from_previous']:.6g}`, `{final['p_qx_l2_from_previous']:.6g}`, `{final['p_qx_linf_from_previous']:.6g}`",
        f"- qy L1/L2/Linf: `{final['p_qy_l1_from_previous']:.6g}`, `{final['p_qy_l2_from_previous']:.6g}`, `{final['p_qy_linf_from_previous']:.6g}`",
        "",
        "## Smooth-Wave Proxy",
        "",
        f"Amplitude error reduction: `{smooth['amplitude_error_reduction']:.6g}`.",
        "",
        f"Phase-proxy error reduction: `{smooth['phase_proxy_error_reduction']:.6g}`.",
        "",
        "## Event Gate",
        "",
        "Production Tohoku convergence may resume only if the build/test matrix and robustness gates remain green; h300 and temporal convergence remain gated.",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docs-root", type=Path, default=DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=DEFAULT_FIGURE_ROOT)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--levels", type=int, nargs="+", default=[4, 8, 16, 32])
    parser.add_argument("--final-time", type=float, default=0.01)
    parser.add_argument("--cfl", type=float, default=0.04)
    parser.add_argument("--quadrature-order", type=int, default=4)
    parser.add_argument("--executable-sha256", default="")
    parser.add_argument("--skip-figures", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    update_state(args.external_root, "started")
    gradient = gradient_verification()
    limiter = limiter_verification()
    write_json(args.docs_root / "regional2d_r9_gradient_verification.json", gradient)
    write_json(args.docs_root / "regional2d_r9_limiter_verification.json", limiter)
    update_state(args.external_root, "gradient_limiter_complete")
    rows, temporal = run_mms(args.levels, ["first_order", "unlimited_linear", "limited_linear"], args.final_time, args.cfl, args.quadrature_order)
    fieldnames = list(rows[0].keys())
    write_csv(args.docs_root / "regional2d_r9_mms_scheme_comparison.csv", fieldnames, rows)
    classification = classify(rows, temporal)
    smooth = smooth_wave_comparison()
    summary = verification_summary(rows, temporal, classification, args.executable_sha256)
    write_json(args.docs_root / "regional2d_r9_scheme_verification.json", summary)
    write_json(args.docs_root / "regional2d_r9_smooth_wave_comparison.json", smooth)
    write_diagnosis(args.docs_root / "regional2d_r9_diagnosis.md", summary, rows, smooth)
    if not args.skip_figures:
        make_figures(args.figure_root, rows, summary, smooth)
    update_state(args.external_root, "decision_complete", {"classification": classification})
    print(json.dumps({"classification": classification, "temporal": temporal["maximum_l1_l2_change"]}, indent=2))


if __name__ == "__main__":
    main()
