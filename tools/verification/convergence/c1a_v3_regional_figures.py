#!/usr/bin/env python3
"""Generate committed C1A-R3 Regional2D convergence summaries and SVG figures."""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence


STUDY_ID = "regional-spatial-fine-resolution-v3"
SECTION_ID = "kamaishi-nearshore-interface"
RUN_ID = "kamaishi-etopo-usgs-v1"
FORCING_WINDOW_S = (245.0, 545.0)
SECTION_WIDTH_M = 8000.0
COMMON_SUPPORT_COUNT = 401
R2_PEAK_ETA_RICHARDSON_GCI = {
    "difference_ratio": 5.889006674865321,
    "gci21_percent": 0.2230768044504527,
    "h_fine_to_coarse_m": [
        638.4873841390533,
        902.2121959966825,
        1237.7047845399395,
    ],
    "observed_order": 5.703540827117854,
    "richardson_extrapolated": 0.8642828346896743,
    "status": "computed",
    "values_fine_to_coarse": [
        0.8627431707728945,
        0.8532204454315746,
        0.7971410523336324,
    ],
}
COLORS = {
    "h1000": "#4c78a8",
    "h900": "#f58518",
    "h800": "#54a24b",
    "eta": "#4c78a8",
    "qn": "#f58518",
    "Qn": "#54a24b",
}


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


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def repo_relative(path: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root()).as_posix()
    except ValueError:
        return str(path)


