# Case Schema Verification v0.1

- **Work Package:** `SWE-DAT-CFG-WP1`
- **Issues:** `#158`, `#159`, `#160`, `#161`
- **Policy:** `architecture/target_dependency_policy_v0.1.json`
- **Interface policy:** `architecture/interface_contract_policy_v0.1.json`
- **Document date:** 2026-07-30

## Evidence

The implementation adds:

| Item | Evidence |
|---|---|
| JSON schema | `schemas/case/1.0.0/case.schema.json` |
| Public data API | `src/data/include/tsunami/data/CaseConfiguration*.hpp` |
| Private parser/validator/writer | `src/data/src/CaseConfiguration*.cpp` |
| Valid fixtures | `tests/fixtures/cases/valid/*.json` |
| Invalid fixtures | `tests/fixtures/cases/invalid/*.json` |
| Migration fixture | `tests/fixtures/cases/migration/legacy_0_1_0.json` |
| Catch2 coverage | `tests/data/case_configuration_tests.cpp` |

## Validator Commands

```sh
python3 -m json.tool schemas/case/1.0.0/case.schema.json
python3 -m json.tool architecture/target_dependency_policy_v0.1.json
python3 -m json.tool architecture/interface_contract_policy_v0.1.json
python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json
python3 tools/architecture/validate_interface_contracts.py --interfaces architecture/interface_contract_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json --cases architecture/case_lifecycle_policy_v0.1.json
```

## Build And Test Commands

```sh
cmake --build build/linux-gcc-test --target tsunami_tests
ctest --test-dir build/linux-gcc-test -R 'case configuration|case schema|case parsing|case validation|case serialisation|case fixtures|migration' --output-on-failure
```

Clean GCC and Clang preset verification was run before merge:

```sh
rm -rf build/linux-gcc-test build/linux-clang-test
env VCPKG_ROOT=/home/helios/vcpkg QT_ROOT=/usr cmake --preset linux-gcc-test
cmake --build --preset linux-gcc-test-build
ctest --test-dir build/linux-gcc-test --output-on-failure
env VCPKG_ROOT=/home/helios/vcpkg QT_ROOT=/usr cmake --preset linux-clang-test
cmake --build --preset linux-clang-test-build
ctest --test-dir build/linux-clang-test --output-on-failure
```

Observed result:

| Preset | Result |
|---|---|
| `linux-gcc-test` configure | passed |
| `linux-gcc-test-build` | passed |
| `ctest --test-dir build/linux-gcc-test --output-on-failure` | 95/95 passed |
| `linux-clang-test` configure | passed |
| `linux-clang-test-build` | passed |
| `ctest --test-dir build/linux-clang-test --output-on-failure` | 95/95 passed |

## Review Results

| Check | Result |
|---|---|
| Schema syntax | covered by `json.tool` |
| Compatibility classification | exact, patch, forward minor, migration and unsupported major covered |
| Valid fixtures | parse, canonical serialise, reparse and byte-stability covered |
| Invalid fixtures | deterministic diagnostic code, JSON pointer and `state_changed=false` covered |
| Public headers | forbidden external tokens scanned by tests and interface policy |
| Target policy | `tsunami_data` is an active static library with public `tsunami_core` and private `nlohmann-json` ownership |

## Handoffs

`SWE-DAT-MAN-WP1` consumes dataset binding identifiers and the relative
`manifests/` path but remains responsible for dataset provenance and manifest
schema content. Application-service and GUI workstreams can consume the public
case API without depending on a JSON type.
