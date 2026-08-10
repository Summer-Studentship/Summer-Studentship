#!/usr/bin/env python3
"""Discover and render Local3D OpenFOAM/VTK outputs for R14 handoff."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Sequence


def utc_now() -> str:
    return datetime.now(UTC).isoformat().replace("+00:00", "Z")


def numeric_time_dirs(case_root: Path) -> list[float]:
    times = []
    for child in case_root.iterdir():
        if child.is_dir():
            try:
                times.append(float(child.name))
            except ValueError:
                continue
    return sorted(times)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _vtk_index(path: Path) -> int:
    match = re.search(r"_(\d+)\.vtk$", path.name)
    return int(match.group(1)) if match else -1


def root_vtk_records(case_root: Path) -> list[dict[str, Any]]:
    vtk_root = case_root / "VTK"
    vtk_files = sorted((path for path in vtk_root.glob("case_*.vtk") if path.is_file()), key=_vtk_index) if vtk_root.is_dir() else []
    times = numeric_time_dirs(case_root)
    records = []
    for index, path in enumerate(vtk_files):
        records.append({
            "path": str(path),
            "vtk_index": _vtk_index(path),
            "time_s": times[index] if index < len(times) else None,
            "sha256": sha256(path),
        })
    return records


CAMERA_PRESETS: dict[str, dict[str, list[float]]] = {
    "overview": {
        "position": [1200.0, -1400.0, 650.0],
        "focal_point": [350.0, 3500.0, 35.0],
        "view_up": [0.0, 0.0, 1.0],
    },
    "barrier_oblique": {
        "position": [900.0, 2700.0, 420.0],
        "focal_point": [350.0, 3550.0, 45.0],
        "view_up": [0.0, 0.0, 1.0],
    },
    "barrier_side": {
        "position": [-950.0, 3500.0, 260.0],
        "focal_point": [350.0, 3500.0, 45.0],
        "view_up": [0.0, 0.0, 1.0],
    },
    "inlet_to_barrier": {
        "position": [350.0, -850.0, 260.0],
        "focal_point": [350.0, 3500.0, 35.0],
        "view_up": [0.0, 0.0, 1.0],
    },
}


STYLE_PROFILES: dict[str, dict[str, Any]] = {
    "scientific": {
        "background": [1.0, 1.0, 1.0],
        "water_opacity": 1.0,
        "water_colour": "alpha.water scalar colouring",
        "specular": 0.05,
        "terrain_shading": "neutral",
        "barrier_shading": "neutral",
    },
    "poster": {
        "background": [0.97, 0.98, 0.96],
        "water_opacity": 0.88,
        "water_colour": "alpha.water scalar colouring",
        "specular": 0.25,
        "terrain_shading": "warm neutral",
        "barrier_shading": "dark neutral",
    },
    "ocean": {
        "background": [0.88, 0.94, 1.0],
        "water_opacity": 0.72,
        "water_colour": "alpha.water scalar colouring",
        "specular": 0.45,
        "terrain_shading": "muted seabed",
        "barrier_shading": "matte grey",
    },
}


def select_vtk_record(discovery: dict[str, Any], requested_time: float | None) -> dict[str, Any]:
    records = list(discovery["root_vtk_records"])
    if not records:
        raise RuntimeError(f"no root case_*.vtk files found in {Path(discovery['case_root']) / 'VTK'}")
    if requested_time is None:
        return records[-1]
    return min(records, key=lambda record: abs(float(record["time_s"] or 0.0) - requested_time))


def discover_case(case_root: Path) -> dict[str, Any]:
    vtk_root = case_root / "VTK"
    vtk_files = sorted((path for path in vtk_root.glob("*.vtk") if path.is_file()), key=lambda path: (path.parent.as_posix(), _vtk_index(path), path.name)) if vtk_root.is_dir() else []
    patch_vtk = sorted(path for path in vtk_root.glob("*/*.vtk") if path.is_file()) if vtk_root.is_dir() else []
    fields = {}
    for time in numeric_time_dirs(case_root):
        folder = case_root / f"{time:g}"
        present = sorted(path.name for path in folder.iterdir() if path.is_file())
        fields[f"{time:g}"] = present
    all_fields = sorted({field for present in fields.values() for field in present})
    patch_names = sorted({path.parent.name for path in patch_vtk})
    return {
        "schema": {"name": "tsunami.r14.local3d_paraview_discovery", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "case_root": str(case_root),
        "time_directories": numeric_time_dirs(case_root),
        "fields_by_time": fields,
        "alpha_field_name": "alpha.water" if "alpha.water" in all_fields else None,
        "velocity_field_name": "U" if "U" in all_fields else None,
        "pressure_field_name": "p_rgh" if "p_rgh" in all_fields else ("p" if "p" in all_fields else None),
        "patch_names": patch_names,
        "vtk_files": [str(path) for path in vtk_files],
        "patch_vtk_files": [str(path) for path in patch_vtk],
        "root_vtk_records": root_vtk_records(case_root),
        "has_case_foam": (case_root / "case.foam").is_file(),
        "has_barrier_patch_vtk": any("barrier" in path.name.lower() for path in patch_vtk),
        "camera_presets": CAMERA_PRESETS,
        "style_profiles": STYLE_PROFILES,
        "free_surface_extraction": {
            "method": "alpha.water contour",
            "isovalue": 0.5,
            "status": "SUPPORTED_BY_RENDERER",
            "note": "Derived visual surface only; no numerical field alteration.",
        },
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def render_with_paraview(case_root: Path, output: Path, *, style: str, camera: str, time_s: float | None, surface_mode: str, resolution: Sequence[int]) -> dict[str, Any]:
    try:
        from paraview.simple import (  # type: ignore
            ColorBy,
            Contour,
            GetActiveViewOrCreate,
            HideScalarBarIfNotNeeded,
            LegacyVTKReader,
            Render,
            SaveScreenshot,
            Show,
        )
    except Exception as exc:  # pragma: no cover - exercised only under pvpython.
        raise RuntimeError("ParaView Python modules are not available; run with pvpython for rendering") from exc

    discovery = discover_case(case_root)
    vtk_record = select_vtk_record(discovery, time_s)
    vtk_path = vtk_record["path"]
    reader = LegacyVTKReader(FileNames=[vtk_path])
    source = reader
    free_surface = {"method": "root volume rendering", "isovalue": None, "status": "NOT_REQUESTED"}
    if surface_mode == "alpha-contour":
        source = Contour(Input=reader)
        source.ContourBy = ["POINTS", "alpha.water"]
        source.Isosurfaces = [0.5]
        free_surface = {
            "method": "alpha.water contour",
            "isovalue": 0.5,
            "status": "REQUESTED",
            "note": "Derived free-surface visualisation only; source fields are not changed.",
        }
    view = GetActiveViewOrCreate("RenderView")
    display = Show(source, view)
    ColorBy(display, ("POINTS", "alpha.water"))
    profile = STYLE_PROFILES[style]
    display.Opacity = profile["water_opacity"]
    try:
        display.Specular = profile["specular"]
    except Exception:
        pass
    view.Background = profile["background"]
    preset = CAMERA_PRESETS[camera]
    view.CameraPosition = preset["position"]
    view.CameraFocalPoint = preset["focal_point"]
    view.CameraViewUp = preset["view_up"]
    HideScalarBarIfNotNeeded(display.LookupTable, view)
    Render()
    output.parent.mkdir(parents=True, exist_ok=True)
    SaveScreenshot(str(output), view, ImageResolution=list(resolution))
    return {
        "schema": {"name": "tsunami.r14.local3d_paraview_render", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "case_root": str(case_root),
        "style": style,
        "style_profile": profile,
        "camera": camera,
        "camera_preset": preset,
        "requested_time_s": time_s,
        "selected_time_s": vtk_record["time_s"],
        "source_vtk": vtk_path,
        "source_vtk_sha256": vtk_record["sha256"],
        "free_surface_extraction": free_surface,
        "resolution": list(resolution),
        "output": str(output),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--style", choices=tuple(STYLE_PROFILES), default="scientific")
    parser.add_argument("--camera", choices=tuple(CAMERA_PRESETS), default="overview")
    parser.add_argument("--time", type=float, default=None)
    parser.add_argument("--surface-mode", choices=("volume", "alpha-contour"), default="volume")
    parser.add_argument("--resolution", nargs=2, type=int, metavar=("WIDTH", "HEIGHT"), default=(1800, 1100))
    parser.add_argument("--discover-only", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.discover_only:
        payload = discover_case(args.case_root)
    else:
        payload = render_with_paraview(
            args.case_root,
            args.output,
            style=args.style,
            camera=args.camera,
            time_s=args.time,
            surface_mode=args.surface_mode,
            resolution=args.resolution,
        )
    manifest_path = args.output.with_suffix(args.output.suffix + ".manifest.json")
    write_json(manifest_path, payload)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
