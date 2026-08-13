#!/usr/bin/env python3
"""Generate the R20 publication-oriented figure refinement package.

R20 is a figure-only pass. It reuses the accepted R16--R19 geospatial,
bathymetry, corridor and Local3D geometry lineage, and does not run Regional2D,
Local3D, calibration, replay, source, mesh or solver workflows.
"""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import html
import json
import math
import os
import shutil
import subprocess
import sys
import textwrap
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

import numpy as np
from osgeo import gdal, ogr


REPO_ROOT = Path(__file__).resolve().parents[2]
R20_ROOT = REPO_ROOT / "deliverables/figures/r20_publication"
PUBLICATION_ROOT = R20_ROOT / "publication"
PREVIEW_ROOT = R20_ROOT / "previews"
SOURCE_ROOT = R20_ROOT / "sources"
PROVENANCE_ROOT = R20_ROOT / "provenance"
DATA_ROOT = SOURCE_ROOT / "data"
GDAL_ROOT = SOURCE_ROOT / "gdal"
SVG_SOURCE_ROOT = SOURCE_ROOT / "svg"
BLENDER_ROOT = SOURCE_ROOT / "blender"
IMAGEMAGICK_ROOT = SOURCE_ROOT / "imagemagick"
PYTHON_SOURCE_ROOT = SOURCE_ROOT / "python"

DOC_HANDOFF = REPO_ROOT / "docs/project/r20_publication_figure_handoff.md"
DOC_COMPLETION = REPO_ROOT / "docs/project/r20_completion_report.md"
DOC_SELECTION = REPO_ROOT / "docs/project/r20_selection_rationale.md"

R16_ROOT = REPO_ROOT / "deliverables/figures/r16_publication"
R17_ROOT = REPO_ROOT / "deliverables/figures/r17_closure"
R18_ROOT = REPO_ROOT / "deliverables/figures/r18_poster"
R19_ROOT = REPO_ROOT / "deliverables/figures/r19_tikz"

R16_DERIVED = R16_ROOT / "sources/qgis/derived"
R16_LAYERS = R16_ROOT / "sources/qgis/layers"
R17_TERRAIN = R17_ROOT / "sources/blender/terrain/etopo_corridor_blender_utm54_200m.tif"
R17_RENDER_SCRIPT = REPO_ROOT / "tools/figures/blender/render_corridor_bathymetry.py"

JAPAN_RASTER = R16_DERIVED / "etopo_japan_context_utm54.tif"
JAPAN_COASTLINE = R16_DERIVED / "japan_context_coastline_0m.gpkg"
DETAIL_RASTER = R16_DERIVED / "etopo_corridor_kamaishi_utm54.tif"
DETAIL_COASTLINE = R16_DERIVED / "corridor_coastline_0m.gpkg"
CORRIDOR_POLYGON = R16_LAYERS / "corridor_polygon.geojson"
CORRIDOR_CENTRELINE = R16_LAYERS / "corridor_centreline.geojson"
EVENT_POINTS = R16_LAYERS / "event_and_kamaishi_points.geojson"
LOCAL3D_FOOTPRINT = R16_LAYERS / "local3d_candidate_footprint.geojson"
R19_GEOMETRY = R19_ROOT / "data/domain_geometry.json"
R19_MILESTONES = R19_ROOT / "data/milestone_positions.json"
R19_PROFILE = R19_ROOT / "data/bathymetry_profile.csv"

STARTING_HEAD = "eb8b165d175659b214905cb0d35c4358b54a2635"
BRANCH_NAME = "feat/r20-publication-figure-refinement"
WORKTREE_PATH = Path("/home/helios/Projects/Summer-Studentship-r20-publication")

SVG_NS = "http://www.w3.org/2000/svg"
XLINK_NS = "http://www.w3.org/1999/xlink"


@dataclass(frozen=True)
class Point:
    x: float
    y: float


@dataclass(frozen=True)
class Box:
    x: float
    y: float
    w: float
    h: float


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def ensure_layout() -> None:
    for path in [
        PUBLICATION_ROOT,
        PREVIEW_ROOT,
        PROVENANCE_ROOT,
        DATA_ROOT,
        GDAL_ROOT,
        SVG_SOURCE_ROOT,
        BLENDER_ROOT / "candidates",
        IMAGEMAGICK_ROOT,
        PYTHON_SOURCE_ROOT,
    ]:
        path.mkdir(parents=True, exist_ok=True)


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(line.rstrip() for line in textwrap.dedent(text).splitlines()) + "\n", encoding="utf-8")


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


