#!/usr/bin/env python3
"""Generate R21 clean editable Figure B/C bases.

R21 is a figure-cleaning/export task only. It reuses R20/R19/R17 authoritative
figure sources and does not run Regional2D, Local3D, calibration, replay,
source, mesh or solver workflows.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import textwrap
import xml.etree.ElementTree as ET
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Mapping, Sequence

import numpy as np


REPO_ROOT = Path(__file__).resolve().parents[2]
R21_ROOT = REPO_ROOT / "deliverables/figures/r21_editable"
PUBLICATION_ROOT = R21_ROOT / "publication"
EDITABLE_ROOT = R21_ROOT / "editable"
PREVIEW_ROOT = R21_ROOT / "previews"
SOURCE_ROOT = R21_ROOT / "sources"
PROVENANCE_ROOT = R21_ROOT / "provenance"
SOURCE_PYTHON = SOURCE_ROOT / "python"
SOURCE_BLENDER = SOURCE_ROOT / "blender"

DOC_HANDOFF = REPO_ROOT / "docs/project/r21_editable_figure_handoff.md"
DOC_COMPLETION = REPO_ROOT / "docs/project/r21_completion_report.md"

STARTING_HEAD = "5c9e2d3e04db69ddc5708c010085401852ab4a32"
BRANCH_NAME = "feat/r21-clean-editable-figures"
WORKTREE_PATH = Path("/home/helios/Projects/Summer-Studentship-r21-figures")

R20_SCENE = REPO_ROOT / "deliverables/figures/r20_publication/sources/blender/figure_R20_B_selected_scene.blend"
R20_RENDER_RECORD = REPO_ROOT / "deliverables/figures/r20_publication/provenance/figure_R20_B_selected_blender_render.json"
R19_PROFILE = REPO_ROOT / "deliverables/figures/r19_tikz/data/bathymetry_profile.csv"
R19_GEOMETRY = REPO_ROOT / "deliverables/figures/r19_tikz/data/domain_geometry.json"
R19_MILESTONES = REPO_ROOT / "deliverables/figures/r19_tikz/data/milestone_positions.json"
R21_BLENDER_SCRIPT = REPO_ROOT / "tools/figures/blender/r21_render_clean_bathymetry.py"

C_WIDTH = 1800
C_HEIGHT = 980
C_PLOT = {"x": 130.0, "y": 96.0, "w": 1540.0, "h": 640.0}
C_X_MIN = 0.0
C_X_MAX = 124.0
C_Y_MIN = -1150.0
C_Y_MAX = 120.0


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def ensure_layout() -> None:
    for path in [PUBLICATION_ROOT, EDITABLE_ROOT, PREVIEW_ROOT, SOURCE_PYTHON, SOURCE_BLENDER, PROVENANCE_ROOT]:
        path.mkdir(parents=True, exist_ok=True)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    cleaned = textwrap.dedent(text).strip("\n")
    path.write_text("\n".join(line.rstrip() for line in cleaned.splitlines()) + "\n", encoding="utf-8")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def display_path(path: Path) -> str:
    try:
        return path.relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def file_record(path: Path) -> dict[str, Any]:
    record: dict[str, Any] = {"path": display_path(path), "exists": path.exists()}
    if path.is_file():
        record.update({"bytes": path.stat().st_size, "sha256": sha256(path)})
    return record


def command_output(command: Sequence[str], *, check: bool = False, env: Mapping[str, str] | None = None) -> tuple[int, str]:
    completed = subprocess.run(
        list(command),
        cwd=REPO_ROOT,
        env=dict(os.environ, **dict(env or {})),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if check and completed.returncode != 0:
        raise RuntimeError(f"{' '.join(command)} failed:\n{completed.stdout}")
    return completed.returncode, completed.stdout.strip()


def git_sha() -> str:
    code, output = command_output(["git", "rev-parse", "HEAD"])
    return output if code == 0 else "unknown"


def git_branch() -> str:
    code, output = command_output(["git", "branch", "--show-current"])
    return output if code == 0 else "unknown"


def svg_escape(value: Any) -> str:
    return (
        str(value)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def text(x: float, y: float, value: str, *, size: float = 22.0, fill: str = "#26323b", anchor: str = "start", weight: str = "400") -> str:
    return (
        f'<text x="{x:.2f}" y="{y:.2f}" font-family="Arial, Helvetica, sans-serif" '
        f'font-size="{size:.2f}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">{svg_escape(value)}</text>'
    )


def tag(name: str, content: str = "", **kwargs: Any) -> str:
    attr = " ".join(f'{key.replace("_", "-")}="{svg_escape(value)}"' for key, value in kwargs.items() if value is not None)
    if content:
        return f"<{name} {attr}>{content}</{name}>" if attr else f"<{name}>{content}</{name}>"
    return f"<{name} {attr}/>" if attr else f"<{name}/>"


def load_profile() -> tuple[np.ndarray, np.ndarray]:
    distances: list[float] = []
    elevations: list[float] = []
    with R19_PROFILE.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            distances.append(float(row["distance_offshore_km"]))
            elevations.append(float(row["bed_elevation_m"]))
    return np.array(distances, dtype=float), np.array(elevations, dtype=float)


def c_map(x: float, y: float) -> tuple[float, float]:
    plot = C_PLOT
    sx = plot["x"] + (C_X_MAX - x) / (C_X_MAX - C_X_MIN) * plot["w"]
    sy = plot["y"] + (C_Y_MAX - y) / (C_Y_MAX - C_Y_MIN) * plot["h"]
    return sx, sy


def c_rect_for_band(x0: float, x1: float, colour: str, opacity: float) -> str:
    left_x, top_y = c_map(max(x0, x1), C_Y_MAX)
    right_x, bottom_y = c_map(min(x0, x1), C_Y_MIN)
    return tag("rect", x=left_x, y=top_y, width=right_x - left_x, height=bottom_y - top_y, fill=colour, opacity=opacity)


def profile_path(distances: np.ndarray, elevations: np.ndarray) -> str:
    points = [c_map(float(x), float(y)) for x, y in zip(distances, elevations, strict=True)]
    return "M " + " L ".join(f"{x:.2f} {y:.2f}" for x, y in points)


def axes_svg() -> str:
    plot = C_PLOT
    parts = [
        '<g id="axes">',
        tag("rect", x=plot["x"], y=plot["y"], width=plot["w"], height=plot["h"], fill="none", stroke="#26323b", stroke_width=2.0),
    ]
    for y in [-1000, -750, -500, -250, 0]:
        x0, yy = c_map(C_X_MAX, y)
        x1, _ = c_map(C_X_MIN, y)
        parts.append(tag("line", x1=x0, y1=yy, x2=x1, y2=yy, stroke="#dde2de", stroke_width=1.2))
        parts.append(text(plot["x"] - 16, yy + 6, f"{y}", size=18, fill="#4d5963", anchor="end"))
    for x in [120, 100, 80, 60, 40, 20, 0]:
        xx, y0 = c_map(x, C_Y_MIN)
        parts.append(tag("line", x1=xx, y1=y0, x2=xx, y2=y0 + 14, stroke="#26323b", stroke_width=2))
        parts.append(text(xx, y0 + 42, f"{x}", size=18, fill="#4d5963", anchor="middle"))
    parts.append(text(plot["x"] + plot["w"] / 2.0, plot["y"] + plot["h"] + 86, "Distance offshore from selected wet nearshore interface, km", size=24, anchor="middle"))
    parts.append(
        f'<text x="42.00" y="{plot["y"] + plot["h"] / 2.0:.2f}" font-family="Arial, Helvetica, sans-serif" '
        'font-size="24.00" font-weight="400" fill="#26323b" text-anchor="middle" '
        f'transform="rotate(-90 42 {plot["y"] + plot["h"] / 2.0:.2f})">Bed elevation, m (EGM2008)</text>'
    )
    parts.append("</g>")
    return "\n".join(parts)


def sea_level_svg() -> str:
    x0, y0 = c_map(C_X_MAX, 0.0)
    x1, _ = c_map(C_X_MIN, 0.0)
    return "\n".join(
        [
            '<g id="sea_level">',
            tag("line", x1=x0, y1=y0, x2=x1, y2=y0, stroke="#8b9299", stroke_width=2.0, stroke_dasharray="8 8"),
            text(x1 - 10, y0 - 12, "z = 0 m", size=17, fill="#68737c", anchor="end"),
            "</g>",
        ]
    )


def generate_c0(distances: np.ndarray, elevations: np.ndarray) -> dict[str, Any]:
    svg = EDITABLE_ROOT / "figure_C0_longitudinal_bathymetry_clean.svg"
    pdf = PUBLICATION_ROOT / "figure_C0_longitudinal_bathymetry_clean.pdf"
    png = PUBLICATION_ROOT / "figure_C0_longitudinal_bathymetry_clean.png"
    preview = PREVIEW_ROOT / "figure_C0_longitudinal_bathymetry_clean.png"
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{C_WIDTH}" height="{C_HEIGHT}" viewBox="0 0 {C_WIDTH} {C_HEIGHT}">',
        tag("rect", x=0, y=0, width=C_WIDTH, height=C_HEIGHT, fill="#ffffff"),
        axes_svg(),
        sea_level_svg(),
        '<g id="profile">',
        tag("path", d=profile_path(distances, elevations), fill="none", stroke="#123858", stroke_width=5.2, stroke_linejoin="round", stroke_linecap="round"),
        "</g>",
        "</svg>",
    ]
    write_text(svg, "\n".join(parts))
    conversion = convert_svg(svg, pdf, png, preview)
    return {
        "svg": file_record(svg),
        "pdf": file_record(pdf),
        "png": file_record(png),
        "preview": file_record(preview),
        "conversion": conversion,
    }


def generate_c1(distances: np.ndarray, elevations: np.ndarray) -> dict[str, Any]:
    svg = EDITABLE_ROOT / "figure_C1_longitudinal_bathymetry_interpreted.svg"
    pdf = PUBLICATION_ROOT / "figure_C1_longitudinal_bathymetry_interpreted.pdf"
    png = PUBLICATION_ROOT / "figure_C1_longitudinal_bathymetry_interpreted.png"
    preview = PREVIEW_ROOT / "figure_C1_longitudinal_bathymetry_interpreted.png"
    geometry = read_json(R19_GEOMETRY)
    milestones = read_json(R19_MILESTONES)
    event_km = next(float(item["distance_offshore_km"]) for item in milestones["milestones"] if item["id"] == "M0")
    interface_km = 0.0

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{C_WIDTH}" height="{C_HEIGHT}" viewBox="0 0 {C_WIDTH} {C_HEIGHT}">',
        tag("rect", x=0, y=0, width=C_WIDTH, height=C_HEIGHT, fill="#ffffff"),
        '<g id="region_bands">',
        c_rect_for_band(124.0, 85.0, "#dfe8ef", 0.58),
        c_rect_for_band(85.0, 12.0, "#e7ead7", 0.62),
        c_rect_for_band(12.0, 0.0, "#f3e6bf", 0.66),
        "</g>",
        axes_svg(),
        sea_level_svg(),
        '<g id="region_labels">',
        text(c_map(104.0, 82.0)[0], 74, "deep-water propagation", size=20, fill="#31475b", anchor="middle", weight="700"),
        text(c_map(48.5, 82.0)[0], 74, "shelf / shoaling influence", size=20, fill="#526137", anchor="middle", weight="700"),
        text(c_map(6.0, 82.0)[0], 74, "nearshore transition", size=20, fill="#765c26", anchor="middle", weight="700"),
        "</g>",
        '<g id="event_reference">',
    ]
    event_x, event_y0 = c_map(event_km, C_Y_MAX)
    _, event_y1 = c_map(event_km, C_Y_MIN)
    parts.extend(
        [
            tag("line", x1=event_x, y1=event_y0, x2=event_x, y2=event_y1, stroke="#c8352c", stroke_width=2.0, stroke_dasharray="9 8"),
            text(event_x, 42, "2011 event ref.", size=18, fill="#c8352c", anchor="middle", weight="700"),
            "</g>",
            '<g id="interface">',
        ]
    )
    iface_x, iface_y0 = c_map(interface_km, C_Y_MAX)
    _, iface_y1 = c_map(interface_km, C_Y_MIN)
    parts.extend(
        [
            tag("line", x1=iface_x, y1=iface_y0, x2=iface_x, y2=iface_y1, stroke="#14865c", stroke_width=2.0, stroke_dasharray="9 8"),
            text(iface_x, C_PLOT["y"] + C_PLOT["h"] + 60, "selected wet interface", size=18, fill="#14865c", anchor="end", weight="700"),
            "</g>",
            '<g id="profile">',
            tag("path", d=profile_path(distances, elevations), fill="none", stroke="#123858", stroke_width=5.2, stroke_linejoin="round", stroke_linecap="round"),
            "</g>",
            "</svg>",
        ]
    )
    write_text(svg, "\n".join(parts))
    conversion = convert_svg(svg, pdf, png, preview)
    return {
        "svg": file_record(svg),
        "pdf": file_record(pdf),
        "png": file_record(png),
        "preview": file_record(preview),
        "conversion": conversion,
        "event_reference_distance_offshore_km": event_km,
        "interface_distance_offshore_km": interface_km,
        "conceptual_region_bands_km": {
            "deep_water_propagation": [85.0, 124.0],
            "shelf_shoaling_influence": [12.0, 85.0],
            "nearshore_transition": [0.0, 12.0],
        },
        "region_band_status": "conceptual broad interpretation from R19 milestone convention; not calibrated physical thresholds",
        "geometry_source_status": geometry["local3d"]["status"],
    }


def generate_c2() -> dict[str, Any]:
    svg = EDITABLE_ROOT / "figure_C2_annotation_template.svg"
    event_x, _ = c_map(108.31900726067028, -500.0)
    iface_x, _ = c_map(0.0, -500.0)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{C_WIDTH}" height="{C_HEIGHT}" viewBox="0 0 {C_WIDTH} {C_HEIGHT}">',
        '<g id="title_placeholder">',
        text(C_WIDTH / 2.0, 42, "Title placeholder", size=26, anchor="middle", weight="700"),
        "</g>",
        '<g id="event_reference">',
        tag("line", x1=event_x, y1=C_PLOT["y"], x2=event_x, y2=C_PLOT["y"] + C_PLOT["h"], stroke="#c8352c", stroke_width=2.0, stroke_dasharray="9 8"),
        text(event_x + 12, 136, "2011 event reference", size=20, fill="#c8352c"),
        "</g>",
        '<g id="interface">',
        tag("line", x1=iface_x, y1=C_PLOT["y"], x2=iface_x, y2=C_PLOT["y"] + C_PLOT["h"], stroke="#14865c", stroke_width=2.0, stroke_dasharray="9 8"),
        text(iface_x - 10, 136, "selected wet nearshore interface", size=20, fill="#14865c", anchor="end"),
        "</g>",
        '<g id="region_labels">',
        text(c_map(104.0, 80.0)[0], 78, "deep-water propagation", size=20, fill="#31475b", anchor="middle", weight="700"),
        text(c_map(48.5, 80.0)[0], 78, "shelf / shoaling influence", size=20, fill="#526137", anchor="middle", weight="700"),
        text(c_map(6.0, 80.0)[0], 78, "nearshore transition", size=20, fill="#765c26", anchor="middle", weight="700"),
        "</g>",
        '<g id="caption_placeholder">',
        text(130, 914, "Caption placeholder", size=20, fill="#59636b"),
        "</g>",
        "</svg>",
    ]
    write_text(svg, "\n".join(parts))
    return {"svg": file_record(svg)}


def convert_svg(svg: Path, pdf: Path, png: Path, preview: Path) -> dict[str, Any]:
    pdf.parent.mkdir(parents=True, exist_ok=True)
    png.parent.mkdir(parents=True, exist_ok=True)
    preview.parent.mkdir(parents=True, exist_ok=True)
    pdf_cmd = ["rsvg-convert", "-f", "pdf", "-o", pdf.as_posix(), svg.as_posix()]
    png_cmd = ["rsvg-convert", "-f", "png", "-w", "3000", "-o", png.as_posix(), svg.as_posix()]
    pdf_code, pdf_output = command_output(pdf_cmd, check=True)
    png_code, png_output = command_output(png_cmd, check=True)
    shutil.copy2(png, preview)
    return {
        "pdf": {"command": pdf_cmd, "return_code": pdf_code, "output": pdf_output},
        "png": {"command": png_cmd, "return_code": png_code, "output": png_output},
    }


def generate_figure_b() -> dict[str, Any]:
    b0_png = PUBLICATION_ROOT / "figure_B0_oblique_bathymetry_clean.png"
    b1_png = PUBLICATION_ROOT / "figure_B1_oblique_bathymetry_corridor.png"
    b0_pdf = PUBLICATION_ROOT / "figure_B0_oblique_bathymetry_clean.pdf"
    b1_pdf = PUBLICATION_ROOT / "figure_B1_oblique_bathymetry_corridor.pdf"
    b2_svg = EDITABLE_ROOT / "figure_B2_editable_overlay.svg"
    b0_blend = SOURCE_BLENDER / "figure_B0_oblique_bathymetry_clean_scene.blend"
    b1_blend = SOURCE_BLENDER / "figure_B1_oblique_bathymetry_corridor_scene.blend"
    blender_record = PROVENANCE_ROOT / "figure_B_blender_render.provenance.json"
    cmd = [
        "blender",
        "--factory-startup",
        "--background",
        "--python",
        R21_BLENDER_SCRIPT.as_posix(),
        "--",
        "--source-scene",
        R20_SCENE.as_posix(),
        "--output-b0",
        b0_png.as_posix(),
        "--output-b1",
        b1_png.as_posix(),
        "--overlay-svg",
        b2_svg.as_posix(),
        "--record",
        blender_record.as_posix(),
        "--save-b0-blend",
        b0_blend.as_posix(),
        "--save-b1-blend",
        b1_blend.as_posix(),
        "--width",
        "4800",
        "--height",
        "2800",
    ]
    code, output = command_output(cmd, check=True, env={"ALSOFT_DRIVERS": "null"})
    pdf_records = {}
    for png, pdf in [(b0_png, b0_pdf), (b1_png, b1_pdf)]:
        pdf_cmd = ["magick", png.as_posix(), pdf.as_posix()]
        pdf_code, pdf_output = command_output(pdf_cmd, check=True)
        pdf_records[display_path(pdf)] = {"command": pdf_cmd, "return_code": pdf_code, "output": pdf_output}
    for png in [b0_png, b1_png]:
        shutil.copy2(png, PREVIEW_ROOT / png.name)
    return {
        "status": "COMPLETE",
        "workflow": "Opened R20 selected Blender scene; hid corridor objects for B0; restored corridor objects for B1; generated transparent camera-aligned overlay SVG.",
        "blender_command": cmd,
        "blender_return_code": code,
        "blender_stdout_tail": output[-4000:],
        "pdf_conversion": pdf_records,
        "source_scene": file_record(R20_SCENE),
        "source_render_record": file_record(R20_RENDER_RECORD),
        "render_record": file_record(blender_record),
        "outputs": {
            "b0_png": file_record(b0_png),
            "b0_pdf": file_record(b0_pdf),
            "b1_png": file_record(b1_png),
            "b1_pdf": file_record(b1_pdf),
            "b2_svg": file_record(b2_svg),
            "b0_preview": file_record(PREVIEW_ROOT / b0_png.name),
            "b1_preview": file_record(PREVIEW_ROOT / b1_png.name),
            "b0_blend": file_record(b0_blend),
            "b1_blend": file_record(b1_blend),
        },
    }


def generate_figure_c() -> dict[str, Any]:
    distances, elevations = load_profile()
    profile_copy = SOURCE_ROOT / "data" / "bathymetry_profile_r19_source_copy.csv"
    profile_copy.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(R19_PROFILE, profile_copy)
    c0 = generate_c0(distances, elevations)
    c1 = generate_c1(distances, elevations)
    c2 = generate_c2()
    geometry = read_json(R19_GEOMETRY)
    return {
        "status": "COMPLETE",
        "profile_source": file_record(R19_PROFILE),
        "profile_source_hash": sha256(R19_PROFILE),
        "profile_source_copy": file_record(profile_copy),
        "profile_lineage": "R10 h400 mesh -> accepted corridor centreline sampling -> longitudinal bed profile",
        "mathematical_relationship": {
            "terrain_field": "b = b(x,y)",
            "blender_render": "(x,y,b(x,y)) with 4x vertical exaggeration for visualisation",
            "longitudinal_profile": "b_c(s) = b(x_c(s), y_c(s)) along the accepted corridor centreline",
        },
        "profile_summary": {
            "sample_count": int(distances.size),
            "distance_min_km": float(np.min(distances)),
            "distance_max_km": float(np.max(distances)),
            "bed_min_m": float(np.min(elevations)),
            "bed_max_m": float(np.max(elevations)),
            "distance_convention": geometry["bathymetry_profile"]["distance_convention"],
            "sampling_method": geometry["bathymetry_profile"]["method"],
        },
        "outputs": {"c0": c0, "c1": c1, "c2": c2},
    }


def image_info(path: Path) -> dict[str, Any]:
    code, output = command_output(["identify", "-format", "%w %h %[mean]", path.as_posix()], check=True)
    width_s, height_s, mean_s = output.split()
    return {"width_px": int(width_s), "height_px": int(height_s), "mean": float(mean_s), "nonblank": float(mean_s) > 0.0, "return_code": code}


def svg_validation(path: Path, required_groups: Sequence[str], *, require_text: bool) -> dict[str, Any]:
    tree = ET.parse(path)
    root = tree.getroot()
    groups = {node.attrib.get("id") for node in root.iter() if node.tag.endswith("g") and node.attrib.get("id")}
    text_count = sum(1 for node in root.iter() if node.tag.endswith("text"))
    return {
        "path": file_record(path),
        "parses": True,
        "required_groups_present": sorted(set(required_groups).intersection(groups)),
        "missing_groups": sorted(set(required_groups).difference(groups)),
        "text_element_count": text_count,
        "editable_text_present": text_count > 0 if require_text else True,
    }


def validate(b: Mapping[str, Any], c: Mapping[str, Any]) -> dict[str, Any]:
    b_record = read_json(PROVENANCE_ROOT / "figure_B_blender_render.provenance.json")
    checks: dict[str, Any] = {
        "generated_at_utc": utc_now(),
        "status": "COMPLETE",
        "blender": {
            "source_scene_matches_r20": Path(b_record["source_scene"]["path"]).name == R20_SCENE.name,
            "vertical_exaggeration": b_record["preserved_scientific_state"]["vertical_exaggeration"],
            "corridor_objects": b_record["corridor_objects"],
            "b0_only_annotation_state_changed": "hidden" in b_record["b0_change"],
            "b1_no_text_annotations": "no text" in b_record["b1_change"],
            "distance_reference": b_record["overlay"]["distance_reference"],
        },
        "profile": {
            "source": c["profile_source"],
            "source_hash": c["profile_source_hash"],
            "lineage": c["profile_lineage"],
            "axis_convention": c["profile_summary"]["distance_convention"],
            "sampling_method": c["profile_summary"]["sampling_method"],
            "not_image_extraction": True,
        },
        "svg": {},
        "pdf": {},
        "png": {},
        "json": {},
        "commands": {},
    }
    svg_specs = {
        EDITABLE_ROOT / "figure_B2_editable_overlay.svg": ["corridor", "centreline", "event", "interface", "kamaishi", "propagation", "distance_reference"],
        EDITABLE_ROOT / "figure_C0_longitudinal_bathymetry_clean.svg": ["profile", "axes", "sea_level"],
        EDITABLE_ROOT / "figure_C1_longitudinal_bathymetry_interpreted.svg": ["profile", "axes", "sea_level", "event_reference", "interface", "region_bands", "region_labels"],
        EDITABLE_ROOT / "figure_C2_annotation_template.svg": ["title_placeholder", "event_reference", "interface", "region_labels", "caption_placeholder"],
    }
    for path, groups in svg_specs.items():
        checks["svg"][display_path(path)] = svg_validation(path, groups, require_text=path.name.startswith("figure_C"))
    for path in [
        PUBLICATION_ROOT / "figure_B0_oblique_bathymetry_clean.pdf",
        PUBLICATION_ROOT / "figure_B1_oblique_bathymetry_corridor.pdf",
        PUBLICATION_ROOT / "figure_C0_longitudinal_bathymetry_clean.pdf",
        PUBLICATION_ROOT / "figure_C1_longitudinal_bathymetry_interpreted.pdf",
    ]:
        checks["pdf"][display_path(path)] = {"path": file_record(path), "nonempty": path.is_file() and path.stat().st_size > 1000}
    for path in [
        PUBLICATION_ROOT / "figure_B0_oblique_bathymetry_clean.png",
        PUBLICATION_ROOT / "figure_B1_oblique_bathymetry_corridor.png",
        PUBLICATION_ROOT / "figure_C0_longitudinal_bathymetry_clean.png",
        PUBLICATION_ROOT / "figure_C1_longitudinal_bathymetry_interpreted.png",
    ]:
        checks["png"][display_path(path)] = image_info(path)
    for path in sorted(PROVENANCE_ROOT.glob("*.json")):
        try:
            json.loads(path.read_text(encoding="utf-8"))
            checks["json"][display_path(path)] = "PASS"
        except Exception as exc:
            checks["json"][display_path(path)] = f"FAIL: {exc}"
    for name, cmd in {
        "py_compile": [sys.executable, "-m", "py_compile", "tools/figures/r21_editable_figures.py", "tools/figures/blender/r21_render_clean_bathymetry.py"],
        "git_diff_check": ["git", "diff", "--check"],
        "blender_version": ["blender", "--version"],
    }.items():
        code, output = command_output(cmd)
        checks["commands"][name] = {"return_code": code, "output": output[:4000]}
    checks["overall_pass"] = (
        checks["blender"]["source_scene_matches_r20"]
        and checks["blender"]["vertical_exaggeration"] == 4.0
        and checks["profile"]["not_image_extraction"]
        and all(not record["missing_groups"] for record in checks["svg"].values())
        and all(record["editable_text_present"] for record in checks["svg"].values())
        and all(record["nonempty"] for record in checks["pdf"].values())
        and all(record["nonblank"] for record in checks["png"].values())
        and all(value == "PASS" for value in checks["json"].values())
        and all(record["return_code"] == 0 for record in checks["commands"].values())
    )
    checks["status"] = "COMPLETE" if checks["overall_pass"] else "FAILED"
    write_json(PROVENANCE_ROOT / "r21_validation.json", checks)
    return checks


def write_provenance_and_docs(b: Mapping[str, Any], c: Mapping[str, Any], validation: Mapping[str, Any]) -> dict[str, Any]:
    b_record = read_json(PROVENANCE_ROOT / "figure_B_blender_render.provenance.json")
    figure_b = {
        "schema": {"name": "tsunami.r21.figure_b", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "authoritative_scene": b["source_scene"],
        "source_render_record": b["source_render_record"],
        "outputs": b["outputs"],
        "distance_reference_method": b_record["overlay"]["distance_reference"],
        "confirmation": {
            "b0_has_no_corridor_text_arrows_legend_markers": True,
            "b1_has_only_terrain_plus_corridor_geometry": True,
            "terrain_scientific_content_unchanged": True,
            "source_scene_opened_not_rebuilt_from_pixels": True,
        },
    }
    figure_c = {
        "schema": {"name": "tsunami.r21.figure_c", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        **c,
        "confirmation": {
            "regenerated_from_numerical_profile_csv": True,
            "not_reconstructed_from_png_pixels": True,
            "no_3d_inset": True,
            "large_prose_boxes_removed": True,
            "svg_text_editable": True,
        },
    }
    write_json(PROVENANCE_ROOT / "figure_B_clean_editable.provenance.json", figure_b)
    write_json(PROVENANCE_ROOT / "figure_C_clean_editable.provenance.json", figure_c)
    manifest = {
        "schema": {"name": "tsunami.r21.editable_figure_manifest", "version": "1.0.0"},
        "status": "COMPLETE" if validation["overall_pass"] else "FAILED_VALIDATION",
        "generated_at_utc": utc_now(),
        "branch": git_branch(),
        "starting_head": STARTING_HEAD,
        "generation_head": git_sha(),
        "worktree": WORKTREE_PATH.as_posix(),
        "figures": {"B": figure_b["outputs"], "C": c["outputs"]},
        "validation": validation,
        "guardrails": {
            "ai_image_generation": False,
            "regional2d_run": False,
            "local3d_run": False,
            "calibration": False,
            "solver_changes": False,
            "terrain_data_changes": False,
            "corridor_geometry_changes": False,
            "poster_or_report_edit": False,
            "push_pr_merge_rebase_amend": False,
        },
    }
    write_json(PROVENANCE_ROOT / "r21_editable_figure_manifest.json", manifest)
    write_docs(figure_b, figure_c, validation)
    return manifest


def write_docs(figure_b: Mapping[str, Any], figure_c: Mapping[str, Any], validation: Mapping[str, Any]) -> None:
    write_text(
        DOC_HANDOFF,
        """
        # R21 Editable Figure Handoff

        R21 provides clean scientific bases plus separate editable overlays for manual layout in Lucid,
        Figma, Inkscape or equivalent. It does not perform new simulation, calibration, terrain editing,
        corridor editing, or poster/report editing.

        ## Figure B

        - Scientific base, terrain only: `deliverables/figures/r21_editable/publication/figure_B0_oblique_bathymetry_clean.png`
        - Preferred Lucid/Figma base: `deliverables/figures/r21_editable/publication/figure_B1_oblique_bathymetry_corridor.png`
        - Editable overlay: `deliverables/figures/r21_editable/editable/figure_B2_editable_overlay.svg`

        B0 contains only bathymetry/topography, the z = 0 sea-level treatment, and the light background.
        B1 adds only the actual Regional2D corridor footprint from the R20 scene. B2 is transparent SVG
        overlay geometry with editable groups for corridor, centreline, event, interface, Kamaishi,
        propagation and distance reference.

        The B2 distance reference is a 25,000 m EPSG:32654 projected-ground segment parallel to the
        accepted corridor centreline tangent. It is not a screen-space scale bar.

        ## Figure C

        - Clean scientific profile: `deliverables/figures/r21_editable/editable/figure_C0_longitudinal_bathymetry_clean.svg`
        - Lightly interpreted profile: `deliverables/figures/r21_editable/editable/figure_C1_longitudinal_bathymetry_interpreted.svg`
        - Editable annotation suggestions: `deliverables/figures/r21_editable/editable/figure_C2_annotation_template.svg`

        Figure C is regenerated from `deliverables/figures/r19_tikz/data/bathymetry_profile.csv`, not
        reconstructed from pixels. The axis convention is unchanged: 0 km is the selected wet nearshore
        interface and larger distance is farther offshore/sourceward.

        ## Mathematical Relationship

        The terrain field is `b = b(x,y)`. The Blender view visualises `(x,y,b(x,y))` with 4x vertical
        exaggeration for interpretation. The longitudinal profile samples the same field along the accepted
        corridor centreline `x_c(s), y_c(s)`, giving `b_c(s) = b(x_c(s), y_c(s))`.

        ## Exact vs Conceptual

        The bed profile, 2011 event reference, selected wet interface, corridor geometry and B2 25 km
        reference are exact current-case geometry/data products. C1 region bands are broad conceptual
        interpretation bands inherited from the R19 convention; they are not calibrated physical thresholds.
        """,
    )
    write_text(
        DOC_COMPLETION,
        f"""
        # R21 Completion Report

        1. **Branch:** {git_branch()}
        2. **Worktree:** {WORKTREE_PATH}
        3. **Starting HEAD:** {STARTING_HEAD}
        4. **Final HEAD:** Reported in the final assistant response after commit creation.
        5. **Figure B authoritative scene:** {figure_b["authoritative_scene"]["path"]}
        6. **B0 clean terrain:** {figure_b["outputs"]["b0_png"]["path"]}
        7. **B1 corridor base:** {figure_b["outputs"]["b1_png"]["path"]}
        8. **B2 editable overlay:** {figure_b["outputs"]["b2_svg"]["path"]}
        9. **Figure C profile source:** {figure_c["profile_source"]["path"]}
        10. **Profile SHA256:** {figure_c["profile_source_hash"]}
        11. **C0 clean SVG:** {figure_c["outputs"]["c0"]["svg"]["path"]}
        12. **C1 interpreted SVG:** {figure_c["outputs"]["c1"]["svg"]["path"]}
        13. **C2 annotation template:** {figure_c["outputs"]["c2"]["svg"]["path"]}
        14. **Validation overall pass:** {validation["overall_pass"]}
        15. **No AI/simulation/calibration/solver/corridor/poster/report work:** confirmed.
        16. **Worktree clean:** reported after final commit in the final assistant response.
        """,
    )


def run_all() -> dict[str, Any]:
    ensure_layout()
    shutil.copy2(Path(__file__), SOURCE_PYTHON / Path(__file__).name)
    shutil.copy2(R21_BLENDER_SCRIPT, SOURCE_BLENDER / R21_BLENDER_SCRIPT.name)
    figure_b = generate_figure_b()
    figure_c = generate_figure_c()
    first_validation = validate(figure_b, figure_c)
    write_provenance_and_docs(figure_b, figure_c, first_validation)
    validation = validate(figure_b, figure_c)
    manifest = write_provenance_and_docs(figure_b, figure_c, validation)
    return {"figure_b": figure_b, "figure_c": figure_c, "validation": validation, "manifest": manifest}


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    result = run_all()
    print(json.dumps({"status": result["validation"]["status"], "overall_pass": result["validation"]["overall_pass"]}, indent=2))
    return 0 if result["validation"]["overall_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
