# Field Validation Evidence v0.1

## Commands

- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --build build/linux-gcc-test --target tsunami_tests`
- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-gcc-test-workflow`
- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-clang-test-workflow`
- `ctest --test-dir build/linux-gcc-test --show-only`
- `ctest --test-dir build/linux-gcc-test --output-on-failure`
- `ctest --test-dir build/linux-gcc-test -R 'FVM field|MeshBinding|Deterministic reference fields|IFieldView' --output-on-failure`
- `python3 tools/architecture/validate_regional_2d_fields.py --root .`
- `python3 tools/architecture/validate_regional_2d_mesh.py --root .`
- `python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json`
- `python3 tools/architecture/validate_layer_ownership.py --layers architecture/layer_ownership_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json`

## Current Evidence

- CMake: `4.4.0`.
- GCC: `g++ (GCC) 16.1.1 20260625`.
- Clang: `clang version 22.1.8`.
- vcpkg baseline root: `/tmp/tsunami-fvm-vcpkg`.
- vcpkg commit: `d015e31e90838a4c9dfa3eed45979bc70d9357fc`.
- Previous CTest discovery count: 21.
- Final CTest discovery count: 30.
- Field-only CTest subset: 9 tests.
- GCC workflow passed.
- Clang workflow passed.
- Full CTest passed with 30 tests.
- Field tests cover traits, construction, storage, ownership, mesh binding, count validation, copy operations, deterministic fixtures and Qt-free inspection.
- Regional2D field, Regional2D mesh, target and layer architecture validators passed.
- Temporary invalid field-validator fixtures were rejected for scalar-only support, missing patch fields, one-value-per-patch interpretation, resize permitted, implicit copying permitted, ID/count-only compatibility, missing unit metadata, Qt dependency, unsupported target dependency and missing reference fixtures.
