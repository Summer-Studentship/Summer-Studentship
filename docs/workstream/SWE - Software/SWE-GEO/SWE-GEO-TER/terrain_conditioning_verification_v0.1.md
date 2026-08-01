# Terrain Conditioning Verification v0.1

## Scope

Branch: `feat/g1-terrain-conditioning`.

This document records G1 terrain-conditioning verification evidence for branch
`feat/g1-terrain-conditioning`.

## Previous Baseline

- Ordinary GCC: 110/110.
- Ordinary Clang: 110/110.
- Geospatial GCC: 117/117.
- Geospatial Clang: 117/117.
- CRS GCC: 123/123.
- CRS Clang: 123/123.

## Verification Matrix

Full CTest presets completed on 2026-07-31:

- Ordinary GCC: `ctest --preset linux-gcc-test-run`, 115/115.
- Ordinary Clang: `ctest --preset linux-clang-test-run`, 115/115.
- Geospatial GCC: `ctest --preset linux-gcc-geospatial-test-run`, 123/123.
- Geospatial Clang: `ctest --preset linux-clang-geospatial-test-run`,
  123/123.
- CRS GCC: `ctest --preset linux-gcc-crs-test-run`, 129/129.
- CRS Clang: `ctest --preset linux-clang-crs-test-run`, 129/129.

Focused terrain checks:

- Ordinary GCC: `./build/linux-gcc-test/tests/tsunami_tests "*terrain*"
  --reporter compact`, 154 assertions in 5 test cases.
- Geospatial GCC: `./build/linux-gcc-geospatial-test/tests/tsunami_tests
  "*terrain*" --reporter compact`, 176 assertions in 6 test cases.

Toolchain evidence:

- CMake: 4.4.0.
- GCC: 16.1.1 20260625.
- Clang: 22.1.8.
- GDAL: 3.12.4 from vcpkg package metadata and `gdal_version.h`.
- PROJ: 9.8.1 from CMake package metadata.

## Branch Commit Evidence

- `7497d4d`: corridor-aligned terrain target grid and theory.
- `4fe605f`: terrain resampling, merge/gap conditioning and GDAL adapter.
- `290d635`: terrain schema, fixtures and lineage tests.
- This document is updated by the final documentation/architecture commit.

## Focused Controls

Focused tests cover:

- corridor-aligned target-grid orientation;
- square pixel-is-area cells and requested spacing preservation;
- symmetric grid padding;
- rotated affine coefficients;
- cell-centre coordinates;
- exact corridor coverage fractions and threshold classification;
- source provenance and registration controls;
- bilinear and area-average kernel policy;
- accepted operation reuse and missing-grid rejection;
- source scale/offset and vertical-step handling;
- explicit source-priority merge;
- overlap mean, RMS and maximum disagreement diagnostics;
- reject-by-default nodata;
- bounded inverse-distance fill;
- same-lineage donor rules;
- complete per-cell lineage;
- immutable positive-up conditioned terrain;
- canonical record byte stability;
- transactional record writing;
- transactional GeoTIFF and inspection artefact writing;
- strict conditioned-terrain artefact bundle read-back into Regional2D preflight
  and conservative terrain transfer.

The focused `[terrain-artifact-readback]` integration tests cover the
producer-to-consumer handoff: `condition_terrain_with_gdal`, record validation,
record write/read, canonical bundle-path derivation, transactional bundle write,
strict GDAL read-back, field-for-field terrain comparison, Regional2D geometry
preflight, conservative raster-cell stencil construction and mesh-bound
`RegionalBathymetry` transfer. Negative cases reject stale role/revision and
lineage-version metadata, duplicate/missing/unsafe paths, out-of-range coverage
and unknown lineage codes.

## Illustrative Fixture

The illustrative terrain fixture is deterministic and non-authoritative. It is
not production Kamaishi or Sendai terrain and is not suitable for scientific
validation.

Fixture geometry:

- target spacing: `10 m`;
- output dimensions: `4 x 2`;
- longitudinal padding: `0 m`;
- transverse padding: `0 m`;
- affine: `[-10, 10, 0, 10, 0, -10]`;
- coverage threshold: `0.5`;
- active/outside/excluded cells: `8 / 0 / 0`.

Fixture sources:

- bathymetry dataset/asset: `bathymetry-primary` / `bathymetry-asset`;
- topography dataset/asset: `topography-primary` / `topography-asset`;
- source registration: `pixel_is_area`;
- source scale/offset: absent, interpreted as scale `1`, offset `0`;
- resampling kernels: bilinear for both sources;
- maximum upsampling factor: `4`;
- coordinate epochs: `2026.0` synthetic metric fixture references;
- grid resources: none required for identity fixture operation;
- vertical steps: no vertical change in the main fixture.

Fixture merge result:

