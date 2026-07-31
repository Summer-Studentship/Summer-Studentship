# G1 Corridor Construction Implementation v0.1

## WBS Mapping

This document covers `SWE-GEO-COR-WP1` and tasks `#199`, `#200` and `#201`.

## Architecture Boundary

The implementation lives in `tsunami_geo` and depends only on `tsunami_core` and `tsunami_data`. Public headers are:

- `ConstructedCorridor.hpp`
- `CorridorConstruction.hpp`
- `CorridorConstructionRecord.hpp`
- `CorridorConstructionSerialisation.hpp`

No GDAL, PROJ, HDF5, Gmsh, solver or Qt type crosses the public boundary.

## Case-Field Interpretation

`origin` is the transformed epicentre. `bearing_degrees_clockwise_from_north` is a consistency constraint. `offshore_extent_m` is `L_pre`, `inland_extent_m` is distance after the target, `width_m` is `W`, and `narrowing.inland_width_m` is `W_i`.

## Reference Inputs and Provenance

The API accepts borrowed transformed epicentre and target references for one operation and copies all evidence into the result record. It validates point set presence, coordinate index, coordinate finiteness, point-set/record source and target references, transformation identity case/manifest revisions, point IDs, document titles, absolute credential-free URIs and canonical UTC timestamps.

## Geometry

The local basis is derived from the two transformed evidence points. Local/global conversion uses dot products against the tangent and left normal. The configured origin residual is measured from case origin to transformed epicentre. Bearing residual uses circular comparison across the `0/360` boundary.

Constant-width construction creates the canonical ring `a_left, a_right, b_right, b_left, a_left`. Narrowing uses a linear epicentre-to-target taper and removes only tolerance-equivalent consecutive duplicates. Flat ends remain perpendicular to the evidence-derived tangent.

## Sponge Semantics

Sponge limits are recorded as internal numerical regions: offshore `xi` interval, side width and remaining unsponge width. They do not alter physical polygon coordinates, area, perimeter or extent.

## Analytic Checks

The constructor validates signed area, perimeter, closure, edge length, self-intersection, flat caps, extent, and scale-aware analytic agreement. Expected failures use `Result<T>` and structured `Error` values with `state_changed=false`.

## Canonical Record

`CorridorConstructionRecord` serialisation is UTF-8 JSON with two-space indentation, fixed field order, deterministic warnings and field paths, explicit nullable evidence fields, LF endings and one terminal newline. `write_corridor_construction_record` validates first, serialises in memory, writes a sibling temporary file and renames it into place.

## Fixtures

Fixtures under `tests/fixtures/geospatial/corridors` include axis-aligned, rotated, narrowing, Kamaishi and Sendai valid cases plus invalid controls. Kamaishi and Sendai coordinates are synthetic, illustrative, non-authoritative and not suitable for scientific validation.

## Downstream Handoff

Terrain conditioning can consume immutable corridor polygons, target references, extents, local frames, width functions, sponge limits and provenance records without redefining corridor geometry.

## Exclusions

This increment does not select production epicentres or target sites, infer run-up, define production dimensions, clip raster/vector data, resample terrain, generate damping coefficients, mesh, tag boundaries, write HDF5/XDMF, prepare solvers or add GUI surfaces.
