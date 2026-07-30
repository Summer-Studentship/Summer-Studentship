# Source Term Verification v0.1

## Scope

This note records verification assets for `SWE-R2D-SRC`.

## Unit Coverage

`tests/r2d/regional_source_terms_tests.cpp` covers:

```text
mesh-bound Manning and Coriolis source construction
invalid source field counts
negative Manning rejection
exact Manning damping
exact Coriolis rotation
Manning kinetic-energy removal
Coriolis kinetic-energy preservation
source timestep estimation
stable timestep selection with source restriction
source-enabled Regional2D solve-loop advancement
```

Existing benchmark tests now also assert that every benchmark owns a mesh-bound
`local_sources` set and advances with that source pointer supplied to
`RegionalSolveRequest`.

Existing CSV tests now check that appended source diagnostics are emitted.

## Benchmark Cases

The source benchmark IDs are:

```text
uniform_manning_decay
uniform_coriolis_oscillation
uniform_manning_coriolis
frictional_wet_dry_dam_break
```

The first three isolate local source behaviour over flat-bed moving water. The
wet-dry case keeps the existing dam-break geometry and adds uniform Manning
friction to exercise dry-safe source staging.

## Local Verification

The following focused build and tests passed during implementation:

```bash
cmake -S . -B build/g2-r2d-src-dev-vcpkg-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTSUNAMI_BUILD_CLI=ON \
  -DTSUNAMI_BUILD_GUI=OFF \
  -DBUILD_TESTING=ON \
  -DVCPKG_MANIFEST_FEATURES=tests \
  -DCMAKE_TOOLCHAIN_FILE=/home/helios/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build/g2-r2d-src-dev-vcpkg-tests \
  --target tsunami_tests tsunami_r2d_benchmark -j2

ctest --test-dir build/g2-r2d-src-dev-vcpkg-tests \
  -R 'source|Source|benchmark|CSV|Regional solve' \
  --output-on-failure
```

Result:

```text
100% tests passed out of 8
```

The following preset acceptance matrix also passed:

```bash
VCPKG_ROOT=/home/helios/vcpkg cmake --preset linux-gcc-test
cmake --build build/linux-gcc-test -j2
ctest --test-dir build/linux-gcc-test --output-on-failure

VCPKG_ROOT=/home/helios/vcpkg cmake --preset linux-clang-test
cmake --build build/linux-clang-test -j2
ctest --test-dir build/linux-clang-test --output-on-failure
```

Results:

```text
GCC:   100% tests passed out of 79
Clang: 100% tests passed out of 79
```
