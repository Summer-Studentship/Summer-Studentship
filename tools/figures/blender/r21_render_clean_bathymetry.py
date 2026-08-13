#!/usr/bin/env python3
"""Render clean R21 oblique bathymetry bases from the authoritative R20 scene.

Run with Blender, for example:

    ALSOFT_DRIVERS=null blender --factory-startup --background \
      --python tools/figures/blender/r21_render_clean_bathymetry.py -- [args]
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import xml.etree.ElementTree as ET
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Sequence

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Vector


REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_SCENE = REPO_ROOT / "deliverables/figures/r20_publication/sources/blender/figure_R20_B_selected_scene.blend"
R20_RENDER_RECORD = REPO_ROOT / "deliverables/figures/r20_publication/provenance/figure_R20_B_selected_blender_render.json"
R19_GEOMETRY = REPO_ROOT / "deliverables/figures/r19_tikz/data/domain_geometry.json"
CORRIDOR_POLYGON = REPO_ROOT / "deliverables/figures/r16_publication/sources/qgis/layers/corridor_polygon.geojson"
CORRIDOR_CENTRELINE = REPO_ROOT / "deliverables/figures/r16_publication/sources/qgis/layers/corridor_centreline.geojson"


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    args = list(argv)
    if "--" in args:
        args = args[args.index("--") + 1 :]
    else:
        args = []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-scene", type=Path, default=DEFAULT_SCENE)
    parser.add_argument("--output-b0", type=Path, required=True)
    parser.add_argument("--output-b1", type=Path, required=True)
    parser.add_argument("--overlay-svg", type=Path, required=True)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--save-b0-blend", type=Path)
    parser.add_argument("--save-b1-blend", type=Path)
    parser.add_argument("--width", type=int, default=4800)
    parser.add_argument("--height", type=int, default=2800)
    return parser.parse_args(args)


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(line.rstrip() for line in text.splitlines()) + "\n", encoding="utf-8")


def file_record(path: Path) -> dict[str, Any]:
    return {"path": path.as_posix(), "exists": path.exists(), "bytes": path.stat().st_size if path.is_file() else 0}


def scene_camera() -> bpy.types.Object:
    camera = bpy.context.scene.camera
    if camera is None:
        raise RuntimeError("R20 source scene has no active camera")
    return camera


def corridor_objects() -> list[bpy.types.Object]:
    names = ("Regional2D corridor subtle fill", "Regional2D corridor outline")
    return [obj for obj in bpy.data.objects if any(name in obj.name for name in names)]


def set_corridor_visibility(visible: bool) -> None:
    for obj in corridor_objects():
        obj.hide_render = not visible
        obj.hide_viewport = not visible


def render_png(path: Path, width: int, height: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.filepath = path.as_posix()
    scene.render.film_transparent = False
    bpy.ops.render.render(write_still=True)


def save_blend(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=path.as_posix())
    backup = path.with_suffix(".blend1")
    if backup.exists():
        backup.unlink()
    return file_record(path)


def geojson_lines(path: Path) -> list[list[tuple[float, float]]]:
    payload = read_json(path)
    lines: list[list[tuple[float, float]]] = []
    for feature in payload["features"]:
        geom = feature["geometry"]
        if geom["type"] == "LineString":
            lines.append([(float(x), float(y)) for x, y in geom["coordinates"]])
        elif geom["type"] == "Polygon":
            lines.extend([[(float(x), float(y)) for x, y in ring] for ring in geom["coordinates"][:1]])
    return lines


def svg_escape(value: Any) -> str:
    return (
        str(value)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def path_d(points: list[tuple[float, float]], project: Any, *, close: bool = False) -> str:
    projected = [project(x, y) for x, y in points]
    if not projected:
        return ""
    commands = [f"M {projected[0][0]:.2f} {projected[0][1]:.2f}"]
    commands.extend(f"L {x:.2f} {y:.2f}" for x, y in projected[1:])
    if close:
        commands.append("Z")
    return " ".join(commands)


def add(a: dict[str, float], b: dict[str, float]) -> dict[str, float]:
    return {"x": float(a["x"]) + float(b["x"]), "y": float(a["y"]) + float(b["y"])}


def scale(a: dict[str, float], value: float) -> dict[str, float]:
    return {"x": float(a["x"]) * value, "y": float(a["y"]) * value}


def projected_ground_point(origin: dict[str, float], tangent: dict[str, float], left_normal: dict[str, float], s_m: float, sigma_m: float) -> tuple[float, float]:
    point = add(add(origin, scale(tangent, s_m)), scale(left_normal, sigma_m))
    return point["x"], point["y"]


def write_overlay_svg(path: Path, width: int, height: int) -> dict[str, Any]:
    r20 = read_json(R20_RENDER_RECORD)
    geometry = read_json(R19_GEOMETRY)
    centre_origin = r20["terrain_mesh"]["centering_origin_m"]
    camera = scene_camera()
    scene = bpy.context.scene

    def project(x_m: float, y_m: float, z_km: float = 0.055) -> tuple[float, float]:
        world = Vector(((float(x_m) - centre_origin["x"]) / 1000.0, (float(y_m) - centre_origin["y"]) / 1000.0, z_km))
        co = world_to_camera_view(scene, camera, world)
        return co.x * width, (1.0 - co.y) * height

    polygon = geojson_lines(CORRIDOR_POLYGON)[0]
    centreline = geojson_lines(CORRIDOR_CENTRELINE)[0]
    event = geometry["event"]["epicentre_projected_m"]
    kamaishi = geometry["event"]["kamaishi_proxy_projected_m"]
    interface = geometry["corridor"]["interface_or_inland_end_projected_m"]
    source = geometry["corridor"]["source_side_start_projected_m"]
    tangent = geometry["corridor"]["tangent_unit"]
    left_normal = geometry["corridor"]["left_normal_unit"]

    arrow_start = projected_ground_point(source, tangent, left_normal, 28_000.0, 0.0)
    arrow_end = projected_ground_point(source, tangent, left_normal, 78_000.0, 0.0)
    ref_start = projected_ground_point(source, tangent, left_normal, 12_000.0, -3_250.0)
    ref_end = projected_ground_point(source, tangent, left_normal, 37_000.0, -3_250.0)
    tick_a0 = projected_ground_point(source, tangent, left_normal, 12_000.0, -3_750.0)
    tick_a1 = projected_ground_point(source, tangent, left_normal, 12_000.0, -2_750.0)
    tick_b0 = projected_ground_point(source, tangent, left_normal, 37_000.0, -3_750.0)
    tick_b1 = projected_ground_point(source, tangent, left_normal, 37_000.0, -2_750.0)

    def line_from_pair(a: tuple[float, float], b: tuple[float, float]) -> str:
        ax, ay = project(a[0], a[1])
        bx, by = project(b[0], b[1])
        return f"M {ax:.2f} {ay:.2f} L {bx:.2f} {by:.2f}"

    event_px = project(event["x"], event["y"], 0.080)
    kamaishi_px = project(kamaishi["x"], kamaishi["y"], 0.150)
    interface_px = project(interface["x"], interface["y"], 0.080)
    ref_mid = project((ref_start[0] + ref_end[0]) / 2.0, (ref_start[1] + ref_end[1]) / 2.0, 0.070)

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        "<defs>",
        '<marker id="propagation_arrow" markerWidth="18" markerHeight="18" refX="15" refY="9" orient="auto"><path d="M 0 0 L 18 9 L 0 18 z" fill="#7b4a32"/></marker>',
        "</defs>",
        '<g id="corridor">',
        f'<path d="{svg_escape(path_d(polygon, project, close=True))}" fill="#e7803c" fill-opacity="0.16" stroke="#b95025" stroke-width="3.5"/>',
        "</g>",
        '<g id="centreline">',
        f'<path d="{svg_escape(path_d(centreline, project))}" fill="none" stroke="#124f80" stroke-width="3.2" stroke-dasharray="18 12"/>',
        "</g>",
        '<g id="event">',
        f'<circle cx="{event_px[0]:.2f}" cy="{event_px[1]:.2f}" r="14" fill="#c8352c" stroke="white" stroke-width="5"/>',
        "</g>",
        '<g id="interface">',
        f'<circle cx="{interface_px[0]:.2f}" cy="{interface_px[1]:.2f}" r="13" fill="#14865c" stroke="white" stroke-width="5"/>',
        "</g>",
        '<g id="kamaishi">',
        f'<circle cx="{kamaishi_px[0]:.2f}" cy="{kamaishi_px[1]:.2f}" r="13" fill="#226da8" stroke="white" stroke-width="5"/>',
        "</g>",
        '<g id="propagation">',
        f'<path d="{line_from_pair(arrow_start, arrow_end)}" fill="none" stroke="#7b4a32" stroke-width="6" marker-end="url(#propagation_arrow)"/>',
        "</g>",
        '<g id="distance_reference">',
        f'<path d="{line_from_pair(ref_start, ref_end)}" fill="none" stroke="#26323b" stroke-width="4"/>',
        f'<path d="{line_from_pair(tick_a0, tick_a1)}" fill="none" stroke="#26323b" stroke-width="4"/>',
        f'<path d="{line_from_pair(tick_b0, tick_b1)}" fill="none" stroke="#26323b" stroke-width="4"/>',
        "</g>",
        '<g id="distance_reference_label">',
        f'<text x="{ref_mid[0]:.2f}" y="{ref_mid[1] + 34:.2f}" font-family="Arial, Helvetica, sans-serif" font-size="34" text-anchor="middle" fill="#26323b">25 km</text>',
        "</g>",
        "</svg>",
    ]
    write_text(path, "\n".join(svg))
    return {
        "source_camera": r20["camera_and_lighting"]["camera"],
        "terrain_centering_origin_m": centre_origin,
        "distance_reference": {
            "length_m": 25_000.0,
            "basis": "EPSG:32654 projected-ground segment parallel to the accepted corridor centreline tangent.",
            "start_projected_m": {"x": ref_start[0], "y": ref_start[1]},
            "end_projected_m": {"x": ref_end[0], "y": ref_end[1]},
        },
        "projected_markers_px": {
            "event": {"x": event_px[0], "y": event_px[1]},
            "kamaishi": {"x": kamaishi_px[0], "y": kamaishi_px[1]},
            "interface": {"x": interface_px[0], "y": interface_px[1]},
        },
    }


def main() -> int:
    args = parse_args(sys.argv)
    for path in [args.output_b0, args.output_b1, args.overlay_svg, args.record]:
        path.parent.mkdir(parents=True, exist_ok=True)

    bpy.ops.wm.open_mainfile(filepath=args.source_scene.as_posix())
    scene = bpy.context.scene
    scene.render.resolution_x = args.width
    scene.render.resolution_y = args.height

    corridor = corridor_objects()
    set_corridor_visibility(False)
    render_png(args.output_b0, args.width, args.height)
    b0_blend = save_blend(args.save_b0_blend)

    set_corridor_visibility(True)
    render_png(args.output_b1, args.width, args.height)
    b1_blend = save_blend(args.save_b1_blend)
    overlay_record = write_overlay_svg(args.overlay_svg, args.width, args.height)

    payload = {
        "schema": {"name": "tsunami.r21.clean_blender_render", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "source_scene": file_record(args.source_scene),
        "source_r20_render_record": file_record(R20_RENDER_RECORD),
        "source_domain_geometry": file_record(R19_GEOMETRY),
        "source_corridor_polygon": file_record(CORRIDOR_POLYGON),
        "source_corridor_centreline": file_record(CORRIDOR_CENTRELINE),
        "resolution": {"width": args.width, "height": args.height},
        "preserved_scientific_state": {
            "terrain_lineage": "R20 selected scene from R17/R16 ETOPO 2022 terrain source",
            "vertical_reference": "EGM2008 z=0 sea-level reference",
            "crs": "EPSG:32654",
            "vertical_exaggeration": 4.0,
            "camera_and_materials": "Opened from R20 selected .blend without scientific retuning.",
        },
        "corridor_objects": [obj.name for obj in corridor],
        "b0_change": "corridor overlay objects hidden; no text, arrows, legend, markers or labels added.",
        "b1_change": "corridor overlay objects restored; no text, arrows, legend, markers or labels added.",
        "overlay": overlay_record,
        "outputs": {
            "b0_png": file_record(args.output_b0),
            "b1_png": file_record(args.output_b1),
            "b2_overlay_svg": file_record(args.overlay_svg),
            "b0_blend": b0_blend,
            "b1_blend": b1_blend,
        },
        "blender": {"version": bpy.app.version_string, "python": sys.version.split()[0], "render_engine": scene.render.engine},
    }
    write_json(args.record, payload)
    print(json.dumps(payload, indent=2, sort_keys=True))
    bpy.ops.wm.quit_blender()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
