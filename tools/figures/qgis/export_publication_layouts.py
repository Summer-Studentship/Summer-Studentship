#!/usr/bin/env python3
"""Export R16 QGIS publication layouts headlessly."""

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


LAYOUT_TO_BASENAME = {
    "01_tohoku_event_corridor": "figure_A_tohoku_kamaishi_corridor",
    "02_corridor_bathymetry": "figure_B_corridor_bathymetry_plan",
    "03_validation_targets": "figure_F_validation_geometry",
    "04_hybrid_domain": "figure_E_hybrid_domain_framework",
}


def export_layouts(*, allow_blocked: bool) -> dict[str, Any]:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    env = r16.qgis_environment()
    if env["status"] == "QGIS_RUNTIME_BLOCKED" or not r16.QGIS_PROJECT.is_file():
        payload = {
            "schema": {"name": "tsunami.r16.qgis_layout_export", "version": "1.0.0"},
            "status": "BLOCKED_BY_QGIS_RUNTIME",
            "reason": "QGIS runtime/project unavailable; final cartographic layouts were not exported.",
            "qgis": env,
            "project": r16.QGIS_PROJECT.as_posix(),
            "requested_layouts": LAYOUT_NAMES,
        }
        r16.write_json(r16.PROVENANCE_ROOT / "qgis_layout_export_status.json", payload)
        if allow_blocked:
            return payload
        raise RuntimeError(payload["reason"])

    from qgis.core import QgsApplication, QgsLayoutExporter, QgsProject  # type: ignore[import-not-found]

    app = QgsApplication([], False)
    app.initQgis()
    exported: dict[str, list[str]] = {}
    try:
        project = QgsProject.instance()
        if not project.read(r16.QGIS_PROJECT.as_posix()):
            raise RuntimeError(f"Could not read {r16.QGIS_PROJECT}")
        manager = project.layoutManager()
        for layout_name in LAYOUT_NAMES:
            layout = manager.layoutByName(layout_name)
            if layout is None:
                raise RuntimeError(f"Missing layout: {layout_name}")
            basename = LAYOUT_TO_BASENAME[layout_name]
            exporter = QgsLayoutExporter(layout)
            outputs: list[str] = []
            pdf = r16.PUBLICATION_ROOT / f"{basename}.pdf"
            svg = r16.PUBLICATION_ROOT / f"{basename}.svg"
            png = r16.PUBLICATION_ROOT / f"{basename}.png"
            r16.PUBLICATION_ROOT.mkdir(parents=True, exist_ok=True)
            exporter.exportToPdf(pdf.as_posix(), QgsLayoutExporter.PdfExportSettings())
            exporter.exportToSvg(svg.as_posix(), QgsLayoutExporter.SvgExportSettings())
            image_settings = QgsLayoutExporter.ImageExportSettings()
            image_settings.dpi = 420
            exporter.exportToImage(png.as_posix(), image_settings)
            outputs.extend([pdf.relative_to(r16.REPO_ROOT).as_posix(), svg.relative_to(r16.REPO_ROOT).as_posix(), png.relative_to(r16.REPO_ROOT).as_posix()])
            exported[layout_name] = outputs
    finally:
        app.exitQgis()

    payload = {
        "schema": {"name": "tsunami.r16.qgis_layout_export", "version": "1.0.0"},
        "status": "COMPLETE",
        "qgis": env,
        "project": r16.file_record(r16.QGIS_PROJECT),
        "exports": exported,
    }
    r16.write_json(r16.PROVENANCE_ROOT / "qgis_layout_export_status.json", payload)
    return payload


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allow-blocked", action="store_true", help="Exit 0 while recording QGIS_RUNTIME_BLOCKED.")
    args = parser.parse_args(argv)
    payload = export_layouts(allow_blocked=args.allow_blocked)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
