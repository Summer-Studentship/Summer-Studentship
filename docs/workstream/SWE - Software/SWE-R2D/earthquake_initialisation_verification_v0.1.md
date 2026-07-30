# Earthquake Initialisation Verification v0.1

## Scope

This note records verification assets for `SWE-R2D-EQK`.

## Unit Coverage

`tests/r2d/regional_earthquake_initialisation_tests.cpp` covers:

```text
mesh-bound three-component seabed-displacement validation
move-only and clone semantics
zero and filled synthetic displacement factories
invalid component counts and non-finite values
earthquake source metadata validation and stable string values
vertical passive transfer with depth, volume and wet/dry preservation
horizontal-slope correction over a planar bed
transactional failure for missing horizontal bathymetry boundaries
prescribed surface transfer with independent wet/dry diagnostics
benchmark catalogue extension and solver-ready construction
deterministic earthquake initialisation CSV output
deterministic propagation metrics for selected earthquake benchmarks
```

Existing benchmark tests also advance every catalogue case, including the five
earthquake cases, through the standalone Regional2D solve loop.

## Benchmark Metrics

The implemented synthetic benchmark IDs are:

```text
earthquake_uniform_vertical_translation
earthquake_localised_vertical_uplift
earthquake_uplift_subsidence_dipole
earthquake_horizontal_slope_correction
earthquake_prescribed_surface_perturbation
```

Focused implementation runs produced the following deterministic diagnostics:

| Case | Key metric | Observed value |
| --- | --- | ---: |
| `earthquake_uniform_vertical_translation` | passive water-volume change | `5.55112e-17` |
| `earthquake_uniform_vertical_translation` | newly wet / newly dry cells | `0 / 0` |
| `earthquake_localised_vertical_uplift` | maximum effective displacement | `0.0373773` |
| `earthquake_localised_vertical_uplift` | propagation metric | positive radial outflow after solve |
| `earthquake_uplift_subsidence_dipole` | water-volume change | `0` |
| `earthquake_horizontal_slope_correction` | effective displacement | `0.002` |
| `earthquake_prescribed_surface_perturbation` | maximum effective displacement | `0.0191376` |
| `earthquake_prescribed_surface_perturbation` | maximum surface perturbation | `0.0906623` |
| `earthquake_prescribed_surface_perturbation` | water-volume change | `-0.0148193` |
| `earthquake_prescribed_surface_perturbation` | newly wet / newly dry cells | `13 / 0` |

The horizontal-slope value checks the planar-bed formula:

```text
dz_eff = 0.03 - 0.2 * 0.12 - (-0.1 * -0.04) = 0.002
```

## Local Verification

The following focused build and tests passed during implementation:

```bash
cmake --build build/g2-r2d-src-dev-vcpkg-tests \
  --target tsunami_tests tsunami_r2d_benchmark -j2

ctest --test-dir build/g2-r2d-src-dev-vcpkg-tests \
  -R 'Earthquake|Horizontal|Prescribed|seabed|metadata|CSV' \
  --output-on-failure
```

Result:

```text
100% tests passed out of 11
```

An output smoke test for `earthquake_uniform_vertical_translation` wrote:

```text
diagnostics.csv
earthquake_initialisation.csv
snapshots.csv
```

The CSV header starts with:

```text
source_kind,event_id,model_id
```

## Acceptance Matrix

The following clean preset matrix passed during implementation:

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
GCC:   100% tests passed out of 87
Clang: 100% tests passed out of 87
```
