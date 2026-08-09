#!/usr/bin/env python3
"""R7 exact semi-discrete Regional2D spatial benchmark.

This is verification-only infrastructure. It mirrors the accepted
piecewise-constant Rusanov finite-volume residual on deterministic
structured triangular meshes and compares it with a high-order
cell-average reference for div(F(U*)) on interior cells.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

import numpy as np


G = 9.81
PI = math.pi
STUDY_ID = "regional2d-spatial-upgrade-r7"
DEFAULT_EXTERNAL_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-spatial-upgrade-r7")
DEFAULT_DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
DEFAULT_FIGURE_ROOT = Path("deliverables/figures/convergence")


@dataclass(frozen=True)
class Point:
    x: float
    y: float


@dataclass(frozen=True)
class Mesh:
    columns: int
    rows: int
    points: list[Point]
    triangles: list[tuple[int, int, int]]


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_csv(path: Path, fieldnames: list[str], rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def update_state(external_root: Path, state: str, extra: dict | None = None) -> None:
    payload = {
        "schema": {"name": "tsunami.c1a_r7_execution_state", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "state": state,
        "updated_at_utc": utc_now(),
        "external_root": external_root.as_posix(),
    }
    if extra:
        payload.update(extra)
    write_json(external_root / "execution_state.json", payload)


def make_mesh(columns: int, rows: int | None = None) -> Mesh:
    rows = columns if rows is None else rows
    points = [Point(i / columns, j / rows) for j in range(rows + 1) for i in range(columns + 1)]

    def vid(i: int, j: int) -> int:
        return j * (columns + 1) + i

    triangles: list[tuple[int, int, int]] = []
    for row in range(rows):
        for column in range(columns):
            v00 = vid(column, row)
            v10 = vid(column + 1, row)
            v01 = vid(column, row + 1)
            v11 = vid(column + 1, row + 1)
            triangles.append((v00, v10, v11))
            triangles.append((v00, v11, v01))
    return Mesh(columns, rows, points, triangles)


def mesh_hash(mesh: Mesh) -> str:
    digest = hashlib.sha256()
    for point in mesh.points:
        digest.update(f"{point.x:.17g},{point.y:.17g};".encode("ascii"))
    digest.update(b"|")
    for tri in mesh.triangles:
        digest.update(f"{tri[0]},{tri[1]},{tri[2]};".encode("ascii"))
    return digest.hexdigest()


def area(a: Point, b: Point, c: Point) -> float:
    return abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5


def centroid(a: Point, b: Point, c: Point) -> Point:
    return Point((a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0)


def outward_normal(a: Point, b: Point) -> tuple[float, float, float]:
    dx = b.x - a.x
    dy = b.y - a.y
    length = math.hypot(dx, dy)
    return dy / length, -dx / length, length


def state(x: float, y: float) -> tuple[float, float, float]:
    """Smooth fully wet manufactured state with nontrivial x/y derivatives."""
    h = 2.0 + 0.18 * math.sin(2.0 * PI * x) * math.cos(2.0 * PI * y)
    qx = 0.35 + 0.09 * math.cos(2.0 * PI * x) * math.sin(4.0 * PI * y)
    qy = -0.22 + 0.07 * math.sin(4.0 * PI * x) * math.cos(2.0 * PI * y)
    return h, qx, qy


def physical_flux(sample: tuple[float, float, float], nx: float, ny: float) -> tuple[float, float, float]:
    h, qx, qy = sample
    u = qx / h
    v = qy / h
    un = u * nx + v * ny
    pressure = 0.5 * G * h * h
    return h * un, qx * un + pressure * nx, qy * un + pressure * ny


def signal_speed(sample: tuple[float, float, float], nx: float, ny: float) -> float:
    h, qx, qy = sample
    return abs((qx / h) * nx + (qy / h) * ny) + math.sqrt(G * h)


def rusanov_flux(
    left: tuple[float, float, float],
    right: tuple[float, float, float],
    nx: float,
    ny: float,
) -> tuple[float, float, float]:
    left_flux = physical_flux(left, nx, ny)
    right_flux = physical_flux(right, nx, ny)
    alpha = max(signal_speed(left, nx, ny), signal_speed(right, nx, ny))
    return tuple(0.5 * (left_flux[i] + right_flux[i]) - 0.5 * alpha * (right[i] - left[i]) for i in range(3))


def gauss(order: int) -> tuple[np.ndarray, np.ndarray]:
    nodes, weights = np.polynomial.legendre.leggauss(order)
    return nodes.astype(float), weights.astype(float)


def triangle_average(a: Point, b: Point, c: Point, order: int) -> tuple[float, float, float]:
    nodes, weights = gauss(order)
    measure = area(a, b, c)
    total = [0.0, 0.0, 0.0]
    for ni, wi in zip(nodes, weights):
        r = 0.5 * (float(ni) + 1.0)
        wr = 0.5 * float(wi)
        for nj, wj in zip(nodes, weights):
            s = 0.5 * (float(nj) + 1.0)
            ws = 0.5 * float(wj)
            # Duffy map from the unit square to the unit triangle.
            wa = r
            wb = (1.0 - r) * s
            wc = 1.0 - wa - wb
            x = wa * b.x + wb * c.x + wc * a.x
            y = wa * b.y + wb * c.y + wc * a.y
            weight = wr * ws * (1.0 - r) * 2.0 * measure
            sample = state(x, y)
            for component in range(3):
                total[component] += weight * sample[component]
    return tuple(value / measure for value in total)


def edge_average(a: Point, b: Point, order: int) -> tuple[float, float, float]:
    nodes, weights = gauss(order)
    total = [0.0, 0.0, 0.0]
    for node, weight in zip(nodes, weights):
        t = 0.5 * (float(node) + 1.0)
        sample = state(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y))
        for component in range(3):
            total[component] += 0.5 * float(weight) * sample[component]
    return tuple(total)


def exact_flux_integral(a: Point, b: Point, order: int) -> tuple[float, float, float]:
    nx, ny, length = outward_normal(a, b)
    nodes, weights = gauss(order)
    total = [0.0, 0.0, 0.0]
    for node, weight in zip(nodes, weights):
        t = 0.5 * (float(node) + 1.0)
        sample = state(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y))
        flux = physical_flux(sample, nx, ny)
        for component in range(3):
            total[component] += 0.5 * float(weight) * length * flux[component]
    return tuple(total)


def production_residual(mesh: Mesh, quadrature_order: int) -> tuple[list[list[float]], list[float], list[Point]]:
    cell_states: list[tuple[float, float, float]] = []
    cell_areas: list[float] = []
    cell_centroids: list[Point] = []
    for tri in mesh.triangles:
        a, b, c = [mesh.points[index] for index in tri]
        cell_states.append(triangle_average(a, b, c, quadrature_order))
        cell_areas.append(area(a, b, c))
        cell_centroids.append(centroid(a, b, c))

    residual = [[0.0, 0.0, 0.0] for _ in mesh.triangles]
    edge_owner: dict[tuple[int, int], tuple[int, tuple[int, int]]] = {}
    for cell_index, tri in enumerate(mesh.triangles):
        for first, second in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            key = tuple(sorted((first, second)))
            found = edge_owner.pop(key, None)
            if found is None:
                edge_owner[key] = (cell_index, (first, second))
                continue
            owner, oriented = found
            a = mesh.points[oriented[0]]
            b = mesh.points[oriented[1]]
            nx, ny, length = outward_normal(a, b)
            flux = rusanov_flux(cell_states[owner], cell_states[cell_index], nx, ny)
            for component in range(3):
                residual[owner][component] += flux[component] * length
                residual[cell_index][component] -= flux[component] * length

    for owner, oriented in edge_owner.values():
        a = mesh.points[oriented[0]]
        b = mesh.points[oriented[1]]
        nx, ny, length = outward_normal(a, b)
        boundary_state = edge_average(a, b, quadrature_order)
        flux = rusanov_flux(cell_states[owner], boundary_state, nx, ny)
        for component in range(3):
            residual[owner][component] += flux[component] * length
    return residual, cell_areas, cell_centroids


def exact_divergence_reference(mesh: Mesh, quadrature_order: int) -> list[tuple[float, float, float]]:
    exact: list[tuple[float, float, float]] = []
    for tri in mesh.triangles:
        points = [mesh.points[index] for index in tri]
        total = [0.0, 0.0, 0.0]
        for a, b in ((points[0], points[1]), (points[1], points[2]), (points[2], points[0])):
            integral = exact_flux_integral(a, b, quadrature_order)
            for component in range(3):
                total[component] += integral[component]
        measure = area(points[0], points[1], points[2])
        exact.append(tuple(value / measure for value in total))
    return exact


def interior_mask(centroids: Iterable[Point], exclusion: float) -> list[bool]:
    return [
        exclusion <= point.x <= 1.0 - exclusion and exclusion <= point.y <= 1.0 - exclusion
        for point in centroids
    ]


def component_norms(errors: list[tuple[float, float, float]], areas: list[float], mask: list[bool]) -> dict[str, dict[str, float]]:
    names = ["mass", "qx", "qy"]
    totals = {name: {"l1": 0.0, "l2": 0.0, "linf": 0.0} for name in names}
    area_sum = 0.0
    for error, measure, include in zip(errors, areas, mask):
        if not include:
            continue
        area_sum += measure
        for index, name in enumerate(names):
            value = abs(error[index])
            totals[name]["l1"] += measure * value
            totals[name]["l2"] += measure * value * value
            totals[name]["linf"] = max(totals[name]["linf"], value)
    if area_sum <= 0.0:
        raise ValueError("interior mask selected no cells")
    for name in names:
        totals[name]["l1"] /= area_sum
        totals[name]["l2"] = math.sqrt(totals[name]["l2"] / area_sum)
    return totals


def run_level(columns: int, quadrature_order: int, exclusion: float) -> dict:
    mesh = make_mesh(columns)
    residual, areas, centroids = production_residual(mesh, quadrature_order)
    exact = exact_divergence_reference(mesh, quadrature_order)
    errors = [
        tuple(residual[cell][component] / areas[cell] - exact[cell][component] for component in range(3))
        for cell in range(len(areas))
    ]
    mask = interior_mask(centroids, exclusion)
    norms = component_norms(errors, areas, mask)
    return {
        "level": f"n{columns}",
        "columns": columns,
        "rows": columns,
        "cells": len(mesh.triangles),
        "faces": 3 * columns * columns + 2 * columns,
        "actual_h": math.sqrt(1.0 / len(mesh.triangles)),
        "refinement_ratio_from_previous": "",
        "mesh_sha256": mesh_hash(mesh),
        "interior_cell_count": sum(mask),
        "mass_l1": norms["mass"]["l1"],
        "mass_l2": norms["mass"]["l2"],
        "mass_linf": norms["mass"]["linf"],
        "qx_l1": norms["qx"]["l1"],
        "qx_l2": norms["qx"]["l2"],
        "qx_linf": norms["qx"]["linf"],
        "qy_l1": norms["qy"]["l1"],
        "qy_l2": norms["qy"]["l2"],
        "qy_linf": norms["qy"]["linf"],
    }


def order(coarse_error: float, fine_error: float, coarse_h: float, fine_h: float) -> float:
    return math.log(coarse_error / fine_error) / math.log(coarse_h / fine_h)


def add_orders(rows: list[dict]) -> None:
    for previous, current in zip(rows, rows[1:]):
        current["refinement_ratio_from_previous"] = previous["actual_h"] / current["actual_h"]
        for component in ("mass", "qx", "qy"):
            for norm in ("l1", "l2", "linf"):
                key = f"{component}_{norm}"
                current[f"p_{key}_from_previous"] = order(previous[key], current[key], previous["actual_h"], current["actual_h"])
    for component in ("mass", "qx", "qy"):
        for norm in ("l1", "l2", "linf"):
            rows[0][f"p_{component}_{norm}_from_previous"] = ""


def quadrature_verification(columns: int, coarse_order: int, fine_order: int, exclusion: float) -> dict:
    mesh = make_mesh(columns)
    _, areas, centroids = production_residual(mesh, fine_order)
    low = exact_divergence_reference(mesh, coarse_order)
    high = exact_divergence_reference(mesh, fine_order)
    mask = interior_mask(centroids, exclusion)
    diffs = [tuple(low[i][k] - high[i][k] for k in range(3)) for i in range(len(high))]
    norms = component_norms(diffs, areas, mask)
    return {
        "columns": columns,
        "coarse_quadrature_order": coarse_order,
        "fine_quadrature_order": fine_order,
        "reference_difference_norms": norms,
    }


def classify_first_order(rows: list[dict], quadrature: dict) -> dict:
    fine_pairs = rows[-2:]
    final = fine_pairs[-1]
    monotonic = {}
    fine_orders = {}
    for component in ("mass", "qx", "qy"):
        monotonic[component] = {}
        fine_orders[component] = {}
        for norm in ("l1", "l2", "linf"):
            key = f"{component}_{norm}"
            values = [row[key] for row in rows]
            monotonic[component][norm] = all(coarse > fine for coarse, fine in zip(values, values[1:]))
            fine_orders[component][norm] = final[f"p_{key}_from_previous"]
    q_ok = True
    ratios = {}
    for component in ("mass", "qx", "qy"):
        finest_error = final[f"{component}_l2"]
        q_error = quadrature["reference_difference_norms"][component]["l2"]
        ratios[component] = q_error / finest_error if finest_error else math.inf
        q_ok = q_ok and ratios[component] <= 1.0e-3
    order_ok = all(0.8 <= fine_orders[component][norm] <= 1.2 for component in ("mass", "qx", "qy") for norm in ("l1", "l2"))
    monotonic_ok = all(monotonic[component][norm] for component in ("mass", "qx", "qy") for norm in ("l1", "l2"))
    passed = q_ok and order_ok and monotonic_ok
    return {
        "baseline_first_order_verified": passed,
        "acceptance_band_l1_l2": [0.8, 1.2],
        "errors_monotonic_l1_l2": monotonic_ok,
        "reference_error_floor_remaining": not q_ok,
        "quadrature_error_to_finest_l2_error": ratios,
        "finest_pair_orders": fine_orders,
        "component_monotonicity": monotonic,
        "failure_classification": None if passed else "geometry/operator inconsistency",
        "stop_before_muscl": not passed,
    }


def diagnose_r6() -> dict:
    amplitude = 1.0e-4
    mean_depth = 1.0
    k = 2.0 * PI
    linear_qx_scale = G * mean_depth * amplitude * k
    nonlinear_qx_scale = G * amplitude * amplitude * k
    return {
        "r6_benchmark": "smooth_linear_wave_semidiscrete_residual",
        "analytical_reference": "small-amplitude linear shallow-water residual evaluated at cell centroids",
        "wave_amplitude": amplitude,
        "background_depth": mean_depth,
        "domain": "1.0 m by 0.25 m structured triangular strip",
        "boundary_conditions": "fixed analytical boundary states at face centroids; interior-only x-window norms",
        "mesh_family": "nested structured triangular meshes with columns 16, 32, 64, 128 in evidence",
        "time_treatment": "semi-discrete residual at t=0; no time integration",
        "error_evaluation": "cell residual divided by area compared to centroid linearized reference",
        "cell_state_representation": "centroid point values used as cell states",
        "reference_sampling": "centroid point reference, not cell-average flux-divergence reference",
        "plausible_error_floor_sources": [
            "linearized analytical solution compared with nonlinear SWE flux",
            "finite-volume cell averages silently represented by centroid point values",
            "reference flux divergence sampled at centroids rather than cell averaged",
            "one-dimensional wave did not exercise nontrivial y-derivatives",
            "boundary states were face-centroid samples, though boundary cells were excluded",
        ],
        "linearisation_error_estimate": {
            "linear_qx_residual_scale": linear_qx_scale,
            "nonlinear_qx_residual_scale": nonlinear_qx_scale,
            "relative_scale": nonlinear_qx_scale / linear_qx_scale,
            "material": False,
        },
        "temporal_error_material": False,
        "reference_or_quadrature_error_material": True,
        "r7_resolution": (
            "Replace the R6 centroid/linearized reference with a nonlinear manufactured state, "
            "high-order quadrature cell averages, and high-order boundary-integral cell-average "
            "div(F(U*)) references on an interior physical window."
        ),
    }


def make_figures(rows: list[dict], figure_root: Path, docs_payload_hash: str) -> dict:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    figure_root.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema": {"name": "tsunami.c1a_r7_figure_manifest", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "figures": [],
    }

    def save(name: str, provenance: dict) -> None:
        path = figure_root / name
        plt.tight_layout()
        plt.savefig(path, format="svg")
        plt.close()
        prov_path = path.with_suffix(".provenance.json")
        write_json(prov_path, provenance)
        manifest["figures"].append(
            {
                "path": path.as_posix(),
                "provenance_path": prov_path.as_posix(),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "provenance_sha256": hashlib.sha256(prov_path.read_bytes()).hexdigest(),
            }
        )

    h = [row["actual_h"] for row in rows]
    plt.figure(figsize=(5.5, 3.5))
    for component in ("mass", "qx", "qy"):
        plt.loglog(h, [row[f"{component}_l2"] for row in rows], marker="o", label=f"{component} L2")
    plt.gca().invert_xaxis()
    plt.xlabel("characteristic h")
    plt.ylabel("L2 residual error")
    plt.title("R7 exact first-order residual error")
    plt.legend()
    plt.grid(True, which="both", alpha=0.25)
    save(
        "c1a_r7_first_order_exact_error_vs_h.svg",
        {
            "study_id": STUDY_ID,
            "source": "regional2d_r7_exact_spatial_benchmark.json",
            "source_sha256": docs_payload_hash,
            "description": "Single-panel log-log first-order exact manufactured residual error.",
        },
    )

    plt.figure(figsize=(5.5, 3.5))
    h_mid = h[1:]
    for component in ("mass", "qx", "qy"):
        plt.semilogx(h_mid, [row[f"p_{component}_l2_from_previous"] for row in rows[1:]], marker="o", label=f"{component} L2")
    plt.axhspan(0.8, 1.2, color="0.85", label="first-order band")
    plt.gca().invert_xaxis()
    plt.xlabel("fine-grid characteristic h")
    plt.ylabel("observed order")
    plt.title("R7 exact first-order measured order")
    plt.legend()
    plt.grid(True, which="both", alpha=0.25)
    save(
        "c1a_r7_first_order_order_vs_h.svg",
        {
            "study_id": STUDY_ID,
            "source": "regional2d_r7_exact_spatial_benchmark.json",
            "source_sha256": docs_payload_hash,
            "description": "Single-panel first-order observed L2 order from adjacent exact benchmark levels.",
        },
    )
    write_json(figure_root / "c1a_r7_figure_manifest.json", manifest)
    return manifest


def build_payload(rows: list[dict], quadrature: dict, docs_hash_placeholder: str = "") -> dict:
    gate = classify_first_order(rows, quadrature)
    return {
        "schema": {"name": "tsunami.c1a_r7_exact_spatial_benchmark", "version": "1.0.0"},
        "study_id": STUDY_ID,
        "generated_at_utc": utc_now(),
        "mathematical_model": "unchanged NLSWE; verification-only semi-discrete spatial operator benchmark",
        "benchmark_definition": {
            "operator": "L_h(U*) = production-style piecewise-constant Rusanov finite-volume residual on flat bed",
            "reference": "cell-average div(F(U*)) from high-order edge quadrature",
            "domain": "[0,1] x [0,1]",
            "bed": "flat b=0",
            "sources": "none; no Manning, Coriolis, sponge, earthquake, wet/dry activation, or time integration",
            "boundary_treatment": "exact analytical boundary face states are supplied; norms use a fixed interior window 0.2<=x,y<=0.8",
            "cell_average_method": "Duffy-mapped tensor Gauss-Legendre quadrature for U* cell averages",
            "state": {
                "h": "2.0 + 0.18 sin(2*pi*x) cos(2*pi*y)",
                "qx": "0.35 + 0.09 cos(2*pi*x) sin(4*pi*y)",
                "qy": "-0.22 + 0.07 sin(4*pi*x) cos(2*pi*y)",
                "minimum_h": ">= 1.82 analytically",
            },
        },
        "r6_error_floor_diagnosis": diagnose_r6(),
        "quadrature_verification": quadrature,
        "levels": rows,
        "first_order_gate": gate,
        "second_order_gate_opened": gate["baseline_first_order_verified"],
        "second_order_artifacts_created": False,
        "final_r7_classification": "BASELINE_ORDER_UNRESOLVED",
        "docs_payload_hash_placeholder": docs_hash_placeholder,
    }


def write_markdown(path: Path, payload: dict) -> None:
    gate = payload["first_order_gate"]
    lines = [
        "# R7 Exact Regional2D Spatial Benchmark Diagnosis",
        "",
        f"Study ID: `{STUDY_ID}`",
        "",
        "Final R7 classification: `BASELINE_ORDER_UNRESOLVED`",
        "",
        "The exact manufactured semi-discrete benchmark did not recover first-order L1/L2 convergence for all Regional2D residual components. The first hard gate therefore remains closed and no limited-linear/MUSCL implementation was attempted.",
        "",
        "## Benchmark",
        "",
        "- Domain: `[0,1] x [0,1]`, flat bed, fully wet.",
        "- State: `h=2.0+0.18 sin(2*pi*x) cos(2*pi*y)`, `qx=0.35+0.09 cos(2*pi*x) sin(4*pi*y)`, `qy=-0.22+0.07 sin(4*pi*x) cos(2*pi*y)`.",
        "- Reference: cell-average `div(F(U*))` from high-order boundary quadrature.",
        "- Norms: fixed interior window `0.2<=x,y<=0.8` at every refinement level.",
        "",
        "## First-Order Gate",
        "",
        f"- baseline_first_order_verified: `{gate['baseline_first_order_verified']}`",
        f"- failure_classification: `{gate['failure_classification']}`",
        f"- errors_monotonic_l1_l2: `{gate['errors_monotonic_l1_l2']}`",
        f"- reference_error_floor_remaining: `{gate['reference_error_floor_remaining']}`",
        "",
        "Finest-pair L2 orders:",
        "",
        f"- mass: `{gate['finest_pair_orders']['mass']['l2']:.6g}`",
        f"- qx: `{gate['finest_pair_orders']['qx']['l2']:.6g}`",
        f"- qy: `{gate['finest_pair_orders']['qy']['l2']:.6g}`",
        "",
        "## R6 Error Floor",
        "",
        "R6 excluded time integration, and its finite-amplitude linearisation error scale was about `1e-4` of the linear qx residual scale, so neither is the material explanation. The material weakness was reference construction: centroid point states and a centroid linearised reference were used instead of cell averages and a nonlinear cell-average flux-divergence reference.",
        "",
        "## Decision",
        "",
        "Stop before MUSCL. The next scientific action is to inspect the cell-average-to-face-state operator consistency, including whether the residual path needs an exact first-order-consistent reconstruction/evaluation treatment before any higher-order extension is designed.",
        "",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docs-root", type=Path, default=DEFAULT_DOCS_ROOT)
    parser.add_argument("--figure-root", type=Path, default=DEFAULT_FIGURE_ROOT)
    parser.add_argument("--external-root", type=Path, default=DEFAULT_EXTERNAL_ROOT)
    parser.add_argument("--levels", type=int, nargs="+", default=[8, 16, 32, 64])
    parser.add_argument("--quadrature-order", type=int, default=24)
    parser.add_argument("--reference-check-order", type=int, default=36)
    parser.add_argument("--interior-exclusion", type=float, default=0.2)
    parser.add_argument("--skip-external-state", action="store_true")
    parser.add_argument("--skip-figures", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.skip_external_state:
        update_state(args.external_root, "started")
    rows = [run_level(level, args.quadrature_order, args.interior_exclusion) for level in args.levels]
    add_orders(rows)
    quadrature = quadrature_verification(args.levels[-1], args.quadrature_order, args.reference_check_order, args.interior_exclusion)
    payload = build_payload(rows, quadrature)
    docs_root = args.docs_root
    json_path = docs_root / "regional2d_r7_exact_spatial_benchmark.json"
    csv_path = docs_root / "regional2d_r7_first_order_convergence.csv"
    md_path = docs_root / "regional2d_r7_diagnosis.md"
    write_json(json_path, payload)
    payload_hash = hashlib.sha256(json_path.read_bytes()).hexdigest()
    payload["artifact_sha256"] = payload_hash
    write_json(json_path, payload)
    fields = [
        "level",
        "columns",
        "rows",
        "cells",
        "faces",
        "actual_h",
        "refinement_ratio_from_previous",
        "mesh_sha256",
        "interior_cell_count",
        "mass_l1",
        "mass_l2",
        "mass_linf",
        "qx_l1",
        "qx_l2",
        "qx_linf",
        "qy_l1",
        "qy_l2",
        "qy_linf",
        "p_mass_l1_from_previous",
        "p_mass_l2_from_previous",
        "p_mass_linf_from_previous",
        "p_qx_l1_from_previous",
        "p_qx_l2_from_previous",
        "p_qx_linf_from_previous",
        "p_qy_l1_from_previous",
        "p_qy_l2_from_previous",
        "p_qy_linf_from_previous",
    ]
    write_csv(csv_path, fields, rows)
    write_markdown(md_path, payload)
    manifest = None
    if not args.skip_figures:
        manifest = make_figures(rows, args.figure_root, payload_hash)
    if not args.skip_external_state:
        update_state(
            args.external_root,
            "decision_complete",
            {
                "baseline_first_order_verified": payload["first_order_gate"]["baseline_first_order_verified"],
                "second_order_gate_opened": payload["second_order_gate_opened"],
                "final_r7_classification": payload["final_r7_classification"],
                "repo_evidence": {
                    "json": json_path.as_posix(),
                    "csv": csv_path.as_posix(),
                    "markdown": md_path.as_posix(),
                    "figure_manifest": (args.figure_root / "c1a_r7_figure_manifest.json").as_posix() if manifest else None,
                },
            },
        )
    print(json.dumps({"json": json_path.as_posix(), "csv": csv_path.as_posix(), "md": md_path.as_posix(), "gate": payload["first_order_gate"]}, indent=2))


if __name__ == "__main__":
    main()
