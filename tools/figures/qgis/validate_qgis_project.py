#!/usr/bin/env python3
"""Validate the R16 QGIS project and referenced GIS layers."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any, Sequence


REPO_ROOT = Path(__file__).resolve().parents[3]
if str(REPO_ROOT / "tools/figures") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "tools/figures"))

import r16_publication as r16
from build_tohoku_kamaishi_project import LAYOUT_NAMES


def validate_project(*, allow_blocked: bool) -> dict[str, Any]:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    env = r16.qgis_environment()
    gpkg = r16.QGIS_LAYER_ROOT / "r16_publication_layers.gpkg"
    layer_status: dict[str, Any] = {}
    if gpkg.is_file():
        code, output = r16.command_output(["ogrinfo", gpkg.as_posix()])
        layer_status["geopackage"] = {"status": "PASS" if code == 0 else "FAIL", "ogrinfo": output.splitlines()[:24]}
    else:
        layer_status["geopackage"] = {"status": "MISSING", "path": gpkg.as_posix()}

    if env["status"] == "QGIS_RUNTIME_BLOCKED" or not r16.QGIS_PROJECT.is_file():
        payload = {
            "schema": {"name": "tsunami.r16.qgis_project_validation", "version": "1.0.0"},
            "status": "BLOCKED_BY_QGIS_RUNTIME",
            "reason": "QGIS runtime/project unavailable; GeoPackage layer validation was performed where possible.",
            "qgis": env,
            "project": r16.QGIS_PROJECT.as_posix(),
            "layers": layer_status,
            "requested_layouts": LAYOUT_NAMES,
        }
        r16.write_json(r16.PROVENANCE_ROOT / "qgis_project_validation_status.json", payload)
        if allow_blocked:
            return payload
        raise RuntimeError(payload["reason"])

    from qgis.core import QgsApplication, QgsProject  # type: ignore[import-not-found]

    app = QgsApplication([], False)
    app.initQgis()
    try:
        project = QgsProject.instance()
        if not project.read(r16.QGIS_PROJECT.as_posix()):
            raise RuntimeError(f"Could not read {r16.QGIS_PROJECT}")
        broken_layers = [layer.name() for layer in project.mapLayers().values() if not layer.isValid()]
        missing_layouts = [layout for layout in LAYOUT_NAMES if project.layoutManager().layoutByName(layout) is None]
    finally:
        app.exitQgis()

    status = "COMPLETE" if not broken_layers and not missing_layouts else "FAILED"
    payload = {
        "schema": {"name": "tsunami.r16.qgis_project_validation", "version": "1.0.0"},
        "status": status,
        "qgis": env,
        "project": r16.file_record(r16.QGIS_PROJECT),
        "layers": layer_status,
        "broken_layers": broken_layers,
        "missing_layouts": missing_layouts,
    }
    r16.write_json(r16.PROVENANCE_ROOT / "qgis_project_validation_status.json", payload)
    if status != "COMPLETE":
        raise RuntimeError(json.dumps(payload, indent=2, sort_keys=True))
    return payload


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allow-blocked", action="store_true", help="Exit 0 while recording QGIS_RUNTIME_BLOCKED.")
    args = parser.parse_args(argv)
    payload = validate_project(allow_blocked=args.allow_blocked)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
