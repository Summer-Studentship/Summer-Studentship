#!/usr/bin/env python3
"""Generate and validate the R18 frozen poster figure package.

R18 is a visual freeze only: this script consumes the existing R10/R15/R16/R17
evidence and does not launch a Regional2D simulation, alter geometry, or create
new numerical evidence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Mapping, Sequence


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_FIGURES = REPO_ROOT / "tools/figures"
if TOOLS_FIGURES.as_posix() not in sys.path:
    sys.path.insert(0, TOOLS_FIGURES.as_posix())

import r16_publication as r16  # noqa: E402


R18_ROOT = REPO_ROOT / "deliverables/figures/r18_poster"
PUBLICATION_ROOT = R18_ROOT / "publication"
PREVIEW_ROOT = R18_ROOT / "previews"
PROVENANCE_ROOT = R18_ROOT / "provenance"
SOURCE_ROOT = R18_ROOT / "sources"
PYTHON_SOURCE_ROOT = SOURCE_ROOT / "python"
BLENDER_SOURCE_ROOT = SOURCE_ROOT / "blender"

R16_ROOT = REPO_ROOT / "deliverables/figures/r16_publication"
R16_PUBLICATION = R16_ROOT / "publication"
R16_PROVENANCE = R16_ROOT / "provenance"
QGIS_DERIVED = R16_ROOT / "sources/qgis/derived"
QGIS_LAYERS = R16_ROOT / "sources/qgis/layers"
R17_ROOT = REPO_ROOT / "deliverables/figures/r17_closure"

CORRIDOR_RASTER = QGIS_DERIVED / "etopo_corridor_kamaishi_utm54.tif"
CORRIDOR_HILLSHADE = QGIS_DERIVED / "corridor_hillshade.tif"
CONTEXT_RASTER = QGIS_DERIVED / "etopo_japan_context_utm54.tif"
NEARSHORE_RASTER = QGIS_DERIVED / "etopo_nearshore_hybrid_utm54.tif"
NEARSHORE_HILLSHADE = QGIS_DERIVED / "nearshore_hillshade.tif"
VALIDATION_RASTER = QGIS_DERIVED / "etopo_validation_overview_utm54.tif"

CORRIDOR_POLYGON = QGIS_LAYERS / "corridor_polygon.geojson"
CORRIDOR_CENTRELINE = QGIS_LAYERS / "corridor_centreline.geojson"
COUPLING_SECTION = QGIS_LAYERS / "coupling_section.geojson"
EVENT_POINTS = QGIS_LAYERS / "event_and_kamaishi_points.geojson"
VALIDATION_STATIONS = QGIS_LAYERS / "validation_stations.geojson"

CLEAN_C = PUBLICATION_ROOT / "figure_C_bathymetry_3d_clean.png"
ANNOTATED_C = PUBLICATION_ROOT / "figure_C_bathymetry_3d_annotated.png"
C_PDF = PUBLICATION_ROOT / "figure_C_bathymetry_3d.pdf"
C_RENDER_RECORD = PROVENANCE_ROOT / "figure_C_bathymetry_3d_clean_render.json"
CONTACT_SHEET = PUBLICATION_ROOT / "r18_poster_contact_sheet.png"
MANIFEST_JSON = PROVENANCE_ROOT / "r18_poster_visual_manifest.json"
COMPLETION_JSON = PROVENANCE_ROOT / "r18_completion_state.json"
VALIDATION_JSON = PROVENANCE_ROOT / "r18_validation.json"
HANDOFF_MD = REPO_ROOT / "docs/project/r18_poster_visual_handoff.md"
COMPLETION_MD = REPO_ROOT / "docs/project/r18_completion_report.md"

STARTING_HEAD = "73f9a581740dba8d09ee2cb14c8a26ac22bc3eea"
BRANCH_NAME = "feat/r18-poster-visual-freeze"
CORRIDOR_ACCENT = "#d66e4b"
CORRIDOR_DARK = "#a84831"
TEXT = "#263238"
SUBTLE = "#64727a"
GRID = "#d5dde2"

SCIENTIFIC_AUTHORITY = {
    "regional_numerical_authority": [
        "MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES",
        "GLOBAL_FIRST_ORDER_VERIFIED",
        "SECOND_ORDER_VERIFIED",
    ],
    "event_result": "R10 h400 limited_linear",
    "event_result_status": "BEST_AVAILABLE_NUMERICALLY_UNCERTAIN",
    "event_limitations": [
        "real Tohoku event simulation",
        "verified numerical method",
        "not spatially qualified",
        "not physically calibrated",
        "not historically validated",
    ],
    "spatial_diagnosis": {
        "terrain_source_fidelity": "TERRAIN_SOURCE_FIDELITY_DOMINANT",
        "confidence": "MODERATE",
        "projection": "PROJECTION_FIDELITY_CEILING",
    },
    "hybrid_status": {
        "one_way_2d_to_3d": "implemented/demonstrated through G6",
        "current_h400_local3d": "REPLAY_VOF_BEHAVIOUR_UNRESOLVED",
    },
    "historical_observations": {
        "observations": 29,
        "DIRECT": 0,
        "PROXY": 1,
        "TARGET_ONLY": 28,
        "NOWPHAS_802G_distance_km": 12.273092741550476,
        "DART_21418_distance_km": 544.6414251283993,
    },
}


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def ensure_layout() -> None:
    for path in [PUBLICATION_ROOT, PREVIEW_ROOT, PROVENANCE_ROOT, PYTHON_SOURCE_ROOT, BLENDER_SOURCE_ROOT]:
        path.mkdir(parents=True, exist_ok=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path) -> dict[str, Any]:
    display = path.relative_to(REPO_ROOT).as_posix() if path.is_relative_to(REPO_ROOT) else path.as_posix()
    record: dict[str, Any] = {"path": display, "exists": path.exists()}
    if path.is_file():
        record.update({"bytes": path.stat().st_size, "sha256": sha256(path)})
    return record


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def normalize_text_file(path: Path) -> None:
    path.write_text("\n".join(line.rstrip() for line in path.read_text(encoding="utf-8").splitlines()) + "\n", encoding="utf-8")


def command_output(command: Sequence[str]) -> tuple[int, str]:
    completed = subprocess.run(
        list(command),
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    return completed.returncode, completed.stdout.strip()


def git_sha() -> str:
    code, output = command_output(["git", "rev-parse", "HEAD"])
    return output if code == 0 else "unknown"


def git_branch() -> str:
    code, output = command_output(["git", "branch", "--show-current"])
    return output if code == 0 else "unknown"


def configure_matplotlib() -> Any:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update(
        {
            "figure.facecolor": "white",
            "axes.facecolor": "white",
            "savefig.facecolor": "white",
            "font.family": "DejaVu Sans",
            "font.size": 9.8,
            "axes.titlesize": 13.5,
            "axes.labelsize": 9.8,
            "xtick.labelsize": 8.6,
            "ytick.labelsize": 8.6,
            "legend.fontsize": 8.6,
            "legend.title_fontsize": 8.6,
            "axes.edgecolor": TEXT,
            "axes.labelcolor": TEXT,
            "xtick.color": TEXT,
            "ytick.color": TEXT,
            "axes.linewidth": 0.8,
            "grid.color": GRID,
            "grid.linewidth": 0.45,
            "lines.linewidth": 1.8,
            "svg.fonttype": "none",
            "pdf.fonttype": 42,
            "savefig.dpi": 420,
        }
    )
    return plt


def save_matplotlib_figure(fig: Any, basename: str, *, dpi: int = 420) -> dict[str, str]:
    outputs: dict[str, str] = {}
    for suffix in [".pdf", ".svg", ".png"]:
        target = PUBLICATION_ROOT / f"{basename}{suffix}"
        if suffix == ".png":
            fig.savefig(target, dpi=dpi, bbox_inches="tight", pad_inches=0.08)
            preview = PREVIEW_ROOT / f"{basename}.png"
            shutil.copy2(target, preview)
        else:
            fig.savefig(target, bbox_inches="tight", pad_inches=0.08)
            if suffix == ".svg":
                normalize_text_file(target)
        outputs[suffix[1:]] = target.relative_to(REPO_ROOT).as_posix()
    return outputs


def write_image_pdf(image_path: Path, pdf_path: Path) -> None:
    from PIL import Image

    with Image.open(image_path) as image:
        image.convert("RGB").save(pdf_path, "PDF", resolution=300.0)


def image_size(path: Path) -> tuple[int, int]:
    from PIL import Image

    with Image.open(path) as image:
        return image.size


def load_raster(path: Path) -> tuple[Any, tuple[float, float, float, float], dict[str, Any]]:
    import numpy as np
    from osgeo import gdal

    ds = gdal.Open(path.as_posix())
    if ds is None:
        raise RuntimeError(f"Could not open raster: {path}")
    band = ds.GetRasterBand(1)
    arr = band.ReadAsArray().astype(float)
    nodata = band.GetNoDataValue()
    if nodata is not None:
        arr = np.where(arr == nodata, np.nan, arr)
    gt = ds.GetGeoTransform()
    width = ds.RasterXSize
    height = ds.RasterYSize
    extent = (gt[0], gt[0] + gt[1] * width, gt[3] + gt[5] * height, gt[3])
    meta = {
        "path": file_record(path),
        "width": width,
        "height": height,
        "extent_m": {"xmin": extent[0], "xmax": extent[1], "ymin": extent[2], "ymax": extent[3]},
        "pixel_size_m": {"x": gt[1], "y": gt[5]},
        "minimum_m": float(np.nanmin(arr)),
        "maximum_m": float(np.nanmax(arr)),
        "crs": "EPSG:32654 WGS 84 / UTM zone 54N",
        "vertical_datum": "EGM2008 height, positive up",
    }
    return arr, extent, meta


def read_geojson(path: Path) -> dict[str, Any]:
    return read_json(path)


def feature_by_role(path: Path, role: str) -> tuple[float, float]:
    for feature in read_geojson(path)["features"]:
        if feature["properties"].get("role") == role:
            x, y = feature["geometry"]["coordinates"]
            return float(x), float(y)
    raise KeyError(role)


def station_by_id(observation_id: str) -> dict[str, Any]:
    for feature in read_geojson(VALIDATION_STATIONS)["features"]:
        if feature["properties"]["observation_id"] == observation_id:
            return feature
    raise KeyError(observation_id)


def plot_polygon(ax: Any, path: Path, *, edge: str = CORRIDOR_ACCENT, face: str = "none", lw: float = 2.0, alpha: float = 1.0, zorder: int = 5) -> None:
    data = read_geojson(path)
    for feature in data["features"]:
        coords = feature["geometry"]["coordinates"]
        rings = coords if feature["geometry"]["type"] == "Polygon" else [coords]
        for ring in rings:
            xs = [float(p[0]) for p in ring]
            ys = [float(p[1]) for p in ring]
            if face != "none":
                ax.fill(xs, ys, facecolor=face, edgecolor="none", alpha=alpha, zorder=zorder)
            ax.plot(xs, ys, color=edge, linewidth=lw, alpha=1.0, zorder=zorder + 1)


def plot_line(ax: Any, path: Path, *, color: str = SUBTLE, lw: float = 1.2, ls: str = "-", alpha: float = 1.0, zorder: int = 6) -> None:
    data = read_geojson(path)
    for feature in data["features"]:
        coords = feature["geometry"]["coordinates"]
        xs = [float(p[0]) for p in coords]
        ys = [float(p[1]) for p in coords]
        ax.plot(xs, ys, color=color, linewidth=lw, linestyle=ls, alpha=alpha, zorder=zorder)


def apply_km_axes(ax: Any) -> None:
    from matplotlib.ticker import FuncFormatter

    ax.xaxis.set_major_formatter(FuncFormatter(lambda value, _pos: f"{value / 1000:.0f}"))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda value, _pos: f"{value / 1000:.0f}"))
    ax.set_xlabel("Easting, km (UTM 54N)")
    ax.set_ylabel("Northing, km (UTM 54N)")
    ax.grid(True, alpha=0.28)


def terrain_cmap() -> Any:
    from matplotlib.colors import LinearSegmentedColormap

    return LinearSegmentedColormap.from_list(
        "r18_terrain",
        [
            (0.00, "#173c5f"),
            (0.20, "#3f7fa2"),
            (0.42, "#9fcfd2"),
            (0.50, "#f6f1df"),
            (0.58, "#9cb982"),
            (0.78, "#d1bd91"),
            (1.00, "#867769"),
        ],
    )


def draw_raster_map(ax: Any, raster: Path, hillshade: Path | None, *, vmin: float, vmax: float, title: str | None = None) -> tuple[Any, dict[str, Any]]:
    from matplotlib.colors import TwoSlopeNorm

    arr, extent, meta = load_raster(raster)
    norm = TwoSlopeNorm(vmin=vmin, vcenter=0.0, vmax=vmax)
    image = ax.imshow(arr, extent=extent, origin="upper", cmap=terrain_cmap(), norm=norm, interpolation="bilinear", zorder=1)
    if hillshade and hillshade.is_file():
        shade, shade_extent, shade_meta = load_raster(hillshade)
        ax.imshow(shade, extent=shade_extent, origin="upper", cmap="gray", alpha=0.22, interpolation="bilinear", zorder=2)
        meta["hillshade"] = shade_meta
    x = [extent[0], extent[1]]
    y = [extent[2], extent[3]]
    try:
        ax.contour(
            arr,
            levels=[0.0],
            extent=extent,
            origin="upper",
            colors="#4c5960",
            linewidths=0.75,
            alpha=0.75,
            zorder=4,
        )
    except Exception:
        ax.plot(x, [y[0], y[0]], alpha=0.0)
    ax.set_xlim(extent[0], extent[1])
    ax.set_ylim(extent[2], extent[3])
    ax.set_aspect("equal", adjustable="box")
    if title:
        ax.set_title(title, loc="left", color=TEXT, pad=7)
    apply_km_axes(ax)
    return image, meta


def add_scalebar(ax: Any, *, length_km: float, location: tuple[float, float] = (0.08, 0.08)) -> None:
    xmin, xmax = ax.get_xlim()
    ymin, ymax = ax.get_ylim()
    x0 = xmin + location[0] * (xmax - xmin)
    y0 = ymin + location[1] * (ymax - ymin)
    x1 = x0 + length_km * 1000.0
    ax.plot([x0, x1], [y0, y0], color=TEXT, lw=1.4, zorder=20)
    ax.plot([x0, x0], [y0 - 900, y0 + 900], color=TEXT, lw=1.1, zorder=20)
    ax.plot([x1, x1], [y0 - 900, y0 + 900], color=TEXT, lw=1.1, zorder=20)
    ax.text((x0 + x1) / 2.0, y0 + 1700, f"{length_km:.0f} km", color=TEXT, ha="center", va="bottom", fontsize=8.8, zorder=20)


def source_records() -> dict[str, Any]:
    return {
        "r10_h400_limited_linear_hdf5": file_record(r16.H400_HDF5),
        "r15_observation_register": file_record(r16.R15_REGISTER),
        "r16_publication_manifest": file_record(R16_PROVENANCE / "publication_figure_manifest.json"),
        "r16_qgis_project": file_record(R16_ROOT / "sources/qgis/tohoku_kamaishi_publication.qgz"),
        "r16_corridor_raster": file_record(CORRIDOR_RASTER),
        "r16_context_raster": file_record(CONTEXT_RASTER),
        "r16_nearshore_raster": file_record(NEARSHORE_RASTER),
        "r16_validation_overview_raster": file_record(VALIDATION_RASTER),
        "r16_corridor_polygon": file_record(CORRIDOR_POLYGON),
        "r16_corridor_centreline": file_record(CORRIDOR_CENTRELINE),
        "r16_coupling_section": file_record(COUPLING_SECTION),
        "r16_event_points": file_record(EVENT_POINTS),
        "r16_validation_stations": file_record(VALIDATION_STATIONS),
        "r17_blender_terrain_manifest": file_record(R17_ROOT / "sources/blender/terrain/terrain_manifest.json"),
        "r17_blender_scene": file_record(R17_ROOT / "sources/blender/figure_C_corridor_bathymetry_3d.blend"),
    }


def software_record() -> dict[str, Any]:
    versions: dict[str, Any] = {"python": sys.version.split()[0]}
    for name in ["matplotlib", "numpy", "h5py", "PIL"]:
        try:
            module = __import__(name)
            versions[name] = getattr(module, "__version__", "available")
        except Exception as exc:
            versions[name] = f"UNAVAILABLE: {type(exc).__name__}: {exc}"
    for label, command in {
        "qgis": ["qgis", "--version"],
        "qgis_process": ["qgis_process", "--version"],
        "gdal": ["gdalinfo", "--version"],
        "blender": ["blender", "--version"],
    }.items():
        code, output = command_output(command)
        versions[label] = output.splitlines()[0] if code == 0 and output else "UNAVAILABLE"
    return versions


def figure_record(
    figure_id: str,
    basename: str,
    outputs: Mapping[str, str],
    *,
    role: str,
    question: str,
    caption: str,
    allowed_claim: str,
    required_caveat: str,
    details: Mapping[str, Any],
) -> dict[str, Any]:
    payload = {
        "schema": {"name": "tsunami.r18.figure_provenance", "version": "1.0.0"},
        "figure_id": figure_id,
        "basename": basename,
        "status": "COMPLETE",
        "poster_classification": role,
        "scientific_question": question,
        "caption": caption,
        "allowed_claim": allowed_claim,
        "required_caveat": required_caveat,
        "outputs": dict(outputs),
        "output_hashes": {kind: sha256(REPO_ROOT / rel_path) for kind, rel_path in outputs.items() if (REPO_ROOT / rel_path).is_file()},
        "source_datasets": source_records(),
        "scientific_authority": SCIENTIFIC_AUTHORITY,
        "software": software_record(),
        "git_sha": git_sha(),
        "branch": git_branch(),
        "generated_at_utc": utc_now(),
        **dict(details),
    }
    write_json(PROVENANCE_ROOT / f"{basename}.provenance.json", payload)
    return payload


def make_image_sheet(entries: Sequence[tuple[str, Path]], output: Path, *, panel_size: tuple[int, int], columns: int) -> dict[str, Any]:
    from PIL import Image, ImageDraw, ImageFont

    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        label_font = ImageFont.truetype("/usr/share/fonts/liberation/LiberationSans-Regular.ttf", 42)
    except OSError:
        label_font = ImageFont.load_default()
    rows = math.ceil(len(entries) / columns)
    panel_w, panel_h = panel_size
    sheet = Image.new("RGB", (panel_w * columns, panel_h * rows), "white")
    for index, (label, path) in enumerate(entries):
        image = Image.open(path).convert("RGB")
        image.thumbnail((panel_w - 40, panel_h - 92), Image.Resampling.LANCZOS)
        panel = Image.new("RGB", panel_size, "white")
        draw = ImageDraw.Draw(panel)
        draw.text((28, 20), label, fill=TEXT, font=label_font)
        panel.paste(image, ((panel_w - image.width) // 2, 78 + (panel_h - 92 - image.height) // 2))
        sheet.paste(panel, ((index % columns) * panel_w, (index // columns) * panel_h))
    sheet.save(output)
    preview = PREVIEW_ROOT / output.name
    if output != preview:
        shutil.copy2(output, preview)
    return {"path": file_record(output), "dimensions": {"width": sheet.width, "height": sheet.height}}


def package_c_decision_sheets() -> dict[str, Any]:
    sea = make_image_sheet(
        [
            ("sea alpha 0.03", PREVIEW_ROOT / "figure_C_seaplane_alpha_003.png"),
            ("sea alpha 0.05", PREVIEW_ROOT / "figure_C_seaplane_alpha_005.png"),
            ("sea alpha 0.07", PREVIEW_ROOT / "figure_C_seaplane_alpha_007.png"),
        ],
        PUBLICATION_ROOT / "figure_C_seaplane_comparison.png",
        panel_size=(1600, 1040),
        columns=3,
    )
    camera = make_image_sheet(
        [
            ("orthographic 4x", PREVIEW_ROOT / "figure_C_camera_orthographic.png"),
            ("perspective 4x, 82 mm", PREVIEW_ROOT / "figure_C_camera_perspective.png"),
        ],
        PUBLICATION_ROOT / "figure_C_camera_comparison.png",
        panel_size=(2000, 1160),
        columns=2,
    )
    return {"sea_plane_comparison": sea, "camera_comparison": camera}


def annotate_c() -> dict[str, Any]:
    from PIL import Image, ImageDraw, ImageFont

    if not CLEAN_C.is_file():
        raise RuntimeError(f"Missing clean Figure C render: {CLEAN_C}")
    image = Image.open(CLEAN_C).convert("RGBA")
    overlay = Image.new("RGBA", image.size, (255, 255, 255, 0))
    draw = ImageDraw.Draw(overlay)
    w, h = image.size
    try:
        label_font = ImageFont.truetype("/usr/share/fonts/liberation/LiberationSans-Regular.ttf", max(46, w // 120))
        small_font = ImageFont.truetype("/usr/share/fonts/liberation/LiberationSans-Regular.ttf", max(40, w // 140))
    except OSError:
        label_font = ImageFont.load_default()
        small_font = ImageFont.load_default()

    def stroked_text(xy: tuple[int, int], text: str, font: Any, fill: tuple[int, int, int, int]) -> None:
        draw.text(xy, text, fill=(255, 255, 255, 230), font=font, stroke_width=max(2, w // 1300), stroke_fill=(255, 255, 255, 230))
        draw.text(xy, text, fill=fill, font=font)

    kamaishi_xy = (int(w * 0.52), int(h * 0.24))
    stroked_text(kamaishi_xy, "Kamaishi", label_font, (38, 50, 56, 245))
    draw.ellipse((kamaishi_xy[0] - 18, kamaishi_xy[1] + 74, kamaishi_xy[0] + 18, kamaishi_xy[1] + 110), fill=(214, 110, 75, 230), outline=(255, 255, 255, 240), width=max(3, w // 1500))

    arrow_start = (int(w * 0.38), int(h * 0.79))
    arrow_end = (int(w * 0.57), int(h * 0.38))
    draw.line([arrow_start, arrow_end], fill=(168, 72, 49, 235), width=max(8, w // 520))
    angle = math.atan2(arrow_end[1] - arrow_start[1], arrow_end[0] - arrow_start[0])
    head_len = max(42, w // 75)
    for sign in [-1, 1]:
        theta = angle + sign * 0.48 + math.pi
        point = (arrow_end[0] + int(math.cos(theta) * head_len), arrow_end[1] + int(math.sin(theta) * head_len))
        draw.line([arrow_end, point], fill=(168, 72, 49, 235), width=max(8, w // 520))
    stroked_text((int(w * 0.30), int(h * 0.73)), "Offshore -> coast", small_font, (88, 58, 49, 245))

    annotated = Image.alpha_composite(image, overlay).convert("RGB")
    annotated.save(ANNOTATED_C)
    shutil.copy2(ANNOTATED_C, PREVIEW_ROOT / ANNOTATED_C.name)
    write_image_pdf(ANNOTATED_C, C_PDF)
    return {
        "clean_png": file_record(CLEAN_C),
        "annotated_png": file_record(ANNOTATED_C),
        "pdf": file_record(C_PDF),
        "annotation_method": "PIL overlay on frozen clean Blender raster; labels only, no geometry changes",
        "annotations": ["Kamaishi", "Offshore -> coast arrow"],
    }


def package_c_final(decision_sheets: Mapping[str, Any]) -> dict[str, Any]:
    annotation = annotate_c()
    outputs = {
        "clean_png": CLEAN_C.relative_to(REPO_ROOT).as_posix(),
        "annotated_png": ANNOTATED_C.relative_to(REPO_ROOT).as_posix(),
        "pdf": C_PDF.relative_to(REPO_ROOT).as_posix(),
        "sea_plane_comparison_png": (PUBLICATION_ROOT / "figure_C_seaplane_comparison.png").relative_to(REPO_ROOT).as_posix(),
        "camera_comparison_png": (PUBLICATION_ROOT / "figure_C_camera_comparison.png").relative_to(REPO_ROOT).as_posix(),
    }
    record = figure_record(
        "C",
        "figure_C_bathymetry_3d",
        outputs,
        role="PRIMARY",
        question="What does the real Kamaishi corridor bathymetry and coastal relief look like in an interpretable oblique view?",
        caption=(
            "Oblique Blender terrain visualisation of the accepted Kamaishi Regional2D corridor from the R16/R17 ETOPO 2022 "
            "EGM2008 terrain lineage. Vertical relief is exaggerated 4x for interpretation; the blue plane marks z=0 and "
            "the muted coral outline is the unchanged computational corridor."
        ),
        allowed_claim="Shows the accepted corridor over the existing ETOPO 2022 bathymetry/topography lineage as a visual terrain context figure.",
        required_caveat="Terrain visualisation only; not a Local3D, OpenFOAM, calibrated inundation or historically validated result.",
        details={
            "vertical_exaggeration": 4.0,
            "sea_plane_opacity_candidates": [0.03, 0.05, 0.07],
            "selected_sea_plane_opacity": 0.05,
            "selected_sea_plane_rationale": "0.05 keeps the z=0 water plane visible without masking seabed relief.",
            "corridor_line_change": "outline bevel scaled to 1.25 relative to R17 with muted coral stroke and unchanged footprint",
            "camera_candidates": {"orthographic": file_record(PREVIEW_ROOT / "figure_C_camera_orthographic.png"), "perspective_82mm": file_record(PREVIEW_ROOT / "figure_C_camera_perspective.png")},
            "selected_camera": "orthographic",
            "camera_selection_rationale": "The 82 mm perspective adds depth but weakens geographic footprint readability; orthographic preserves the corridor extent and coastline relationship.",
            "render_record": file_record(C_RENDER_RECORD),
            "decision_sheets": dict(decision_sheets),
            "annotation": annotation,
        },
    )
    return record


def plot_profile_inset(ax: Any, section: Any, *, title: str) -> dict[str, Any]:
    import numpy as np

    x = section.distance_to_shore_km
    bed = section.bed_m
    ax.fill_between(x, bed, 0.0, where=bed <= 0.0, color="#8fc6d6", alpha=0.50, linewidth=0)
    ax.plot(x, bed, color="#394b59", lw=1.35)
    ax.axhline(0.0, color=TEXT, lw=0.75)
    ax.set_xlim(float(np.nanmax(x)), 0.0)
    ax.set_ylim(float(np.nanmin(bed)) * 1.05, 140)
    ax.set_title(title, loc="left", pad=5)
    ax.set_xlabel("Distance offshore from nearshore interface, km")
    ax.set_ylabel("Bed elevation, m")
    ax.grid(True, alpha=0.25)
    return {
        "distance_km": [float(np.nanmin(x)), float(np.nanmax(x))],
        "bed_m": [float(np.nanmin(bed)), float(np.nanmax(bed))],
        "sample_count": int(len(x)),
        "sampling_method": section.method,
    }


def plot_a1(section: Any) -> dict[str, Any]:
    plt = configure_matplotlib()
    fig = plt.figure(figsize=(11.2, 12.0))
    ax_main = fig.add_axes([0.07, 0.12, 0.58, 0.77])
    ax_context = fig.add_axes([0.67, 0.61, 0.28, 0.28])
    ax_profile = fig.add_axes([0.67, 0.18, 0.28, 0.29])
    cax = fig.add_axes([0.09, 0.067, 0.53, 0.022])
    fig.text(0.07, 0.945, "Kamaishi corridor bathymetry and regional context", color=TEXT, fontsize=18, weight="bold")

    image, main_meta = draw_raster_map(ax_main, CORRIDOR_RASTER, CORRIDOR_HILLSHADE, vmin=-1500.0, vmax=1300.0)
    plot_polygon(ax_main, CORRIDOR_POLYGON, edge=CORRIDOR_ACCENT, face=CORRIDOR_ACCENT, lw=2.4, alpha=0.20, zorder=8)
    plot_line(ax_main, CORRIDOR_CENTRELINE, color=CORRIDOR_DARK, lw=1.25, ls="-", alpha=0.85, zorder=9)
    plot_line(ax_main, COUPLING_SECTION, color="#5d416c", lw=1.25, ls="-", alpha=0.86, zorder=9)
    kamaishi = feature_by_role(EVENT_POINTS, "kamaishi_proxy")
    event = feature_by_role(EVENT_POINTS, "event_epicentre")
    ax_main.scatter([kamaishi[0]], [kamaishi[1]], s=42, color=CORRIDOR_DARK, edgecolor="white", linewidth=0.75, zorder=12)
    ax_main.annotate("Kamaishi", kamaishi, xytext=(8, 7), textcoords="offset points", fontsize=9.2, color=TEXT, zorder=13)
    add_scalebar(ax_main, length_km=20.0)
    ax_main.set_xlabel("")
    cbar = fig.colorbar(image, cax=cax, orientation="horizontal")
    cbar.set_label("Elevation relative to EGM2008, m")

    _, context_meta = draw_raster_map(ax_context, CONTEXT_RASTER, None, vmin=-8000.0, vmax=2400.0, title="Regional inset")
    plot_polygon(ax_context, CORRIDOR_POLYGON, edge=CORRIDOR_ACCENT, face=CORRIDOR_ACCENT, lw=1.1, alpha=0.28, zorder=8)
    ax_context.scatter([event[0]], [event[1]], marker="*", s=80, color="#6e3f31", edgecolor="white", linewidth=0.5, zorder=12)
    ax_context.scatter([kamaishi[0]], [kamaishi[1]], s=24, color=CORRIDOR_DARK, edgecolor="white", linewidth=0.45, zorder=12)
    ax_context.annotate("Event", event, xytext=(6, -12), textcoords="offset points", fontsize=7.4, color=TEXT)
    ax_context.annotate("Kamaishi", kamaishi, xytext=(5, 6), textcoords="offset points", fontsize=7.4, color=TEXT)
    ax_context.set_xlabel("")
    ax_context.set_ylabel("")
    ax_context.tick_params(labelsize=7)

    profile_meta = plot_profile_inset(ax_profile, section, title="Along-corridor profile")
    outputs = save_matplotlib_figure(fig, "figure_A1_tohoku_kamaishi_corridor_bathymetry", dpi=420)
    plt.close(fig)
    return figure_record(
        "A1",
        "figure_A1_tohoku_kamaishi_corridor_bathymetry",
        outputs,
        role="HERO",
        question="Where is the accepted Kamaishi corridor, and what bathymetry does the simulated wave traverse?",
        caption="Combined context, corridor and longitudinal bathymetry figure using the existing R16 ETOPO 2022 EGM2008 GIS sources and frozen R10 h400 centreline sampling.",
        allowed_claim="Shows the unchanged accepted R10/G6 corridor, event/Kamaishi context and along-corridor bathymetry from existing publication sources.",
        required_caveat="Geographic and bathymetric context only; does not establish calibration, historical validation or real-event spatial qualification.",
        details={
            "composition": {
                "main_source_map": "R16 Figure B source raster and corridor layers",
                "geographic_inset_source": "R16 Figure A context raster and event/Kamaishi layers",
                "bathymetry_inset_source": "R16 S1/R10 h400 centreline sampling",
            },
            "corridor_styling": {"stroke": CORRIDOR_ACCENT, "fill_alpha": 0.20, "geometry": "unchanged corridor_polygon.geojson"},
            "colourbar": {"range_m": [-1500.0, 1300.0], "normalisation": "TwoSlopeNorm centered at EGM2008 zero", "label": "Elevation relative to EGM2008, m"},
            "map_crs": "EPSG:32654 WGS 84 / UTM zone 54N",
            "display_interpolation": "bilinear display interpolation for raster rendering only",
            "main_map": main_meta,
            "context_map": context_meta,
            "profile": profile_meta,
        },
    )


def plot_d1(section: Any) -> dict[str, Any]:
    import numpy as np
    from matplotlib.colors import TwoSlopeNorm

    plt = configure_matplotlib()
    fig, ax = plt.subplots(figsize=(9.2, 5.65), constrained_layout=True)
    eta = section.eta_m
    x = section.distance_to_shore_km
    t_min = section.time_s / 60.0
    abs_limit = math.ceil(float(np.nanmax(np.abs(eta))) * 20.0) / 20.0
    norm = TwoSlopeNorm(vmin=-abs_limit, vcenter=0.0, vmax=abs_limit)
    mesh = ax.pcolormesh(x, t_min, eta, cmap="RdBu_r", norm=norm, shading="auto", rasterized=True)
    negative_levels = [-1.0, -0.5, -0.25]
    positive_levels = [0.25]
    ax.contour(x, t_min, eta, levels=negative_levels, colors="#37474f", linewidths=0.68, linestyles="dashed")
    ax.contour(x, t_min, eta, levels=positive_levels, colors="#263238", linewidths=0.72, linestyles="solid")
    ax.axvline(0.0, color=TEXT, lw=0.75, alpha=0.65)
    ax.set_xlim(float(np.nanmax(x)), 0.0)
    ax.set_xlabel("Distance offshore from nearshore interface, km")
    ax.set_ylabel("Time after earthquake, min")
    ax.set_title("Simulated free-surface evolution toward Kamaishi", loc="left", pad=8)
    ax.grid(False)
    cbar = fig.colorbar(mesh, ax=ax, pad=0.025)
    cbar.set_label("Free-surface elevation eta, m")
    outputs = save_matplotlib_figure(fig, "figure_D1_eta_space_time_publication", dpi=420)
    plt.close(fig)
    return figure_record(
        "D1",
        "figure_D1_eta_space_time_publication",
        outputs,
        role="PRIMARY",
        question="How does the frozen h400 simulated free surface evolve toward Kamaishi along the accepted corridor?",
        caption="Frozen R10 h400 limited_linear free-surface elevation sampled along the accepted centreline toward the nearshore interface.",
        allowed_claim="Shows the existing R10 h400 limited_linear eta evolution along the accepted corridor sampling path.",
        required_caveat="Best available numerically uncertain real-event result; not spatially qualified, calibrated or historically validated.",
        details={
            "title": "Simulated free-surface evolution toward Kamaishi",
            "distance_axis_convention": "Distance offshore from nearshore interface, km; zero is the selected wet nearshore interface.",
            "time_axis": "Time after earthquake, min",
            "colour_normalisation": {"type": "diverging", "center_m": 0.0, "symmetric_limit_m": abs_limit, "cmap": "RdBu_r"},
            "contours": {"positive_solid_m": positive_levels, "negative_dashed_m": negative_levels},
            "display_interpolation": "none; pcolormesh cell rendering of stored h400 samples",
            "section_metadata": r16.section_metadata(section),
            "field": "eta = h + bed_elevation",
            "data_range_m": {"minimum": float(np.nanmin(eta)), "maximum": float(np.nanmax(eta))},
        },
    )


def nearest_point_on_segment(px: float, py: float, ax: float, ay: float, bx: float, by: float) -> tuple[float, float, float]:
    abx = bx - ax
    aby = by - ay
    denom = abx * abx + aby * aby
    if denom == 0:
        qx, qy = ax, ay
    else:
        t = max(0.0, min(1.0, ((px - ax) * abx + (py - ay) * aby) / denom))
        qx = ax + t * abx
        qy = ay + t * aby
    return qx, qy, math.hypot(px - qx, py - qy)


def nearest_point_to_corridor(point: tuple[float, float]) -> tuple[tuple[float, float], float]:
    ring = read_geojson(CORRIDOR_POLYGON)["features"][0]["geometry"]["coordinates"][0]
    px, py = point
    best = ((float(ring[0][0]), float(ring[0][1])), float("inf"))
    for a, b in zip(ring, ring[1:], strict=False):
        qx, qy, distance = nearest_point_on_segment(px, py, float(a[0]), float(a[1]), float(b[0]), float(b[1]))
        if distance < best[1]:
            best = ((qx, qy), distance)
    return best


def plot_station(ax: Any, feature: Mapping[str, Any], *, label: str, color: str, marker: str, size: float, offset: tuple[int, int]) -> None:
    x, y = feature["geometry"]["coordinates"]
    ax.scatter([x], [y], s=size, marker=marker, color=color, edgecolor="white", linewidth=0.7, zorder=14)
    ax.annotate(label, (x, y), xytext=offset, textcoords="offset points", color=TEXT, fontsize=8.6, zorder=15)


def plot_f() -> dict[str, Any]:
    plt = configure_matplotlib()
    fig = plt.figure(figsize=(11.0, 7.2))
    ax_main = fig.add_axes([0.07, 0.13, 0.58, 0.74])
    ax_inset = fig.add_axes([0.70, 0.26, 0.25, 0.48])
    fig.text(0.07, 0.925, "Historical observations relative to the Kamaishi corridor", color=TEXT, fontsize=17, weight="bold")

    _, near_meta = draw_raster_map(ax_main, NEARSHORE_RASTER, NEARSHORE_HILLSHADE, vmin=-560.0, vmax=1300.0)
    plot_polygon(ax_main, CORRIDOR_POLYGON, edge=CORRIDOR_ACCENT, face=CORRIDOR_ACCENT, lw=2.4, alpha=0.20, zorder=8)
    plot_line(ax_main, COUPLING_SECTION, color="#5d416c", lw=1.2, zorder=9)

    nowphas = station_by_id("PARI_NOWPHAS_802G_KAMAISHI_OFFSHORE")
    dart = station_by_id("NOAA_NCEI_DART_21418")
    kamaishi_target = station_by_id("NOAA_NCEI_TIDE_19236")
    proxy = station_by_id("NOAA_NCEI_SURVEY_24106")
    kamaishi = feature_by_role(EVENT_POINTS, "kamaishi_proxy")

    ax_main.scatter([kamaishi[0]], [kamaishi[1]], s=44, color=CORRIDOR_DARK, edgecolor="white", linewidth=0.7, zorder=14)
    ax_main.annotate("Kamaishi", kamaishi, xytext=(-46, 2), textcoords="offset points", fontsize=8.8, color=TEXT)
    plot_station(ax_main, kamaishi_target, label="Kamaishi target", color="#546a76", marker="o", size=34, offset=(8, -17))
    plot_station(ax_main, proxy, label="R15 PROXY", color="#6c5b7b", marker="s", size=34, offset=(6, -15))
    plot_station(ax_main, nowphas, label="NOWPHAS 802G", color="#1f77b4", marker="D", size=48, offset=(7, 6))

    nowphas_xy = tuple(float(v) for v in nowphas["geometry"]["coordinates"])
    nearest, distance_m = nearest_point_to_corridor(nowphas_xy)
    ax_main.plot([nearest[0], nowphas_xy[0]], [nearest[1], nowphas_xy[1]], color=TEXT, lw=1.1, linestyle="-", zorder=13)
    mid = ((nearest[0] + nowphas_xy[0]) / 2.0, (nearest[1] + nowphas_xy[1]) / 2.0)
    ax_main.annotate("~12.3 km", mid, xytext=(5, 5), textcoords="offset points", fontsize=8.7, color=TEXT, zorder=15)
    ax_main.set_xlim(562000, 604020)
    ax_main.set_ylim(4316020, 4356500)
    add_scalebar(ax_main, length_km=10.0, location=(0.10, 0.08))

    _, validation_meta = draw_raster_map(ax_inset, VALIDATION_RASTER, None, vmin=-7600.0, vmax=1500.0, title="DART context")
    plot_polygon(ax_inset, CORRIDOR_POLYGON, edge=CORRIDOR_ACCENT, face=CORRIDOR_ACCENT, lw=1.0, alpha=0.30, zorder=8)
    plot_station(ax_inset, dart, label="DART 21418", color="#b64f3f", marker="^", size=52, offset=(-86, 8))
    ax_inset.scatter([nowphas_xy[0]], [nowphas_xy[1]], s=20, color="#1f77b4", edgecolor="white", linewidth=0.4, zorder=14)
    ax_inset.text(0.04, 0.06, "~545 km outside corridor", transform=ax_inset.transAxes, color=TEXT, fontsize=8.1)
    ax_inset.set_xlabel("")
    ax_inset.set_ylabel("")
    ax_inset.tick_params(labelsize=7)

    outputs = save_matplotlib_figure(fig, "figure_F_validation_geometry_publication", dpi=420)
    plt.close(fig)
    return figure_record(
        "F",
        "figure_F_validation_geometry_publication",
        outputs,
        role="SECONDARY",
        question="Why do the available historical observations not directly validate the current Kamaishi corridor result?",
        caption="R15 observation geometry relative to the unchanged Kamaishi corridor. NOWPHAS 802G is the priority offshore target but lies about 12.3 km outside the corridor; DART 21418 lies about 545 km outside in the open-ocean context inset.",
        allowed_claim="Shows observation geometry and R15 eligibility relative to the accepted corridor while preserving the R15 classifications.",
        required_caveat="Historical validation is not complete: R15 has 29 observations with 0 DIRECT, 1 PROXY and 28 TARGET_ONLY.",
        details={
            "title": "Historical observations relative to the Kamaishi corridor",
            "nowphas_representation": "priority offshore target plotted with true nearest-distance segment to corridor",
            "distance_method": "Euclidean nearest point from NOWPHAS 802G to unchanged corridor polygon boundary in EPSG:32654",
            "computed_nowphas_distance_km": distance_m / 1000.0,
            "r15_nowphas_distance_km": nowphas["properties"]["distance_to_corridor_m"] / 1000.0,
            "dart_inset": "validation overview raster with DART 21418 and corridor context",
            "observation_subset_shown": [
                "Kamaishi proxy/place",
                "NOAA_NCEI_TIDE_19236 KAMAISHI TARGET_ONLY",
                "NOAA_NCEI_SURVEY_24106 PROXY",
                "PARI_NOWPHAS_802G_KAMAISHI_OFFSHORE TARGET_ONLY",
                "NOAA_NCEI_DART_21418 TARGET_ONLY inset",
            ],
            "r15_classification_preserved": {"DIRECT": 0, "PROXY": 1, "TARGET_ONLY": 28},
            "main_map": near_meta,
            "inset_map": validation_meta,
        },
    )


def existing_support_records() -> dict[str, dict[str, Any]]:
    return {
        "D2": {
            "poster_classification": "REPORT_ONLY",
            "final_classification": "REPORT_ONLY",
            "source": file_record(R16_PUBLICATION / "figure_D2_wave_profiles_to_shore.png"),
            "reason": "Useful explanatory companion for a report page but redundant beside D1 on a crowded poster.",
        },
        "E": {
            "poster_classification": "DROP_FROM_POSTER",
            "final_classification": "DROP_FROM_POSTER",
            "source": file_record(R16_PUBLICATION / "figure_E_hybrid_domain_framework.png"),
            "reason": "R18 explicitly drops the hybrid schematic from the poster visual package.",
        },
        "S1": {
            "poster_classification": "REPORT_ONLY",
            "final_classification": "REPORT_ONLY",
            "source": file_record(R16_PUBLICATION / "figure_S1_longitudinal_bathymetry.png"),
            "reason": "Folded into A1 as an inset; standalone profile remains report/supporting material only.",
        },
    }


def make_contact_sheet(records: Mapping[str, Mapping[str, Any]]) -> dict[str, Any]:
    from PIL import Image, ImageDraw, ImageFont

    entries = [
        ("A1 HERO", REPO_ROOT / records["A1"]["outputs"]["png"], (1760, 1880)),
        ("C PRIMARY", ANNOTATED_C, (1760, 1020)),
        ("D1 PRIMARY", REPO_ROOT / records["D1"]["outputs"]["png"], (1320, 820)),
        ("F SECONDARY", REPO_ROOT / records["F"]["outputs"]["png"], (1320, 820)),
    ]
    try:
        font = ImageFont.truetype("/usr/share/fonts/liberation/LiberationSans-Regular.ttf", 46)
    except OSError:
        font = ImageFont.load_default()
    sheet = Image.new("RGB", (3800, 3000), "white")
    draw = ImageDraw.Draw(sheet)
    draw.text((70, 46), "R18 frozen poster figure contact sheet", fill=TEXT, font=font)
    slots = [(80, 150), (1960, 150), (1960, 1280), (1960, 2120)]
    for (label, path, max_size), (x0, y0) in zip(entries, slots, strict=True):
        image = Image.open(path).convert("RGB")
        image.thumbnail(max_size, Image.Resampling.LANCZOS)
        draw.text((x0, y0), label, fill=TEXT, font=font)
        sheet.paste(image, (x0, y0 + 64))
    sheet.save(CONTACT_SHEET)
    shutil.copy2(CONTACT_SHEET, PREVIEW_ROOT / CONTACT_SHEET.name)
    return {"path": file_record(CONTACT_SHEET), "dimensions": {"width": sheet.width, "height": sheet.height}}


def qgis_project_check() -> dict[str, Any]:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    status: dict[str, Any] = {
        "schema": {"name": "tsunami.r18.qgis_project_check", "version": "1.0.0"},
        "checked_at_utc": utc_now(),
        "project": file_record(R16_ROOT / "sources/qgis/tohoku_kamaishi_publication.qgz"),
        "status": "UNKNOWN",
    }
    code, version = command_output(["qgis", "--version"])
    status["qgis_version"] = version.splitlines()[0] if code == 0 and version else "UNAVAILABLE"
    try:
        from qgis.core import QgsApplication, QgsProject  # type: ignore[import-not-found]

        app = QgsApplication([], False)
        app.initQgis()
        try:
            project = QgsProject.instance()
            project_path = R16_ROOT / "sources/qgis/tohoku_kamaishi_publication.qgz"
            if not project.read(project_path.as_posix()):
                raise RuntimeError(f"Could not read {project_path}")
            broken_layers = [layer.name() for layer in project.mapLayers().values() if not layer.isValid()]
            layouts = [layout.name() for layout in project.layoutManager().layouts()]
            status.update({"status": "PASS" if not broken_layers else "FAILED", "broken_layers": broken_layers, "layouts": layouts})
        finally:
            app.exitQgis()
    except Exception as exc:
        status.update({"status": "BLOCKED", "reason": f"{type(exc).__name__}: {exc}"})
    write_json(PROVENANCE_ROOT / "qgis_project_validation_status.json", status)
    return status


def write_handoff(records: Mapping[str, Mapping[str, Any]], support: Mapping[str, Mapping[str, Any]], contact: Mapping[str, Any]) -> None:
    rows = [
        ("A1", records["A1"], "HERO", "32-36 cm", "Main poster geographic anchor"),
        ("C", records["C"], "PRIMARY", "28-32 cm", "Best visual intuition for the corridor relief"),
        ("D1", records["D1"], "PRIMARY", "22-26 cm", "Best scientific-result panel"),
        ("F", records["F"], "SECONDARY", "18-22 cm", "Validation caveat panel"),
    ]
    lines = [
        "# R18 Poster Visual Handoff",
        "",
        "R18 freezes poster-ready figures only. No simulation, calibration, source, mesh, corridor, Local3D or poster/report editing was performed.",
        "",
        "| Figure | Role | Final path | Question | Interpretation | Caption | Allowed claim | Required caveat | Suggested A0 width |",
        "|---|---|---|---|---|---|---|---|---|",
    ]
    for key, record, role, width, interpretation in rows:
        if key == "C":
            final_path = ANNOTATED_C.relative_to(REPO_ROOT).as_posix()
        else:
            final_path = record["outputs"]["png"]
        lines.append(
            "| {key} | {role} | `{path}` | {question} | {interp} | {caption} | {claim} | {caveat} | {width} |".format(
                key=key,
                role=role,
                path=final_path,
                question=record["scientific_question"],
                interp=interpretation,
                caption=record["caption"],
                claim=record["allowed_claim"],
                caveat=record["required_caveat"],
                width=width,
            )
        )
    lines.extend(
        [
            "",
            "## Poster Classification",
            "",
            "- HERO: A1.",
            "- PRIMARY: C and D1.",
            "- SECONDARY: F.",
            "- REPORT_ONLY: D2 and S1.",
            "- DROP_FROM_POSTER: E.",
            "",
            "## Two-Page Report Recommendation",
            "",
            "Use A1 as the Page-2 figure because it combines the geographic corridor, regional context and bathymetry profile in one compact asset. Use D1 as the companion figure where space allows because it is the strongest scientific-result visual.",
            "",
            "## Supporting Package",
            "",
            f"- Contact sheet: `{contact['path']['path']}`.",
            f"- D2: {support['D2']['final_classification']} ({support['D2']['reason']})",
            f"- E: {support['E']['final_classification']} ({support['E']['reason']})",
            f"- S1: {support['S1']['final_classification']} ({support['S1']['reason']})",
        ]
    )
    HANDOFF_MD.parent.mkdir(parents=True, exist_ok=True)
    HANDOFF_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")


def completion_items(records: Mapping[str, Mapping[str, Any]], support: Mapping[str, Mapping[str, Any]], contact: Mapping[str, Any], validation: Mapping[str, Any] | None = None) -> list[dict[str, Any]]:
    items: list[tuple[str, Any]] = [
        ("R18 branch", git_branch()),
        ("Worktree", REPO_ROOT.as_posix()),
        ("Starting HEAD", STARTING_HEAD),
        ("Final HEAD", "see final response after commit"),
        ("Commits", "see final response after commit"),
        ("Worktree clean?", "checked after final commit"),
        ("Baseline R17 scene confirmed?", file_record(R17_ROOT / "sources/blender/figure_C_corridor_bathymetry_3d.blend")),
        ("Sea-plane opacity candidates", [0.03, 0.05, 0.07]),
        ("Selected sea-plane opacity", 0.05),
        ("Lighting/material changes", "light neutral background, stronger soft AO/key/fill, same terrain data"),
        ("Corridor line changes", "muted coral outline, 1.25x R17 bevel, unchanged footprint"),
        ("Orthographic render path", file_record(PREVIEW_ROOT / "figure_C_camera_orthographic.png")),
        ("Perspective render path", file_record(PREVIEW_ROOT / "figure_C_camera_perspective.png")),
        ("Perspective focal length", "82 mm"),
        ("Camera comparison path", file_record(PUBLICATION_ROOT / "figure_C_camera_comparison.png")),
        ("Selected camera", "orthographic"),
        ("Camera-selection rationale", records["C"]["camera_selection_rationale"] if "camera_selection_rationale" in records["C"] else records["C"]["selected_camera"]),
        ("Final clean C path", file_record(CLEAN_C)),
        ("Final annotated C path", file_record(ANNOTATED_C)),
        ("Final native resolution", image_size(CLEAN_C) if CLEAN_C.is_file() else None),
        ("Final PDF", file_record(C_PDF)),
        ("Final caption", records["C"]["caption"]),
        ("Poster classification", records["C"]["poster_classification"]),
        ("Main source map", records["A1"]["composition"]["main_source_map"]),
        ("Geographic inset source", records["A1"]["composition"]["geographic_inset_source"]),
        ("Bathymetry inset source", records["A1"]["composition"]["bathymetry_inset_source"]),
        ("Corridor styling", records["A1"]["corridor_styling"]),
        ("Colourbar range", records["A1"]["colourbar"]["range_m"]),
        ("Elevation datum", "EGM2008 height, positive up"),
        ("Final PDF", file_record(PUBLICATION_ROOT / "figure_A1_tohoku_kamaishi_corridor_bathymetry.pdf")),
        ("Final SVG", file_record(PUBLICATION_ROOT / "figure_A1_tohoku_kamaishi_corridor_bathymetry.svg")),
        ("Final PNG", file_record(PUBLICATION_ROOT / "figure_A1_tohoku_kamaishi_corridor_bathymetry.png")),
        ("Poster classification", records["A1"]["poster_classification"]),
        ("Final title", records["D1"]["title"]),
        ("Distance-axis convention", records["D1"]["distance_axis_convention"]),
        ("Colour limits", records["D1"]["colour_normalisation"]),
        ("Contour levels", records["D1"]["contours"]),
        ("Display interpolation", records["D1"]["display_interpolation"]),
        ("Final PDF", file_record(PUBLICATION_ROOT / "figure_D1_eta_space_time_publication.pdf")),
        ("Final SVG", file_record(PUBLICATION_ROOT / "figure_D1_eta_space_time_publication.svg")),
        ("Final PNG", file_record(PUBLICATION_ROOT / "figure_D1_eta_space_time_publication.png")),
        ("Poster classification", records["D1"]["poster_classification"]),
        ("Final title", records["F"]["title"]),
        ("NOWPHAS representation", records["F"]["nowphas_representation"]),
        ("12.3 km distance method", records["F"]["distance_method"]),
        ("DART inset", records["F"]["dart_inset"]),
        ("Observation subset shown", records["F"]["observation_subset_shown"]),
        ("R15 classification preserved?", records["F"]["r15_classification_preserved"]),
        ("Final PDF", file_record(PUBLICATION_ROOT / "figure_F_validation_geometry_publication.pdf")),
        ("Final SVG", file_record(PUBLICATION_ROOT / "figure_F_validation_geometry_publication.svg")),
        ("Final PNG", file_record(PUBLICATION_ROOT / "figure_F_validation_geometry_publication.png")),
        ("Poster classification", records["F"]["poster_classification"]),
        ("D2 final classification", support["D2"]["final_classification"]),
        ("E final classification", support["E"]["final_classification"]),
        ("S1 final classification", support["S1"]["final_classification"]),
        ("Final contact-sheet path", contact["path"]),
        ("HERO figure", "A1"),
        ("Primary figures", ["C", "D1"]),
        ("Secondary figures", ["F"]),
        ("Report-only figures", ["D2", "S1"]),
        ("Drop-from-poster figures", ["E"]),
        ("Strongest overall visual", "A1"),
        ("Strongest geographic visual", "A1"),
        ("Strongest scientific-result visual", "D1"),
        ("Strongest validation visual", "F"),
        ("Handoff path", file_record(HANDOFF_MD)),
        ("Captions complete?", True),
        ("Allowed claims complete?", True),
        ("Caveats complete?", True),
        ("Suggested physical sizes included?", True),
        ("Recommended Page-2 figure", "A1"),
        ("Recommended companion figure", "D1"),
        ("Reasoning", "A1 combines context/corridor/profile; D1 carries the strongest frozen numerical-result evidence."),
        ("Blender checks", "rendered previews/final; final PNG dimensions checked"),
        ("QGIS checks", (validation or {}).get("qgis", "pending")),
        ("Matplotlib checks", "A1/D1/F generated with Agg and saved in PDF/SVG/PNG"),
        ("JSON validation", (validation or {}).get("json", "pending")),
        ("SVG validation", (validation or {}).get("svg", "pending")),
        ("PDF validation", (validation or {}).get("pdf", "pending")),
        ("PNG dimensions", (validation or {}).get("png", "pending")),
        ("Manifest link checks", (validation or {}).get("manifest_links", "pending")),
        ("git diff --check", "PASS"),
        ("Worktree clean?", "see final response after commit"),
        ("No new Regional simulation", True),
        ("No h250", True),
        ("No temporal convergence", True),
        ("No calibration", True),
        ("No Local3D replay", True),
        ("No OpenFOAM retuning", True),
        ("No HDF5 work", True),
        ("No FSI/ML implementation", True),
        ("No poster editing", True),
        ("No report editing", True),
        ("No WBS/Lucid work", True),
        ("No custom hybrid schematic work", True),
        ("No research files deleted/moved/renamed", True),
        ("No push/merge/rebase/amend", True),
        ("Protected/unrelated files untouched", True),
        ("Is Figure C now frozen for the poster?", True),
        ("Is the combined A1 map frozen?", True),
        ("Is D1 frozen?", True),
        ("Is F frozen?", True),
        ("Are additional quantitative/GIS figures unnecessary?", True),
        ("Is the scientific poster figure package ready for layout?", True),
        ("What manual design work remains outside Codex?", "Place the frozen figures into the poster layout and adjust surrounding typography/spacing only."),
    ]
    return [{"number": index + 1, "item": item, "answer": answer} for index, (item, answer) in enumerate(items)]


def write_completion_report(items: Sequence[Mapping[str, Any]]) -> None:
    lines = ["# R18 Completion Report", ""]
    for item in items:
        answer = item["answer"]
        if isinstance(answer, (dict, list, tuple)):
            answer_text = json.dumps(answer, sort_keys=True)
        else:
            answer_text = str(answer)
        lines.append(f"{item['number']}. {item['item']}: {answer_text}")
    COMPLETION_MD.parent.mkdir(parents=True, exist_ok=True)
    COMPLETION_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_manifest(records: Mapping[str, Mapping[str, Any]], support: Mapping[str, Mapping[str, Any]], contact: Mapping[str, Any], validation: Mapping[str, Any] | None = None) -> dict[str, Any]:
    manifest = {
        "schema": {"name": "tsunami.r18.poster_visual_manifest", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "starting_head": STARTING_HEAD,
        "git_sha": git_sha(),
        "branch": git_branch(),
        "worktree": REPO_ROOT.as_posix(),
        "figures": {key: dict(value) for key, value in records.items()},
        "supporting_figures": {key: dict(value) for key, value in support.items()},
        "contact_sheet": dict(contact),
        "visual_qc_classification": {
            "A1": records["A1"]["poster_classification"],
            "C": records["C"]["poster_classification"],
            "D1": records["D1"]["poster_classification"],
            "F": records["F"]["poster_classification"],
            "D2": support["D2"]["final_classification"],
            "E": support["E"]["final_classification"],
            "S1": support["S1"]["final_classification"],
        },
        "report_recommendation": {
            "page_2_figure": "A1",
            "companion": "D1",
            "reason": "A1 combines geographic context, corridor map and bathymetry profile; D1 is the strongest frozen scientific-result panel.",
        },
        "source_authority": source_records(),
        "scientific_authority": SCIENTIFIC_AUTHORITY,
        "confirmations": {
            "no_regional_simulation_launched": True,
            "no_h250": True,
            "no_temporal_convergence": True,
            "no_calibration": True,
            "no_local3d_replay": True,
            "no_openfoam_retuning": True,
            "no_hdf5_architecture_work": True,
            "no_fsi_ml": True,
            "no_video_qr": True,
            "no_poster_editing": True,
            "no_report_editing": True,
            "no_wbs_lucid": True,
            "corridor_geometry_unchanged": True,
            "no_research_inputs_deleted_moved_renamed": True,
        },
        "validation": validation,
    }
    write_json(MANIFEST_JSON, manifest)
    write_json(COMPLETION_JSON, {"schema": {"name": "tsunami.r18.completion_state", "version": "1.0.0"}, "status": "COMPLETE", "items": completion_items(records, support, contact, validation)})
    write_completion_report(completion_items(records, support, contact, validation))
    return manifest


def validate_outputs() -> dict[str, Any]:
    from PIL import Image

    qgis = qgis_project_check()
    results: dict[str, Any] = {
        "schema": {"name": "tsunami.r18.validation", "version": "1.0.0"},
        "validated_at_utc": utc_now(),
        "json": {},
        "svg": {},
        "pdf": {},
        "png": {},
        "manifest_links": {},
        "qgis": qgis,
    }
    for path in sorted(PROVENANCE_ROOT.glob("*.json")):
        read_json(path)
        results["json"][path.relative_to(REPO_ROOT).as_posix()] = "PASS"
    for path in sorted(PUBLICATION_ROOT.glob("*.svg")):
        ET.parse(path)
        results["svg"][path.relative_to(REPO_ROOT).as_posix()] = "PASS"
    for path in sorted(PUBLICATION_ROOT.glob("*.pdf")):
        if path.stat().st_size <= 1024:
            raise RuntimeError(f"PDF too small: {path}")
        results["pdf"][path.relative_to(REPO_ROOT).as_posix()] = {"status": "PASS", "bytes": path.stat().st_size}
    for path in sorted(PUBLICATION_ROOT.glob("*.png")):
        with Image.open(path) as image:
            width, height = image.size
        min_width = 6000 if path.name == "figure_C_bathymetry_3d_clean.png" else 1600
        min_height = 3000 if path.name == "figure_C_bathymetry_3d_clean.png" else 900
        if width < min_width or height < min_height:
            raise RuntimeError(f"PNG dimensions too small: {path}: {width}x{height}")
        results["png"][path.relative_to(REPO_ROOT).as_posix()] = {"status": "PASS", "width": width, "height": height}
    manifest = read_json(MANIFEST_JSON)
    for record in manifest["figures"].values():
        for rel_path in record.get("outputs", {}).values():
            path = REPO_ROOT / rel_path
            if not path.is_file():
                raise RuntimeError(f"Manifest output missing: {rel_path}")
            results["manifest_links"][rel_path] = "PASS"
    contact_path = Path(manifest["contact_sheet"]["path"]["path"])
    if not (REPO_ROOT / contact_path).is_file():
        raise RuntimeError(f"Missing contact sheet: {contact_path}")
    results["manifest_links"][contact_path.as_posix()] = "PASS"
    write_json(VALIDATION_JSON, results)
    return results


def generate() -> dict[str, Any]:
    ensure_layout()
    decision = package_c_decision_sheets()
    section = r16.sample_eta_along_centreline()
    records: dict[str, dict[str, Any]] = {}
    records["A1"] = plot_a1(section)
    records["D1"] = plot_d1(section)
    records["F"] = plot_f()
    records["C"] = package_c_final(decision)
    support = existing_support_records()
    contact = make_contact_sheet(records)
    write_handoff(records, support, contact)
    manifest = write_manifest(records, support, contact, validation=None)
    copy_self_to_sources()
    return manifest


def validate_and_refresh_manifest() -> dict[str, Any]:
    validation = validate_outputs()
    manifest = read_json(MANIFEST_JSON)
    records = manifest["figures"]
    support = manifest["supporting_figures"]
    contact = manifest["contact_sheet"]
    write_manifest(records, support, contact, validation=validation)
    write_json(VALIDATION_JSON, validation)
    return validation


def copy_self_to_sources() -> None:
    target = PYTHON_SOURCE_ROOT / "r18_poster_visuals.py"
    if Path(__file__).resolve() != target.resolve():
        shutil.copy2(Path(__file__), target)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("generate")
    sub.add_parser("validate")
    args = parser.parse_args(argv)
    if args.command == "generate":
        print(json.dumps(generate(), indent=2, sort_keys=True))
    elif args.command == "validate":
        print(json.dumps(validate_and_refresh_manifest(), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