def coupling_dir(case_root: Path) -> Path:
    return case_root / "runs" / RUN_ID / "outputs/regional2d/coupling" / SECTION_ID


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
                coords = []
                for offset in range(node_count):
                    coords.append(tuple(float(value) for value in lines[index + offset].split()[:3]))
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
        raise RuntimeError(f"could not find physical boundary {boundary_name!r} in {mesh_path}")
    line_elements: list[tuple[float, float, float]] = []
    for entity_dim, entity_tag, element_type, _, node_tags in elements:
        if entity_dim == 1 and entity_tag in boundary_entities and element_type == 1:
            a = nodes[node_tags[0]]
            b = nodes[node_tags[1]]
            line_elements.append(((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, math.hypot(a[0] - b[0], a[1] - b[1])))
    if len(line_elements) != len(samples):
        raise RuntimeError(f"{mesh_path} has {len(line_elements)} boundary faces, but metadata has {len(samples)} samples")
    matched: dict[int, float] = {}
    available = list(line_elements)
    for sample in samples:
        sx = float(sample["x_m"])
        sy = float(sample["y_m"])
        best_index = min(range(len(available)), key=lambda i: math.hypot(available[i][0] - sx, available[i][1] - sy))
        cx, cy, length = available.pop(best_index)
        distance = math.hypot(cx - sx, cy - sy)
        if distance > 1.0e-6:
            raise RuntimeError(f"could not match boundary face centre within tolerance for {mesh_path}")
        matched[int(sample["local_index"])] = length
    return matched


@dataclass(frozen=True)
class LevelData:
    level_id: str
    case_root: Path
    samples: list[dict[str, str]]
    metadata: dict[str, Any]
    corridor: dict[str, Any]
    face_lengths_m: dict[int, float]
    offset_by_index_m: dict[int, float]
    baseline_by_index: dict[int, dict[str, str]]
    series: list[dict[str, float]]


def derive_series(level_id: str, case_root: Path) -> LevelData:
    cdir = coupling_dir(case_root)
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
    face_lengths = parse_boundary_face_lengths(case_root / "meshes/kamaishi-regional.msh", "boundary.inland", metadata["samples"])
    by_time: dict[float, list[dict[str, str]]] = {}
    for row in samples:
        by_time.setdefault(float(row["time"]), []).append(row)
    baseline_time = min(by_time)
    baseline = {int(row["local_index"]): row for row in by_time[baseline_time]}
    series: list[dict[str, float]] = []
    for time in sorted(by_time):
        rows = by_time[time]
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
        signed_eta = max(eta_values, key=lambda value: abs(value))
        signed_qn = max(qn_values, key=lambda value: abs(value))
        series.append({"time_s": time, "eta_m": signed_eta, "qn_m2_per_s": signed_qn, "Qn_m3_per_s": Qn, "qbar_m2_per_s": Qn / SECTION_WIDTH_M})
    return LevelData(level_id, case_root, samples, metadata, corridor, face_lengths, offsets, baseline, series)


def rows_in_window(series: Sequence[dict[str, float]]) -> list[dict[str, float]]:
    start, end = FORCING_WINDOW_S
    return [row for row in series if start - 1.0e-9 <= row["time_s"] <= end + 1.0e-9]


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


def common_support() -> list[float]:
    start = -SECTION_WIDTH_M * 0.5
    step = SECTION_WIDTH_M / (COMMON_SUPPORT_COUNT - 1)
    return [start + step * index for index in range(COMMON_SUPPORT_COUNT)]


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
        elif quantity == "bed":
            value = float(row["bed_elevation"])
        else:
            raise ValueError(quantity)
        points.append((level.offset_by_index_m[local_index], value))
    return points


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
    if abs(value) >= 1000:
        return f"{value:.0f}"
    if abs(value) >= 10:
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
    parts.extend(
        [
            f'<line x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}" stroke="#333" stroke-width="1.4"/>',
            f'<line x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}" stroke="#333" stroke-width="1.4"/>',
        ]
    )
    for level, point_text in points.items():
        parts.append(f'<polyline fill="none" stroke="{colors[level]}" stroke-width="3" stroke-linejoin="round" stroke-linecap="round" points="{point_text}"/>')
    legend_x = 102
    legend_y = 76
    for offset, level in enumerate(series_by_level):
        y = legend_y + offset * 24
        parts.append(f'<line x1="{legend_x}" y1="{y}" x2="{legend_x+28}" y2="{y}" stroke="{colors[level]}" stroke-width="4"/>')
        parts.append(f'<text x="{legend_x+38}" y="{y+5}" font-family="Arial, sans-serif" font-size="14" fill="#222">{html.escape(level)}</text>')
    parts.append(f'<text x="{(left+width-right)/2:.1f}" y="{height-28}" text-anchor="middle" font-family="Arial, sans-serif" font-size="15" fill="#333">{html.escape(x_label)}</text>')
    parts.append(f'<text transform="translate(25 {(top+height-bottom)/2:.1f}) rotate(-90)" text-anchor="middle" font-family="Arial, sans-serif" font-size="15" fill="#333">{html.escape(y_label)}</text>')
    if extra_note:
        parts.append(f'<text x="{left}" y="{height-8}" font-family="Arial, sans-serif" font-size="12" fill="#666">{html.escape(extra_note)}</text>')
    parts.append("</svg>")
    output.write_text("\n".join(parts) + "\n", encoding="utf-8")


def svg_runtime_plot(output: Path, metrics: dict[str, Any]) -> None:
    comparisons = metrics["comparisons"]
    rows = [
        {"label": "h900 vs h1000", "runtime_s": metrics["levels"]["h900"]["runtime_wall_clock_s"], "eta": comparisons["h900_vs_h1000"]["eta_waveform"]["nrmse"] * 100.0, "qn": comparisons["h900_vs_h1000"]["qn_waveform"]["nrmse"] * 100.0, "Qn": comparisons["h900_vs_h1000"]["Qn_waveform"]["nrmse"] * 100.0},
        {"label": "h800 vs h900", "runtime_s": metrics["levels"]["h800"]["runtime_wall_clock_s"], "eta": comparisons["h800_vs_h900"]["eta_waveform"]["nrmse"] * 100.0, "qn": comparisons["h800_vs_h900"]["qn_waveform"]["nrmse"] * 100.0, "Qn": comparisons["h800_vs_h900"]["Qn_waveform"]["nrmse"] * 100.0},
    ]
    series = {
        key: [{"runtime_s": row["runtime_s"], "nrmse_percent": row[key]} for row in rows]
        for key in ("eta", "qn", "Qn")
    }
    svg_line_plot(
        output,
        "C1A-R3 Runtime vs Forcing Error",
        "Waveform NRMSE against the next coarser Regional2D level plotted against full-run wall time.",
        "Fine-level wall clock (s)",
        "Waveform NRMSE (%)",
        series,
        "nrmse_percent",
        COLORS,
        x_key="runtime_s",
        extra_note="2% target exceeded by the h800 vs h900 medium-to-fine forcing comparison.",
    )
    text = output.read_text(encoding="utf-8")
    # Add the 2% target line after rendering, using the existing plot bounds would overcomplicate the simple SVG helper.
    # The provenance sidecar records the threshold exactly.
    output.write_text(text, encoding="utf-8")


def provenance_payload(figure: Path, quantity: str, metrics_path: Path, levels: dict[str, LevelData]) -> dict[str, Any]:
    return {
        "schema": {"name": "tsunami.figure_provenance", "version": "1.0.0"},
        "figure": repo_relative(figure),
        "generated_at_utc": utc_now(),
        "renderer": "stdlib_svg",
        "study_id": STUDY_ID,
        "quantity": quantity,
        "forcing_window_s": list(FORCING_WINDOW_S),
        "section_width_m": SECTION_WIDTH_M,
        "common_support_point_count": COMMON_SUPPORT_COUNT,
        "source_metrics": str(metrics_path),
        "source_inputs": {
            level_id: {
                "case_root": str(level.case_root),
                "samples_csv_sha256": sha256(coupling_dir(level.case_root) / "samples.csv"),
                "metadata_json_sha256": sha256(coupling_dir(level.case_root) / "metadata.json"),
                "mesh_msh_sha256": sha256(level.case_root / "meshes/kamaishi-regional.msh"),
            }
            for level_id, level in levels.items()
        },
    }


def generate_figures(external_root: Path, figure_root: Path) -> dict[str, Any]:
    metrics_path = external_root / "fine_resolution_v3_metrics.json"
    metrics = read_json(metrics_path)
    h1000_case = external_root.parent / "regional/spatial/L0/case"
    levels = {
        "h1000": derive_series("h1000", h1000_case),
        "h900": derive_series("h900", external_root / "spatial/h900/case"),
        "h800": derive_series("h800", external_root / "spatial/h800/case"),
    }
    figure_root.mkdir(parents=True, exist_ok=True)
    outputs: list[dict[str, str]] = []
    figure_specs = [
        ("c1a_v3_eta_waveform.svg", "C1A-R3 eta Waveform Convergence", "Maximum signed free-surface perturbation over the coupling section.", "eta perturbation (m)", "eta_m", "eta waveform"),
        ("c1a_v3_qn_waveform.svg", "C1A-R3 qn Waveform Convergence", "Maximum signed normal-momentum perturbation over the coupling section.", "qn perturbation (m^2/s)", "qn_m2_per_s", "qn waveform"),
        ("c1a_v3_Qn_waveform.svg", "C1A-R3 Qn Waveform Convergence", "Integrated normal discharge over the coupling section.", "Qn (m^3/s)", "Qn_m3_per_s", "Qn waveform"),
    ]
    for filename, title, desc, ylabel, key, quantity in figure_specs:
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
            extra_note="Classification: not_spatially_qualified; formal window 245-545 s.",
        )
        provenance = figure.with_suffix(".provenance.json")
        write_json(provenance, provenance_payload(figure, quantity, metrics_path, levels))
        outputs.append({"figure": repo_relative(figure), "provenance": repo_relative(provenance)})
    crest_time = float(metrics["levels"]["h800"]["forcing_window_qoi"]["peak_Qn_time_s"])
    support = common_support()
    profile_series: dict[str, list[dict[str, float]]] = {}
    bed_series: dict[str, list[dict[str, float]]] = {}
    for level_id, level in levels.items():
        profile = interpolate_profile(profile_at_time(level, crest_time, "qn"), support)
        bed = interpolate_profile(profile_at_time(level, 0.0, "bed"), support)
        profile_series[level_id] = [{"offset_m": x, "qn_m2_per_s": y} for x, y in zip(support, profile)]
        bed_series[level_id] = [{"offset_m": x, "bed_m": y} for x, y in zip(support, bed)]
    for filename, title, desc, ylabel, key, series, quantity in [
        ("c1a_v3_section_profile_principal_crest.svg", "C1A-R3 Section Profile at Principal Crest", f"Normal-momentum perturbation interpolated onto the fixed 8 km support at t={crest_time:g} s.", "qn perturbation (m^2/s)", "qn_m2_per_s", profile_series, "section qn profile"),
        ("c1a_v3_bed_profile.svg", "C1A-R3 Bed-Profile Convergence", "Coupling-section bed elevation interpolated onto the fixed 8 km support.", "bed elevation (m)", "bed_m", bed_series, "bed profile"),
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
        provenance = figure.with_suffix(".provenance.json")
        payload = provenance_payload(figure, quantity, metrics_path, levels)
        payload["profile_time_s"] = crest_time if "section_profile" in filename else 0.0
        write_json(provenance, payload)
        outputs.append({"figure": repo_relative(figure), "provenance": repo_relative(provenance)})
    runtime_figure = figure_root / "c1a_v3_runtime_vs_error.svg"
    svg_runtime_plot(runtime_figure, metrics)
    runtime_provenance = runtime_figure.with_suffix(".provenance.json")
    payload = provenance_payload(runtime_figure, "runtime vs forcing waveform error", metrics_path, levels)
    payload["threshold_percent"] = 2.0
    payload["comparisons"] = ["h900_vs_h1000", "h800_vs_h900"]
    write_json(runtime_provenance, payload)
    outputs.append({"figure": repo_relative(runtime_figure), "provenance": repo_relative(runtime_provenance)})
    manifest = {
        "schema": {"name": "tsunami.c1a_v3_figure_manifest", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "classification": metrics["qualification"]["status"],
        "source_metrics": str(metrics_path),
        "outputs": outputs,
    }
    write_json(figure_root / "c1a_v3_figure_manifest.json", manifest)
    write_json(
        figure_root / "c1a_convergence_not_generated.json",
        {
            "schema": {"name": "tsunami.c1a_convergence_figure_provenance", "version": "1.2.0"},
            "status": "generated_fine_resolution_v3_figures",
            "classification": metrics["qualification"]["status"],
            "generated_at_utc": utc_now(),
            "manifest": repo_relative(figure_root / "c1a_v3_figure_manifest.json"),
            "source_metrics": str(metrics_path),
            "legacy_resource_aware_figure": "deliverables/figures/convergence/c1a_resource_aware_regional_spatial_section_qn.svg",
        },
    )
    return manifest


def pilot_summary(path: Path) -> dict[str, Any]:
    record = read_json(path)
    summary = {
        "level_id": record["level_id"],
        "status": record["status"],
        "requested_solver_spacing_m": record.get("requested_solver_spacing_m"),
        "mesh_scan": record.get("mesh_scan"),
        "wall_clock_s": record.get("wall_clock_s"),
        "projected_600s_wall_s": record.get("projected_600s_wall_s"),
        "resource_usage": record.get("resource_usage"),
        "timestep": record.get("timestep"),
    }
    if record.get("status") == "pilot_failed":
        log_tail = str(record.get("r2d_log_tail", ""))
        cause = ""
        for line in log_tail.splitlines():
            if line.startswith("cause_code="):
                cause = line.split("=", 1)[1]
                break
        summary["pilot_final_time_s"] = record.get("pilot_final_time_s")
        summary["failure_cause_code"] = cause or None
        summary["state_changed"] = False if "state_changed=false" in log_tail else None
    else:
        summary["simulated_duration_s"] = record.get("simulated_duration_s")
    return {key: value for key, value in summary.items() if value is not None}


def update_docs(external_root: Path, docs_root: Path, figure_root: Path, manifest: dict[str, Any]) -> None:
    metrics = read_json(external_root / "fine_resolution_v3_metrics.json")
    mesh_scan = read_json(external_root / "mesh_candidate_scan.json")
    old_summary_path = docs_root / "regional_convergence_summary.json"
    summary = {
        "schema": {"name": "tsunami.c1a_regional_convergence_summary", "version": "1.2.0"},
        "generated_at_utc": utc_now(),
        "study_id": STUDY_ID,
        "r3_mesh_contract_commit": "1770b37",
        "mesh_resolution_contract": {
            "cause_of_previous_750_to_500_mapping": "The R1 L1 external profile explicitly set profile.spacing_m=750.0 and profile.mesh_size_m=500.0. The solver honoured mesh_size_m=500.0; this was not a Gmsh rounding or snapping effect.",
            "corrective_action": "C1A-R3 records separate terrain_source_resolution, terrain_processing_resolution, solver_mesh_target_size, and actual_characteristic_mesh_size, and asserts that requested solver mesh equals configured mesh unless an explicit tier mapping is declared.",
            "regression_tested": True,
        },
        "previous_attempts_retained": {
            "attempt_1": {
                "path": "/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional/spatial",
                "summary": "h1000 completed; nominal h750 was actually mesh_size_m=500 and resource-limited; h500 was not started.",
            },
            "attempt_2": {
                "path": "/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional/resource_aware_v2",
                "classification": "not_spatially_qualified_resource_aware",
                "retained_peak_eta_m": {
                    "h2000": 0.7971410523336324,
                    "h1400": 0.8532204454315746,
                    "h1000": 0.8627431707728945,
                },
                "retained_h1400_to_h1000_waveform_nrmse": {
                    "eta": 0.2202143796463868,
                    "qn": 0.38717614517697835,
                    "three_support_qn": 0.4243704422342331,
                },
                "retained_peak_qn_m2_per_s": {
                    "h2000": 3.331570023009954,
                    "h1400": 2.3909292494294663,
                    "h1000": 4.2421347278949835,
                },
                "peak_eta_richardson_gci": R2_PEAK_ETA_RICHARDSON_GCI,
            },
        },
        "external_root": str(external_root),
        "mesh_candidate_scan": {
            "external_record": str(external_root / "mesh_candidate_scan.json"),
            "candidates": [
                {
                    "level_id": candidate["level_id"],
                    "requested_solver_spacing_m": candidate["requested_solver_spacing_m"],
                    "solver_mesh_target_size_m": candidate["solver_mesh_target_size_m"],
                    "terrain_processing_resolution_m": candidate["terrain_processing_resolution_m"],
                    "active_cells": candidate["active_cells"],
                    "total_cells": candidate["total_cells"],
                    "actual_characteristic_mesh_size_m": candidate["actual_characteristic_mesh_size_m"],
                    "memory_estimate_mib": candidate["memory_estimate_mib"],
                    "tier_mapping": candidate["resolution_contract"]["tier_mapping"],
                }
                for candidate in mesh_scan["candidates"]
            ],
            "h750_expected_active_cells_from_h1000_reference": mesh_scan["expected_750_active_cells_from_1000_reference"],
        },
        "pilots": {
            "h900": pilot_summary(external_root / "pilots/h900/pilot.json"),
            "h800": pilot_summary(external_root / "pilots/h800/pilot.json"),
            "h650": pilot_summary(external_root / "pilots/h650/pilot.json"),
            "h750": {
                "status": "interrupted_non_evidence",
                "record": str(external_root / "pilots/h750/pilot.json"),
            },
        },
        "h1000_reuse_proof": metrics["h1000_reuse_proof"],
        "spatial": {
            "classification": metrics["qualification"]["status"],
            "qualification": metrics["qualification"],
            "selected_ladder": metrics["selected_ladder"],
            "section_integration": metrics["section_integration"],
            "common_support": metrics["common_support"],
            "levels": metrics["levels"],
            "comparisons": metrics["comparisons"],
            "richardson_gci": metrics["richardson_gci"],
            "dominant_residual_error_mechanism": metrics["dominant_residual_error_mechanism"],
            "figures": manifest["outputs"],
        },
        "production_discretisation": {
            "selected": False,
            "reason": "C1A-R3 medium-to-fine forcing waveform changes exceed the 2% spatial qualification target; no Regional2D production mesh is selected.",
        },
        "temporal": metrics["temporal_gate"],
        "physical_parameter_invariance": {
            "terrain_source_unchanged": True,
            "domain_unchanged_for_selected_ladder": True,
            "no_observations_used": metrics["no_observations_used"],
            "no_calibration_performed": metrics["no_calibration_performed"],
            "local3d_not_started": metrics["local3d_not_started"],
        },
        "fabricated_results": False,
    }
    write_json(old_summary_path, summary)
    spatial_rows: list[dict[str, Any]] = []
    for level_id in ("h1000", "h900", "h800"):
        level = metrics["levels"][level_id]
        timestep = level["timestep"]
        qoi = level["forcing_window_qoi"]
        spatial_rows.append(
            {
                "level_id": level_id,
                "status": "passed",
                "study_id": STUDY_ID,
                "requested_solver_spacing_m": level["requested_solver_spacing_m"],
                "solver_mesh_target_size_m": level["requested_solver_spacing_m"],
                "active_cells": level["active_cells"],
                "total_cells": level.get("total_cells"),
                "actual_characteristic_mesh_size_m": level["actual_characteristic_mesh_size_m"],
                "achieved_final_time_s": 600.0,
                "wall_clock_s": level.get("runtime_wall_clock_s"),
                "peak_memory_kb": (level.get("resource_usage") or {}).get("peak_memory_kb"),
                "diagnostics_rows": timestep["diagnostics_rows"],
                "step_count": timestep["step_count"],
                "min_dt_s": timestep["minimum_dt_s"],
                "mean_dt_s": timestep["mean_dt_s"],
                "median_dt_s": timestep["median_dt_s"],
                "max_dt_s": timestep["maximum_dt_s"],
                "rejected_attempts_total": timestep.get("rejected_attempts_total"),
                "source_restriction_rows": timestep.get("source_restriction_rows"),
                "forcing_window_peak_eta_m": qoi["peak_eta_abs_m"],
                "peak_eta_time_s": qoi["peak_eta_time_s"],
                "forcing_window_peak_qn_m2_per_s": qoi["peak_qn_abs_m2_per_s"],
                "peak_qn_time_s": qoi["peak_qn_time_s"],
                "forcing_window_peak_Qn_m3_per_s": qoi["peak_Qn_abs_m3_per_s"],
                "peak_Qn_time_s": qoi["peak_Qn_time_s"],
                "forcing_window_peak_qbar_m2_per_s": qoi["peak_qbar_abs_m2_per_s"],
                "peak_qbar_time_s": qoi["peak_qbar_time_s"],
                "wet_cell_min": level["wetdry"]["wet_cell_min"],
                "wet_cell_max": level["wetdry"]["wet_cell_max"],
                "classification_note": "valid C1A-R3 spatial level; waveform qualification failed",
            }
        )
    for level_id in ("h850", "h750", "h700", "h650", "h600"):
        candidate = next(item for item in mesh_scan["candidates"] if item["level_id"] == level_id)
        note = "mesh-only candidate"
        status = "mesh_preflight_generated"
        if level_id == "h650":
            status = "pilot_failed_preflight"
            note = "pilot failed C++ preflight: r2d.preflight.terrain_support_missing"
        if level_id == "h750":
            status = "interrupted_non_evidence"
            note = "broad pilot interrupted; incomplete record is not convergence evidence"
        spatial_rows.append(
            {
                "level_id": level_id,
                "status": status,
                "study_id": STUDY_ID,
                "requested_solver_spacing_m": candidate["requested_solver_spacing_m"],
                "solver_mesh_target_size_m": candidate["solver_mesh_target_size_m"],
                "active_cells": candidate["active_cells"],
                "total_cells": candidate["total_cells"],
                "actual_characteristic_mesh_size_m": candidate["actual_characteristic_mesh_size_m"],
                "classification_note": note,
            }
        )
    spatial_fields = [
        "level_id",
        "status",
        "study_id",
        "requested_solver_spacing_m",
        "solver_mesh_target_size_m",
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
        "wet_cell_min",
        "wet_cell_max",
        "classification_note",
    ]
    write_csv(docs_root / "regional_spatial_convergence.csv", spatial_fields, spatial_rows)
    temporal_rows = [
        {
            "level_id": level_id,
            "status": "not_started",
            "cfl_target": "",
            "maximum_timestep_s": "",
            "reason": "Temporal convergence remains gated because C1A-R3 Regional2D spatial qualification did not pass.",
        }
        for level_id in ("T0", "T1", "T2")
    ]
    write_csv(docs_root / "regional_temporal_convergence.csv", ["level_id", "status", "cfl_target", "maximum_timestep_s", "reason"], temporal_rows)
    production_md = f"""# C1A-R3 Regional2D Production Discretisation Decision

Status: not selected.

C1A-R3 corrected the fine-mesh construction path by separating terrain source resolution, terrain processing resolution, solver mesh target size, and actual characteristic mesh size. The previous nominal 750 m anomaly was traced to an explicit external profile with `profile.spacing_m = 750.0` and `profile.mesh_size_m = 500.0`; it was not Gmsh rounding or snapping.

The new study identity is `{STUDY_ID}`. Corrected mesh-only candidates preserved the requested solver target: h900, h850, h800, h750, h700, h650, and h600. The h750 candidate produced 4008 active cells, close to the expected 4302 active cells from h1000 scaling and far from the previous 9320-cell 500 m solve.

The completed spatial ladder is h1000 reused from C1A-R1 plus new h900 and h800 full 600 s Regional2D runs. Actual characteristic mesh sizes are {metrics['levels']['h1000']['actual_characteristic_mesh_size_m']:.3f} m, {metrics['levels']['h900']['actual_characteristic_mesh_size_m']:.3f} m, and {metrics['levels']['h800']['actual_characteristic_mesh_size_m']:.3f} m. The h900 and h800 runs completed in {metrics['levels']['h900']['runtime_wall_clock_s']:.3f} s and {metrics['levels']['h800']['runtime_wall_clock_s']:.3f} s.

Spatial convergence did not qualify. For the medium-to-fine h900 to h800 pair, eta waveform NRMSE is {metrics['comparisons']['h800_vs_h900']['eta_waveform']['nrmse']:.6f}, qn waveform NRMSE is {metrics['comparisons']['h800_vs_h900']['qn_waveform']['nrmse']:.6f}, and integrated Qn waveform NRMSE is {metrics['comparisons']['h800_vs_h900']['Qn_waveform']['nrmse']:.6f}; all exceed the 2% forcing-waveform target. The dominant residual mechanisms are {', '.join(metrics['dominant_residual_error_mechanism'])}.

No production Regional2D mesh or timestep policy is selected. Temporal convergence remains gated. No observations were used, no physics calibration was performed, and no Local3D convergence was started.

ETOPO caveat: these mesh changes measure discretisation behaviour of the interpolated ETOPO-derived terrain representation; they do not add bathymetric information beyond the source product.
"""
    (docs_root / "regional_production_discretisation.md").write_text(production_md, encoding="utf-8")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--external-root",
        type=Path,
        default=Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional-spatial-fine-resolution-v3"),
    )
    parser.add_argument("--figure-root", type=Path, default=repo_root() / "deliverables/figures/convergence")
    parser.add_argument(
        "--docs-root",
        type=Path,
        default=repo_root() / "docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    manifest = generate_figures(args.external_root, args.figure_root)
    update_docs(args.external_root, args.docs_root, args.figure_root, manifest)
    print(json.dumps({"status": "generated", "manifest": manifest}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
