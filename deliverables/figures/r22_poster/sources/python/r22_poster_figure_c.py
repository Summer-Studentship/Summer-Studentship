#!/usr/bin/env python3
"""Generate poster-facing Figure C from the frozen R21 bathymetry profile."""

from __future__ import annotations

import csv
import hashlib
import json
import shutil
import subprocess
import xml.etree.ElementTree as ET
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[5]
R22_ROOT = REPO_ROOT / "deliverables/figures/r22_poster"
PUBLICATION_ROOT = R22_ROOT / "publication"
PREVIEW_ROOT = R22_ROOT / "previews"
PROVENANCE_ROOT = R22_ROOT / "provenance"
SOURCE_DATA_ROOT = R22_ROOT / "sources/data"

SOURCE_CSV = REPO_ROOT / "deliverables/figures/r19_tikz/data/bathymetry_profile.csv"
MILESTONES_JSON = REPO_ROOT / "deliverables/figures/r19_tikz/data/milestone_positions.json"
EXPECTED_SOURCE_SHA = "8166d8743ef5e030e9775f9e17c8acfdef497411cffc508a9698a1e6f7e21ab3"

SVG_OUT = PUBLICATION_ROOT / "figure_C3_longitudinal_bathymetry_poster.svg"
PDF_OUT = PUBLICATION_ROOT / "figure_C3_longitudinal_bathymetry_poster.pdf"
PNG_OUT = PUBLICATION_ROOT / "figure_C3_longitudinal_bathymetry_poster.png"
PREVIEW_OUT = PREVIEW_ROOT / PNG_OUT.name
PROVENANCE_OUT = PROVENANCE_ROOT / "figure_C3_longitudinal_bathymetry_poster.provenance.json"

