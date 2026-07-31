# G1 Terrain Conditioning Implementation v0.1

## WBS Mapping

This document covers `SWE-GEO-TER-WP1` and Tasks `#203`, `#204` and `#205`.

## Architecture Boundary

`tsunami_geo` owns corridor-aligned target grids, exact coverage masks, source
contracts, merge/gap policies, conditioned terrain values and canonical records.
`tsunami_geo_gdal` owns GDAL-backed source resampling, GeoTIFF output and
inspection artefacts. Public domain headers expose no GDAL, PROJ, HDF5, Gmsh,
solver or Qt types.

## Domain Flow

`prepare_terrain_conditioning` validates the case, manifest, constructed
corridor, corridor record, bathymetry source and topography source before
building the target grid and exact coverage mask. Source dataset IDs must match
the case bathymetry/topography bindings.

The target grid preserves requested spacing, uses square pixel-is-area cells,
records symmetric longitudinal/transverse padding and stores the rotated affine
derived from the corridor tangent and left normal.

Coverage is exact for the straight-edge corridor geometry by local polygon
clipping against every target cell. Outside and below-threshold cells are
explained geometric exclusions, not unresolved nodata.

## Resampling and Merge

Terrain source requests require pixel-is-area registration, matching import
records, matching raster transformation plans, accepted transformation records,
non-ballpark operations and bounded upsampling. Bilinear and area-average
kernels are explicit. Source scale/offset and supported vertical steps are
applied once.

`condition_terrain_from_resampled_sources` merges the two resampled rasters with
explicit source priority. Overlap statistics record count, signed mean, RMS,
maximum absolute difference and exceedances. The implementation never blends by
shoreline, source sign or elevation sign.

Unresolved active nodata is rejected unless bounded inverse-distance fill is
explicitly enabled. Fill uses deterministic eight-neighbour components,
interior-only components and one source lineage family.

## Records and GeoTIFFs

`TerrainConditioningRecord` records the terrain identity, scenario, source
datasets and assets, import and transformation identities, corridor identity,
target grid, policies, resampling records, diagnostics, uncertainty status,
output path and digest status.

`write_terrain_conditioning_record` is transactional and canonical.
`write_conditioned_terrain_geotiff_with_gdal` writes Float64 positive-up
pixel-is-area terrain with affine transform, CRS metadata where available, band
unit and mask. Inspection helpers write elevation, coverage and lineage TIFFs.

## Downstream Handoff

Mesh construction can consume immutable terrain values, valid masks, lineage,
target spacing, rotated affine, corridor coverage and conditioning provenance
without redefining resampling, merge or gap-resolution policy.

## Regional2D Geometry Preflight Contract

`validate_regional2d_geometry_preflight` is the Regional2D acceptance gate for
binding an imported triangular FVM mesh to one accepted corridor and one
conditioned terrain product. The API lives in `tsunami::r2d`, borrows immutable
`ConstructedCorridor`, `CorridorConstructionRecord`, `ConditionedTerrainRaster`,
`TerrainConditioningRecord` and `FiniteVolumeMesh` inputs, and optionally accepts
generic imported physical-group evidence populated from the Gmsh importer.

The preflight composes the existing corridor and terrain record validators, then
adds cross-domain checks for corridor geometry consistency, basis
orthonormality, bearing compatibility, shared computational target reference,
terrain/corridor identity, terrain grid/vector consistency, support lookup for
every mesh vertex and cell centroid, required nodata explanation, Regional2D
patch identity and canonical internal owner/neighbour ordering. It performs no
CRS conversion, terrain interpolation, bathymetry field transfer, solver
execution, GDAL access or GUI formatting.

On success the report retains only acceptance evidence: corridor and terrain
identity, mesh counts, internal/boundary face counts, patch names and face
counts, mesh bounds, terrain support bounds, minimum cell measure, minimum face
length and `accepted` status. On failure it returns the existing structured
`tsunami::core::Error` diagnostic with stable `r2d.preflight.*` codes,
`validation` category, `error` severity, operation
`validate_regional2d_geometry_preflight`, `SWE-GEO-CHK-WP1` rule context and
deterministic mesh/corridor/terrain/entity keys.

Focused verification on this branch:

```text
cmake --build --preset linux-gcc-test-build --target tsunami_tests
./build/linux-gcc-test/tests/tsunami_tests "[preflight]" --reporter compact
```

The focused GCC preflight filter passed 99 assertions across 11 test cases,
covering the valid imported triangular mesh path, corridor/terrain/CRS
mismatches, terrain support and nodata failures, required/empty/extra patches,
noncanonical internal owner/neighbour ordering, lower-level FVM degenerate-cell
rejection, deterministic diagnostic context and transactional non-mutation.

## Exclusions

This increment does not implement production terrain selection, automatic
target spacing, automatic source priority, coastline extraction, smoothing,
mesh generation, Gmsh, HDF5, XDMF, solver fields, solver initialisation, Qt,
GUI rendering, network grid download, digest computation or manifest mutation.
