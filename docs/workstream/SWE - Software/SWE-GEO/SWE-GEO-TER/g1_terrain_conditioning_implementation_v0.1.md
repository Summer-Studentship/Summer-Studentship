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

`write_terrain_conditioning_record` is transactional and canonical. The
conditioned terrain GeoTIFF handoff is a three-artefact bundle derived from the
record-owned primary output path: `<stem>.tif`, `<stem>.coverage.tif` and
`<stem>.lineage.tif` under `outputs/terrain/`. The record remains schema v2; no
companion paths are added to it.

`tsunami_geo_gdal` writes and strictly reads the bundle under
`TSUNAMI_ARTIFACT_CONTRACT_VERSION=1`. Every artefact carries role, terrain
identity/revision, case revision, manifest revision, output dataset/process,
record schema, formula version and vertical datum/unit/positive metadata. The
lineage artefact also carries
`TSUNAMI_LINEAGE_ENCODING_VERSION=terrain-cell-lineage-code-v1`.

The band contract is:

- bed elevation: one `Float64` band, description `bed_elevation`, unit `m`,
  pixel-is-area and the exact terrain validity mask;
- corridor coverage: one `Float64` band, description
  `corridor_coverage_fraction`, dimensionless unit `1`, finite values in
  `[0, 1]` and no nodata/scale/offset conversion;
- cell lineage: one `UInt16` band, description `cell_lineage_code`,
  dimensionless unit `1`, exact domain lineage codes and no floating-point
  decoding.

Read-back validates the GTiff driver, dimensions, full six-coefficient affine,
affine-derived extent, pixel-is-area registration, semantic horizontal CRS
equality through GDAL/OGR and record-authored vertical metadata. It reconstructs
only through `make_conditioned_terrain_raster` and rejects missing evidence,
unknown lineage codes, stale metadata, swapped roles, unsafe paths, unexpected
scale/offset/nodata and mask/coverage/lineage contradictions.

The bundle writer rejects malformed in-memory terrain storage before creating
any output directory, transaction directory, temporary file, target or sidecar.
All four raster arrays must match the grid cell count exactly before semantic
validation indexes them.

The bundle writer writes all three sibling temporary GeoTIFFs first, then uses
the strict reader to validate the temporary bundle and compare the terrain
field-for-field before replacing targets. Because three independent filesystem
renames cannot be atomic, the operation provides all-or-preserve semantics with
transaction-owned unique backups and rollback failure reporting rather than
claiming atomicity.

The replacement commit point is successful strict read-back validation of the
final installed target bundle while the backups still exist. Failures before
that point roll back to the previous target and sidecar state, reporting
`rollback_failed` with `state_changed=true` when restoration is incomplete.
After that point the new bundle is committed. If transaction-owned backup
cleanup then fails, the writer returns
`geo.terrain.artifact_write.cleanup_failed` with `state_changed=true` and
recovery-directory context; it keeps the valid installed bundle and retained
backup data rather than attempting a destructive post-commit rollback.

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

The focused GCC preflight filter passed 180 assertions across 13 test cases,
covering the valid imported triangular mesh path, corridor/terrain/CRS
mismatches, terrain support and nodata failures, required/empty/extra patches,
noncanonical internal owner/neighbour ordering, lower-level FVM degenerate-cell
rejection, deterministic diagnostic context and transactional non-mutation.

## Exclusions

This increment does not implement production terrain selection, automatic
target spacing, automatic source priority, coastline extraction, smoothing,
mesh generation, Gmsh, HDF5, XDMF, solver fields, solver initialisation, Qt,
GUI rendering, network grid download, digest computation or manifest mutation.