def command_output(command: Sequence[str], *, cwd: Path | None = None, check: bool = False, env: Mapping[str, str] | None = None) -> tuple[int, str]:
    completed = subprocess.run(
        list(command),
        cwd=cwd or REPO_ROOT,
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


def raster_extent(path: Path) -> dict[str, float]:
    ds = gdal.Open(path.as_posix())
    if ds is None:
        raise RuntimeError(f"Could not open raster: {path}")
    gt = ds.GetGeoTransform()
    xs = [gt[0], gt[0] + ds.RasterXSize * gt[1], gt[0] + ds.RasterYSize * gt[2], gt[0] + ds.RasterXSize * gt[1] + ds.RasterYSize * gt[2]]
    ys = [gt[3], gt[3] + ds.RasterXSize * gt[4], gt[3] + ds.RasterYSize * gt[5], gt[3] + ds.RasterXSize * gt[4] + ds.RasterYSize * gt[5]]
    return {
        "xmin": min(xs),
        "xmax": max(xs),
        "ymin": min(ys),
        "ymax": max(ys),
        "width_px": float(ds.RasterXSize),
        "height_px": float(ds.RasterYSize),
    }


def raster_stats(path: Path) -> dict[str, Any]:
    ds = gdal.Open(path.as_posix())
    if ds is None:
        raise RuntimeError(f"Could not open raster: {path}")
    band = ds.GetRasterBand(1)
    stats = band.GetStatistics(True, True)
    return {
        "path": file_record(path),
        "size_px": {"width": ds.RasterXSize, "height": ds.RasterYSize},
        "extent": raster_extent(path),
        "band_1": {"min_m": stats[0], "max_m": stats[1], "mean_m": stats[2], "stddev_m": stats[3]},
        "projection_head": (ds.GetProjection() or "").splitlines()[:4],
    }


def colour_relief(source: Path, output: Path) -> dict[str, Any]:
    ramp = GDAL_ROOT / "etopo_publication_relief_ramp.txt"
    write_text(
        ramp,
        """
        -9000  5 32 70
        -4000  15 58 100
        -1500  35 92 138
        -800   71 132 166
        -300   126 178 190
        -50    197 219 210
        0      236 238 222
        60     201 220 169
        250    157 190 124
        700    179 151 105
        1200   142 120 94
        3000   238 234 222
        """,
    )
    code, output_text = command_output(
        ["gdaldem", "color-relief", "-alpha", source.as_posix(), ramp.as_posix(), output.as_posix()],
        check=True,
    )
    return {"command": ["gdaldem", "color-relief", "-alpha", display_path(source), display_path(ramp), display_path(output)], "return_code": code, "output": output_text}


def embed_png(path: Path) -> str:
    return "data:image/png;base64," + base64.b64encode(path.read_bytes()).decode("ascii")


def relative_image_href(image: Path, svg: Path) -> str:
    return os.path.relpath(image, svg.parent).replace(os.sep, "/")


def svg_escape(text: Any) -> str:
    return html.escape(str(text), quote=True)


def attrs(**kwargs: Any) -> str:
    parts = []
    for key, value in kwargs.items():
        if value is None:
            continue
        parts.append(f'{key.replace("_", "-")}="{svg_escape(value)}"')
    return " ".join(parts)


def tag(name: str, content: str = "", **kwargs: Any) -> str:
    attr = attrs(**kwargs)
    if content:
        return f"<{name} {attr}>{content}</{name}>" if attr else f"<{name}>{content}</{name}>"
    return f"<{name} {attr}/>" if attr else f"<{name}/>"


def text(x: float, y: float, value: str, *, size: float = 24, weight: str = "400", fill: str = "#17212b", anchor: str = "start", extra: str = "") -> str:
    return (
        f'<text x="{x:.2f}" y="{y:.2f}" font-family="Arial, Helvetica, sans-serif" '
        f'font-size="{size:.2f}" font-weight="{weight}" fill="{fill}" text-anchor="{anchor}" {extra}>{svg_escape(value)}</text>'
    )


def multiline_text(x: float, y: float, lines: Sequence[str], *, size: float = 22, line_height: float = 1.25, fill: str = "#17212b", weight: str = "400", anchor: str = "start") -> str:
    tspans = []
    for idx, line in enumerate(lines):
        dy = 0 if idx == 0 else size * line_height
        tspans.append(f'<tspan x="{x:.2f}" dy="{dy:.2f}">{svg_escape(line)}</tspan>')
    return (
        f'<text x="{x:.2f}" y="{y:.2f}" font-family="Arial, Helvetica, sans-serif" font-size="{size:.2f}" '
        f'font-weight="{weight}" fill="{fill}" text-anchor="{anchor}">' + "".join(tspans) + "</text>"
    )


def path_polyline(points: Sequence[Point], mapper: Any, *, close: bool = False) -> str:
    mapped = [mapper(point) for point in points]
    if not mapped:
        return ""
    commands = [f"M {mapped[0].x:.2f} {mapped[0].y:.2f}"]
    commands.extend(f"L {point.x:.2f} {point.y:.2f}" for point in mapped[1:])
    if close:
        commands.append("Z")
    return " ".join(commands)


def read_geojson_geometry(path: Path) -> list[list[Point]]:
    payload = read_json(path)
    geometries: list[list[Point]] = []
    for feature in payload["features"]:
        geom = feature["geometry"]
        if geom["type"] == "LineString":
            geometries.append([Point(float(x), float(y)) for x, y in geom["coordinates"]])
        elif geom["type"] == "Polygon":
            geometries.extend([[Point(float(x), float(y)) for x, y in ring] for ring in geom["coordinates"][:1]])
        elif geom["type"] == "Point":
            x, y = geom["coordinates"]
            geometries.append([Point(float(x), float(y))])
    return geometries


def read_ogr_lines(path: Path) -> list[list[Point]]:
    ds = ogr.Open(path.as_posix())
    if ds is None:
        raise RuntimeError(f"Could not open vector layer: {path}")
    lines: list[list[Point]] = []
    layer = ds.GetLayer(0)
    for feature in layer:
        geom = feature.GetGeometryRef()
        if geom is None:
            continue
        collect_ogr_points(geom.Clone(), lines)
    return lines


def collect_ogr_points(geom: Any, lines: list[list[Point]]) -> None:
    gtype = geom.GetGeometryType()
    if gtype in (ogr.wkbLineString, ogr.wkbLineString25D, ogr.wkbLinearRing):
        lines.append([Point(float(geom.GetX(i)), float(geom.GetY(i))) for i in range(geom.GetPointCount())])
    elif gtype in (ogr.wkbMultiLineString, ogr.wkbMultiLineString25D, ogr.wkbGeometryCollection):
        for i in range(geom.GetGeometryCount()):
            collect_ogr_points(geom.GetGeometryRef(i), lines)
    elif gtype in (ogr.wkbPolygon, ogr.wkbPolygon25D):
        ring = geom.GetGeometryRef(0)
        if ring is not None:
            collect_ogr_points(ring, lines)
    elif gtype in (ogr.wkbMultiPolygon, ogr.wkbMultiPolygon25D):
        for i in range(geom.GetGeometryCount()):
            collect_ogr_points(geom.GetGeometryRef(i), lines)


def fit_mapper(extent: Mapping[str, float], box: Box, *, crop: Mapping[str, float] | None = None) -> tuple[Any, Box, Mapping[str, float]]:
    data = dict(crop or extent)
    dx = data["xmax"] - data["xmin"]
    dy = data["ymax"] - data["ymin"]
    scale = min(box.w / dx, box.h / dy)
    drawn = Box(box.x + (box.w - dx * scale) / 2.0, box.y + (box.h - dy * scale) / 2.0, dx * scale, dy * scale)

    def mapper(point: Point) -> Point:
        return Point(drawn.x + (point.x - data["xmin"]) * scale, drawn.y + (data["ymax"] - point.y) * scale)

    return mapper, drawn, {"xmin": data["xmin"], "xmax": data["xmax"], "ymin": data["ymin"], "ymax": data["ymax"], "scale_px_per_m": scale}


def wgs84_to_utm54n(latitude_deg: float, longitude_deg: float) -> Point:
    a = 6378137.0
    f = 1 / 298.257223563
    k0 = 0.9996
    e2 = f * (2 - f)
    ep2 = e2 / (1 - e2)
    lat = math.radians(latitude_deg)
    lon = math.radians(longitude_deg)
    lon0 = math.radians(141.0)
    n = a / math.sqrt(1 - e2 * math.sin(lat) ** 2)
    t = math.tan(lat) ** 2
    c = ep2 * math.cos(lat) ** 2
    aa = math.cos(lat) * (lon - lon0)
    m = a * (
        (1 - e2 / 4 - 3 * e2**2 / 64 - 5 * e2**3 / 256) * lat
        - (3 * e2 / 8 + 3 * e2**2 / 32 + 45 * e2**3 / 1024) * math.sin(2 * lat)
        + (15 * e2**2 / 256 + 45 * e2**3 / 1024) * math.sin(4 * lat)
        - (35 * e2**3 / 3072) * math.sin(6 * lat)
    )
    easting = k0 * n * (
        aa
        + (1 - t + c) * aa**3 / 6
        + (5 - 18 * t + t**2 + 72 * c - 58 * ep2) * aa**5 / 120
    ) + 500000
    northing = k0 * (
        m
        + n
        * math.tan(lat)
        * (
            aa**2 / 2
            + (5 - t + 9 * c + 4 * c**2) * aa**4 / 24
            + (61 - 58 * t + t**2 + 600 * c - 330 * ep2) * aa**6 / 720
        )
    )
    return Point(easting, northing)


def convert_svg(svg: Path, png: Path, pdf: Path, *, width_px: int | None = None) -> dict[str, Any]:
    png.parent.mkdir(parents=True, exist_ok=True)
    pdf.parent.mkdir(parents=True, exist_ok=True)
    png_cmd = ["rsvg-convert", "-f", "png", "-o", png.as_posix()]
    if width_px is not None:
        png_cmd.extend(["-w", str(width_px)])
    png_cmd.append(svg.as_posix())
    png_code, png_output = command_output(png_cmd, check=True)
    pdf_code, pdf_output = command_output(["rsvg-convert", "-f", "pdf", "-o", pdf.as_posix(), svg.as_posix()], check=True)
    return {
        "png": {"command": png_cmd, "return_code": png_code, "output": png_output},
        "pdf": {"command": ["rsvg-convert", "-f", "pdf", "-o", display_path(pdf), display_path(svg)], "return_code": pdf_code, "output": pdf_output},
    }


def generate_context_map() -> dict[str, Any]:
    japan_png = GDAL_ROOT / "r20_A_japan_context_relief.png"
    detail_png = GDAL_ROOT / "r20_A_kamaishi_corridor_relief.png"
    relief_records = [colour_relief(JAPAN_RASTER, japan_png), colour_relief(DETAIL_RASTER, detail_png)]

    geometry = read_json(R19_GEOMETRY)
    event = Point(geometry["event"]["epicentre_projected_m"]["x"], geometry["event"]["epicentre_projected_m"]["y"])
    kamaishi = Point(geometry["event"]["kamaishi_proxy_projected_m"]["x"], geometry["event"]["kamaishi_proxy_projected_m"]["y"])
    interface = Point(geometry["corridor"]["interface_or_inland_end_projected_m"]["x"], geometry["corridor"]["interface_or_inland_end_projected_m"]["y"])
    source_start = Point(geometry["corridor"]["source_side_start_projected_m"]["x"], geometry["corridor"]["source_side_start_projected_m"]["y"])
    polygon = read_geojson_geometry(CORRIDOR_POLYGON)[0]
    centreline = read_geojson_geometry(CORRIDOR_CENTRELINE)[0]

    width, height = 2400, 1380
    context_box = Box(90, 170, 650, 1030)
    detail_box = Box(800, 170, 1510, 1030)
    japan_extent = raster_extent(JAPAN_RASTER)
    detail_extent = raster_extent(DETAIL_RASTER)
    context_mapper, context_drawn, context_data = fit_mapper(japan_extent, context_box)
    detail_mapper, detail_drawn, detail_data = fit_mapper(detail_extent, detail_box)

    parts = [
        f'<svg xmlns="{SVG_NS}" xmlns:xlink="{XLINK_NS}" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        "<defs>",
        '<marker id="arrow" markerWidth="12" markerHeight="12" refX="10" refY="6" orient="auto"><path d="M 0 0 L 12 6 L 0 12 z" fill="#23313d"/></marker>',
        "</defs>",
        tag("rect", x=0, y=0, width=width, height=height, fill="#f7f8f6"),
    ]
    for panel, title, subtitle in [
        (context_box, "A  Japan / Tohoku context", "Kamaishi and epicentral reference in NE Honshu context"),
        (detail_box, "B  Accepted Kamaishi corridor", "Regional2D footprint, centreline and selected wet interface"),
    ]:
        parts.append(tag("rect", x=panel.x - 18, y=panel.y - 58, width=panel.w + 36, height=panel.h + 94, fill="#ffffff", stroke="#d7d9d4", stroke_width=1.2))
        parts.append(text(panel.x, panel.y - 24, title, size=24, weight="700"))
        parts.append(text(panel.x, panel.y + 7, subtitle, size=17, fill="#5a6670"))
    parts.append(text(90, 82, "2011 Tohoku Event and Accepted Kamaishi Corridor", size=38, weight="700"))

    parts.append(tag("image", href=embed_png(japan_png), x=context_drawn.x, y=context_drawn.y, width=context_drawn.w, height=context_drawn.h, preserveAspectRatio="none"))
    parts.append(tag("rect", x=context_drawn.x, y=context_drawn.y, width=context_drawn.w, height=context_drawn.h, fill="none", stroke="#202a33", stroke_width=2))
    for line in read_ogr_lines(JAPAN_COASTLINE):
        d = path_polyline(line, context_mapper)
        if d:
            parts.append(tag("path", d=d, fill="none", stroke="#394a53", stroke_width=2.0, opacity=0.88))
    for line in [polygon]:
        parts.append(tag("path", d=path_polyline(line, context_mapper, close=True), fill="#e7803c", fill_opacity=0.20, stroke="#bd5528", stroke_width=2.4))
    parts.append(tag("path", d=path_polyline(centreline, context_mapper), fill="none", stroke="#1d5f98", stroke_width=2.2, stroke_dasharray="10 8", opacity=0.95))
    for point, colour, label, dx, dy in [
        (event, "#c8352c", "2011 event", 16, 5),
        (kamaishi, "#226da8", "Kamaishi", 14, -10),
    ]:
        p = context_mapper(point)
        parts.append(tag("circle", cx=p.x, cy=p.y, r=9, fill=colour, stroke="white", stroke_width=3))
        parts.append(text(p.x + dx, p.y + dy, label, size=20, weight="700", fill="#16222c", extra='style="paint-order: stroke; stroke: white; stroke-width: 6px; stroke-linejoin: round;"'))
    # Highlight the detailed-panel extent on the context map.
    detail_corners = [
        Point(detail_extent["xmin"], detail_extent["ymin"]),
        Point(detail_extent["xmax"], detail_extent["ymin"]),
        Point(detail_extent["xmax"], detail_extent["ymax"]),
        Point(detail_extent["xmin"], detail_extent["ymax"]),
        Point(detail_extent["xmin"], detail_extent["ymin"]),
    ]
    parts.append(tag("path", d=path_polyline(detail_corners, context_mapper), fill="none", stroke="#17212b", stroke_width=2, stroke_dasharray="8 7"))

    parts.append(tag("image", href=embed_png(detail_png), x=detail_drawn.x, y=detail_drawn.y, width=detail_drawn.w, height=detail_drawn.h, preserveAspectRatio="none"))
    parts.append(tag("rect", x=detail_drawn.x, y=detail_drawn.y, width=detail_drawn.w, height=detail_drawn.h, fill="none", stroke="#202a33", stroke_width=2))
    for line in read_ogr_lines(DETAIL_COASTLINE):
        d = path_polyline(line, detail_mapper)
        if d:
            parts.append(tag("path", d=d, fill="none", stroke="#33454e", stroke_width=2.4, opacity=0.9))
    parts.append(tag("path", d=path_polyline(polygon, detail_mapper, close=True), fill="#e7803c", fill_opacity=0.18, stroke="#b95025", stroke_width=4.5))
    parts.append(tag("path", d=path_polyline(centreline, detail_mapper), fill="none", stroke="#124f80", stroke_width=4.0, stroke_dasharray="16 12"))
    detail_points = [
        (event, "#c8352c", "2011 event reference", 17, 8),
        (kamaishi, "#226da8", "Kamaishi", 17, -13),
        (interface, "#14865c", "selected wet nearshore interface", -330, 44),
        (source_start, "#53336d", "source-side offshore end", -270, 38),
    ]
    for point, colour, label, dx, dy in detail_points:
        p = detail_mapper(point)
        r = 10 if "interface" not in label and "source-side" not in label else 8
        parts.append(tag("circle", cx=p.x, cy=p.y, r=r, fill=colour, stroke="white", stroke_width=3))
        parts.append(text(p.x + dx, p.y + dy, label, size=20, weight="700", fill="#17212b", extra='style="paint-order: stroke; stroke: white; stroke-width: 6px; stroke-linejoin: round;"'))
    # North arrow and scale bar in the detailed map.
    nx, ny = detail_drawn.x + detail_drawn.w - 86, detail_drawn.y + 126
    parts.append(tag("line", x1=nx, y1=ny + 64, x2=nx, y2=ny, stroke="#23313d", stroke_width=4, marker_end="url(#arrow)"))
    parts.append(text(nx, ny - 15, "N", size=24, weight="700", anchor="middle"))
    scale_px = 50_000 * float(detail_data["scale_px_per_m"])
    sx, sy = detail_drawn.x + 58, detail_drawn.y + detail_drawn.h - 64
    parts.append(tag("line", x1=sx, y1=sy, x2=sx + scale_px, y2=sy, stroke="#17212b", stroke_width=4))
    for tick_x in [sx, sx + scale_px / 2, sx + scale_px]:
        parts.append(tag("line", x1=tick_x, y1=sy - 13, x2=tick_x, y2=sy + 13, stroke="#17212b", stroke_width=3))
    parts.append(text(sx, sy + 42, "0", size=20, anchor="middle"))
    parts.append(text(sx + scale_px / 2, sy + 42, "25", size=20, anchor="middle"))
    parts.append(text(sx + scale_px, sy + 42, "50 km", size=20, anchor="middle"))

    legend_x, legend_y = 92, 1238
    parts.append(text(legend_x, legend_y, "Legend", size=20, weight="700"))
    entries = [
        ("corridor footprint", "#e7803c", "rect"),
        ("corridor centreline", "#124f80", "dash"),
        ("2011 event reference", "#c8352c", "circle"),
        ("Kamaishi proxy", "#226da8", "circle"),
        ("selected wet interface", "#14865c", "circle"),
    ]
    lx = legend_x + 92
    for idx, (label, colour, kind) in enumerate(entries):
        x = lx + idx * 360
        y = legend_y - 8
        if kind == "rect":
            parts.append(tag("rect", x=x, y=y - 15, width=38, height=22, fill=colour, fill_opacity=0.25, stroke="#b95025", stroke_width=3))
        elif kind == "dash":
            parts.append(tag("line", x1=x, y1=y - 4, x2=x + 42, y2=y - 4, stroke=colour, stroke_width=4, stroke_dasharray="12 8"))
        else:
            parts.append(tag("circle", cx=x + 19, cy=y - 4, r=10, fill=colour, stroke="white", stroke_width=3))
        parts.append(text(x + 54, y + 3, label, size=18, fill="#26323b"))
    parts.append(text(90, 1336, "CRS: EPSG:32654; vertical colour relief from ETOPO 2022 EGM2008. Geometry is the accepted G6/R19 delivery corridor.", size=17, fill="#59636b"))
    parts.append("</svg>")

    svg = PUBLICATION_ROOT / "figure_R20_A_geographic_corridor_context.svg"
    png = PUBLICATION_ROOT / "figure_R20_A_geographic_corridor_context.png"
    pdf = PUBLICATION_ROOT / "figure_R20_A_geographic_corridor_context.pdf"
    write_text(svg, "\n".join(parts))
    convert_record = convert_svg(svg, png, pdf, width_px=3600)
    preview = PREVIEW_ROOT / png.name
    shutil.copy2(png, preview)
    provenance = {
        "schema": {"name": "tsunami.r20.figure_provenance", "version": "1.0.0"},
        "figure_id": "R20-A",
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "caption": "Top-down geographic context map showing northeast Honshu, Kamaishi, the 2011 Tohoku epicentral reference and the accepted G6/R19 Kamaishi corridor footprint and centreline.",
        "workflow": "GDAL colour relief from R16 QGIS-derived ETOPO rasters, OGR vector read of R16 corridor/coastline layers, direct SVG cartographic composition, rsvg-convert export.",
        "inputs": {
            "japan_raster": file_record(JAPAN_RASTER),
            "detail_raster": file_record(DETAIL_RASTER),
            "japan_coastline": file_record(JAPAN_COASTLINE),
            "detail_coastline": file_record(DETAIL_COASTLINE),
            "corridor_polygon": file_record(CORRIDOR_POLYGON),
            "corridor_centreline": file_record(CORRIDOR_CENTRELINE),
            "domain_geometry": file_record(R19_GEOMETRY),
        },
        "derived_sources": {"relief_records": relief_records, "japan_relief": file_record(japan_png), "detail_relief": file_record(detail_png)},
        "outputs": {"svg": file_record(svg), "pdf": file_record(pdf), "png": file_record(png), "preview": file_record(preview)},
        "conversion": convert_record,
        "scientific_limits": ["context figure only", "no new simulation", "not a validation claim"],
    }
    write_json(PROVENANCE_ROOT / "figure_R20_A_geographic_corridor_context.provenance.json", provenance)
    return provenance


def render_blender_candidates() -> dict[str, Any]:
    candidates = [
        {"id": "soft", "vertical_exaggeration": 4.0, "sea_alpha": 0.055, "fill_alpha": 0.11, "outline": 0.75},
        {"id": "balanced", "vertical_exaggeration": 4.0, "sea_alpha": 0.075, "fill_alpha": 0.15, "outline": 0.80},
        {"id": "relief", "vertical_exaggeration": 6.0, "sea_alpha": 0.060, "fill_alpha": 0.12, "outline": 0.75},
    ]
    records: dict[str, Any] = {}
    for candidate in candidates:
        raw = BLENDER_ROOT / "candidates" / f"figure_R20_B_candidate_{candidate['id']}.png"
        record = PROVENANCE_ROOT / f"figure_R20_B_candidate_{candidate['id']}_render.json"
        cmd = [
            "blender",
            "--factory-startup",
            "--background",
            "--python",
            R17_RENDER_SCRIPT.as_posix(),
            "--",
            "--terrain",
            R17_TERRAIN.as_posix(),
            "--output",
            raw.as_posix(),
            "--record",
            record.as_posix(),
            "--vertical-exaggeration",
            str(candidate["vertical_exaggeration"]),
            "--width",
            "2500",
            "--height",
            "1450",
            "--camera",
            "orthographic",
            "--sea-alpha",
            str(candidate["sea_alpha"]),
            "--corridor-fill-alpha",
            str(candidate["fill_alpha"]),
            "--corridor-outline-scale",
            str(candidate["outline"]),
        ]
        code, output = command_output(cmd, env={"ALSOFT_DRIVERS": "null"}, check=True)
        records[candidate["id"]] = {
            **candidate,
            "command": cmd,
            "return_code": code,
            "stdout_tail": output[-4000:],
            "raw_output": file_record(raw),
            "render_record": file_record(record),
        }

    selected = "balanced"
    final_raw = BLENDER_ROOT / "figure_R20_B_selected_raw_blender.png"
    final_record = PROVENANCE_ROOT / "figure_R20_B_selected_blender_render.json"
    final_blend = BLENDER_ROOT / "figure_R20_B_selected_scene.blend"
    chosen = next(candidate for candidate in candidates if candidate["id"] == selected)
    cmd = [
        "blender",
        "--factory-startup",
        "--background",
        "--python",
        R17_RENDER_SCRIPT.as_posix(),
        "--",
        "--terrain",
        R17_TERRAIN.as_posix(),
        "--output",
        final_raw.as_posix(),
        "--record",
        final_record.as_posix(),
        "--vertical-exaggeration",
        str(chosen["vertical_exaggeration"]),
        "--width",
        "4800",
        "--height",
        "2800",
        "--camera",
        "orthographic",
        "--sea-alpha",
        str(chosen["sea_alpha"]),
        "--corridor-fill-alpha",
        str(chosen["fill_alpha"]),
        "--corridor-outline-scale",
        str(chosen["outline"]),
        "--save-blend",
        final_blend.as_posix(),
    ]
    code, output = command_output(cmd, env={"ALSOFT_DRIVERS": "null"}, check=True)
    blend_backup = final_blend.with_suffix(".blend1")
    if blend_backup.exists():
        blend_backup.unlink()
    return {
        "candidate_records": records,
        "selected_candidate": selected,
        "selection_parameters": chosen,
        "selection_rationale": (
            "The balanced 4x vertical-exaggeration candidate was selected because it preserves the R17 accepted "
            "relief interpretation, keeps the sea-level plane visible without washing out shelf texture, and gives "
            "the accepted corridor footprint enough contrast for poster use without dominating the terrain."
        ),
        "final_raw": file_record(final_raw),
        "final_blender_record": file_record(final_record),
        "final_blend": file_record(final_blend),
        "final_blend_backup_removed": not blend_backup.exists(),
        "final_command": cmd,
        "final_return_code": code,
        "final_stdout_tail": output[-4000:],
    }


def magick_label_ops(x: int, y: int, label: str, *, size: int, fill: str, bold: bool = True, stroke: int = 7) -> list[str]:
    font = "Liberation-Sans-Bold" if bold else "Liberation-Sans"
    return [
        "-font",
        font,
        "-pointsize",
        str(size),
        "-fill",
        "white",
        "-stroke",
        "white",
        "-strokewidth",
        str(stroke),
        "-annotate",
        f"+{x}+{y}",
        label,
        "-fill",
        fill,
        "-stroke",
        "none",
        "-strokewidth",
        "0",
        "-annotate",
        f"+{x}+{y}",
        label,
    ]


def generate_oblique_figure(blender: dict[str, Any]) -> dict[str, Any]:
    raw = BLENDER_ROOT / "figure_R20_B_selected_raw_blender.png"
    pdf = PUBLICATION_ROOT / "figure_R20_B_oblique_bathymetry_topography.pdf"
    png = PUBLICATION_ROOT / "figure_R20_B_oblique_bathymetry_topography.png"
    preview = PREVIEW_ROOT / png.name
    stale_svg = SVG_SOURCE_ROOT / "figure_R20_B_oblique_bathymetry_annotated.svg"
    if stale_svg.exists():
        stale_svg.unlink()
    draw_source = IMAGEMAGICK_ROOT / "figure_R20_B_annotation_command.json"
    magick_png_cmd = [
        "magick",
        raw.as_posix(),
        "-alpha",
        "remove",
        "-alpha",
        "off",
        "-stroke",
        "#24313b",
        "-strokewidth",
        "5",
        "-draw",
        "line 2890,830 2768,900",
        "-stroke",
        "#8d4a36",
        "-strokewidth",
        "8",
        "-draw",
        "line 1845,1932 2130,1655 polygon 2130,1655 2070,1670 2112,1718",
        "-stroke",
        "#8d4a36",
        "-strokewidth",
        "5",
        "-draw",
        "line 3090,1196 2850,1060",
        "-stroke",
        "white",
        "-strokewidth",
        "9",
        "-fill",
        "#b55745",
        "-draw",
        "circle 2472,640 2493,640",
        *magick_label_ops(2500, 625, "Kamaishi coast", size=68, fill="#24313b", stroke=7),
        *magick_label_ops(2920, 810, "selected wet nearshore interface", size=46, fill="#24313b", stroke=6),
        *magick_label_ops(1620, 1998, "deep-water propagation path", size=50, fill="#6f3e2e", stroke=6),
        *magick_label_ops(3120, 1230, "accepted Regional2D corridor footprint", size=45, fill="#663a2d", stroke=6),
        "-stroke",
        "#d2d5cf",
        "-strokewidth",
        "2",
        "-fill",
        "rgba(255,255,255,0.82)",
        "-draw",
        "roundrectangle 210,2260 1370,2620 16,16",
        "-draw",
        "roundrectangle 3230,2375 4590,2565 16,16",
        "-fill",
        "#5e9cc1",
        "-stroke",
        "none",
        "-draw",
        "rectangle 266,2372 336,2402",
        "-fill",
        "#b5cfa0",
        "-draw",
        "rectangle 266,2435 336,2465",
        "-fill",
        "#bedfec",
        "-stroke",
        "#8eb5c5",
        "-strokewidth",
        "2",
        "-draw",
        "rectangle 266,2498 336,2528",
        *magick_label_ops(260, 2328, "Visual key", size=43, fill="#26323b", stroke=3),
        *magick_label_ops(358, 2401, "bathymetry below z = 0 m", size=35, fill="#26323b", bold=False, stroke=3),
        *magick_label_ops(358, 2464, "land topography above z = 0 m", size=35, fill="#26323b", bold=False, stroke=3),
        *magick_label_ops(358, 2527, "EGM2008 sea-level reference plane", size=35, fill="#26323b", bold=False, stroke=3),
        *magick_label_ops(3275, 2442, "ETOPO 2022 EGM2008 terrain; EPSG:32654", size=36, fill="#26323b", bold=False, stroke=3),
        *magick_label_ops(3275, 2488, "Vertical exaggeration: 4x. Terrain render only; no new simulation.", size=36, fill="#26323b", bold=False, stroke=3),
        png.as_posix(),
    ]
    png_code, png_output = command_output(magick_png_cmd, check=True)
    magick_pdf_cmd = ["magick", png.as_posix(), pdf.as_posix()]
    pdf_code, pdf_output = command_output(magick_pdf_cmd, check=True)
    write_json(
        draw_source,
        {
            "schema": {"name": "tsunami.r20.imagemagick_annotation", "version": "1.0.0"},
            "status": "COMPLETE",
            "generated_at_utc": utc_now(),
            "png_command": magick_png_cmd,
            "png_return_code": png_code,
            "png_output": png_output,
            "pdf_command": magick_pdf_cmd,
            "pdf_return_code": pdf_code,
            "pdf_output": pdf_output,
        },
    )
    convert_record = {"png": {"command": magick_png_cmd, "return_code": png_code, "output": png_output}, "pdf": {"command": magick_pdf_cmd, "return_code": pdf_code, "output": pdf_output}}
    shutil.copy2(png, preview)
    contact = generate_bathymetry_contact_sheet(blender)
    provenance = {
        "schema": {"name": "tsunami.r20.figure_provenance", "version": "1.0.0"},
        "figure_id": "R20-B",
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "caption": (
            "Oblique Blender terrain visualisation of the accepted Kamaishi corridor using the R17/R16 ETOPO 2022 "
            "bathymetry/topography lineage. The selected final uses 4x vertical exaggeration, a restrained sea-level "
            "plane and a subtle accepted-corridor overlay; annotations are interpretive labels only."
        ),
        "workflow": "Blender EEVEE render from real ETOPO raster mesh, ImageMagick annotation over selected render, ImageMagick PDF/PNG export.",
        "inputs": {
            "terrain": file_record(R17_TERRAIN),
            "render_script": file_record(R17_RENDER_SCRIPT),
            "corridor_polygon": file_record(CORRIDOR_POLYGON),
            "r17_provenance": file_record(R17_ROOT / "provenance/figure_C_corridor_bathymetry_3d.provenance.json"),
            "r18_annotated_reference": file_record(R18_ROOT / "publication/figure_C_bathymetry_3d_annotated.png"),
        },
        "candidate_selection": blender,
        "outputs": {
            "pdf": file_record(pdf),
            "png": file_record(png),
            "preview": file_record(preview),
            "raw_blender_png": file_record(raw),
            "imagemagick_annotation_source": file_record(draw_source),
            "blend": blender["final_blend"],
            "contact_sheet": file_record(contact),
        },
        "svg_publication_note": "No final publication SVG is provided because the core figure is a raster Blender terrain render; annotation/export is recorded as an ImageMagick source command.",
        "conversion": convert_record,
        "scientific_limits": ["terrain render only", "not a Local3D replay result", "not a validation claim"],
    }
    write_json(PROVENANCE_ROOT / "figure_R20_B_oblique_bathymetry_topography.provenance.json", provenance)
    return provenance


def generate_bathymetry_contact_sheet(blender: Mapping[str, Any]) -> Path:
    width, height = 900, 522
    gap = 34
    label_h = 82
    sheet_w = width * 3 + gap * 4
    sheet_h = height + label_h + 76
    svg = SVG_SOURCE_ROOT / "figure_R20_B_candidate_contact_sheet.svg"
    png = PREVIEW_ROOT / "figure_R20_B_candidate_contact_sheet.png"
    order = ["soft", "balanced", "relief"]
    labels = {
        "soft": "soft sea plane / 4x VE",
        "balanced": "selected balanced / 4x VE",
        "relief": "stronger relief / 6x VE",
    }
    parts = [
        f'<svg xmlns="{SVG_NS}" xmlns:xlink="{XLINK_NS}" width="{sheet_w}" height="{sheet_h}" viewBox="0 0 {sheet_w} {sheet_h}">',
        tag("rect", x=0, y=0, width=sheet_w, height=sheet_h, fill="#f7f8f6"),
        text(55, 58, "R20-B candidate comparison", size=38, weight="700"),
    ]
    for idx, key in enumerate(order):
        x = gap + idx * (width + gap)
        y = label_h
        path = Path(blender["candidate_records"][key]["raw_output"]["path"])
        if not path.is_absolute():
            path = REPO_ROOT / path
        thumb = BLENDER_ROOT / "candidates" / f"figure_R20_B_candidate_{key}_thumb.png"
        command_output(["magick", path.as_posix(), "-resize", f"{width}x{height}!", thumb.as_posix()], check=True)
        parts.append(tag("image", href=embed_png(thumb), x=x, y=y, width=width, height=height, preserveAspectRatio="none"))
        parts.append(tag("rect", x=x, y=y, width=width, height=height, fill="none", stroke="#cdd2cc", stroke_width=4))
        if key == "balanced":
            parts.append(tag("rect", x=x + 14, y=y + 14, width=width - 28, height=height - 28, fill="none", stroke="#b95025", stroke_width=8))
        parts.append(text(x + 22, y + height + 48, labels[key], size=34, weight="700" if key == "balanced" else "400"))
    parts.append("</svg>")
    write_text(svg, "\n".join(parts))
    command_output(["rsvg-convert", "-f", "png", "-w", "3600", "-o", png.as_posix(), svg.as_posix()], check=True)
    return png


def load_profile() -> tuple[np.ndarray, np.ndarray]:
    distances: list[float] = []
    elevations: list[float] = []
    with R19_PROFILE.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            distances.append(float(row["distance_offshore_km"]))
            elevations.append(float(row["bed_elevation_m"]))
    return np.array(distances, dtype=float), np.array(elevations, dtype=float)


def data_mapper(x_min: float, x_max: float, y_min: float, y_max: float, plot: Box) -> Any:
    def mapper(x: float, y: float) -> Point:
        sx = plot.x + (x_max - x) / (x_max - x_min) * plot.w
        sy = plot.y + (y_max - y) / (y_max - y_min) * plot.h
        return Point(sx, sy)

    return mapper


def generate_hybrid_longitudinal() -> dict[str, Any]:
    distances, elevations = load_profile()
    geometry = read_json(R19_GEOMETRY)
    milestones = read_json(R19_MILESTONES)
    local3d_km = geometry["local3d"]["dimensions_m"]["length"] / 1000.0
    x_min, x_max = -2.0, 124.0
    y_min, y_max = -1150.0, 160.0
    width, height = 2500, 1250
    plot = Box(170, 220, 1980, 720)
    mapper = data_mapper(x_min, x_max, y_min, y_max, plot)
    svg = PUBLICATION_ROOT / "figure_R20_C_hybrid_longitudinal_2d3d_corridor.svg"
    png = PUBLICATION_ROOT / "figure_R20_C_hybrid_longitudinal_2d3d_corridor.png"
    pdf = PUBLICATION_ROOT / "figure_R20_C_hybrid_longitudinal_2d3d_corridor.pdf"
    raw_b = BLENDER_ROOT / "figure_R20_B_selected_raw_blender.png"
    cue_thumb = BLENDER_ROOT / "figure_R20_C_terrain_cue_thumbnail.png"
    command_output(["magick", raw_b.as_posix(), "-resize", "640x374!", cue_thumb.as_posix()], check=True)

    def rect_for_band(x0: float, x1: float, colour: str, opacity: float) -> str:
        left = mapper(max(x0, x1), y_max)
        right = mapper(min(x0, x1), y_min)
        return tag("rect", x=left.x, y=left.y, width=right.x - left.x, height=right.y - left.y, fill=colour, opacity=opacity)

    bed_points = [mapper(float(x), float(y)) for x, y in zip(distances, elevations, strict=True)]
    bed_path = "M " + " L ".join(f"{p.x:.2f} {p.y:.2f}" for p in bed_points)
    zero_left = mapper(x_max, 0)
    zero_right = mapper(x_min, 0)
    parts = [
        f'<svg xmlns="{SVG_NS}" xmlns:xlink="{XLINK_NS}" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        "<defs>",
        '<filter id="soft-halo" x="-20%" y="-20%" width="140%" height="140%"><feFlood flood-color="white" flood-opacity="0.88"/><feComposite in2="SourceGraphic" operator="out"/><feGaussianBlur stdDeviation="2.2"/><feComposite in2="SourceGraphic" operator="over"/></filter>',
        '<marker id="arrow-c" markerWidth="12" markerHeight="12" refX="10" refY="6" orient="auto"><path d="M 0 0 L 12 6 L 0 12 z" fill="#7b4a32"/></marker>',
        "</defs>",
        tag("rect", x=0, y=0, width=width, height=height, fill="#fbfbf8"),
        text(170, 88, "Hybrid corridor interpretation from real longitudinal bathymetry", size=38, weight="700"),
        text(170, 126, "R10 h400 accepted corridor profile with nearshore 2D-to-3D forcing context; no new simulation output", size=21, fill="#53606a"),
        tag("rect", x=plot.x, y=plot.y, width=plot.w, height=plot.h, fill="#ffffff", stroke="#273440", stroke_width=2.2),
        rect_for_band(124, 85, "#dfe8ef", 0.68),
        rect_for_band(85, 12, "#e7ead7", 0.72),
        rect_for_band(12, 0, "#f3e6bf", 0.78),
        rect_for_band(0, -local3d_km, "#ead1c1", 0.78),
        tag("line", x1=zero_left.x, y1=zero_left.y, x2=zero_right.x, y2=zero_right.y, stroke="#8b9299", stroke_width=2, stroke_dasharray="8 8"),
    ]
    for y in [-1000, -750, -500, -250, 0]:
        p0, p1 = mapper(x_max, y), mapper(x_min, y)
        parts.append(tag("line", x1=p0.x, y1=p0.y, x2=p1.x, y2=p1.y, stroke="#d8ddd9", stroke_width=1.2))
        parts.append(text(plot.x - 20, p0.y + 6, f"{y}", size=19, anchor="end", fill="#3e4851"))
    for x in [120, 100, 80, 60, 40, 20, 0]:
        p0, p1 = mapper(x, y_min), mapper(x, y_min - 18)
        parts.append(tag("line", x1=p0.x, y1=p0.y, x2=p1.x, y2=p1.y, stroke="#273440", stroke_width=2))
        parts.append(text(p1.x, p1.y + 35, f"{x}", size=19, anchor="middle", fill="#3e4851"))
    parts.extend(
        [
            text(plot.x + plot.w / 2, plot.y + plot.h + 92, "Distance offshore from selected wet nearshore interface (km)", size=26, anchor="middle", fill="#26323b"),
            text(58, plot.y + plot.h / 2, "Bed elevation (m, EGM2008)", size=26, anchor="middle", fill="#26323b", extra='transform="rotate(-90 58 580)"'),
            text(520, 194, "deep-water propagation", size=24, weight="700", fill="#31475b"),
            text(1140, 194, "shelf / shoaling influence", size=24, weight="700", fill="#526137"),
            text(1856, 194, "nearshore coupling", size=24, weight="700", fill="#765c26"),
            text(2050, 194, "candidate Local3D", size=22, weight="700", fill="#774536"),
            tag("path", d=bed_path, fill="none", stroke="#133858", stroke_width=6, stroke_linejoin="round", stroke_linecap="round"),
        ]
    )
    for milestone in milestones["milestones"]:
        if "distance_offshore_km" not in milestone:
            continue
        x = float(milestone["distance_offshore_km"])
        if x < x_min or x > x_max:
            continue
        p_top, p_bottom = mapper(x, y_max), mapper(x, y_min)
        colour = "#c8352c" if milestone["id"] == "M0" else "#14865c" if milestone["id"] == "M4" else "#80503b"
        dash = "10 8" if milestone["id"] in {"M0", "M4", "M5"} else "6 8"
        parts.append(tag("line", x1=p_top.x, y1=plot.y, x2=p_bottom.x, y2=plot.y + plot.h, stroke=colour, stroke_width=2.4, stroke_dasharray=dash, opacity=0.85))
        label = {"M0": "2011 event ref.", "M4": "selected wet interface", "M5": "Local3D length"}.get(milestone["id"], milestone["id"])
        y_label = 996 if milestone["id"] == "M4" else 164
        parts.append(text(p_top.x, y_label, label, size=20, weight="700", fill=colour, anchor="middle", extra='style="paint-order: stroke; stroke: white; stroke-width: 5px; stroke-linejoin: round;"'))

    p_l3d_a, p_l3d_b = mapper(0.0, -1090), mapper(-local3d_km, -1090)
    parts.append(tag("line", x1=p_l3d_a.x, y1=p_l3d_a.y, x2=p_l3d_b.x, y2=p_l3d_b.y, stroke="#7b4a32", stroke_width=4, marker_end="url(#arrow-c)"))
    parts.append(text((p_l3d_a.x + p_l3d_b.x) / 2, p_l3d_a.y + 35, f"L3D = {local3d_km:.3f} km", size=18, fill="#7b4a32", anchor="middle"))

    inset = Box(1848, 318, 460, 268)
    parts.append(tag("rect", x=inset.x - 10, y=inset.y - 10, width=inset.w + 20, height=inset.h + 78, rx=12, fill="#ffffff", fill_opacity=0.88, stroke="#d2d5cf", stroke_width=1.8))
    parts.append(tag("image", href=embed_png(cue_thumb), x=inset.x, y=inset.y, width=inset.w, height=inset.h, preserveAspectRatio="xMidYMid slice"))
    parts.append(text(inset.x + 18, inset.y + inset.h + 42, "3D terrain cue: ETOPO corridor render, not simulation output", size=18, fill="#26323b"))
    near = mapper(4.5, -120)
    parts.append(tag("line", x1=inset.x + 90, y1=inset.y + inset.h, x2=near.x, y2=near.y, stroke="#7b4a32", stroke_width=2.5, stroke_dasharray="7 7"))

    callout = Box(190, 1064, 1540, 116)
    parts.append(tag("rect", x=callout.x, y=callout.y, width=callout.w, height=callout.h, rx=13, fill="#ffffff", fill_opacity=0.92, stroke="#d7d9d4", stroke_width=1.6))
    parts.append(
        multiline_text(
            callout.x + 28,
            callout.y + 38,
            [
                "Interpretation: the bed profile is sampled from the accepted h400/R10 corridor; shaded bands are physically motivated regions over that profile.",
                "The one-way 2D-to-3D concept is marked at the selected wet interface; the Local3D zone is framework context only.",
            ],
            size=19,
            line_height=1.35,
            fill="#26323b",
        )
    )
    parts.append(text(170, 1222, "Profile source: deliverables/figures/r19_tikz/data/bathymetry_profile.csv; no thresholds beyond accepted geometry are introduced.", size=17, fill="#606a72"))
    parts.append("</svg>")

    write_text(svg, "\n".join(parts))
    convert_record = convert_svg(svg, png, pdf, width_px=3600)
    preview = PREVIEW_ROOT / png.name
    shutil.copy2(png, preview)
    profile_copy = DATA_ROOT / "r20_longitudinal_bathymetry_profile.csv"
    shutil.copy2(R19_PROFILE, profile_copy)
    provenance = {
        "schema": {"name": "tsunami.r20.figure_provenance", "version": "1.0.0"},
        "figure_id": "R20-C",
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "caption": "Hybrid longitudinal corridor figure combining the real R10 h400 bed-elevation profile with physically motivated propagation/coupling regions and a small oblique ETOPO terrain cue.",
        "workflow": "Direct SVG plotting from R19 profile CSV and geometry JSON, embedded selected R20-B Blender terrain cue, rsvg-convert export.",
        "inputs": {
            "profile_csv": file_record(R19_PROFILE),
            "profile_copy": file_record(profile_copy),
            "domain_geometry": file_record(R19_GEOMETRY),
            "milestone_positions": file_record(R19_MILESTONES),
            "terrain_cue_raw": file_record(raw_b),
            "terrain_cue_thumbnail": file_record(cue_thumb),
        },
        "profile_summary": {
            "sample_count": int(distances.size),
            "distance_min_km": float(np.min(distances)),
            "distance_max_km": float(np.max(distances)),
            "bed_min_m": float(np.min(elevations)),
            "bed_max_m": float(np.max(elevations)),
            "local3d_length_km": float(local3d_km),
        },
        "outputs": {"svg": file_record(svg), "pdf": file_record(pdf), "png": file_record(png), "preview": file_record(preview)},
        "conversion": convert_record,
        "scientific_limits": ["framework interpretation over real profile", "not a new Local3D result", "no new thresholds claimed"],
    }
    write_json(PROVENANCE_ROOT / "figure_R20_C_hybrid_longitudinal_2d3d_corridor.provenance.json", provenance)
    return provenance


def image_info(path: Path) -> dict[str, Any]:
    code, output = command_output(["identify", "-format", "%w %h %[mean]", path.as_posix()], check=True)
    width_s, height_s, mean_s = output.split()
    return {"path": file_record(path), "width_px": int(width_s), "height_px": int(height_s), "mean": float(mean_s), "nonblank": float(mean_s) > 0.0, "return_code": code}


def validate_outputs(provenance: Mapping[str, Any]) -> dict[str, Any]:
    figure_outputs = {
        "R20-A": {
            "pdf": PUBLICATION_ROOT / "figure_R20_A_geographic_corridor_context.pdf",
            "svg": PUBLICATION_ROOT / "figure_R20_A_geographic_corridor_context.svg",
            "png": PUBLICATION_ROOT / "figure_R20_A_geographic_corridor_context.png",
        },
        "R20-B": {
            "pdf": PUBLICATION_ROOT / "figure_R20_B_oblique_bathymetry_topography.pdf",
            "png": PUBLICATION_ROOT / "figure_R20_B_oblique_bathymetry_topography.png",
            "blend": BLENDER_ROOT / "figure_R20_B_selected_scene.blend",
        },
        "R20-C": {
            "pdf": PUBLICATION_ROOT / "figure_R20_C_hybrid_longitudinal_2d3d_corridor.pdf",
            "svg": PUBLICATION_ROOT / "figure_R20_C_hybrid_longitudinal_2d3d_corridor.svg",
            "png": PUBLICATION_ROOT / "figure_R20_C_hybrid_longitudinal_2d3d_corridor.png",
        },
    }
    checks: dict[str, Any] = {"figures": {}, "json": {}, "commands": {}}
    for fig_id, outputs in figure_outputs.items():
        record: dict[str, Any] = {}
        for kind, path in outputs.items():
            if kind == "pdf":
                record[kind] = {"path": file_record(path), "nonempty": path.is_file() and path.stat().st_size > 1000}
            elif kind == "svg":
                try:
                    ET.parse(path)
                    record[kind] = {"path": file_record(path), "parses": True, "error": None}
                except Exception as exc:
                    record[kind] = {"path": file_record(path), "parses": False, "error": str(exc)}
            elif kind == "png":
                record[kind] = image_info(path)
            elif kind == "blend":
                record[kind] = {"path": file_record(path), "nonempty": path.is_file() and path.stat().st_size > 1000}
        checks["figures"][fig_id] = record
    for path in sorted(PROVENANCE_ROOT.glob("*.json")):
        try:
            json.loads(path.read_text(encoding="utf-8"))
            checks["json"][display_path(path)] = "PASS"
        except Exception as exc:
            checks["json"][display_path(path)] = f"FAIL: {exc}"
    for name, cmd in {
        "py_compile": [sys.executable, "-m", "py_compile", __file__],
        "qgis_version": ["qgis", "--version"],
        "blender_version": ["blender", "--version"],
        "git_diff_check": ["git", "diff", "--check"],
    }.items():
        code, output = command_output(cmd)
        checks["commands"][name] = {"return_code": code, "output": output[:4000]}
    checks["overall_pass"] = (
        all(record.get("pdf", {}).get("nonempty", True) for record in checks["figures"].values())
        and all(record.get("png", {}).get("nonblank", True) for record in checks["figures"].values())
        and all(record.get("svg", {}).get("parses", True) for record in checks["figures"].values())
        and all(value == "PASS" for value in checks["json"].values())
        and checks["commands"]["py_compile"]["return_code"] == 0
        and checks["commands"]["git_diff_check"]["return_code"] == 0
    )
    checks["status"] = "COMPLETE" if checks["overall_pass"] else "FAILED"
    checks["generated_at_utc"] = utc_now()
    write_json(PROVENANCE_ROOT / "r20_validation.json", checks)
    return checks


def write_manifest(provenances: Mapping[str, Any], validation: Mapping[str, Any]) -> dict[str, Any]:
    manifest = {
        "schema": {"name": "tsunami.r20.figure_manifest", "version": "1.0.0"},
        "status": "COMPLETE" if validation.get("overall_pass") else "FAILED_VALIDATION",
        "generated_at_utc": utc_now(),
        "branch": git_branch(),
        "starting_head": STARTING_HEAD,
        "generation_head": git_sha(),
        "worktree": WORKTREE_PATH.as_posix(),
        "figures": {
            key: {
                "status": value["status"],
                "caption": value["caption"],
                "outputs": value["outputs"],
                "scientific_limits": value["scientific_limits"],
            }
            for key, value in provenances.items()
        },
        "validation": validation,
        "reused_sources": {
            "r16_publication": file_record(R16_ROOT),
            "r17_closure": file_record(R17_ROOT),
            "r18_poster": file_record(R18_ROOT),
            "r19_tikz": file_record(R19_ROOT),
        },
        "guardrails": {
            "new_regional2d_runs": False,
            "new_local3d_runs": False,
            "calibration": False,
            "replay_or_retuning": False,
            "ai_image_generation": False,
            "synthetic_geography": False,
            "push_pr_merge_rebase_amend": False,
        },
    }
    write_json(PROVENANCE_ROOT / "r20_publication_figure_manifest.json", manifest)
    return manifest


def write_docs(manifest: Mapping[str, Any]) -> None:
    write_text(
        DOC_HANDOFF,
        f"""
        # R20 Publication Figure Handoff

        R20 creates a small figure-only refinement package under `deliverables/figures/r20_publication/`.
        The package reuses accepted R16--R19/G6 geometry and terrain sources, and performs no new Regional2D,
        Local3D, calibration, replay, mesh, source or solver work.

        ## Final figures

        | Figure | Final outputs | Intended use | Notes |
        |---|---|---|---|
        | R20-A geographic corridor context | `publication/figure_R20_A_geographic_corridor_context.{{pdf,svg,png}}` | Poster or report context figure | Strongest for showing Japan/Tohoku/Kamaishi and the accepted corridor in one clean map. |
        | R20-B oblique bathymetry/topography | `publication/figure_R20_B_oblique_bathymetry_topography.{{pdf,png}}` | Poster-primary terrain figure | Blender terrain render from real ETOPO data; no final SVG because the core evidence is raster terrain. Editable `.blend` retained in `sources/blender/`. |
        | R20-C hybrid longitudinal corridor | `publication/figure_R20_C_hybrid_longitudinal_2d3d_corridor.{{pdf,svg,png}}` | Report bridge figure and poster footer candidate | Combines the real corridor bed profile with nearshore transition / candidate Local3D context. |

        ## Caveats

        These figures are explanatory and cartographic. They do not imply completed historical validation,
        accepted Local3D replay behaviour, new production simulations, calibration, or new geometry authority.
        The Local3D zone in R20-C is a framework/candidate context derived from accepted G6 dimensions.

        ## Provenance

        Per-figure provenance JSON files and `r20_publication_figure_manifest.json` are in
        `deliverables/figures/r20_publication/provenance/`.
        """,
    )
    write_text(
        DOC_SELECTION,
        f"""
        # R20 Selection Rationale

        ## R20-A

        R20-A was selected as the final map because it materially improves the Japan/Tohoku/Kamaishi context:
        the national/regional panel establishes northeast Honshu, while the larger detailed panel keeps the
        accepted corridor, centreline, Kamaishi proxy, event reference and selected wet interface legible.
        It reuses R16 QGIS-derived ETOPO and coastline layers and R19 accepted corridor geometry.

        ## R20-B

        Three Blender candidates were rendered: soft 4x, balanced 4x and stronger-relief 6x. The balanced 4x
        candidate was selected because it preserves the previously accepted R17 terrain interpretation, improves
        corridor visibility, and keeps the sea-level plane visible without flattening the shelf-to-offshore relief.
        The 6x version made relief more dramatic but less conservative; the softer 4x version made the footprint
        too quiet for poster use.

        ## R20-C

        R20-C was selected because it keeps the real R19 longitudinal bed profile as the visual anchor, then adds
        restrained physically motivated zones behind the data. The embedded 3D cue is deliberately labelled as an
        ETOPO terrain cue rather than a simulation result, so the figure communicates the 2D-to-3D framework
        without overclaiming replay or validation status.
        """,
    )
    write_text(
        DOC_COMPLETION,
        f"""
        # R20 Completion Report

        1. **Branch:** {git_branch()}
        2. **Worktree:** {WORKTREE_PATH}
        3. **Starting HEAD:** {STARTING_HEAD}
        4. **Final HEAD:** Reported in the final assistant response after commit creation.
        5. **Figures created:** R20-A geographic corridor context; R20-B oblique bathymetry/topography; R20-C hybrid longitudinal 2D-to-3D corridor.
        6. **Existing sources reused:** R16 QGIS/ETOPO rasters and coastline/corridor layers; R17 Blender terrain/render lineage; R18 bathymetry annotation reference; R19 accepted geometry, profile and milestones.
        7. **Selected oblique figure:** R20-B balanced 4x vertical exaggeration candidate.
        8. **Selection reason:** balanced land/bathymetry contrast, readable corridor overlay, conservative relief and poster-suitable clarity.
        9. **Map/context improvement:** yes; the final map uses a national/Tohoku context panel plus a detailed Kamaishi corridor panel with clearer labels, scale and north arrow.
        10. **Hybrid longitudinal improvement:** yes; the final figure combines the real corridor bed profile with nearshore transition/coupling context and an embedded 3D terrain cue.
        11. **Validation:** see `deliverables/figures/r20_publication/provenance/r20_validation.json`.
        12. **No simulations/calibration/replay:** confirmed; no Regional2D, Local3D, calibration, replay, retuning, source, mesh or solver workflows were run.
        13. **Worktree clean:** reported after final commit in the final assistant response.
        """,
    )


def run_all() -> dict[str, Any]:
    ensure_layout()
    shutil.copy2(Path(__file__), PYTHON_SOURCE_ROOT / Path(__file__).name)
    provenance_a = generate_context_map()
    blender = render_blender_candidates()
    provenance_b = generate_oblique_figure(blender)
    provenance_c = generate_hybrid_longitudinal()
    provenances = {"R20-A": provenance_a, "R20-B": provenance_b, "R20-C": provenance_c}
    validation = validate_outputs(provenances)
    manifest = write_manifest(provenances, validation)
    write_docs(manifest)
    return {"manifest": manifest, "validation": validation}


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-blender", action="store_true", help="Reuse an existing selected R20-B raw render if present.")
    args = parser.parse_args(argv)
    if args.skip_blender:
        raise SystemExit("--skip-blender is intentionally not implemented for the freeze run; use full generation for provenance.")
    result = run_all()
    print(json.dumps({"status": "COMPLETE", "overall_pass": result["validation"]["overall_pass"]}, indent=2))
    return 0 if result["validation"]["overall_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
