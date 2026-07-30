# Boundary Reflection and Absorption Verification v0.1

## Scope

This note records the verification assets added for `SWE-R2D-BC`.

## Unit Coverage

`tests/r2d/regional_boundary_relaxation_tests.cpp` covers:

```text
far-field validation and local depth derivation
mixed componentwise/radiation/transmissive boundary sets
transactional exterior-state population
radiation characteristic regimes
relaxation-zone profiles, source residuals and timestep limits
physical residual lake-at-rest preservation
solve-loop rejection of ambiguous boundary modes
```

Existing positivity selection coverage now expects equal active bounds to
report `multiple`.

`tests/r2d_benchmarks/benchmark_case_tests.cpp` now verifies that every
Regional2D benchmark owns complete scalar and physical boundary inputs and
advances through the physical solve-loop path.

## Benchmark Cases

Two programmatic cases were added:

```text
outgoing_linear_wave_radiation
outgoing_linear_wave_radiation_sponge
```

Both use the accepted structured triangular mesh generator, flat bathymetry and
a small right-moving linearised surface-elevation packet. The right patch uses
a Regional2D radiation override. The sponge case additionally applies a
right-patch relaxation zone targeting the still-water far field.

The standalone benchmark accepts:

```text
--sponge-width
--sponge-rate
--sponge-exponent
```

These flags replace the case sponge with a right-patch relaxation layer using
the supplied profile parameters.

## Local Verification

The following library-only build was run because the configured test preset
depends on a stale `/tmp/tsunami-fvm-vcpkg` toolchain path and local Catch2 is
not installed:

```bash
cmake -S . -B build/g2-r2d-bc-lib -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTSUNAMI_BUILD_CLI=OFF \
  -DTSUNAMI_BUILD_GUI=OFF \
  -DBUILD_TESTING=OFF

cmake --build build/g2-r2d-bc-lib \
  --target tsunami_r2d tsunami_r2d_benchmarks tsunami_r2d_io -j2
```

The local production library build passed.

The following local test configure was attempted and blocked by missing Catch2:

```bash
cmake -S . -B build/g2-r2d-bc-tests-local -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTSUNAMI_BUILD_CLI=OFF \
  -DTSUNAMI_BUILD_GUI=OFF \
  -DBUILD_TESTING=ON
```

Expected CI remains the authoritative full CTest path because it supplies the
declared vcpkg test dependencies.
