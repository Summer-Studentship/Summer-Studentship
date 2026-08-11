# R16 QGIS Source Package

Status: `QGIS_RUNTIME_BLOCKED`.

This directory contains the editable GIS inputs prepared for the R16 publication cartography workstream:

- `layers/r16_publication_layers.gpkg`: corridor, centreline, coupling section, candidate Local3D footprint, event/Kamaishi points and R15 validation stations in `EPSG:32654`.
- `layers/*.geojson`: source GeoJSON files used to build the GeoPackage.
- `styles/*.qml`: starter QGIS styles for the prepared layers.

The requested master project path is:

```text
deliverables/figures/r16_publication/sources/qgis/tohoku_kamaishi_publication.qgz
```

No `.qgz` is committed from this runtime because `qgis`, `qgis_process`, PyQGIS and Qt Python bindings are unavailable. Use:

```bash
QT_QPA_PLATFORM=offscreen python3 tools/figures/qgis/build_tohoku_kamaishi_project.py
QT_QPA_PLATFORM=offscreen python3 tools/figures/qgis/export_publication_layouts.py
QT_QPA_PLATFORM=offscreen python3 tools/figures/qgis/validate_qgis_project.py
```

in a QGIS-capable environment to author the project, export layouts and validate the cartographic outputs.
