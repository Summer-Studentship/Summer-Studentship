# Mesh Validation Evidence v0.1

## Commands

- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-gcc-test-workflow`
- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-clang-test-workflow`
- `ctest --test-dir build/linux-gcc-test --show-only`
- `ctest --test-dir build/linux-gcc-test -R 'FVM|Regional2D|Public FVM' --output-on-failure`
- `python3 tools/architecture/validate_regional_2d_mesh.py --root .`
- `python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json`
- `python3 tools/architecture/validate_layer_ownership.py --layers architecture/layer_ownership_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json`

## Evidence

- GCC workflow passed with 21 discovered Catch2 tests.
- Clang workflow passed with 21 discovered Catch2 tests.
- The FVM/Regional2D/Qt-isolation subset passed with 6 discovered tests.
- FVM tests cover raw record separation, valid fixture geometry, immutable inspection, diagnostic context and representative invalid mesh mutations.
- The Regional2D mesh architecture validator passed.
- Temporary invalid validator fixtures were rejected for dimension 3, polygonal-cell claim, raw topology centroid, raw face unit normal, missing area-vector orientation, FVM linking Qt, missing fixture and missing closure invariant.
- Existing target and layer architecture validators passed after `tsunami_fvm` became a static library.

## Diagnostic Context

Mesh validation failures use:

- category: `numerical`;
- severity: `error`;
- `operation=make_finite_volume_mesh`;
- `rule_id=SWE-FVM-MSH-WP1`;
- `state_changed=false`.