WIDTH = 1800
HEIGHT = 1120
PLOT = {"x": 130.0, "y": 170.0, "w": 1540.0, "h": 560.0}
X_MIN = 0.0
X_MAX = 124.0
Y_MIN = -1150.0
Y_MAX = 120.0
INTERFACE_KM = 0.0
ZONE_BANDS = {
    "Offshore long-wave propagation": (85.0, 124.0),
    "Shelf / shoaling influence": (12.0, 85.0),
    "Nearshore transition": (0.0, 12.0),
}
ZONE_EXPLANATIONS = {
    "Offshore long-wave propagation": "Depth-averaged, approximately hydrostatic tsunami propagation is efficiently represented by the Regional2D NLSWE model.",
    "Shelf / shoaling influence": "Decreasing water depth and changing bathymetry increasingly modify wave celerity, amplitude and waveform toward the coast.",
    "Nearshore transition": "Increasingly local free-surface deformation, vertical motion and structure interaction motivate transfer toward high-fidelity Local3D impact modelling.",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def svg_escape(value: Any) -> str:
    return (
        str(value)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def tag(name: str, content: str = "", **kwargs: Any) -> str:
    attr = " ".join(f'{key.replace("_", "-")}="{svg_escape(value)}"' for key, value in kwargs.items() if value is not None)
    if content:
        return f"<{name} {attr}>{content}</{name}>" if attr else f"<{name}>{content}</{name}>"
    return f"<{name} {attr}/>" if attr else f"<{name}/>"


def text(
    x: float,
    y: float,
    value: str,
    *,
    size: float = 22.0,
    fill: str = "#26323B",
    anchor: str = "start",
    weight: str = "400",
    extra: str = "",
) -> str:
    return (
        f'<text x="{x:.2f}" y="{y:.2f}" font-family="Arial, Helvetica, sans-serif" '
        f'font-size="{size:.2f}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}"{extra}>'
        f"{svg_escape(value)}</text>"
    )


def wrapped_text(x: float, y: float, value: str, *, width: int, line_height: float, size: float, fill: str = "#1B2430") -> str:
    words = value.split()
    lines: list[str] = []
    current: list[str] = []
    current_len = 0
    for word in words:
        projected = current_len + len(word) + (1 if current else 0)
        if projected > width and current:
            lines.append(" ".join(current))
            current = [word]
            current_len = len(word)
        else:
            current.append(word)
            current_len = projected
    if current:
        lines.append(" ".join(current))
    return "\n".join(text(x, y + i * line_height, line, size=size, fill=fill) for i, line in enumerate(lines))


def load_profile() -> tuple[list[float], list[float]]:
    distances: list[float] = []
    elevations: list[float] = []
    with SOURCE_CSV.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            distances.append(float(row["distance_offshore_km"]))
            elevations.append(float(row["bed_elevation_m"]))
    return distances, elevations


def load_event_reference() -> float:
    milestones = json.loads(MILESTONES_JSON.read_text(encoding="utf-8"))
    return next(
        float(item["distance_offshore_km"])
        for item in milestones["milestones"]
        if item["id"] == "M0"
    )


def c_map(x: float, y: float) -> tuple[float, float]:
    sx = PLOT["x"] + (X_MAX - x) / (X_MAX - X_MIN) * PLOT["w"]
    sy = PLOT["y"] + (Y_MAX - y) / (Y_MAX - Y_MIN) * PLOT["h"]
    return sx, sy


def c_rect_for_band(x0: float, x1: float, colour: str, opacity: float) -> str:
    left_x, top_y = c_map(max(x0, x1), Y_MAX)
    right_x, bottom_y = c_map(min(x0, x1), Y_MIN)
    return tag("rect", x=left_x, y=top_y, width=right_x - left_x, height=bottom_y - top_y, fill=colour, opacity=opacity)


def profile_path(distances: list[float], elevations: list[float]) -> str:
    points = [c_map(x, y) for x, y in zip(distances, elevations, strict=True)]
    return "M " + " L ".join(f"{x:.2f} {y:.2f}" for x, y in points)


def axes_svg() -> str:
    parts = [
        '<g id="axes">',
        tag("rect", x=PLOT["x"], y=PLOT["y"], width=PLOT["w"], height=PLOT["h"], fill="none", stroke="#26323B", stroke_width=2.0),
    ]
    for y in [-1000, -750, -500, -250, 0]:
        x0, yy = c_map(X_MAX, y)
        x1, _ = c_map(X_MIN, y)
        parts.append(tag("line", x1=x0, y1=yy, x2=x1, y2=yy, stroke="#DDE2DE", stroke_width=1.2))
        parts.append(text(PLOT["x"] - 16, yy + 6, f"{y}", size=18, fill="#4D5963", anchor="end"))
    for x in [120, 100, 80, 60, 40, 20, 0]:
        xx, y0 = c_map(x, Y_MIN)
        parts.append(tag("line", x1=xx, y1=y0, x2=xx, y2=y0 + 14, stroke="#26323B", stroke_width=2))
        parts.append(text(xx, y0 + 42, f"{x}", size=18, fill="#4D5963", anchor="middle"))
    parts.append(text(PLOT["x"] + PLOT["w"] / 2.0, PLOT["y"] + PLOT["h"] + 84, "Distance offshore from selected wet nearshore interface, km", size=24, anchor="middle"))
    parts.append(
        text(
            42.0,
            PLOT["y"] + PLOT["h"] / 2.0,
            "Bed elevation, m (EGM2008)",
            size=24,
            anchor="middle",
            extra=f' transform="rotate(-90 42 {PLOT["y"] + PLOT["h"] / 2.0:.2f})"',
        )
    )
    parts.append("</g>")
    return "\n".join(parts)


def sea_level_svg() -> str:
    x0, y0 = c_map(X_MAX, 0.0)
    x1, _ = c_map(X_MIN, 0.0)
    return "\n".join(
        [
            '<g id="sea_level">',
            tag("line", x1=x0, y1=y0, x2=x1, y2=y0, stroke="#8B9299", stroke_width=2.0, stroke_dasharray="8 8"),
            text(x1 - 10, y0 - 12, "z = 0 m", size=17, fill="#68737C", anchor="end"),
            "</g>",
        ]
    )


def notes_svg() -> str:
    band_style = {
        "Offshore long-wave propagation": "#31475B",
        "Shelf / shoaling influence": "#526137",
        "Nearshore transition": "#765C26",
    }
    x_positions = [130.0, 615.0, 1100.0]
    parts = ['<g id="zone_explanations">']
    for heading, x in zip(ZONE_EXPLANATIONS, x_positions, strict=True):
        parts.append(text(x, 890, heading, size=24, fill=band_style[heading], weight="700"))
        parts.append(wrapped_text(x, 926, ZONE_EXPLANATIONS[heading], width=48, line_height=28, size=21))
    parts.append("</g>")
    parts.append('<g id="interpretive_boundary_note">')
    parts.append(
        wrapped_text(
            130,
            1030,
            "Region boundaries are interpretive rather than fixed physical thresholds; the selected wet nearshore interface is the current case-specific 2D->3D extraction location.",
            width=136,
            line_height=24,
            size=18,
            fill="#56616F",
        )
    )
    parts.append(
        text(
            130,
            1092,
            "Profile: R10 h400 corridor-centreline sampling; bed elevation referenced to EGM2008.  b_c(s)=b(x_c(s),y_c(s)).",
            size=18,
            fill="#56616F",
        )
    )
    parts.append("</g>")
    return "\n".join(parts)


def render_svg(distances: list[float], elevations: list[float], event_km: float) -> str:
    band_style = {
        "Offshore long-wave propagation": ("#DFE8EF", "#31475B", 104.0),
        "Shelf / shoaling influence": ("#E7EAD7", "#526137", 48.5),
        "Nearshore transition": ("#F3E6BF", "#765C26", 6.0),
    }
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">',
        tag("rect", x=0, y=0, width=WIDTH, height=HEIGHT, fill="#FFFFFF"),
        '<g id="title">',
        text(130, 52, "Figure C. Longitudinal bathymetry along the Kamaishi Regional2D corridor", size=34, fill="#1B2430", weight="700"),
        text(130, 92, "Bed elevation sampled along the corridor centreline from the offshore event region toward the selected wet nearshore interface.", size=22, fill="#56616F"),
        "</g>",
        '<g id="region_bands">',
    ]
    for label, (x0, x1) in ZONE_BANDS.items():
        fill, _text_colour, _label_x = band_style[label]
        parts.append(c_rect_for_band(x0, x1, fill, 0.72))
    parts.extend(["</g>", axes_svg(), sea_level_svg(), '<g id="region_labels">'])
    for label, (_x0, _x1) in ZONE_BANDS.items():
        _fill, text_colour, label_x = band_style[label]
        parts.append(text(c_map(label_x, 86.0)[0], 150, label, size=20, fill=text_colour, anchor="middle", weight="700"))
    parts.append("</g>")

    event_x, event_y0 = c_map(event_km, Y_MAX)
    _, event_y1 = c_map(event_km, Y_MIN)
    iface_x, iface_y0 = c_map(INTERFACE_KM, Y_MAX)
    _, iface_y1 = c_map(INTERFACE_KM, Y_MIN)
    parts.extend(
        [
            '<g id="event_reference">',
            tag("line", x1=event_x, y1=event_y0, x2=event_x, y2=event_y1, stroke="#C8352C", stroke_width=2.0, stroke_dasharray="9 8"),
            text(event_x - 10, event_y1 - 26, "2011 event ref.", size=18, fill="#C8352C", anchor="end", weight="700", extra=f' transform="rotate(-90 {event_x - 10:.2f} {event_y1 - 26:.2f})"'),
            "</g>",
            '<g id="interface">',
            tag("line", x1=iface_x, y1=iface_y0, x2=iface_x, y2=iface_y1, stroke="#14865C", stroke_width=2.0, stroke_dasharray="9 8"),
            text(iface_x - 10, iface_y1 - 24, "selected wet interface", size=18, fill="#14865C", anchor="end", weight="700"),
            "</g>",
            '<g id="profile">',
            tag("path", d=profile_path(distances, elevations), fill="none", stroke="#123858", stroke_width=5.2, stroke_linejoin="round", stroke_linecap="round"),
            "</g>",
            notes_svg(),
            "</svg>",
        ]
    )
    return "\n".join(parts)


def command_output(command: list[str]) -> tuple[int, str]:
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    return completed.returncode, completed.stdout.strip()


def convert_outputs() -> dict[str, Any]:
    pdf_cmd = ["rsvg-convert", "-f", "pdf", "-o", PDF_OUT.as_posix(), SVG_OUT.as_posix()]
    png_cmd = ["rsvg-convert", "-f", "png", "-w", "3600", "-o", PNG_OUT.as_posix(), SVG_OUT.as_posix()]
    pdf_code, pdf_output = command_output(pdf_cmd)
    png_code, png_output = command_output(png_cmd)
    if pdf_code != 0:
        raise RuntimeError(pdf_output)
    if png_code != 0:
        raise RuntimeError(png_output)
    shutil.copy2(PNG_OUT, PREVIEW_OUT)
    return {
        "pdf": {"command": pdf_cmd, "return_code": pdf_code, "output": pdf_output},
        "png": {"command": png_cmd, "return_code": png_code, "output": png_output},
    }


def validate_svg_text(path: Path) -> dict[str, Any]:
    tree = ET.parse(path)
    text_count = sum(1 for node in tree.getroot().iter() if node.tag.endswith("text"))
    groups = {node.attrib.get("id") for node in tree.getroot().iter() if node.tag.endswith("g")}
    required = {"title", "axes", "sea_level", "region_bands", "region_labels", "event_reference", "interface", "profile", "zone_explanations", "interpretive_boundary_note"}
    return {
        "parses": True,
        "text_element_count": text_count,
        "editable_text_present": text_count > 0,
        "missing_groups": sorted(required.difference(groups)),
    }


def image_mean(path: Path) -> dict[str, Any]:
    code, output = command_output(["magick", "identify", "-format", "%w %h %[mean]", path.as_posix()])
    if code != 0:
        raise RuntimeError(output)
    width, height, mean = output.split()
    return {"width_px": int(width), "height_px": int(height), "mean": float(mean), "nonblank": float(mean) > 0.0}


def main() -> None:
    for path in [PUBLICATION_ROOT, PREVIEW_ROOT, PROVENANCE_ROOT, SOURCE_DATA_ROOT]:
        path.mkdir(parents=True, exist_ok=True)

    source_sha = sha256(SOURCE_CSV)
    if source_sha != EXPECTED_SOURCE_SHA:
        raise RuntimeError(f"Unexpected bathymetry CSV hash: {source_sha}")

    distances, elevations = load_profile()
    event_km = load_event_reference()
    SVG_OUT.write_text(render_svg(distances, elevations, event_km) + "\n", encoding="utf-8")
    source_copy = SOURCE_DATA_ROOT / "bathymetry_profile_r19_source_copy.csv"
    shutil.copy2(SOURCE_CSV, source_copy)
    conversion = convert_outputs()

    result = {
        "generated_at_utc": datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "status": "COMPLETE",
        "source_csv": str(SOURCE_CSV.relative_to(REPO_ROOT)),
        "source_sha256": source_sha,
        "source_copy": str(source_copy.relative_to(REPO_ROOT)),
        "lineage": "R10 h400 Regional mesh -> accepted corridor centreline -> sampled bed field -> longitudinal profile",
        "curve_preservation": {
            "data_unchanged": True,
            "x_coordinate_unchanged": True,
            "y_coordinate_unchanged": True,
            "axis_limits_unchanged": {"x": [X_MIN, X_MAX], "y": [Y_MIN, Y_MAX]},
            "sea_level_reference_unchanged": True,
            "event_reference_km": event_km,
            "selected_interface_km": INTERFACE_KM,
        },
        "zone_labels": list(ZONE_BANDS.keys()),
        "zone_explanations": ZONE_EXPLANATIONS,
        "interpretive_boundary_note": "Region boundaries are interpretive rather than fixed physical thresholds; the selected wet nearshore interface is the current case-specific 2D->3D extraction location.",
        "source_line": "Profile: R10 h400 corridor-centreline sampling; bed elevation referenced to EGM2008.",
        "outputs": {
            "svg": str(SVG_OUT.relative_to(REPO_ROOT)),
            "pdf": str(PDF_OUT.relative_to(REPO_ROOT)),
            "png": str(PNG_OUT.relative_to(REPO_ROOT)),
            "preview": str(PREVIEW_OUT.relative_to(REPO_ROOT)),
        },
        "conversion": conversion,
        "validation": {
            "svg": validate_svg_text(SVG_OUT),
            "pdf_nonempty": PDF_OUT.exists() and PDF_OUT.stat().st_size > 1000,
            "png": image_mean(PNG_OUT),
        },
    }
    PROVENANCE_OUT.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
