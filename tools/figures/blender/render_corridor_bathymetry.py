#!/usr/bin/env python3
"""Headless Blender renderer for the R17 corridor bathymetry figure.

Run with:

    ALSOFT_DRIVERS=null blender --factory-startup --background --python tools/figures/blender/render_corridor_bathymetry.py -- [args]
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Sequence

import bpy
import numpy as np
from mathutils import Vector
from osgeo import gdal


REPO_ROOT = Path(__file__).resolve().parents[3]
R17_ROOT = REPO_ROOT / "deliverables/figures/r17_closure"
TERRAIN_MANIFEST = R17_ROOT / "sources/blender/terrain/terrain_manifest.json"
DEFAULT_TERRAIN = R17_ROOT / "sources/blender/terrain/etopo_corridor_blender_utm54_200m.tif"
CORRIDOR_GEOJSON = REPO_ROOT / "deliverables/figures/r16_publication/sources/qgis/layers/corridor_polygon.geojson"
EVENT_POINTS_GEOJSON = REPO_ROOT / "deliverables/figures/r16_publication/sources/qgis/layers/event_and_kamaishi_points.geojson"


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def parse_blender_args(argv: Sequence[str]) -> argparse.Namespace:
    args = list(argv)
    if "--" in args:
        args = args[args.index("--") + 1 :]
    else:
        args = []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--terrain", type=Path, default=DEFAULT_TERRAIN)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--vertical-exaggeration", type=float, required=True)
    parser.add_argument("--width", type=int, default=2400)
    parser.add_argument("--height", type=int, default=1400)
    parser.add_argument("--save-blend", type=Path)
    return parser.parse_args(args)


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def make_material(name: str, colour: tuple[float, float, float, float], *, alpha: float | None = None) -> bpy.types.Material:
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    rgba = colour if alpha is None else (colour[0], colour[1], colour[2], alpha)
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        if "Base Color" in bsdf.inputs:
            bsdf.inputs["Base Color"].default_value = rgba
        if "Alpha" in bsdf.inputs:
            bsdf.inputs["Alpha"].default_value = rgba[3]
        if "Roughness" in bsdf.inputs:
            bsdf.inputs["Roughness"].default_value = 0.82
    mat.diffuse_color = rgba
    if rgba[3] < 1.0:
        mat.blend_method = "BLEND"
        mat.use_screen_refraction = False
        mat.show_transparent_back = True
    return mat


def load_raster(path: Path) -> tuple[np.ndarray, tuple[float, float, float, float, float, float]]:
    ds = gdal.Open(path.as_posix())
    if ds is None:
        raise RuntimeError(f"Could not open terrain raster: {path}")
    band = ds.GetRasterBand(1)
    arr = band.ReadAsArray().astype(np.float32)
    nodata = band.GetNoDataValue()
    if nodata is not None:
        arr = np.where(arr == nodata, np.nan, arr)
    if not np.isfinite(arr).any():
        raise RuntimeError("Terrain raster has no finite elevation samples")
    return arr, ds.GetGeoTransform()


def terrain_materials() -> tuple[list[bpy.types.Material], np.ndarray]:
    colours = [
        ("deep bathymetry", (0.060, 0.200, 0.380, 1.0)),
        ("outer shelf", (0.100, 0.340, 0.550, 1.0)),
        ("mid bathymetry", (0.200, 0.500, 0.700, 1.0)),
        ("shallow bathymetry", (0.620, 0.820, 0.880, 1.0)),
        ("sea-level transition", (0.920, 0.940, 0.880, 1.0)),
        ("low land", (0.550, 0.700, 0.420, 1.0)),
        ("upland", (0.680, 0.550, 0.360, 1.0)),
        ("high relief", (0.450, 0.350, 0.260, 1.0)),
    ]
    thresholds = np.array([-1200.0, -800.0, -400.0, -100.0, 0.0, 250.0, 700.0], dtype=np.float32)
    return [make_material(name, colour) for name, colour in colours], thresholds


def build_terrain(arr: np.ndarray, gt: tuple[float, float, float, float, float, float], exaggeration: float) -> tuple[bpy.types.Object, dict[str, Any]]:
    rows, cols = arr.shape
    fill_value = float(np.nanmin(arr))
    elevation = np.nan_to_num(arr, nan=fill_value)
    dx = float(gt[1])
    dy = float(gt[5])
    x = gt[0] + (np.arange(cols, dtype=np.float32) + 0.5) * dx
    y = gt[3] + (np.arange(rows, dtype=np.float32) + 0.5) * dy
    x0 = float((x.min() + x.max()) / 2.0)
    y0 = float((y.min() + y.max()) / 2.0)
    xx, yy = np.meshgrid((x - x0) / 1000.0, (y - y0) / 1000.0)
    zz = elevation * exaggeration / 1000.0

    verts = [(float(a), float(b), float(c)) for a, b, c in zip(xx.ravel(), yy.ravel(), zz.ravel(), strict=True)]
    faces = []
    for row in range(rows - 1):
        base = row * cols
        next_base = (row + 1) * cols
        for col in range(cols - 1):
            faces.append((base + col, base + col + 1, next_base + col + 1, next_base + col))

    mesh = bpy.data.meshes.new("r17_etopo_corridor_terrain")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("ETOPO 2022 corridor terrain", mesh)
    bpy.context.collection.objects.link(obj)

    materials, thresholds = terrain_materials()
    for mat in materials:
        mesh.materials.append(mat)
    face_elevation = 0.25 * (elevation[:-1, :-1] + elevation[:-1, 1:] + elevation[1:, :-1] + elevation[1:, 1:])
    material_index = np.digitize(face_elevation.ravel(), thresholds)
    for polygon, index in zip(mesh.polygons, material_index, strict=True):
        polygon.material_index = int(index)
        polygon.use_smooth = True

    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    try:
        bpy.ops.object.shade_smooth()
        modifier = obj.modifiers.new("smooth render normals", "WEIGHTED_NORMAL")
        modifier.weight = 50
        modifier.keep_sharp = True
    except Exception:
        pass
    obj.select_set(False)

    metadata = {
        "rows": rows,
        "cols": cols,
        "vertices": len(verts),
        "faces": len(faces),
        "horizontal_unit": "1 Blender unit = 1 km",
        "source_pixel_size_m": {"x": dx, "y": dy},
        "source_origin_m": {"x": gt[0], "y": gt[3]},
        "centering_origin_m": {"x": x0, "y": y0},
        "elevation_m": {
            "min": float(np.nanmin(arr)),
            "max": float(np.nanmax(arr)),
            "mean": float(np.nanmean(arr)),
        },
        "vertical_exaggeration": exaggeration,
    }
    return obj, metadata


def build_sea_plane(metadata: dict[str, Any]) -> bpy.types.Object:
    x_half = abs(metadata["source_pixel_size_m"]["x"]) * metadata["cols"] / 2000.0
    y_half = abs(metadata["source_pixel_size_m"]["y"]) * metadata["rows"] / 2000.0
    verts = [(-x_half, -y_half, 0.0), (x_half, -y_half, 0.0), (x_half, y_half, 0.0), (-x_half, y_half, 0.0)]
    mesh = bpy.data.meshes.new("egm2008_sea_level_plane")
    mesh.from_pydata(verts, [], [(0, 1, 2, 3)])
    mesh.update()
    obj = bpy.data.objects.new("EGM2008 z=0 sea-level reference plane", mesh)
    bpy.context.collection.objects.link(obj)
    obj.data.materials.append(make_material("subtle sea-level plane", (0.720, 0.860, 0.920, 0.09)))
    return obj


def geojson_polygon(path: Path) -> list[tuple[float, float]]:
    feature = read_json(path)["features"][0]
    coords = feature["geometry"]["coordinates"][0]
    return [(float(x), float(y)) for x, y in coords]


def build_corridor_overlay(metadata: dict[str, Any]) -> None:
    origin = metadata["centering_origin_m"]
    coords = [((x - origin["x"]) / 1000.0, (y - origin["y"]) / 1000.0, 0.035) for x, y in geojson_polygon(CORRIDOR_GEOJSON)]
    fill = bpy.data.meshes.new("regional2d_corridor_surface_overlay")
    fill.from_pydata(coords, [], [tuple(range(len(coords)))])
    fill.update()
    fill_obj = bpy.data.objects.new("Regional2D corridor subtle fill", fill)
    bpy.context.collection.objects.link(fill_obj)
    fill_obj.data.materials.append(make_material("corridor subtle fill", (0.920, 0.340, 0.180, 0.12)))

    curve = bpy.data.curves.new("regional2d_corridor_outline", "CURVE")
    curve.dimensions = "3D"
    curve.resolution_u = 1
    curve.bevel_depth = 0.075
    curve.bevel_resolution = 3
    spline = curve.splines.new("POLY")
    spline.points.add(len(coords) - 1)
    for point, coord in zip(spline.points, coords, strict=True):
        point.co = (coord[0], coord[1], coord[2] + 0.02, 1.0)
    outline = bpy.data.objects.new("Regional2D corridor outline", curve)
    bpy.context.collection.objects.link(outline)
    outline.data.materials.append(make_material("corridor warm accent", (0.920, 0.340, 0.180, 1.0)))


def configure_scene(output: Path, width: int, height: int) -> dict[str, Any]:
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.film_transparent = False
    scene.render.filepath = output.as_posix()
    scene.world = bpy.data.worlds.new("R17 light neutral world") if scene.world is None else scene.world
    scene.world.color = (1.0, 1.0, 1.0)
    scene.world.use_nodes = True
    background = scene.world.node_tree.nodes.get("Background") if scene.world.node_tree else None
    if background is not None:
        background.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
        background.inputs["Strength"].default_value = 0.85
    if hasattr(scene, "eevee"):
        for attr, value in [("taa_render_samples", 96), ("use_gtao", True), ("gtao_distance", 4), ("gtao_factor", 0.45)]:
            if hasattr(scene.eevee, attr):
                setattr(scene.eevee, attr, value)
    try:
        scene.view_settings.view_transform = "Standard"
        scene.view_settings.look = "None"
        scene.view_settings.exposure = 0.15
        scene.view_settings.gamma = 1.0
    except Exception:
        pass
    return {"engine": scene.render.engine, "resolution": {"width": width, "height": height}}


def look_at(obj: bpy.types.Object, target: Vector) -> None:
    direction = target - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def configure_camera_and_light() -> dict[str, Any]:
    camera_data = bpy.data.cameras.new("R17 scientific oblique camera")
    camera = bpy.data.objects.new("R17 scientific oblique camera", camera_data)
    bpy.context.collection.objects.link(camera)
    camera.location = Vector((92.0, -118.0, 56.0))
    target = Vector((-3.0, 15.0, -1.2))
    look_at(camera, target)
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 118.0
    camera.data.lens = 55
    bpy.context.scene.camera = camera

    light_data = bpy.data.lights.new("soft key light", "AREA")
    light = bpy.data.objects.new("soft key light", light_data)
    bpy.context.collection.objects.link(light)
    light.location = Vector((-40.0, -40.0, 75.0))
    light.data.energy = 1700.0
    light.data.size = 70.0

    fill_data = bpy.data.lights.new("very soft fill", "AREA")
    fill = bpy.data.objects.new("very soft fill", fill_data)
    bpy.context.collection.objects.link(fill)
    fill.location = Vector((45.0, 65.0, 42.0))
    fill.data.energy = 320.0
    fill.data.size = 95.0
    return {
        "camera": {
            "type": "orthographic",
            "location_km": list(camera.location),
            "target_km": list(target),
            "ortho_scale_km": camera.data.ortho_scale,
        },
        "lighting": {
            "key": {"type": "AREA", "location_km": list(light.location), "energy": light.data.energy, "size_km": light.data.size},
            "fill": {"type": "AREA", "location_km": list(fill.location), "energy": fill.data.energy, "size_km": fill.data.size},
        },
    }


def render(args: argparse.Namespace) -> dict[str, Any]:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.record.parent.mkdir(parents=True, exist_ok=True)
    if args.save_blend:
        args.save_blend.parent.mkdir(parents=True, exist_ok=True)

    terrain_manifest = read_json(TERRAIN_MANIFEST)
    arr, gt = load_raster(args.terrain)
    _, terrain_metadata = build_terrain(arr, gt, args.vertical_exaggeration)
    build_sea_plane(terrain_metadata)
    build_corridor_overlay(terrain_metadata)
    scene_record = configure_scene(args.output, args.width, args.height)
    camera_record = configure_camera_and_light()

    bpy.ops.render.render(write_still=True)
    blend_record: dict[str, Any] | None = None
    if args.save_blend:
        bpy.ops.wm.save_as_mainfile(filepath=args.save_blend.as_posix())
        blend_record = {"path": args.save_blend.as_posix(), "bytes": args.save_blend.stat().st_size}

    payload = {
        "schema": {"name": "tsunami.r17.blender_render", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "terrain_manifest": terrain_manifest,
        "terrain_mesh": terrain_metadata,
        "render": scene_record,
        "camera_and_lighting": camera_record,
        "sea_level_plane": {
            "z_km": 0.0,
            "vertical_reference": terrain_manifest["vertical_reference"],
            "material": "subtle transparent pale blue, alpha 0.09",
        },
        "corridor_overlay": {
            "source": CORRIDOR_GEOJSON.as_posix(),
            "method": "actual corridor polygon in EPSG:32654, centred with the terrain and drawn as a thin muted warm accent slightly above the sea-level plane for legibility",
        },
        "output": {"path": args.output.as_posix(), "bytes": args.output.stat().st_size if args.output.is_file() else 0},
        "blend_scene": blend_record,
        "blender": {
            "version": bpy.app.version_string,
            "python": sys.version.split()[0],
            "render_engine": bpy.context.scene.render.engine,
        },
    }
    write_json(args.record, payload)
    return payload


def main() -> int:
    args = parse_blender_args(sys.argv)
    payload = render(args)
    print(json.dumps(payload, indent=2, sort_keys=True))
    bpy.ops.wm.quit_blender()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
