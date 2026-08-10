#!/usr/bin/env python3
"""Discover and render Local3D OpenFOAM/VTK outputs for R14 handoff."""

from __future__ import annotations

import argparse
import json
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


def discover_case(case_root: Path) -> dict[str, Any]:
    vtk_root = case_root / "VTK"
    vtk_files = sorted(path for path in vtk_root.glob("*.vtk") if path.is_file()) if vtk_root.is_dir() else []
    patch_vtk = sorted(path for path in vtk_root.glob("*/*.vtk") if path.is_file()) if vtk_root.is_dir() else []
    fields = {}
    for time in numeric_time_dirs(case_root):
        folder = case_root / f"{time:g}"
        present = sorted(path.name for path in folder.iterdir() if path.is_file())
        fields[f"{time:g}"] = present
    return {
        "schema": {"name": "tsunami.r14.local3d_paraview_discovery", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "case_root": str(case_root),
        "time_directories": numeric_time_dirs(case_root),
        "fields_by_time": fields,
        "vtk_files": [str(path) for path in vtk_files],
        "patch_vtk_files": [str(path) for path in patch_vtk],
        "has_case_foam": (case_root / "case.foam").is_file(),
        "has_barrier_patch_vtk": any("barrier" in path.name.lower() for path in patch_vtk),
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def render_with_paraview(case_root: Path, output: Path, *, style: str) -> dict[str, Any]:
    try:
        from paraview.simple import (  # type: ignore
            ColorBy,
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
    if not discovery["vtk_files"]:
        raise RuntimeError(f"no root VTK files found in {case_root / 'VTK'}")
    vtk_path = discovery["vtk_files"][-1]
    reader = LegacyVTKReader(FileNames=[vtk_path])
    view = GetActiveViewOrCreate("RenderView")
    display = Show(reader, view)
    ColorBy(display, ("POINTS", "alpha.water"))
    if style == "ocean":
        display.Opacity = 0.72
        view.Background = [0.88, 0.94, 1.0]
    else:
        view.Background = [1.0, 1.0, 1.0]
    view.CameraPosition = [1200.0, -1400.0, 650.0]
    view.CameraFocalPoint = [350.0, 3500.0, 35.0]
    view.CameraViewUp = [0.0, 0.0, 1.0]
    HideScalarBarIfNotNeeded(display.LookupTable, view)
    Render()
    output.parent.mkdir(parents=True, exist_ok=True)
    SaveScreenshot(str(output), view, ImageResolution=[1800, 1100])
    return {
        "schema": {"name": "tsunami.r14.local3d_paraview_render", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "case_root": str(case_root),
        "style": style,
        "source_vtk": vtk_path,
        "output": str(output),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--style", choices=("scientific", "ocean"), default="scientific")
    parser.add_argument("--discover-only", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.discover_only:
        payload = discover_case(args.case_root)
    else:
        payload = render_with_paraview(args.case_root, args.output, style=args.style)
    manifest_path = args.output.with_suffix(args.output.suffix + ".manifest.json")
    write_json(manifest_path, payload)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
