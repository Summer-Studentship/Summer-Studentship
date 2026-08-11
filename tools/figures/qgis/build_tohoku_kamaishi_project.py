#!/usr/bin/env python3
"""Build the R16B editable QGIS publication project and layouts."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path
from typing import Any, Callable, Sequence


REPO_ROOT = Path(__file__).resolve().parents[3]
if str(REPO_ROOT / "tools/figures") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "tools/figures"))

import r16_publication as r16


GROUP_NAMES = {
    "context": "01 \u2014 Geographic Context",
    "event": "02 \u2014 2011 Tohoku Event",
    "regional": "03 \u2014 Regional2D Domain",
    "terrain": "04 \u2014 Bathymetry / Topography",
    "hybrid": "05 \u2014 Hybrid Framework",
    "validation": "06 \u2014 Historical Validation",
    "helpers": "07 \u2014 Publication Helpers",
}

LAYOUT_NAMES = [
    "01_TOHOKU_EVENT_CORRIDOR",
    "02_CORRIDOR_BATHYMETRY",
    "03_HYBRID_DOMAIN",
    "04_VALIDATION_GEOMETRY",
    "05_CORRIDOR_BATHYMETRY_OBLIQUE",
]

OLD_LAYOUT_NAMES = [
    "01_tohoku_event_corridor",
    "02_corridor_bathymetry",
    "03_validation_targets",
    "04_hybrid_domain",
]

FIGURE_C_RENDER = r16.QGIS_ROOT / "derived" / "figure_C_vtk_oblique_terrain.png"
VERTICAL_EXAGGERATION = 18.0


def run_checked(command: Sequence[str]) -> str:
    code, output = r16.command_output(command)
    if code != 0:
        raise RuntimeError(f"{' '.join(command)} failed:\n{output}")
    return output


def prepare_derived_terrain() -> dict[str, Any]:
    derived = r16.QGIS_ROOT / "derived"
    derived.mkdir(parents=True, exist_ok=True)

    crops = {
        "japan_context": {
            "path": derived / "etopo_japan_context_utm54.tif",
            "extent": [120000, 3275000, 1280000, 5050000],
            "resolution_m": 5000,
            "description": "Japan and northwest Pacific context crop from ETOPO 2022.",
        },
        "regional": {
            "path": derived / "etopo_regional_tohoku_utm54.tif",
            "extent": [395000, 4175000, 720000, 4420000],
            "resolution_m": 1000,
            "description": "Tohoku event/Kamaishi corridor crop from ETOPO 2022.",
        },
        "corridor": {
            "path": derived / "etopo_corridor_kamaishi_utm54.tif",
            "extent": [556000, 4210000, 642000, 4357000],
            "resolution_m": 400,
            "description": "Kamaishi corridor bathymetry/topography crop from ETOPO 2022.",
        },
        "nearshore": {
            "path": derived / "etopo_nearshore_hybrid_utm54.tif",
            "extent": [562000, 4316000, 604000, 4356500],
            "resolution_m": 220,
            "description": "Nearshore crop for the Regional2D to Local3D framework and validation geometry.",
        },
        "validation_overview": {
            "path": derived / "etopo_validation_overview_utm54.tif",
            "extent": [535000, 4200000, 1195000, 4410000],
            "resolution_m": 2000,
            "description": "Overview crop spanning the Kamaishi corridor and the DART 21418 target.",
        },
    }
    for crop in crops.values():
        xmin, ymin, xmax, ymax = crop["extent"]
        run_checked(
            [
                "gdalwarp",
                "-overwrite",
                "-of",
                "GTiff",
                "-t_srs",
                "EPSG:32654",
                "-te",
                str(xmin),
                str(ymin),
                str(xmax),
                str(ymax),
                "-tr",
                str(crop["resolution_m"]),
                str(crop["resolution_m"]),
                "-r",
                "bilinear",
                "-dstnodata",
                "-99999",
                "-co",
                "COMPRESS=DEFLATE",
                r16.SOURCE_ETOPO.as_posix(),
                crop["path"].as_posix(),
            ]
        )

    hillshades: dict[str, Path] = {}
    for key in ["regional", "corridor", "nearshore"]:
        target = derived / f"{key}_hillshade.tif"
        run_checked(
            [
                "gdaldem",
                "hillshade",
                crops[key]["path"].as_posix(),
                target.as_posix(),
                "-compute_edges",
                "-az",
                "315",
                "-alt",
                "45",
                "-of",
                "GTiff",
                "-co",
                "COMPRESS=DEFLATE",
            ]
        )
        hillshades[key] = target

    coastlines: dict[str, Path] = {}
    for key in ["japan_context", "regional", "corridor", "nearshore", "validation_overview"]:
        target = derived / f"{key}_coastline_0m.gpkg"
        if target.exists():
            target.unlink()
        run_checked(
            [
                "gdal_contour",
                "-f",
                "GPKG",
                "-a",
                "elevation_m",
                "-fl",
                "0",
                crops[key]["path"].as_posix(),
                target.as_posix(),
                "-nln",
                "coastline_0m",
            ]
        )
        coastlines[key] = target

    render = render_oblique_terrain(crops["corridor"]["path"], r16.load_corridor())
    status = {
        "schema": {"name": "tsunami.r16b.derived_terrain", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": r16.utc_now(),
        "source": r16.file_record(r16.SOURCE_ETOPO),
        "crs": "EPSG:32654 WGS 84 / UTM zone 54N; vertical EGM2008 height, positive up",
        "crops": {key: {**value, "path": r16.file_record(value["path"])} for key, value in crops.items()},
        "hillshades": {key: r16.file_record(path) for key, path in hillshades.items()},
        "coastlines": {key: r16.file_record(path) for key, path in coastlines.items()},
        "oblique_render": render,
    }
    r16.write_json(r16.PROVENANCE_ROOT / "r16b_derived_terrain_status.json", status)
    return status


def render_oblique_terrain(raster_path: Path, corridor: dict[str, Any]) -> dict[str, Any]:
    try:
        import pyvista  # type: ignore[import-not-found]  # noqa: F401

        pyvista_status = "AVAILABLE_NOT_USED_DIRECT_VTK_CHOSEN_FOR_HEADLESS_REPRODUCIBILITY"
    except Exception as exc:  # pragma: no cover - host dependent.
        pyvista_status = f"UNAVAILABLE: {type(exc).__name__}: {exc}"

    import numpy as np
    from osgeo import gdal
    from vtkmodules.util import numpy_support
    from vtkmodules.vtkCommonCore import VTK_FLOAT, vtkPoints
    from vtkmodules.vtkCommonDataModel import vtkStructuredGrid
    from vtkmodules.vtkFiltersSources import vtkPlaneSource
    from vtkmodules.vtkIOImage import vtkPNGWriter
    from vtkmodules.vtkRenderingCore import (
        vtkActor,
        vtkColorTransferFunction,
        vtkDataSetMapper,
        vtkPolyDataMapper,
        vtkRenderer,
        vtkRenderWindow,
        vtkWindowToImageFilter,
    )

    import vtkmodules.vtkRenderingOpenGL2  # noqa: F401

    ds = gdal.Open(raster_path.as_posix())
    if ds is None:
        raise RuntimeError(f"Could not open {raster_path}")
    band = ds.GetRasterBand(1)
    arr = band.ReadAsArray().astype(float)
    nodata = band.GetNoDataValue()
    if nodata is not None:
        arr = np.where(arr == nodata, np.nan, arr)
    stride = max(1, int(max(arr.shape) / 220))
    arr = arr[::stride, ::stride]
    gt = ds.GetGeoTransform()
    rows, cols = arr.shape
    xs = gt[0] + (np.arange(cols) * stride + 0.5) * gt[1]
    ys = gt[3] + (np.arange(rows) * stride + 0.5) * gt[5]
    xx, yy = np.meshgrid(xs, ys)
    z_raw = np.nan_to_num(arr, nan=-2500.0)
    x0 = float(np.nanmean(xx))
    y0 = float(np.nanmean(yy))

    points = vtkPoints()
    scalars_np = z_raw.astype(np.float32).ravel(order="C")
    for x_val, y_val, z_val in zip(xx.ravel(order="C"), yy.ravel(order="C"), scalars_np, strict=True):
        points.InsertNextPoint(float(x_val - x0), float(y_val - y0), float(z_val * VERTICAL_EXAGGERATION))

    grid = vtkStructuredGrid()
    grid.SetDimensions(cols, rows, 1)
    grid.SetPoints(points)
    scalars = numpy_support.numpy_to_vtk(scalars_np, deep=True, array_type=VTK_FLOAT)
    scalars.SetName("elevation_m")
    grid.GetPointData().SetScalars(scalars)

    colour = vtkColorTransferFunction()
    colour.SetColorSpaceToDiverging()
    for value, rgb in [
        (-2400.0, (0.02, 0.10, 0.28)),
        (-1200.0, (0.05, 0.25, 0.55)),
        (-500.0, (0.15, 0.47, 0.74)),
        (-100.0, (0.63, 0.82, 0.90)),
        (0.0, (0.94, 0.95, 0.90)),
        (150.0, (0.52, 0.68, 0.42)),
        (700.0, (0.62, 0.48, 0.32)),
        (1400.0, (0.36, 0.28, 0.22)),
    ]:
        colour.AddRGBPoint(value, *rgb)

    mapper = vtkDataSetMapper()
    mapper.SetInputData(grid)
    mapper.SetLookupTable(colour)
    mapper.SetScalarRange(-2400.0, 1400.0)

    terrain_actor = vtkActor()
    terrain_actor.SetMapper(mapper)
    terrain_actor.GetProperty().SetInterpolationToPhong()

    sea = vtkPlaneSource()
    sea.SetOrigin(float(np.nanmin(xx) - x0), float(np.nanmin(yy) - y0), 0.0)
    sea.SetPoint1(float(np.nanmax(xx) - x0), float(np.nanmin(yy) - y0), 0.0)
    sea.SetPoint2(float(np.nanmin(xx) - x0), float(np.nanmax(yy) - y0), 0.0)
    sea_mapper = vtkPolyDataMapper()
    sea_mapper.SetInputConnection(sea.GetOutputPort())
    sea_actor = vtkActor()
    sea_actor.SetMapper(sea_mapper)
    sea_actor.GetProperty().SetColor(0.56, 0.78, 0.88)
    sea_actor.GetProperty().SetOpacity(0.22)

    outline_actor = corridor_outline_actor(corridor, x0, y0)

    renderer = vtkRenderer()
    renderer.SetBackground(1.0, 1.0, 1.0)
    renderer.AddActor(terrain_actor)
    renderer.AddActor(sea_actor)
    renderer.AddActor(outline_actor)
    renderer.ResetCamera()
    camera = renderer.GetActiveCamera()
    camera.SetFocalPoint(0.0, 15000.0, -9000.0)
    camera.SetPosition(65000.0, -150000.0, 52000.0)
    camera.SetViewUp(0.0, 0.0, 1.0)
    camera.SetClippingRange(100.0, 500000.0)

    window = vtkRenderWindow()
    window.SetOffScreenRendering(1)
    window.AddRenderer(renderer)
    window.SetSize(3000, 1800)
    window.Render()

    window_to_image = vtkWindowToImageFilter()
    window_to_image.SetInput(window)
    window_to_image.SetScale(1)
    window_to_image.ReadFrontBufferOff()
    window_to_image.Update()

    FIGURE_C_RENDER.parent.mkdir(parents=True, exist_ok=True)
    writer = vtkPNGWriter()
    writer.SetFileName(FIGURE_C_RENDER.as_posix())
    writer.SetInputConnection(window_to_image.GetOutputPort())
    writer.Write()
    window.Finalize()

    return {
        "status": "COMPLETE",
        "renderer": "direct VTK offscreen structured-grid render",
        "qgis_3d_attempts": [
            {
                "status": "NOT_USED",
                "reason": "QGIS 4 headless processing exposed 3d:tessellate only; no stable print-layout 3D export path was available.",
            }
        ],
        "pyvista_status": pyvista_status,
        "vertical_exaggeration": VERTICAL_EXAGGERATION,
        "source_raster": r16.file_record(raster_path),
        "render": r16.file_record(FIGURE_C_RENDER),
        "coordinate_note": "Horizontal coordinates are EPSG:32654 metres, centred for rendering; elevation is EGM2008 metres with positive-up convention.",
    }


def corridor_outline_actor(corridor: dict[str, Any], x0: float, y0: float) -> Any:
    from vtkmodules.vtkCommonCore import vtkPoints
    from vtkmodules.vtkCommonDataModel import vtkCellArray, vtkPolyData, vtkPolyLine
    from vtkmodules.vtkRenderingCore import vtkActor, vtkPolyDataMapper

    points = vtkPoints()
    polyline = vtkPolyLine()
    polygon = corridor["polygon_points"]
    polyline.GetPointIds().SetNumberOfIds(len(polygon))
    for idx, point in enumerate(polygon):
        points.InsertNextPoint(float(point.x - x0), float(point.y - y0), 1800.0)
        polyline.GetPointIds().SetId(idx, idx)

    cells = vtkCellArray()
    cells.InsertNextCell(polyline)
    polydata = vtkPolyData()
    polydata.SetPoints(points)
    polydata.SetLines(cells)
    mapper = vtkPolyDataMapper()
    mapper.SetInputData(polydata)
    actor = vtkActor()
    actor.SetMapper(mapper)
    actor.GetProperty().SetColor(0.89, 0.20, 0.16)
    actor.GetProperty().SetLineWidth(4.0)
    return actor


def build_project(*, allow_blocked: bool) -> dict[str, Any]:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    env = r16.qgis_environment()
    if env["status"] == "QGIS_RUNTIME_BLOCKED":
        existing_layers = r16.PROVENANCE_ROOT / "r16_gis_layer_manifest.json"
        layers = r16.read_json(existing_layers) if existing_layers.is_file() else r16.prepare_gis_layers()
        payload = {
            "schema": {"name": "tsunami.r16.qgis_project_build", "version": "2.0.0"},
            "status": "BLOCKED_BY_QGIS_RUNTIME",
            "reason": "PyQGIS/qgis_process unavailable; derived GeoPackage layers were prepared but .qgz could not be authored.",
            "qgis": env,
            "layers": layers,
            "requested_project": r16.QGIS_PROJECT.as_posix(),
            "requested_groups": list(GROUP_NAMES.values()),
            "requested_layouts": LAYOUT_NAMES,
        }
        r16.write_json(r16.PROVENANCE_ROOT / "qgis_project_build_status.json", payload)
        if allow_blocked:
            return payload
        raise RuntimeError(payload["reason"])

    layers = r16.prepare_gis_layers()
    terrain = prepare_derived_terrain()

    from qgis.PyQt.QtGui import QColor, QFont
    from qgis.core import (  # type: ignore[import-not-found]
        Qgis,
        QgsApplication,
        QgsColorRampShader,
        QgsCoordinateReferenceSystem,
        QgsFillSymbol,
        QgsLayerTreeGroup,
        QgsLayoutItemLabel,
        QgsLayoutItemLegend,
        QgsLayoutItemMap,
        QgsLayoutItemPicture,
        QgsLayoutItemScaleBar,
        QgsLayoutPoint,
        QgsLayoutSize,
        QgsLineSymbol,
        QgsMarkerSymbol,
        QgsPalLayerSettings,
        QgsPrintLayout,
        QgsProject,
        QgsRasterLayer,
        QgsRasterShader,
        QgsRectangle,
        QgsSingleBandGrayRenderer,
        QgsSingleBandPseudoColorRenderer,
        QgsSingleSymbolRenderer,
        QgsTextBufferSettings,
        QgsTextFormat,
        QgsUnitTypes,
        QgsVectorLayer,
        QgsVectorLayerSimpleLabeling,
    )

    app = QgsApplication([], False)
    app.initQgis()
    try:
        project = QgsProject.instance()
        project.clear()
        project.setCrs(QgsCoordinateReferenceSystem("EPSG:32654"))
        project.setTitle("R16B Tohoku-Kamaishi publication cartography")
        root = project.layerTreeRoot()

        groups: dict[str, QgsLayerTreeGroup] = {}
        for name in GROUP_NAMES.values():
            groups[name] = root.addGroup(name)

        created_layers: dict[str, Any] = {}
        gpkg = r16.QGIS_LAYER_ROOT / "r16_publication_layers.gpkg"

        def add_layer_to_group(layer: Any, group_key: str) -> Any:
            project.addMapLayer(layer, False)
            groups[GROUP_NAMES[group_key]].addLayer(layer)
            return layer

        def add_vector(
            key: str,
            group_key: str,
            layer_name: str,
            title: str,
            styler: Callable[[Any], None],
            *,
            subset: str | None = None,
            label: str | None = None,
            label_size: float = 8.0,
        ) -> Any:
            uri = f"{gpkg.as_posix()}|layername={layer_name}"
            layer = QgsVectorLayer(uri, title, "ogr")
            if not layer.isValid():
                raise RuntimeError(f"Invalid vector layer: {title}: {uri}")
            if subset:
                layer.setSubsetString(subset)
            styler(layer)
            if label:
                enable_labels(layer, label, label_size)
            created_layers[key] = add_layer_to_group(layer, group_key)
            return layer

        def add_vector_path(
            key: str,
            group_key: str,
            path: Path,
            layer_name: str,
            title: str,
            styler: Callable[[Any], None],
        ) -> Any:
            uri = f"{path.as_posix()}|layername={layer_name}"
            layer = QgsVectorLayer(uri, title, "ogr")
            if not layer.isValid():
                raise RuntimeError(f"Invalid vector layer: {title}: {uri}")
            styler(layer)
            created_layers[key] = add_layer_to_group(layer, group_key)
            return layer

        def add_raster(key: str, group_key: str, path: Path, title: str, styler: Callable[[Any], None]) -> Any:
            layer = QgsRasterLayer(path.as_posix(), title, "gdal")
            if not layer.isValid():
                raise RuntimeError(f"Invalid raster layer: {title}: {path}")
            styler(layer)
            created_layers[key] = add_layer_to_group(layer, group_key)
            return layer

        def set_renderer(layer: Any, symbol: Any) -> None:
            layer.setRenderer(QgsSingleSymbolRenderer(symbol))
            layer.triggerRepaint()

        def style_corridor(layer: Any) -> None:
            symbol = QgsFillSymbol.createSimple(
                {
                    "color": "48,108,170,48",
                    "outline_color": "10,72,138,255",
                    "outline_width": "0.55",
                }
            )
            set_renderer(layer, symbol)

        def style_local(layer: Any) -> None:
            symbol = QgsFillSymbol.createSimple(
                {
                    "color": "190,75,155,72",
                    "outline_color": "122,40,113,255",
                    "outline_width": "0.7",
                }
            )
            set_renderer(layer, symbol)

        def style_line(layer: Any, colour: str, width: str = "0.6", dash: bool = False) -> None:
            props = {"line_color": colour, "line_width": width}
            if dash:
                props["line_style"] = "dash"
            set_renderer(layer, QgsLineSymbol.createSimple(props))

        def style_point(layer: Any, colour: str, size: str = "3.2", outline: str = "35,35,35,255") -> None:
            set_renderer(
                layer,
                QgsMarkerSymbol.createSimple(
                    {"name": "circle", "color": colour, "outline_color": outline, "outline_width": "0.35", "size": size}
                ),
            )

        def style_event(layer: Any) -> None:
            style_point(layer, "205,45,36,255", "3.8", "120,20,16,255")

        def style_kamaishi(layer: Any) -> None:
            style_point(layer, "36,105,170,255", "3.2", "10,50,100,255")

        def style_coupling(layer: Any) -> None:
            style_point(layer, "122,40,113,255", "2.9", "72,18,70,255")

        def style_validation_all(layer: Any) -> None:
            style_point(layer, "110,110,110,150", "2.0", "80,80,80,120")

        def style_validation_proxy(layer: Any) -> None:
            style_point(layer, "237,137,54,255", "3.6", "110,70,20,255")

        def style_validation_priority(layer: Any) -> None:
            style_point(layer, "35,135,120,255", "3.4", "20,80,70,255")

        def style_coastline(layer: Any) -> None:
            style_line(layer, "40,52,60,230", "0.35")

        def style_terrain(layer: Any) -> None:
            provider = layer.dataProvider()
            ramp = QgsColorRampShader()
            ramp.setColorRampType(QgsColorRampShader.Interpolated)
            ramp.setColorRampItemList(
                [
                    QgsColorRampShader.ColorRampItem(-2500.0, QColor("#051937"), "-2500 m"),
                    QgsColorRampShader.ColorRampItem(-1200.0, QColor("#0b3f73"), "-1200 m"),
                    QgsColorRampShader.ColorRampItem(-500.0, QColor("#2a7ab0"), "-500 m"),
                    QgsColorRampShader.ColorRampItem(-100.0, QColor("#8fc7da"), "-100 m"),
                    QgsColorRampShader.ColorRampItem(0.0, QColor("#f4f1df"), "0 m"),
                    QgsColorRampShader.ColorRampItem(150.0, QColor("#8ba867"), "150 m"),
                    QgsColorRampShader.ColorRampItem(700.0, QColor("#a17b52"), "700 m"),
                    QgsColorRampShader.ColorRampItem(1400.0, QColor("#5c4637"), "1400 m"),
                ]
            )
            shader = QgsRasterShader()
            shader.setRasterShaderFunction(ramp)
            renderer = QgsSingleBandPseudoColorRenderer(provider, 1, shader)
            layer.setRenderer(renderer)
            layer.setOpacity(1.0)

        def style_hillshade(layer: Any) -> None:
            layer.setRenderer(QgsSingleBandGrayRenderer(layer.dataProvider(), 1))
            layer.setOpacity(0.30)

        def enable_labels(layer: Any, expression: str, size: float) -> None:
            settings = QgsPalLayerSettings()
            settings.fieldName = expression
            settings.isExpression = True
            if hasattr(QgsPalLayerSettings, "AroundPoint"):
                settings.placement = QgsPalLayerSettings.AroundPoint
            text_format = QgsTextFormat()
            font = QFont("DejaVu Sans")
            text_format.setFont(font)
            text_format.setSize(size)
            buffer = QgsTextBufferSettings()
            buffer.setEnabled(True)
            buffer.setSize(0.8)
            buffer.setColor(QColor("white"))
            text_format.setBuffer(buffer)
            settings.setFormat(text_format)
            layer.setLabeling(QgsVectorLayerSimpleLabeling(settings))
            layer.setLabelsEnabled(True)

        add_raster("japan_terrain", "context", Path(terrain["crops"]["japan_context"]["path"]["path"]), "ETOPO Japan context crop", style_terrain)
        add_raster("regional_terrain", "context", Path(terrain["crops"]["regional"]["path"]["path"]), "ETOPO Tohoku regional crop", style_terrain)
        add_raster("regional_hillshade", "context", Path(terrain["hillshades"]["regional"]["path"]), "regional hillshade", style_hillshade)
        add_raster("corridor_terrain", "terrain", Path(terrain["crops"]["corridor"]["path"]["path"]), "ETOPO Kamaishi corridor bathymetry/topography", style_terrain)
        add_raster("corridor_hillshade", "terrain", Path(terrain["hillshades"]["corridor"]["path"]), "corridor hillshade", style_hillshade)
        add_raster("nearshore_terrain", "hybrid", Path(terrain["crops"]["nearshore"]["path"]["path"]), "ETOPO nearshore hybrid crop", style_terrain)
        add_raster("nearshore_hillshade", "hybrid", Path(terrain["hillshades"]["nearshore"]["path"]), "nearshore hillshade", style_hillshade)
        add_raster("validation_terrain", "validation", Path(terrain["crops"]["validation_overview"]["path"]["path"]), "ETOPO validation overview crop", style_terrain)
        add_raster("conditioned_terrain", "terrain", r16.CONDITIONED_TERRAIN, "G6 conditioned wet corridor terrain", style_terrain)

        for key in ["japan_context", "regional", "corridor", "nearshore", "validation_overview"]:
            add_vector_path(
                f"{key}_coastline",
                "helpers",
                Path(terrain["coastlines"][key]["path"]),
                "coastline_0m",
                f"{key.replace('_', ' ')} 0 m coastline",
                style_coastline,
            )

        add_vector("corridor_polygon", "regional", "corridor_polygon", "R10 h400 Regional2D corridor", style_corridor)
        add_vector("corridor_centreline", "regional", "corridor_centreline", "source-to-Kamaishi centreline", lambda layer: style_line(layer, "31,82,135,255", "0.65", True))
        add_vector("coupling_section", "hybrid", "coupling_section", "selected wet nearshore interface", lambda layer: style_line(layer, "122,40,113,255", "0.9"))
        add_vector("local3d_footprint", "hybrid", "local3d_candidate_footprint", "conceptual Local3D impact-study footprint", style_local)
        add_vector(
            "event_epicentre",
            "event",
            "event_and_kamaishi_points",
            "2011 event epicentre",
            style_event,
            subset="role = 'event_epicentre'",
            label="'2011 event'",
            label_size=8.2,
        )
        add_vector(
            "kamaishi_proxy",
            "event",
            "event_and_kamaishi_points",
            "Kamaishi proxy",
            style_kamaishi,
            subset="role = 'kamaishi_proxy'",
            label="'Kamaishi'",
            label_size=8.2,
        )
        add_vector(
            "coupling_centre",
            "hybrid",
            "event_and_kamaishi_points",
            "selected interface centre",
            style_coupling,
            subset="role = 'coupling_centre'",
        )
        add_vector("validation_all", "validation", "validation_stations", "R15 validation register: all observations", style_validation_all)
        add_vector(
            "validation_proxy",
            "validation",
            "validation_stations",
            "R15 PROXY observation",
            style_validation_proxy,
            subset="eligibility = 'PROXY'",
        )
        add_vector(
            "validation_priority",
            "validation",
            "validation_stations",
            "NOWPHAS 802G and DART 21418 out-of-corridor targets",
            style_validation_priority,
            subset=(
                "observation_id IN ('PARI_NOWPHAS_802G_KAMAISHI_OFFSHORE',"
                "'NOAA_NCEI_DART_21418','NOAA_NCEI_TIDE_19236')"
            ),
        )

        create_layouts(
            project=project,
            layers=created_layers,
            font_factory=lambda bold=False: make_font(QFont, bold),
            qgis_types={
                "Qgis": Qgis,
                "QgsLayoutItemLabel": QgsLayoutItemLabel,
                "QgsLayoutItemLegend": QgsLayoutItemLegend,
                "QgsLayoutItemMap": QgsLayoutItemMap,
                "QgsLayoutItemPicture": QgsLayoutItemPicture,
                "QgsLayoutItemScaleBar": QgsLayoutItemScaleBar,
                "QgsLayoutPoint": QgsLayoutPoint,
                "QgsLayoutSize": QgsLayoutSize,
                "QgsPrintLayout": QgsPrintLayout,
                "QgsRectangle": QgsRectangle,
                "QgsTextFormat": QgsTextFormat,
                "QgsUnitTypes": QgsUnitTypes,
            },
        )

        r16.QGIS_PROJECT.parent.mkdir(parents=True, exist_ok=True)
        if not project.write(r16.QGIS_PROJECT.as_posix()):
            raise RuntimeError(f"Failed to write {r16.QGIS_PROJECT}")
    finally:
        app.exitQgis()

    payload = {
        "schema": {"name": "tsunami.r16.qgis_project_build", "version": "2.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": r16.utc_now(),
        "qgis": env,
        "project": r16.file_record(r16.QGIS_PROJECT),
        "layers": layers,
        "derived_terrain": terrain,
        "groups": list(GROUP_NAMES.values()),
        "layouts": LAYOUT_NAMES,
    }
    r16.write_json(r16.PROVENANCE_ROOT / "qgis_project_build_status.json", payload)
    return payload


def make_font(qfont_type: Any, bold: bool) -> Any:
    font = qfont_type("DejaVu Sans")
    font.setBold(bold)
    return font


def create_layouts(project: Any, layers: dict[str, Any], font_factory: Callable[[bool], Any], qgis_types: dict[str, Any]) -> None:
    manager = project.layoutManager()
    for layout_name in LAYOUT_NAMES + OLD_LAYOUT_NAMES:
        existing = manager.layoutByName(layout_name)
        if existing is not None:
            manager.removeLayout(existing)

    QgsRectangle = qgis_types["QgsRectangle"]

    create_context_layout(project, manager, layers, font_factory, qgis_types, QgsRectangle)
    create_bathymetry_layout(project, manager, layers, font_factory, qgis_types, QgsRectangle)
    create_hybrid_layout(project, manager, layers, font_factory, qgis_types, QgsRectangle)
    create_validation_layout(project, manager, layers, font_factory, qgis_types, QgsRectangle)
    create_oblique_layout(project, manager, font_factory, qgis_types)


def new_layout(project: Any, manager: Any, name: str, qgis_types: dict[str, Any]) -> Any:
    layout = qgis_types["QgsPrintLayout"](project)
    layout.initializeDefaults()
    layout.setName(name)
    page = layout.pageCollection().page(0)
    page.setPageSize(qgis_types["QgsLayoutSize"](297, 210, qgis_types["QgsUnitTypes"].LayoutMillimeters))
    manager.addLayout(layout)
    return layout


def add_label(
    layout: Any,
    text: str,
    x: float,
    y: float,
    w: float,
    h: float,
    qgis_types: dict[str, Any],
    font: Any,
    *,
    size: float,
) -> Any:
    item = qgis_types["QgsLayoutItemLabel"](layout)
    item.setText(text)
    fmt = qgis_types["QgsTextFormat"]()
    fmt.setFont(font)
    fmt.setSize(size)
    item.setTextFormat(fmt)
    layout.addLayoutItem(item)
    item.attemptMove(qgis_types["QgsLayoutPoint"](x, y, qgis_types["QgsUnitTypes"].LayoutMillimeters))
    item.attemptResize(qgis_types["QgsLayoutSize"](w, h, qgis_types["QgsUnitTypes"].LayoutMillimeters))
    return item


def add_map(
    layout: Any,
    layer_list: Sequence[Any],
    extent: Any,
    x: float,
    y: float,
    w: float,
    h: float,
    qgis_types: dict[str, Any],
) -> Any:
    item = qgis_types["QgsLayoutItemMap"](layout)
    layout.addLayoutItem(item)
    item.attemptMove(qgis_types["QgsLayoutPoint"](x, y, qgis_types["QgsUnitTypes"].LayoutMillimeters))
    item.attemptResize(qgis_types["QgsLayoutSize"](w, h, qgis_types["QgsUnitTypes"].LayoutMillimeters))
    item.setExtent(extent)
    item.setLayers(list(reversed(layer_list)))
    item.setKeepLayerSet(True)
    item.setFrameEnabled(True)
    item.refresh()
    return item


def add_legend(layout: Any, linked_map: Any, x: float, y: float, w: float, h: float, qgis_types: dict[str, Any], title: str) -> Any:
    legend = qgis_types["QgsLayoutItemLegend"](layout)
    legend.setTitle(title)
    legend.setLinkedMap(linked_map)
    if hasattr(legend, "setLegendFilterByMapEnabled"):
        legend.setLegendFilterByMapEnabled(True)
    layout.addLayoutItem(legend)
    legend.attemptMove(qgis_types["QgsLayoutPoint"](x, y, qgis_types["QgsUnitTypes"].LayoutMillimeters))
    legend.attemptResize(qgis_types["QgsLayoutSize"](w, h, qgis_types["QgsUnitTypes"].LayoutMillimeters))
    return legend


def add_scale_bar(layout: Any, linked_map: Any, x: float, y: float, qgis_types: dict[str, Any], *, units_per_segment: float = 20) -> None:
    bar = qgis_types["QgsLayoutItemScaleBar"](layout)
    bar.setLinkedMap(linked_map)
    bar.setStyle("Line Ticks Up")
    bar.setUnits(qgis_types["QgsUnitTypes"].DistanceKilometers)
    bar.setNumberOfSegments(3)
    bar.setNumberOfSegmentsLeft(0)
    bar.setUnitsPerSegment(units_per_segment)
    bar.setUnitLabel("km")
    layout.addLayoutItem(bar)
    bar.attemptMove(qgis_types["QgsLayoutPoint"](x, y, qgis_types["QgsUnitTypes"].LayoutMillimeters))
    bar.update()


def create_context_layout(
    project: Any,
    manager: Any,
    layers: dict[str, Any],
    font_factory: Callable[[bool], Any],
    qgis_types: dict[str, Any],
    QgsRectangle: Any,
) -> None:
    layout = new_layout(project, manager, "01_TOHOKU_EVENT_CORRIDOR", qgis_types)
    add_label(layout, "2011 Tohoku Event and Accepted Kamaishi Corridor", 10, 7, 230, 10, qgis_types, font_factory(True), size=15.5)
    main_layers = [
        layers["regional_terrain"],
        layers["regional_hillshade"],
        layers["regional_coastline"],
        layers["corridor_polygon"],
        layers["corridor_centreline"],
        layers["event_epicentre"],
        layers["kamaishi_proxy"],
    ]
    main = add_map(layout, main_layers, QgsRectangle(410000, 4190000, 700000, 4410000), 10, 22, 205, 172, qgis_types)
    add_scale_bar(layout, main, 18, 185, qgis_types, units_per_segment=20)
    inset_layers = [
        layers["japan_terrain"],
        layers["japan_context_coastline"],
        layers["corridor_polygon"],
        layers["event_epicentre"],
        layers["kamaishi_proxy"],
    ]
    add_map(layout, inset_layers, QgsRectangle(135000, 3300000, 1230000, 5020000), 226, 22, 58, 72, qgis_types)
    add_label(layout, "Data: ETOPO 2022, G6 corridor manifest, R15 observation register. CRS: EPSG:32654.", 10, 197, 250, 6, qgis_types, font_factory(False), size=6.4)
    add_label(
        layout,
        "Symbol key\nred dot: 2011 event\nblue dot: Kamaishi\nblue outline: corridor\nblue dashed: centreline",
        226,
        132,
        58,
        38,
        qgis_types,
        font_factory(False),
        size=7.4,
    )


def create_bathymetry_layout(
    project: Any,
    manager: Any,
    layers: dict[str, Any],
    font_factory: Callable[[bool], Any],
    qgis_types: dict[str, Any],
    QgsRectangle: Any,
) -> None:
    layout = new_layout(project, manager, "02_CORRIDOR_BATHYMETRY", qgis_types)
    add_label(layout, "Kamaishi Corridor Bathymetry and Topography", 10, 7, 220, 10, qgis_types, font_factory(True), size=15.5)
    main_layers = [
        layers["corridor_terrain"],
        layers["corridor_hillshade"],
        layers["corridor_coastline"],
        layers["corridor_polygon"],
        layers["corridor_centreline"],
        layers["coupling_section"],
        layers["event_epicentre"],
        layers["kamaishi_proxy"],
    ]
    main = add_map(layout, main_layers, QgsRectangle(558000, 4212000, 638000, 4354000), 10, 22, 214, 172, qgis_types)
    add_scale_bar(layout, main, 18, 185, qgis_types, units_per_segment=20)
    add_label(
        layout,
        "Elevation key, EGM2008\nblue: bathymetry below 0 m\ncream: sea level/coast\ngreen/brown: land\n\nOverlays\nblue outline: corridor\nblue dashed: centreline\npurple line: wet interface",
        232,
        24,
        52,
        72,
        qgis_types,
        font_factory(False),
        size=7.4,
    )
    add_label(
        layout,
        "Hillshade is derived from the same ETOPO crop for relief only. The map uses the accepted corridor geometry unchanged.",
        232,
        108,
        52,
        36,
        qgis_types,
        font_factory(False),
        size=7.1,
    )
    add_label(layout, "Conditioned R10/G6 wet terrain remains in the editable project as a source layer; this map uses the real ETOPO crop for publication context.", 10, 200, 262, 5, qgis_types, font_factory(False), size=6.0)


def create_hybrid_layout(
    project: Any,
    manager: Any,
    layers: dict[str, Any],
    font_factory: Callable[[bool], Any],
    qgis_types: dict[str, Any],
    QgsRectangle: Any,
) -> None:
    layout = new_layout(project, manager, "03_HYBRID_DOMAIN", qgis_types)
    add_label(layout, "Regional2D to Local3D Framework Geography", 10, 7, 215, 10, qgis_types, font_factory(True), size=15.5)
    main_layers = [
        layers["nearshore_terrain"],
        layers["nearshore_hillshade"],
        layers["nearshore_coastline"],
        layers["corridor_polygon"],
        layers["corridor_centreline"],
        layers["coupling_section"],
        layers["local3d_footprint"],
        layers["kamaishi_proxy"],
        layers["coupling_centre"],
    ]
    main = add_map(layout, main_layers, QgsRectangle(563000, 4318000, 603000, 4354500), 10, 22, 214, 172, qgis_types)
    add_scale_bar(layout, main, 18, 185, qgis_types, units_per_segment=10)
    add_label(
        layout,
        "Framework key\nblue outline: Regional2D corridor\nblue dashed: centreline\npurple line: wet nearshore interface\nmagenta area: conceptual Local3D footprint",
        232,
        24,
        52,
        54,
        qgis_types,
        font_factory(False),
        size=7.3,
    )
    add_label(
        layout,
        "One-way conceptual chain: frozen Regional2D corridor -> selected wet nearshore interface -> candidate Local3D impact-study footprint.\n\nLocal3D current-generation status remains REPLAY_VOF_BEHAVIOUR_UNRESOLVED.",
        232,
        91,
        52,
        70,
        qgis_types,
        font_factory(False),
        size=7.0,
    )
    add_label(layout, "No OpenFOAM rerun, no Local3D retuning and no production closure are claimed by this figure.", 10, 200, 240, 5, qgis_types, font_factory(False), size=6.0)


def create_validation_layout(
    project: Any,
    manager: Any,
    layers: dict[str, Any],
    font_factory: Callable[[bool], Any],
    qgis_types: dict[str, Any],
    QgsRectangle: Any,
) -> None:
    layout = new_layout(project, manager, "04_VALIDATION_GEOMETRY", qgis_types)
    add_label(layout, "Historical Validation Geometry: Current Corridor Limitation", 10, 7, 240, 10, qgis_types, font_factory(True), size=15.0)
    near_layers = [
        layers["nearshore_terrain"],
        layers["nearshore_hillshade"],
        layers["nearshore_coastline"],
        layers["corridor_polygon"],
        layers["corridor_centreline"],
        layers["validation_all"],
        layers["validation_proxy"],
        layers["validation_priority"],
        layers["kamaishi_proxy"],
    ]
    main = add_map(layout, near_layers, QgsRectangle(560000, 4316000, 604000, 4357000), 10, 22, 164, 172, qgis_types)
    add_scale_bar(layout, main, 18, 185, qgis_types, units_per_segment=10)
    overview_layers = [
        layers["validation_terrain"],
        layers["validation_overview_coastline"],
        layers["corridor_polygon"],
        layers["validation_priority"],
    ]
    add_map(layout, overview_layers, QgsRectangle(535000, 4200000, 1195000, 4410000), 183, 22, 101, 72, qgis_types)
    add_label(
        layout,
        "Observation key\nsmall grey: R15 register target\norange: PROXY observation\nteal: priority out-of-corridor target\nblue outline: current corridor",
        183,
        102,
        45,
        54,
        qgis_types,
        font_factory(False),
        size=7.0,
    )
    add_label(
        layout,
        "R15 register: 29 observations, 0 DIRECT, 1 PROXY, 28 TARGET_ONLY.\n\nNOWPHAS 802G is about 12.3 km outside the corridor; DART 21418 is about 545 km outside.",
        232,
        102,
        52,
        62,
        qgis_types,
        font_factory(False),
        size=7.0,
    )
    add_label(layout, "This geometry explains why the current R10 h400 result is not historically validated and why the corridor was not altered in R16B.", 10, 200, 258, 5, qgis_types, font_factory(False), size=6.0)


def create_oblique_layout(project: Any, manager: Any, font_factory: Callable[[bool], Any], qgis_types: dict[str, Any]) -> None:
    layout = new_layout(project, manager, "05_CORRIDOR_BATHYMETRY_OBLIQUE", qgis_types)
    add_label(layout, "Oblique Bathymetry View of the Kamaishi Corridor", 10, 7, 230, 10, qgis_types, font_factory(True), size=15.5)
    picture = qgis_types["QgsLayoutItemPicture"](layout)
    picture.setPicturePath(FIGURE_C_RENDER.as_posix())
    layout.addLayoutItem(picture)
    picture.attemptMove(qgis_types["QgsLayoutPoint"](10, 22, qgis_types["QgsUnitTypes"].LayoutMillimeters))
    picture.attemptResize(qgis_types["QgsLayoutSize"](250, 150, qgis_types["QgsUnitTypes"].LayoutMillimeters))
    add_label(
        layout,
        f"Direct VTK offscreen terrain render from the ETOPO corridor crop; vertical exaggeration {VERTICAL_EXAGGERATION:g}x. Red outline marks the accepted corridor footprint.",
        10,
        178,
        252,
        12,
        qgis_types,
        font_factory(False),
        size=7.1,
    )
    add_label(layout, "QGIS 3D was not used because the headless QGIS 4.2 processing surface exposed no stable 3D layout export path in this runtime.", 10, 196, 265, 6, qgis_types, font_factory(False), size=6.4)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--allow-blocked", action="store_true", help="Exit 0 while recording QGIS_RUNTIME_BLOCKED.")
    args = parser.parse_args(argv)
    payload = build_project(allow_blocked=args.allow_blocked)
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
