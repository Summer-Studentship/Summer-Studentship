# Corridor Construction Verification v0.1

## Scope

Branch: `feat/g1-evidence-driven-corridors`.

This document records the G1 corridor-construction verification evidence captured on
31 July 2026 before opening the work-package pull request.

Pre-merge branch commits:

- `11f9b5aadce63960011b1cb5385ff693f81b190c`:
  `feat(geo): define corridor reference and basis contracts [SWE-GEO-COR-WP1-T1]`
- `5b88d6fe6bc9030dd3c8b6d4b89fad3ec7791a3c`:
  `feat(geo): implement flat-ended corridor construction [SWE-GEO-COR-WP1-T2]`
- `388f1b951ea9a122709c037666572fe7a818c6ff`:
  `test(geo): add Kamaishi and Sendai corridor fixtures [SWE-GEO-COR-WP1-T3]`
- The documentation/policy verification commit is the commit that contains this
  document.

Toolchain:

- CMake: 4.4.0.
- GCC: 16.1.1 20260625.
- Clang: 22.1.8.

## Previous Baseline

- Ordinary: 105 tests.
- Geospatial: 112 tests.
- CRS-enabled: 118 tests.
- Regional2D benchmark cases: 15.

## Focused Evidence

Focused corridor tests cover:

- eastward, northward, southwest and arbitrary rotated centreline bases;
- tangent norm, normal norm, orthogonality and right-handed determinant;
- local/global coordinate round trip;
- clockwise-from-north bearing and circular bearing residual;
- valid transformed-point linkage and transformation identity retention;
- case revision, manifest revision, coordinate index, point definition and document URI checks;
- exact origin match, origin mismatch, exact bearing match and bearing mismatch;
- evidence-derived basis usage;
- constant-width canonical vertex order, closure, orientation, extent, area, perimeter and rotated rectangle;
- narrowing width before epicentre, linear taper to target, inland width, duplicate removal, area and perimeter;
- zero, offshore and side sponge semantics;
- polygon validation controls;
- canonical record schema identity, formula version, byte stability, explicit nullable fields, deterministic warnings and LF ending;
- transactional record writing and target preservation on invalid record write;
- public-header checks excluding GDAL, PROJ, HDF5, Gmsh, Qt and JSON-library public types.

## Illustrative Fixture Results

Kamaishi fixture:

- Expected/actual basis: `t=(0.6,0.8)`, `n=(-0.8,0.6)`.
- Expected/actual bearing: `36.86989764584402`.
- Expected/actual vertices:
  `(500,1750)`, `(900,1450)`, `(4500,6250)`, `(4100,6550)`,
  `(500,1750)`.
- Expected/actual area: `3000000`.
- Expected/actual perimeter: `13000`.
- Vertex count: `5`.

Sendai fixture:

- Expected/actual basis: `t=(1,0)`, `n=(0,1)`.
- Expected/actual bearing: `90`.
- Expected/actual vertices:
  `(-2500,1150)`, `(-2500,850)`, `(-2000,850)`, `(2000,940)`,
  `(2500,940)`, `(2500,1060)`, `(2000,1060)`, `(-2000,1150)`,
  `(-2500,1150)`.
- Expected/actual area: `1050000`.
- Expected/actual perimeter: `10422.02458730867`.
- Vertex count: `9`.

Both fixture families are illustrative, non-authoritative and not suitable for scientific validation.

## Architecture Evidence

Validator commands completed successfully:

- `python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json --cmake src/CMakeLists.txt`
- `python3 tools/architecture/validate_layer_ownership.py --policy architecture/layer_ownership_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json`
- `python3 tools/architecture/validate_interface_contracts.py --interfaces architecture/interface_contract_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json --cases architecture/case_lifecycle_policy_v0.1.json`
- `python3 tools/architecture/validate_application_service.py --services architecture/application_service_policy_v0.1.json --interfaces architecture/interface_contract_policy_v0.1.json --cases architecture/case_lifecycle_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json`
- `python3 tools/architecture/validate_diagnostics_failure.py --diagnostics architecture/diagnostic_failure_policy_v0.1.json --services architecture/application_service_policy_v0.1.json --interfaces architecture/interface_contract_policy_v0.1.json --cases architecture/case_lifecycle_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json`
- `python3 tools/architecture/validate_case_lifecycle.py --policy architecture/case_lifecycle_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json`
- `cmake --graphviz=build/linux-gcc-test/target-graph.dot build/linux-gcc-test`
- `python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json --cmake-graph build/linux-gcc-test/target-graph.dot`

Expected architecture state:

- `tsunami_geo` depends on `tsunami_core` and `tsunami_data` only.
- `tsunami_geo_gdal` remains unchanged.
- `tsunami_geo_proj` remains unchanged.

## Test Matrix

All required verification commands completed successfully after adding the corridor
slice. No compiler warnings were emitted in the recorded GCC or Clang builds.

- JSON schema syntax: passed.
- Corridor fixture JSON syntax: passed.
- Corridor and case fixture JSON syntax: passed.
- Focused corridor/case CTest filter on GCC ordinary build: passed.
- Focused corridor Catch2 filter: 5 test cases and 98 assertions passed.
- Ordinary GCC configure, build and CTest: 110/110 passed.
- Ordinary Clang configure, build and CTest: 110/110 passed.
- Geospatial GCC configure, build and CTest: 117/117 passed.
- Geospatial Clang configure, build and CTest: 117/117 passed.
- CRS-enabled GCC configure, build and CTest: 123/123 passed.
- CRS-enabled Clang configure, build and CTest: 123/123 passed.

The test-count increases from the accepted baseline are:

- Ordinary: 105 to 110.
- Geospatial: 112 to 117.
- CRS-enabled: 118 to 123.

## Environment Safeguards

The research stash `wip: preserve unrelated research HTML deletions before SWE-ARC-SVC-WP1` must remain untouched. The unrelated untracked `docs/Latex/Proposed Model/` directory must remain untracked and unstaged.

## WBS Evidence

Issue tree inspected before implementation and closure:

- `#41` `SWE-GEO-COR`: authorised Work Package identified as `#198`.
- `#198` `SWE-GEO-COR-WP1`: authorised child Tasks identified as `#199`,
  `#200` and `#201`.
- `#199` `SWE-GEO-COR-WP1-T1`: epicentre/target centreline and local basis.
- `#200` `SWE-GEO-COR-WP1-T2`: configured lengths, widths, narrowing and
  flat-ended buffering.
- `#201` `SWE-GEO-COR-WP1-T3`: illustrative Kamaishi and Sendai corridor
  polygons and provenance.
- `#202` `SWE-GEO-TER-WP1`: downstream terrain conditioning, not closed here.
- `#218` `SWE-GEO-CHK-WP1`: downstream corridor validation, not closed here.

## Limitations

The slice constructs corridors from already transformed evidence. It does not perform CRS transformation, source-data selection, production Tohoku geometry definition, terrain conditioning, clipping, meshing, HDF5/XDMF writing, solver preparation or GUI integration.
