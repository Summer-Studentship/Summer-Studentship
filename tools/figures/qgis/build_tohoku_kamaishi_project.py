#!/usr/bin/env python3
"""Build the R16 editable QGIS project when PyQGIS is available."""

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


LAYOUT_NAMES = [
    "01_tohoku_event_corridor",
    "02_corridor_bathymetry",
    "03_validation_targets",
    "04_hybrid_domain",
]


def build_project(*, allow_blocked: bool) -> dict[str, Any]:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    env = r16.qgis_environment()
    layers = r16.prepare_gis_layers()
    if env["status"] == "QGIS_RUNTIME_BLOCKED":
        payload = {
            "schema": {"name": "tsunami.r16.qgis_project_build", "version": "1.0.0"},
            "status": "BLOCKED_BY_QGIS_RUNTIME",
            "reason": "PyQGIS/qgis_process unavailable; derived GeoPackage layers and styles were prepared but .qgz could not be authored.",
            "qgis": env,
            "layers": layers,
            "requested_project": r16.QGIS_PROJECT.as_posix(),
            "requested_layouts": LAYOUT_NAMES,
        }
        r16.write_json(r16.PROVENANCE_ROOT / "qgis_project_build_status.json", payload)
        if allow_blocked:
            return payload
        raise RuntimeError(payload["reason"])

    from qgis.core import (  # type: ignore[import-not-found]
        QgsApplication,
        QgsCoordinateReferenceSystem,
        QgsLayerTreeGroup,
        QgsProject,
        QgsRasterLayer,
        QgsVectorLayer,
    )

    app = QgsApplication([], False)
    app.initQgis()
    try:
        project = QgsProject.instance()
        project.clear()
        project.setCrs(QgsCoordinateReferenceSystem("EPSG:32654"))
        root = project.layerTreeRoot()

        groups: dict[str, QgsLayerTreeGroup] = {}
        for name in ["01 Context", "02 Event", "03 Regional Domain", "04 Terrain", "05 Coupling", "06 Validation", "07 Supporting"]:
            groups[name] = root.addGroup(name)

        gpkg = r16.QGIS_LAYER_ROOT / "r16_publication_layers.gpkg"

        def add_vector(group: str, layer_name: str, title: str, style: str | None = None) -> None:
            uri = f"{gpkg.as_posix()}|layername={layer_name}"
            layer = QgsVectorLayer(uri, title, "ogr")
            if not layer.isValid():
                raise RuntimeError(f"Invalid vector layer: {title}: {uri}")
            if style:
                layer.loadNamedStyle((r16.QGIS_STYLE_ROOT / style).as_posix())
            project.addMapLayer(layer, False)
            groups[group].addLayer(layer)

        def add_raster(group: str, path: Path, title: str) -> None:
            layer = QgsRasterLayer(path.as_posix(), title, "gdal")
            if not layer.isValid():
                raise RuntimeError(f"Invalid raster layer: {title}: {path}")
            project.addMapLayer(layer, False)
            groups[group].addLayer(layer)

        add_raster("04 Terrain", r16.SOURCE_ETOPO, "ETOPO 2022 WGS84 + EGM2008 source terrain")
        add_raster("04 Terrain", r16.CONDITIONED_TERRAIN, "G6 conditioned wet corridor terrain")
        add_vector("03 Regional Domain", "corridor_polygon", "R10 h400 Regional2D corridor", "corridor.qml")
        add_vector("03 Regional Domain", "corridor_centreline", "source-to-Kamaishi centreline", "coupling.qml")
        add_vector("05 Coupling", "coupling_section", "selected wet nearshore interface", "coupling.qml")
        add_vector("05 Coupling", "local3d_candidate_footprint", "candidate Local3D impact-study footprint", "corridor.qml")
        add_vector("02 Event", "event_and_kamaishi_points", "event, Kamaishi and coupling reference points", "event_source.qml")
        add_vector("06 Validation", "validation_stations", "R15 validation stations", "validation_stations.qml")

        # Layout creation is intentionally minimal here; final map-frame extent,
        # scalebars and legends should be tuned interactively or extended by the
        # export script in a QGIS runtime.
        manager = project.layoutManager()
        for layout_name in LAYOUT_NAMES:
            if manager.layoutByName(layout_name) is None:
                from qgis.core import QgsPrintLayout

                layout = QgsPrintLayout(project)
                layout.initializeDefaults()
                layout.setName(layout_name)
                manager.addLayout(layout)

        r16.QGIS_PROJECT.parent.mkdir(parents=True, exist_ok=True)
        if not project.write(r16.QGIS_PROJECT.as_posix()):
            raise RuntimeError(f"Failed to write {r16.QGIS_PROJECT}")
    finally:
        app.exitQgis()

    payload = {
        "schema": {"name": "tsunami.r16.qgis_project_build", "version": "1.0.0"},
        "status": "COMPLETE",
        "qgis": env,
        "project": r16.file_record(r16.QGIS_PROJECT),
        "layers": layers,
        "layouts": LAYOUT_NAMES,
    }
    r16.write_json(r16.PROVENANCE_ROOT / "qgis_project_build_status.json", payload)
    return payload


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allow-blocked", action="store_true", help="Exit 0 while recording QGIS_RUNTIME_BLOCKED.")
    args = parser.parse_args(argv)
    payload = build_project(allow_blocked=args.allow_blocked)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
