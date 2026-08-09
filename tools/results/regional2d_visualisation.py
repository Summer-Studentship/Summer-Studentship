#!/usr/bin/env python3
"""Reproducible proof-of-concept visualisations for Regional2D results."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Mapping, Sequence

import numpy as np

from tools.results.regional2d_result import ResultDataset, SyntheticResultDataset, git_sha, sha256, utc_now


def _pyplot():
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection

    return plt, PolyCollection


def _write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _classification(dataset: ResultDataset) -> str:
    return str(dataset.metadata().get("data_class", "UNKNOWN"))


def _finish_figure(fig: Any, path: Path, dataset: ResultDataset, *, figure_type: str, fields: Sequence[str], time_range: Sequence[float] | None = None, units: Mapping[str, str] | None = None) -> dict[str, Any]:
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, format="svg")
    import matplotlib.pyplot as plt

    plt.close(fig)
    provenance = {
        "schema": {"name": "tsunami.figure_provenance", "version": "1.0.0"},
        "figure_path": path.as_posix(),
        "figure_type": figure_type,
        "source_run_id": dataset.metadata().get("run_id"),
        "source_case_id": dataset.metadata().get("case_id"),
        "input_files": [],
        "input_hashes": {},
        "data_classification": _classification(dataset),
        "generating_script": "tools/results/regional2d_visualisation.py",
        "git_sha": git_sha(),
        "generated_at_utc": utc_now(),
        "fields_used": list(fields),
        "time_range_used": list(time_range) if time_range is not None else None,
        "units": dict(units or {}),
        "sha256": sha256(path),
    }
    sidecar = path.with_suffix(".provenance.json")
    _write_json(sidecar, provenance)
    return {"figure": path.as_posix(), "provenance": sidecar.as_posix(), "sha256": provenance["sha256"], "data_classification": provenance["data_classification"]}


def _label_if_synthetic(ax: Any, dataset: ResultDataset) -> None:
    if _classification(dataset) == "SYNTHETIC":
        ax.text(
            0.02,
            0.98,
            "SYNTHETIC / PROOF OF CONCEPT",
            transform=ax.transAxes,
            va="top",
            ha="left",
            fontsize=8,
            color="#8a4600",
            bbox={"facecolor": "#fff4d6", "edgecolor": "#d4a72c", "boxstyle": "round,pad=0.25"},
        )


def plot_mesh(dataset: ResultDataset, path: Path) -> dict[str, Any]:
    plt, PolyCollection = _pyplot()
    mesh = dataset.mesh()
    polys = mesh["points"][mesh["connectivity"]]
    fig, ax = plt.subplots(figsize=(5.2, 4.4), constrained_layout=True)
    ax.add_collection(PolyCollection(polys, facecolors="#d7ecff", edgecolors="#1f6feb", linewidths=1.0))
    centres = mesh["cell_centres"]
    ax.scatter(centres[:, 0], centres[:, 1], s=18, color="#bf8700", label="cell centres")
    ax.plot(mesh["points"][[0, 1], 0], mesh["points"][[0, 1], 1], color="#cf222e", linewidth=2.5, label="coupling section")
    ax.set_aspect("equal", adjustable="box")
    ax.set_title("Regional2D Mesh")
    ax.set_xlabel("x (m)")
    ax.set_ylabel("y (m)")
    ax.legend(frameon=False)
    _label_if_synthetic(ax, dataset)
    return _finish_figure(fig, path, dataset, figure_type="mesh", fields=["points", "connectivity"], units={"x": "m", "y": "m"})


def plot_scalar_field(dataset: ResultDataset, path: Path, *, field: str = "h", time: float | int = 0) -> dict[str, Any]:
    plt, PolyCollection = _pyplot()
    mesh = dataset.mesh()
    values = dataset.field(field, time)
    polys = mesh["points"][mesh["connectivity"]]
    fig, ax = plt.subplots(figsize=(5.2, 4.4), constrained_layout=True)
    collection = PolyCollection(polys, array=np.asarray(values), edgecolors="#24292f", linewidths=0.6, cmap="viridis")
    ax.add_collection(collection)
    fig.colorbar(collection, ax=ax, label={"h": "water depth (m)", "eta": "free surface (m)", "qmag": "|q| (m^2/s)"}.get(field, field))
    ax.set_aspect("equal", adjustable="box")
    ax.set_title(f"Regional2D {field} field")
    ax.set_xlabel("x (m)")
    ax.set_ylabel("y (m)")
    _label_if_synthetic(ax, dataset)
    time_value = float(dataset.times()[int(time)]) if isinstance(time, int) else float(time)
    return _finish_figure(fig, path, dataset, figure_type="field", fields=[field], time_range=[time_value, time_value], units={field: {"h": "m", "eta": "m", "qmag": "m^2/s"}.get(field, "1")})


def plot_coupling_heatmap(dataset: ResultDataset, path: Path, *, field: str = "eta") -> dict[str, Any]:
    plt, _ = _pyplot()
    values = np.asarray(dataset.coupling_field(field))
    times = np.asarray(dataset.coupling_field("time"))
    s = np.asarray(dataset.coupling_field("s"))
    fig, ax = plt.subplots(figsize=(5.8, 4.0), constrained_layout=True)
    image = ax.imshow(values, aspect="auto", origin="lower", extent=[float(s.min()), float(s.max()), float(times.min()), float(times.max())], cmap="magma")
    fig.colorbar(image, ax=ax, label={"eta": "eta (m)", "qn": "qn (m^2/s)"}.get(field, field))
    ax.set_title(f"Regional2D coupling {field}")
    ax.set_xlabel("section coordinate s (m)")
    ax.set_ylabel("time (s)")
    _label_if_synthetic(ax, dataset)
    return _finish_figure(fig, path, dataset, figure_type="coupling_heatmap", fields=[field, "s", "time"], time_range=[float(times.min()), float(times.max())], units={field: {"eta": "m", "qn": "m^2/s"}.get(field, "1"), "s": "m", "time": "s"})


def plot_qn_history(datasets: Sequence[ResultDataset], path: Path) -> dict[str, Any]:
    plt, _ = _pyplot()
    fig, ax = plt.subplots(figsize=(5.8, 3.8), constrained_layout=True)
    fields = ["Qn", "time"]
    for dataset in datasets:
        label = str(dataset.metadata().get("run_id", "result"))
        ax.plot(dataset.coupling_field("time"), dataset.coupling_series("Qn"), marker="o", linewidth=1.7, label=label)
        _label_if_synthetic(ax, dataset)
    ax.set_title("Regional2D Qn Forcing History")
    ax.set_xlabel("time (s)")
    ax.set_ylabel("Qn (m^3/s)")
    ax.grid(True, alpha=0.3)
    ax.legend(frameon=False)
    return _finish_figure(fig, path, datasets[0], figure_type="Qn_history", fields=fields, time_range=[float(datasets[0].coupling_field("time").min()), float(datasets[0].coupling_field("time").max())], units={"Qn": "m^3/s", "time": "s"})


def plot_convergence(path: Path, *, data_class: str = "SYNTHETIC") -> dict[str, Any]:
    plt, _ = _pyplot()
    h = np.asarray([400.0, 300.0, 225.0])
    error = np.asarray([0.08, 0.035, 0.016])
    fig, ax = plt.subplots(figsize=(5.6, 3.8), constrained_layout=True)
    ax.loglog(h, error * 100.0, marker="o", linewidth=1.7, label="PoC event-error trend")
    ax.axhline(2.0, color="#cf222e", linestyle="--", label="2% qualification threshold")
    ax.invert_xaxis()
    ax.set_title("Regional2D Convergence PoC")
    ax.set_xlabel("actual h (m)")
    ax.set_ylabel("NRMSE (%)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(frameon=False)
    if data_class == "SYNTHETIC":
        ax.text(0.02, 0.98, "SYNTHETIC / PROOF OF CONCEPT", transform=ax.transAxes, va="top", ha="left", fontsize=8, color="#8a4600", bbox={"facecolor": "#fff4d6", "edgecolor": "#d4a72c", "boxstyle": "round,pad=0.25"})
    dummy = SyntheticResultDataset()
    return _finish_figure(fig, path, dummy, figure_type="convergence", fields=["h", "error"], units={"h": "m", "error": "%"})


def plot_method_comparison(datasets: Sequence[ResultDataset], path: Path) -> dict[str, Any]:
    plt, _ = _pyplot()
    fig, ax = plt.subplots(figsize=(5.8, 3.8), constrained_layout=True)
    for dataset in datasets:
        label = str(dataset.metadata().get("run_id", "result"))
        ax.plot(dataset.coupling_field("time"), dataset.coupling_series("Qn"), marker="o", linewidth=1.7, label=label)
        _label_if_synthetic(ax, dataset)
    ax.set_title("Regional2D Method Comparison PoC")
    ax.set_xlabel("time (s)")
    ax.set_ylabel("Qn (m^3/s)")
    ax.grid(True, alpha=0.3)
    ax.legend(frameon=False)
    return _finish_figure(fig, path, datasets[0], figure_type="method_comparison", fields=["Qn", "time"], units={"Qn": "m^3/s", "time": "s"})


def generate_synthetic_poc(run_root: Path) -> dict[str, Any]:
    dataset = SyntheticResultDataset()
    figures_root = run_root / "figures"
    outputs = [
        plot_mesh(dataset, figures_root / "mesh" / "synthetic_mesh.svg"),
        plot_scalar_field(dataset, figures_root / "fields" / "synthetic_h_t1.svg", field="h", time=1),
        plot_scalar_field(dataset, figures_root / "fields" / "synthetic_eta_t1.svg", field="eta", time=1),
        plot_scalar_field(dataset, figures_root / "fields" / "synthetic_qmag_t1.svg", field="qmag", time=1),
        plot_coupling_heatmap(dataset, figures_root / "coupling" / "synthetic_eta_heatmap.svg", field="eta"),
        plot_qn_history([dataset], figures_root / "coupling" / "synthetic_Qn_history.svg"),
        plot_convergence(figures_root / "convergence" / "synthetic_error_vs_h.svg"),
        plot_method_comparison([dataset], figures_root / "comparisons" / "synthetic_method_comparison.svg"),
    ]
    manifest = {
        "schema": {"name": "tsunami.regional2d.figure_index", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "source_case_id": dataset.metadata().get("case_id"),
        "source_run_id": dataset.metadata().get("run_id"),
        "data_classification": "SYNTHETIC",
        "figures": outputs,
        "publication_directory_policy": "reserved; no automatic outputs",
    }
    _write_json(figures_root / "index.json", manifest)
    return manifest


def _main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    synthetic = sub.add_parser("generate-synthetic-poc")
    synthetic.add_argument("run_root", type=Path)
    args = parser.parse_args(argv)
    if args.command == "generate-synthetic-poc":
        print(json.dumps(generate_synthetic_poc(args.run_root), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
