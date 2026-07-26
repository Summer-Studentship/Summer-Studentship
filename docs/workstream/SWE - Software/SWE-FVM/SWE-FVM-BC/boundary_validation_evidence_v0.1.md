# Boundary Validation Evidence v0.1

## Commands

- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-gcc-test-workflow`
- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-clang-test-workflow`
- `ctest --test-dir build/linux-gcc-test --show-only`
- `ctest --test-dir build/linux-gcc-test --output-on-failure`
- `ctest --test-dir build/linux-gcc-test -R 'Boundary|boundary|Zero-gradient|Fixed-value|Named' --output-on-failure`
- `python3 tools/architecture/validate_regional_2d_boundaries.py --root .`
- `python3 tools/architecture/validate_regional_2d_fields.py --root .`
- `python3 tools/architecture/validate_regional_2d_mesh.py --root .`
- `python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json`
- `python3 tools/architecture/validate_layer_ownership.py --layers architecture/layer_ownership_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json`

## Current Evidence

- CMake: `4.4.0`.
- GCC: `g++ (GCC) 16.1.1 20260625`.
- Clang: `clang version 22.1.8`.
- Ninja: `1.13.2`.
- vcpkg root: `/tmp/tsunami-fvm-vcpkg`.
- vcpkg commit: `d015e31e90838a4c9dfa3eed45979bc70d9357fc`.
- Previous CTest discovery count: 30.
- Final CTest discovery count: 38.
- Boundary test source count: 8 new `tests/fvm/boundary_tests.cpp` test cases.
- Boundary filter count: 9 tests, including the 8 new boundary tests and the existing boundary-topology mesh test matched by the filter.
- GCC workflow passed with 38/38 tests.
- Clang workflow passed with 38/38 tests.
- Full GCC CTest passed with 38/38 tests.
- Boundary-filtered GCC CTest passed with 9/9 tests.
- Boundary tests cover descriptor metadata, deterministic patch mapping, invalid specification diagnostics, fixed-value scalar/vector copy, zero-gradient scalar/vector owner mapping, named placeholder failure and mesh/field incompatibility transactionality.
- The Regional2D boundary validator passed.
- Regional2D field, Regional2D mesh, target graph and layer ownership validators passed.
- Temporary invalid boundary-policy fixtures were rejected for missing named-reference kind, bad target dependency, non-transactional apply policy and missing diagnostic code.