- merge priority: `bathymetry-primary` first;
- overlap count: `2`;
- overlap conflict result: no rejected conflict;
- initially unresolved cells: `0`;
- filled cells: `0`;
- final unresolved cells: `0`;
- minimum/maximum elevation: `-6 / 5 m`;
- uncertainty status: `not_reported`.

Runtime inspection artefacts are generated in a temporary verification
directory by the GDAL test:

- `illustrative_conditioned_terrain.tif`: 1441 bytes.
- `illustrative_corridor_coverage.tif`: 1439 bytes.
- `illustrative_cell_lineage.tif`: 1035 bytes.

No standalone `gdalinfo` executable was present on `PATH` or in the vcpkg tool
tree during verification; GeoTIFF reopening is therefore covered by the GDAL
adapter's transactional reopen check and the geospatial test assertions.

GeoTIFF creation options:

- `TILED=YES`;
- `COMPRESS=DEFLATE`;
- `PREDICTOR=3` for Float64 and `PREDICTOR=2` for lineage UInt16;
- `BIGTIFF=IF_SAFER`.

## Architecture Evidence

Expected validators:

- `python3 -m json.tool
  schemas/terrain_conditioning_record/1.0.0/terrain_conditioning_record.schema.json`.
- `find tests/fixtures/geospatial/terrain -name '*.json' -print -exec
  python3 -m json.tool {} \;`.
- `python3 tools/architecture/validate_target_graph.py --policy
  architecture/target_dependency_policy_v0.1.json --cmake
  src/CMakeLists.txt`.
- `python3 tools/architecture/validate_layer_ownership.py --layers
  architecture/layer_ownership_policy_v0.1.json --targets
  architecture/target_dependency_policy_v0.1.json`.
- `python3 tools/architecture/validate_interface_contracts.py --interfaces
  architecture/interface_contract_policy_v0.1.json --targets
  architecture/target_dependency_policy_v0.1.json --layers
  architecture/layer_ownership_policy_v0.1.json --cases
  architecture/case_lifecycle_policy_v0.1.json`.
- `python3 tools/architecture/validate_case_lifecycle.py --policy
  architecture/case_lifecycle_policy_v0.1.json --targets
  architecture/target_dependency_policy_v0.1.json --layers
  architecture/layer_ownership_policy_v0.1.json`.
- `python3 tools/architecture/validate_application_service.py --services
  architecture/application_service_policy_v0.1.json --interfaces
  architecture/interface_contract_policy_v0.1.json --cases
  architecture/case_lifecycle_policy_v0.1.json --targets
  architecture/target_dependency_policy_v0.1.json --layers
  architecture/layer_ownership_policy_v0.1.json`.
- `python3 tools/architecture/validate_diagnostics_failure.py --diagnostics
  architecture/diagnostic_failure_policy_v0.1.json --services
  architecture/application_service_policy_v0.1.json --interfaces
  architecture/interface_contract_policy_v0.1.json --cases
  architecture/case_lifecycle_policy_v0.1.json --targets
  architecture/target_dependency_policy_v0.1.json --layers
  architecture/layer_ownership_policy_v0.1.json`.
- `cmake --graphviz=build/linux-gcc-test/target-graph.dot
  build/linux-gcc-test` followed by target graph validation.
- `cmake --graphviz=build/linux-gcc-geospatial-test/target-graph.dot
  build/linux-gcc-geospatial-test` followed by target graph validation.

Expected target state:

- `tsunami_geo`: direct project dependencies remain `tsunami_core` and
  `tsunami_data` only.
- `tsunami_geo_gdal`: direct project dependency remains `tsunami_geo`; direct
  external dependency remains `GDAL::GDAL` only.
- `tsunami_geo_proj`: unchanged.

## Environment Safeguards

The research stash `wip: preserve unrelated research HTML deletions before SWE-ARC-SVC-WP1`
must remain untouched. The unrelated untracked `docs/Latex/Proposed Model/`
directory must remain untracked and unstaged.

## WBS Evidence

Issue tree inspected before implementation:

- `#42` `SWE-GEO-TER`: authorised Work Package identified as `#202`.
- `#202` `SWE-GEO-TER-WP1`: authorised child Tasks identified as `#203`,
  `#204` and `#205`.
- `#203` `SWE-GEO-TER-WP1-T1`: corridor-aligned clipping.
- `#204` `SWE-GEO-TER-WP1-T2`: nodata, merging and resampling policies.
- `#205` `SWE-GEO-TER-WP1-T3`: conditioned terrain fixture and lineage.
- `#206` `SWE-GEO-MSH-WP1`: downstream mesh work, not closed here.

## Limitations

The slice conditions illustrative raster inputs. It does not choose production
Kamaishi or Sendai terrain, infer coastlines, smooth terrain, construct meshes,
tag boundaries, write HDF5/XDMF, prepare solvers or add GUI visualisation.
