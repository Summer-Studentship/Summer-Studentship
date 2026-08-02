#!/usr/bin/env python3
"""Produce a Regional2D vertical seabed-displacement artifact from a USGS finite-fault file.

The parser is dependency-light. GeoTIFF production intentionally requires optional
preprocessing dependencies and is not part of the C++ runtime, CMake configure, or
normal test path.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


ARTIFACT_CONTRACT_VERSION = 1
LOCKED_USGS_SOURCE = (
    "https://earthquake.usgs.gov/product/finite-fault/"
    "usp000hvnu/us/1539808472261/basic_inversion.param"
)


@dataclass(frozen=True)
class SubFault:
    longitude: float
    latitude: float
    depth_km: float
    slip_m: float
    rake_degrees: float
    strike_degrees: float
    dip_degrees: float
    length_km: float
    width_km: float


def _numeric_fields(line: str) -> list[float] | None:
    fields: list[float] = []
    for token in line.replace(",", " ").split():
        try:
            fields.append(float(token))
        except ValueError:
            return None
    return fields if fields else None


def parse_usgs_basic_inversion_param(path: Path) -> list[SubFault]:
    """Parse the USGS basic_inversion.param table used by the Tohoku prompt."""

    subfaults: list[SubFault] = []
    segment_length_km: float | None = None
    segment_width_km: float | None = None
    table_order: str | None = None
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = raw_line.strip()
        if stripped.startswith("#"):
            lower = stripped.lower()
            if "fault_segment" in lower:
                dx = re.search(r"\bDx=\s*([0-9.+\-Ee]+)\s*km", stripped)
                dy = re.search(r"\bDy=\s*([0-9.+\-Ee]+)\s*km", stripped)
                if dx and dy:
                    segment_length_km = float(dx.group(1))
                    segment_width_km = float(dy.group(1))
            if "lat." in lower and "lon." in lower and "slip" in lower:
                table_order = "lat_lon_depth_slip"
            elif "lon." in lower and "lat." in lower and "slip" in lower:
                table_order = "lon_lat_depth_slip"
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = _numeric_fields(line)
        if fields is None or len(fields) < 9:
            continue

        if table_order == "lat_lon_depth_slip" and len(fields) >= 11 and segment_length_km and segment_width_km:
            # Legacy USGS PARAM rows store lat/lon and rupture timing columns;
            # subfault dimensions are segment-level Dx/Dy. The slip column is
            # centimetres in this product family, while GeoClaw expects metres.
            lat, lon, depth_km, slip_cm, rake, strike, dip = fields[:7]
            slip_m = slip_cm / 100.0
            length_km = segment_length_km
            width_km = segment_width_km
        elif table_order == "lon_lat_depth_slip" and len(fields) >= 11 and segment_length_km and segment_width_km:
            lon, lat, depth_km, slip_cm, rake, strike, dip = fields[:7]
            slip_m = slip_cm / 100.0
            length_km = segment_length_km
            width_km = segment_width_km
        else:
            # Compact fixtures retain the Prompt A order with per-row dimensions.
            lon, lat, depth_km, slip_m, rake, strike, dip, length_km, width_km = fields[:9]
        values = (lon, lat, depth_km, slip_m, rake, strike, dip, length_km, width_km)
        if not all(math.isfinite(value) for value in values):
            raise ValueError(f"non-finite subfault value at line {line_number}")
        if length_km <= 0.0 or width_km <= 0.0 or depth_km < 0.0:
            raise ValueError(f"invalid subfault geometry at line {line_number}")
        subfaults.append(SubFault(lon, lat, depth_km, slip_m, rake, strike, dip, length_km, width_km))

    if not subfaults:
        raise ValueError(f"no numeric USGS subfault rows found in {path}")
    return subfaults


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _reference_crs_text(reference: dict | None) -> str | None:
    if not isinstance(reference, dict):
        return None
    authority = reference.get("authority_name")
    code = reference.get("authority_code")
    if isinstance(authority, str) and isinstance(code, str) and authority and code:
        return f"{authority}:{code}"
    projjson = reference.get("canonical_projjson")
    if isinstance(projjson, str) and projjson:
        return projjson
    wkt = reference.get("canonical_wkt2")
    if isinstance(wkt, str) and wkt:
        return wkt
    return None


def _terrain_grid(record_path: Path, coordinate_reference: str) -> dict:
    record = json.loads(record_path.read_text(encoding="utf-8"))
    grid = record["grid"]
    affine = grid["affine"]
    target = record.get("target_reference", {})
    target_crs = _reference_crs_text(target.get("horizontal"))
    if target_crs is None:
        target_crs = coordinate_reference
    return {
        "width": int(grid["width"]),
        "height": int(grid["height"]),
        "target_crs": target_crs,
        "record_target_reference": target,
        "transform": (
            float(affine["origin_x"]),
            float(affine["pixel_width"]),
            float(affine["row_rotation"]),
            float(affine["origin_y"]),
            float(affine["column_rotation"]),
            float(affine["pixel_height"]),
        ),
    }


def _grid_center_arrays(grid: dict):
    try:
        import numpy as np
        from rasterio.transform import Affine
    except ImportError as exc:
        raise SystemExit("GeoTIFF production requires numpy and rasterio.") from exc
    transform = Affine.from_gdal(*grid["transform"])
    rows, cols = np.meshgrid(np.arange(grid["height"], dtype="float64"), np.arange(grid["width"], dtype="float64"), indexing="ij")
    xs, ys = transform * (cols + 0.5, rows + 0.5)
    return np.asarray(xs, dtype="float64"), np.asarray(ys, dtype="float64")


def _working_geographic_axes(grid: dict):
    try:
        import numpy as np
        import rasterio
        from rasterio.transform import Affine
        from pyproj import CRS, Transformer
    except ImportError as exc:
        raise SystemExit(
            "GeoTIFF production requires preprocessing-only dependencies: "
            "pyproj and rasterio. They are intentionally not C++ runtime dependencies."
        ) from exc

    target_crs = CRS.from_user_input(grid["target_crs"])
    target_transform = Affine.from_gdal(*grid["transform"])
    projected_x, projected_y = _grid_center_arrays(grid)
    if target_crs.is_geographic:
        lon = projected_x
        lat = projected_y
        transformed = False
    else:
        transformer = Transformer.from_crs(target_crs, CRS.from_epsg(4326), always_xy=True)
        lon, lat = transformer.transform(projected_x, projected_y)
        lon = np.asarray(lon, dtype="float64")
        lat = np.asarray(lat, dtype="float64")
        transformed = True
    if not (np.all(np.isfinite(lon)) and np.all(np.isfinite(lat))):
        raise ValueError("target grid centres did not transform to finite WGS84 coordinates")
    lon_axis = lon[grid["height"] // 2, :]
    lat_axis = lat[:, grid["width"] // 2]
    return lon_axis, lat_axis, target_transform, rasterio, {
        "target_crs": target_crs.to_string(),
        "target_crs_wkt": target_crs.to_wkt(),
        "working_crs": "EPSG:4326",
        "projected_centres_transformed_to_wgs84": transformed,
        "working_longitude_range": [float(np.min(lon)), float(np.max(lon))],
        "working_latitude_range": [float(np.min(lat)), float(np.max(lat))],
        "working_axis_derivation": "middle-row longitudes and middle-column latitudes from transformed target cell centres",
    }


def _okada_vertical_displacement(subfaults: Iterable[SubFault], grid: dict):
    try:
        import numpy as np
        from clawpack.geoclaw import dtopotools
    except ImportError as exc:
        raise SystemExit(
            "GeoTIFF production requires preprocessing-only dependencies: "
            "clawpack==5.14.0, pyproj and rasterio. They are intentionally not C++ runtime dependencies."
        ) from exc

    xs, ys, transform, rasterio, crs_metadata = _working_geographic_axes(grid)
    fault = dtopotools.Fault()
    fault.subfaults = []
    for item in subfaults:
        subfault = dtopotools.SubFault()
        subfault.coordinate_specification = "top center"
        subfault.longitude = item.longitude
        subfault.latitude = item.latitude
        subfault.depth = item.depth_km * 1000.0
        subfault.slip = item.slip_m
        subfault.rake = item.rake_degrees
        subfault.strike = item.strike_degrees
        subfault.dip = item.dip_degrees
        subfault.length = item.length_km * 1000.0
        subfault.width = item.width_km * 1000.0
        fault.subfaults.append(subfault)

    dtopo = fault.create_dtopography(np.asarray(xs), np.asarray(ys), times=[1.0])
    displacement = np.asarray(dtopo.dZ[-1], dtype="float64")
    return displacement, transform, rasterio, crs_metadata


def write_artifact(args: argparse.Namespace) -> None:
    source = Path(args.source_param)
    terrain_record = Path(args.terrain_record)
    output_tif = Path(args.output_tif)
    output_json = Path(args.output_json)
    subfaults = parse_usgs_basic_inversion_param(source)
    grid = _terrain_grid(terrain_record, args.coordinate_reference)
    displacement, transform, rasterio, crs_metadata = _okada_vertical_displacement(subfaults, grid)

    output_tif.parent.mkdir(parents=True, exist_ok=True)
    with rasterio.open(
        output_tif,
        "w",
        driver="GTiff",
        height=grid["height"],
        width=grid["width"],
        count=1,
        dtype="float64",
        transform=transform,
        crs=grid["target_crs"],
        nodata=-1.0e300,
    ) as dataset:
        dataset.write(displacement, 1)
        dataset.set_band_description(1, "vertical_seabed_displacement")
        if hasattr(dataset, "set_band_unit"):
            dataset.set_band_unit(1, "m")
        dataset.update_tags(1, UNITTYPE="m", unit="m")

    metadata = {
        "artifact_contract_version": ARTIFACT_CONTRACT_VERSION,
        "role": "vertical_seabed_displacement",
        "event_id": args.event_id,
        "model_id": args.model_id,
        "source_format": "USGS finite-fault basic_inversion.param",
        "coordinate_reference": args.coordinate_reference,
        "target_coordinate_reference": grid["target_crs"],
        "working_coordinate_reference": "EPSG:4326",
        "crs_evaluation": crs_metadata,
        "subfault_count": len(subfaults),
        "vertical_unit": "m",
        "source_uri": LOCKED_USGS_SOURCE,
        "source_sha256": _sha256(source),
        "generated_at_utc": args.generated_at_utc,
        "producer": "tools/earthquake/tohoku_usgs_finite_fault.py",
        "parser_sample": [asdict(item) for item in subfaults[:3]],
    }
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-param", required=True)
    parser.add_argument("--terrain-record", required=True)
    parser.add_argument("--output-tif", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--event-id", required=True)
    parser.add_argument("--model-id", default="usgs-usp000hvnu-basic-inversion")
    parser.add_argument("--coordinate-reference", required=True)
    parser.add_argument("--generated-at-utc", required=True)
    args = parser.parse_args()
    write_artifact(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
