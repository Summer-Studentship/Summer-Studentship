# Dataset Manifest Verification v0.1

## Build Context

- Branch: `feat/g1-dataset-provenance-manifest`.
- Commits: `8294dc7` records/schema, `9523ec7` validation/lineage/I/O, `f4e256e` fixtures/tests, final docs/policy commit in this document's history.
- GCC: `g++ (GCC) 16.1.1 20260625`.
- Clang: `clang version 22.1.8`.
- CMake: `4.4.0`.
- vcpkg baseline: `d015e31e90838a4c9dfa3eed45979bc70d9357fc`.
- Previous test baseline: 95 GCC tests and 95 Clang tests passed.
- Final discovered test count after this increment: 102 tests.
- New discovered tests: 7 dataset-manifest tests.

## Fixture Catalogue

- Valid manifests: 5.
- Invalid manifests: 54.
- Migration manifests: 1.
- Illustrative fixture pair: `illustrative_tohoku_kamaishi_corridor_case.json` and `illustrative_tohoku_kamaishi_corridor_manifest.json`.
- Schema syntax: `python3 -m json.tool schemas/dataset_manifest/1.0.0/dataset_manifest.schema.json` passes; this is syntax-only, not JSON Schema validation.

## Test Coverage

The C++ tests verify exact, patch-equivalent and forward-minor compatibility; migration-required legacy manifests; unsupported-major rejection; provider and licence references; dataset origin rules; asset location and digest rules; spatial-reference and vertical metadata rules; spatial and temporal resolution rules; uncertainty rules; processing references; multiple producers; cycle rejection; source-root rejection; case identity and revision matching; role-based case bindings; canonical byte stability; parse-serialise-parse equality; and transactional file writing.

Representative invalid fixtures assert deterministic error codes, rule IDs through context and JSON pointers. A full invalid-directory sweep confirms invalid manifests are rejected without exceptions, while case-binding-only fixtures parse successfully and then fail the binding API.

## Verification Commands

- Focused dataset-manifest tests: `./build/linux-gcc-test/tests/tsunami_tests "[dataset manifest]"` passed with 438 assertions in 7 test cases.
- GCC full suite: `ctest --test-dir build/linux-gcc-test --output-on-failure` passed, 102/102 tests.
- Clean GCC: `cmake --preset linux-gcc-test`, `cmake --build --preset linux-gcc-test-build --parallel 4`, and `ctest --test-dir build/linux-gcc-test --output-on-failure` passed.
- Clean Clang: `cmake --preset linux-clang-test`, `cmake --build --preset linux-clang-test-build --parallel 4`, and `ctest --test-dir build/linux-clang-test --output-on-failure` passed.
- Test discovery: `ctest --test-dir build/linux-gcc-test --show-only` reports 102 tests.
- Architecture validators: target graph, generated CMake graph, layer ownership, interface contracts, case lifecycle, application service and diagnostic failure propagation passed.

## Results

- Exact compatibility: pass.
- Patch-equivalent compatibility: pass.
- Forward-compatible minor through extensions: pass.
- Migration fixture: reports deterministic migration-required diagnostic.
- Unsupported-major fixture: reports deterministic unsupported-major diagnostic.
- Canonical byte stability: pass.
- Parse-serialise-parse equality: pass.
- Transactional manifest write: pass.
- Illustrative Tohoku/Kamaishi pair: validates as illustrative, non-authoritative fixture evidence only.
- GCC result: 102/102 tests passed.
- Clang result: 102/102 tests passed.
- Architecture result: all current validators passed; generated CMake graph reports `tsunami_data->tsunami_core` and imported `nlohmann_json::nlohmann_json` only for `tsunami_data`.
- Warning status: clean GCC and Clang builds completed without warnings in modified production and test code.

## Repository Hygiene

The Research stash `wip: preserve unrelated research HTML deletions before SWE-ARC-SVC-WP1` remains untouched. The unrelated `docs/Latex/Proposed Model/` directory remains untracked and unstaged. No downloader, checksum implementation, GDAL, PROJ, HDF5, GUI, new Python validator, Lucidchart file, dependency-manifest change, preset change or new policy JSON file was added.

## WBS And Handoff

Issue tree at implementation time: `#162` has authorised tasks `#163`, `#164` and `#165`; `#34` has authorised child work package `#162` only identified. Completion unblocks downstream `#166`, `#190` and `#222` without closing those issues.

Actual limitations remain intentional: the manifest validates metadata and provenance only. It does not download, hash, import, transform, condition, persist HDF5 artefacts, prepare solver inputs or inspect data in the GUI.
