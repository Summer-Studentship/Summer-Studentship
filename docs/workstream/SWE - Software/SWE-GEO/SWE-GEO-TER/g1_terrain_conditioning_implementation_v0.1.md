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

## Exclusions

This increment does not implement production terrain selection, automatic
target spacing, automatic source priority, coastline extraction, smoothing,
mesh generation, Gmsh, HDF5, XDMF, solver fields, solver initialisation, Qt,
GUI rendering, network grid download, digest computation or manifest mutation.
