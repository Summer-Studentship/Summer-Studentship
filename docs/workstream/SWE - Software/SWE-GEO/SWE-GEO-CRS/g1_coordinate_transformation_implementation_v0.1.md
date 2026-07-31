# G1 Coordinate Transformation Implementation

**Document ID:** `SWE-GEO-CRS-IMPL-v0.1`
**Work Package:** `SWE-GEO-CRS-WP1`
**Tasks:** `SWE-GEO-CRS-WP1-T1`, `SWE-GEO-CRS-WP1-T2`, `SWE-GEO-CRS-WP1-T3`
**Status:** Implemented G1 vertical slice

## Scope

This increment implements the method-neutral coordinate transformation model in `tsunami_geo` and the optional PROJ-backed adapter in `tsunami_geo_proj`. It does not implement raster resampling, terrain interpolation, terrain merging, corridor construction, mesh generation, HDF5, GUI controls or solver preparation.

## Domain Contracts

`tsunami_geo` owns the stable project-facing contracts:

- source, target and operation descriptors;
- production selection policy with no network, no ballpark and only-best requirements;
- explicit area-of-interest and area-of-use validation;
- geodetic resource evidence and verification status;
- supported vertical identity, unit conversion, sign convention, authoritative offset and geodetic-grid step records;
- transformed point/vector value objects;
- raster transformation plans without resampling;
- canonical coordinate-transformation records and transactional record writing.

The public `tsunami_geo` headers remain free of PROJ, GDAL, Qt, HDF5, Gmsh, Catch2 and JSON-library types.

## PROJ Adapter

`tsunami_geo_proj` is enabled only when `TSUNAMI_ENABLE_CRS_TRANSFORMATIONS=ON`. The adapter:

- creates a private PROJ context with network access disabled;
- discovers source-to-target operations from source import evidence and the case-configured target CRS;
- requests `ALLOW_BALLPARK=NO` and `ONLY_BEST=YES`;
- rejects operations outside the requested area of interest when coverage is required;
- rejects unavailable or unverified grid-resource dependencies;
- normalises execution axes into project storage order;
- transforms point sets and vector features;
- produces raster transformation plans by densifying source raster boundaries, without resampling cells;
- records CRS, datum, epoch, operation, resource, runtime and diagnostic provenance.

## Canonical Records

Coordinate transformation records are written under:

```text
cases/<case_id>/data/processed/transformations/<transformation_id>/<revision>/coordinate_transformation_record.json
```

Record serialisation is deterministic: object fields use a stable order, resource and warning collections are sorted where ordering is not semantically meaningful, and writes are transactional through a temporary file plus atomic rename.

