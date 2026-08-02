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
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


ARTIFACT_CONTRACT_VERSION = 1
LOCKED_USGS_SOURCE = (
    "https://earthquake.usgs.gov/archive/product/finite-fault/"
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
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = _numeric_fields(line)
        if fields is None or len(fields) < 9:
            continue

        # USGS finite-fault param tables are numeric rows. For this locked source
        # the required fields are lon, lat, depth, slip, rake, strike, dip, length, width.
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


def _terrain_grid(record_path: Path) -> dict:
    record = json.loads(record_path.read_text(encoding="utf-8"))
    grid = record["grid"]
    affine = grid["affine"]
    return {
        "width": int(grid["width"]),
        "height": int(grid["height"]),
        "transform": (
            float(affine["origin_x"]),
            float(affine["pixel_width"]),
            float(affine["row_rotation"]),
            float(affine["origin_y"]),
            float(affine["column_rotation"]),
            float(affine["pixel_height"]),
        ),
    }


def _grid_centres(grid: dict) -> tuple[list[float], list[float]]:
    width = grid["width"]
    height = grid["height"]
    x0, dx, rx, y0, cx, dy = grid["transform"]
    xs = [x0 + (col + 0.5) * dx + 0.5 * rx for col in range(width)]
    ys = [y0 + 0.5 * cx + (row + 0.5) * dy for row in range(height)]
    return xs, ys


def _okada_vertical_displacement(subfaults: Iterable[SubFault], grid: dict):
    try:
        import numpy as np
        import rasterio
        from rasterio.transform import Affine
        from clawpack.geoclaw import dtopotools
    except ImportError as exc:
        raise SystemExit(
            "GeoTIFF production requires preprocessing-only dependencies: "
            "clawpack==5.14.0 and rasterio. They are intentionally not C++ runtime dependencies."
        ) from exc

    xs, ys = _grid_centres(grid)
    fault = dtopotools.Fault()
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
    transform = Affine.from_gdal(*grid["transform"])
    return displacement, transform, rasterio


def write_artifact(args: argparse.Namespace) -> None:
    source = Path(args.source_param)
    terrain_record = Path(args.terrain_record)
    output_tif = Path(args.output_tif)
    output_json = Path(args.output_json)
    subfaults = parse_usgs_basic_inversion_param(source)
    grid = _terrain_grid(terrain_record)
    displacement, transform, rasterio = _okada_vertical_displacement(subfaults, grid)

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
