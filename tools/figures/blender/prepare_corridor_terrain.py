#!/usr/bin/env python3
"""Prepare the R17 Blender terrain crop from the authoritative R16 GIS lineage."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Sequence

from osgeo import gdal


REPO_ROOT = Path(__file__).resolve().parents[3]
R16_MANIFEST = REPO_ROOT / "deliverables/figures/r16_publication/provenance/publication_figure_manifest.json"
R16_CORRIDOR = REPO_ROOT / "deliverables/figures/r16_publication/sources/qgis/layers/corridor_polygon.geojson"
R17_ROOT = REPO_ROOT / "deliverables/figures/r17_closure"
TERRAIN_ROOT = R17_ROOT / "sources/blender/terrain"
TERRAIN_RASTER = TERRAIN_ROOT / "etopo_corridor_blender_utm54_200m.tif"
TERRAIN_MANIFEST = TERRAIN_ROOT / "terrain_manifest.json"


def utc_now() -> str:
    return datetime.now(UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(path: Path) -> dict[str, Any]:
    record: dict[str, Any] = {"path": path.as_posix(), "exists": path.exists()}
    if path.is_file():
        record.update({"bytes": path.stat().st_size, "sha256": sha256(path)})
    return record


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def run(command: Sequence[str]) -> str:
    completed = subprocess.run(
        list(command),
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(f"{' '.join(command)} failed:\n{completed.stdout}")
    return completed.stdout.strip()


def raster_summary(path: Path) -> dict[str, Any]:
    ds = gdal.Open(path.as_posix())
    if ds is None:
        raise RuntimeError(f"Could not open raster: {path}")
    band = ds.GetRasterBand(1)
    stats = band.GetStatistics(True, True)
    gt = ds.GetGeoTransform()
    return {
        "path": file_record(path),
        "width": ds.RasterXSize,
        "height": ds.RasterYSize,
        "pixel_size": {"x_m": gt[1], "y_m": gt[5]},
        "origin": {"x_m": gt[0], "y_m": gt[3]},
        "extent": {
            "xmin_m": gt[0],
            "xmax_m": gt[0] + ds.RasterXSize * gt[1],
            "ymax_m": gt[3],
            "ymin_m": gt[3] + ds.RasterYSize * gt[5],
        },
        "band_1": {
            "nodata": band.GetNoDataValue(),
            "minimum_m": stats[0],
            "maximum_m": stats[1],
            "mean_m": stats[2],
            "stddev_m": stats[3],
            "unit": band.GetUnitType() or "m",
        },
        "projection_wkt_head": (ds.GetProjection() or "").splitlines()[:12],
    }


def prepare(*, resolution_m: float) -> dict[str, Any]:
    manifest = read_json(R16_MANIFEST)
    source = Path(manifest["derived_terrain"]["source"]["path"])
    r16_crop = manifest["derived_terrain"]["crops"]["corridor"]
    xmin, ymin, xmax, ymax = [str(value) for value in r16_crop["extent"]]
    TERRAIN_ROOT.mkdir(parents=True, exist_ok=True)
    run(
        [
            "gdalwarp",
            "-overwrite",
            "-of",
            "GTiff",
            "-t_srs",
            "EPSG:32654",
            "-te",
            xmin,
            ymin,
            xmax,
            ymax,
            "-tr",
            str(resolution_m),
            str(resolution_m),
            "-r",
            "bilinear",
            "-dstnodata",
            "-99999",
            "-ot",
            "Float32",
            "-co",
            "COMPRESS=DEFLATE",
            source.as_posix(),
            TERRAIN_RASTER.as_posix(),
        ]
    )
    payload = {
        "schema": {"name": "tsunami.r17.blender_terrain", "version": "1.0.0"},
        "status": "COMPLETE",
        "generated_at_utc": utc_now(),
        "source_authority": {
            "r16_publication_manifest": file_record(R16_MANIFEST),
            "bathymetry_topography_source": file_record(source),
            "r16_corridor_crop": r16_crop,
            "corridor_geometry": file_record(R16_CORRIDOR),
        },
        "output": raster_summary(TERRAIN_RASTER),
        "crs": "EPSG:32654 WGS 84 / UTM zone 54N",
        "vertical_reference": "ETOPO 2022 EGM2008 height, positive up; z = 0 is EGM2008 sea-level reference used for rendering.",
        "visual_interpolation_note": (
            "The render raster is a 200 m EPSG:32654 bilinear resampling of the authoritative ETOPO 2022 "
            "source used by R16. This increases render mesh density only; it does not create new bathymetric authority."
        ),
        "gdal_version": gdal.VersionInfo("--version"),
    }
    write_json(TERRAIN_MANIFEST, payload)
    return payload


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resolution-m", type=float, default=200.0)
    args = parser.parse_args(argv)
    print(json.dumps(prepare(resolution_m=args.resolution_m), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
