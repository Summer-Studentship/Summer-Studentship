# Geospatial Import Verification v0.1

Branch: `feat/g1-geospatial-import`

Committed WBS outputs:

| Commit | Scope |
| --- | --- |
| `f1885d3` | `tsunami_geo` contracts, datum evidence, validation and import-record schema |
| `55d4958` | optional `tsunami_geo_gdal` adapter, feature gate and GDAL reader implementation |
| `ab49914` | generated GeoTIFF/GeoPackage tests and synthetic datum fixtures |
| docs/policy commit | CMake presets, architecture policy, datum register and verification record |

The G1 verification slice is exercised only when `TSUNAMI_ENABLE_GEOSPATIAL=ON`. The ordinary test preset continues to build the method-neutral target without requiring GDAL.

Environment:

| Item | Value |
| --- | --- |
| CMake | 4.4.0 |
| Linked GDAL target | `GDAL::GDAL` through `tsunami_geo_gdal` |
| vcpkg features | `tests;geospatial`; GDAL has the minimal `sqlite3` feature so the required `GPKG` driver is available |
| Selected drivers | `GTiff` and `GPKG` verified at test startup |
| Research stash | `stash@{0}: On main: wip: preserve unrelated research HTML deletions before SWE-ARC-SVC-WP1` remained untouched |
| Unrelated LaTeX | `docs/Latex/Proposed Model/` remained untracked and unstaged |

Verification completed:

```bash
cmake --preset linux-gcc-test
cmake --build build/linux-gcc-test --target tsunami_tests -j4
ctest --preset linux-gcc-test-run
cmake --preset linux-gcc-geospatial-test
cmake --build build/linux-gcc-geospatial-test --target tsunami_tests -j4
ctest --preset linux-gcc-geospatial-test-run
cmake --preset linux-clang-geospatial-test
cmake --build build/linux-clang-geospatial-test --target tsunami_tests -j4
ctest --preset linux-clang-geospatial-test-run
python3 -m json.tool schemas/geospatial_import_record/1.0.0/geospatial_import_record.schema.json
python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json
python3 tools/architecture/validate_layer_ownership.py --layers architecture/layer_ownership_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json
python3 tools/architecture/validate_interface_contracts.py --interfaces architecture/interface_contract_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json --cases architecture/case_lifecycle_policy_v0.1.json
python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json --cmake-graph build/linux-gcc-geospatial-test/target-graph.dot
```

Results:

| Check | Result |
| --- | --- |
| Previous ordinary baseline | 102/102 tests passed |
| Ordinary GCC preset | 102/102 tests passed; no geospatial tests discovered |
| Ordinary Clang preset | 102/102 tests passed; no geospatial tests discovered |
| GCC geospatial preset | 109/109 tests passed |
| Clang geospatial preset | 109/109 tests passed |
| Architecture validators | target, layer, interface and generated CMake graph checks passed |
| Schema syntax | `geospatial_import_record.schema.json` parsed with `python3 -m json.tool` |
| Warning-clean modified code | GCC and Clang geospatial builds completed without modified-code warning failures |

Fixture coverage:

| Area | Coverage |
| --- | --- |
| Raster | generated north-up GeoTIFF, rotated affine GeoTIFF, Float64 values, nodata, scale/offset, pixel registration, multiband rejection and manifest/asset datum conflicts |
| Vector | generated GeoPackage point, line and polygon layers, interior rings, field schema order, integer/real/string/boolean attributes, explicit layer selection, ambiguity rejection, unsupported multipolygon rejection and coordinate/feature guardrails |
| Datum evidence | accepted authoritative horizontal, accepted dataset-declared vertical, inferred horizontal, unknown vertical, conflicting authority code, conflicting vertical sign, gauge station reference and GEBCO-style model-assumption fixtures |
| Import records | manifest and case linkage, adapter/driver identity, native spatial reference, datum evidence, raster/vector summaries, warning order, deterministic JSON bytes, LF ending and transactional write preservation |

Final target graph:

| Target | Direct project deps | External deps |
| --- | --- | --- |
| `tsunami_geo` | `tsunami_core`, `tsunami_data` | none |
| `tsunami_geo_gdal` | `tsunami_geo` | `GDAL::GDAL` |

Public geo headers:

```text
src/geo/include/tsunami/geo/GeospatialImport.hpp
src/geo/include/tsunami/geo/GeospatialImportRecord.hpp
src/geo/include/tsunami/geo/GeospatialImportSerialisation.hpp
src/geo/include/tsunami/geo/ImportedRaster.hpp
src/geo/include/tsunami/geo/ImportedVector.hpp
src/geo/include/tsunami/geo/SpatialReferenceEvidence.hpp
```

Coverage includes real GDAL `GTiff` and `GPKG` driver availability, raster value/mask preservation, affine and rotated extent handling, vector attribute/geometry preservation, layer selection failures, resource limits, CRS and vertical datum conflicts, canonical JSON import-record output and public-header scans for external adapter type leakage. The existing `geospatial` vcpkg feature enables GDAL with the minimal `sqlite3` port feature because the selected G1 vector format is GeoPackage.

Issue tree at verification time:

| Issue | State after merge |
| --- | --- |
| `#190` `SWE-GEO-IMP-WP1` | close after `#191`, `#192` and `#193` |
| `#191` `SWE-GEO-IMP-WP1-T1` | close after PR merge |
| `#192` `SWE-GEO-IMP-WP1-T2` | close after PR merge |
| `#193` `SWE-GEO-IMP-WP1-T3` | close after PR merge |
| `#194` `SWE-GEO-CRS-WP1` | remains open; receives handoff comment |
| `#202` `SWE-GEO-TER-WP1` | remains open |

Out of scope for this work package: NetCDF, shapefile, GeoJSON, remote sources, GeoPackage raster, CRS transformation, datum transformation, resampling, terrain generation, mesh ingestion, checksum computation and solver-ready field construction.
