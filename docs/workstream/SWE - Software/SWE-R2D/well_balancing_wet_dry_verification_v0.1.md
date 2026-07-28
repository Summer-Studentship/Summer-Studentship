# Well-Balancing and Wet-Dry Verification v0.1

## Environment

- Branch: `feat/g2-r2d-well-balanced-wet-dry`.
- Base commit: `5ad288ed48900c376ec9310c1a0ab07461cb35f9`.
- Bathymetry commit: `3f74ff3`.
- Well-balancing commit: `0432ced`.
- Positivity/wet-dry commit: `f944362`.
- Integrated verification commit: the commit containing this evidence document.
- CMake: `4.4.0`.
- GCC: `g++ (GCC) 16.1.1 20260625`.
- Clang: `clang version 22.1.8`.
- vcpkg root: `/tmp/tsunami-fvm-vcpkg`.
- vcpkg checkout: `4d41999284df1b6624c325c2ac7f7e92925d15cb`.
- vcpkg executable: `2026-07-13-bf04c909169fdbb30821c02c6eb01f1cd1295d05`.

## Commands

- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-gcc-test-workflow`
- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-clang-test-workflow`
- `ctest --test-dir build/linux-gcc-test --show-only`
- `ctest --test-dir build/linux-gcc-test --output-on-failure`
- `ctest --test-dir build/linux-gcc-test -R "Bathymetry|Free surface|Hydrostatic|Well-balanced|Positivity|Wet/dry" --output-on-failure`
- `python3 tools/architecture/validate_regional_2d_mesh.py --root .`
- `python3 tools/architecture/validate_regional_2d_fields.py --root .`
- `python3 tools/architecture/validate_regional_2d_boundaries.py --root .`
- `python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json`
- `python3 tools/architecture/validate_layer_ownership.py --layers architecture/layer_ownership_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json`

## Current Results

- Previous discovered CTest count: 55.
- Final discovered CTest count: 64.
- New Regional2D well-balanced/wet-dry tests: 9.
- GCC workflow: 64/64 tests passed.
- Clang workflow: 64/64 tests passed.
- Full GCC CTest: 64/64 tests passed.
- Focused Regional2D well-balanced/wet-dry CTest: 9/9 tests passed.
- Existing Regional2D, target and layer architecture validators: passed.

## Numerical Evidence

- Bathymetry accepts negative, zero and positive finite elevations.
- Nonfinite bathymetry and wrong cell counts are rejected.
- Bathymetry fields are move-only, explicitly deep-cloneable and use unit `m`.
- Free-surface elevation is derived by `eta = h + z_b` and preserves the
  destination on incompatible output fields.
- Flat-bed hydrostatic reconstruction reproduces original wet states.
- Flat-bed pressure-correction error: `0.0` within `1e-12`.
- Flat-bed reconstructed Rusanov flux matches the homogeneous flux within
  `1e-12`.
- Bed-step reconstruction uses `z_f* = max(z_L, z_R)` and nonnegative depths.
- Reconstructed wet momenta preserve original velocities.
- Reconstructed dry momenta are exactly zero.
- Pressure-correction mass components are exactly zero.
- Non-flat wet lake-at-rest maximum residual: `0.0` within `1e-12`.
- Bed-step lake-at-rest maximum residual: `0.0` within `1e-12`.
- Partially dry lake-at-rest maximum residual: `0.0` within `1e-12`.
- Partially dry repeated update preserves the state with no threshold-volume
  correction.
- Internal mass-conservation error is covered by flat-bed equivalence and the
  unchanged homogeneous internal-face cancellation test: `0.0` within `1e-12`.
- Blocked-inundation mass flux: `0.0` within `1e-12`.
- Permitted inundation produces positive wet-side reconstructed depth and
  positive mass flux into the dry side.
- Analytical positivity estimate with `A=0.5`, `h=2`, `D=2` and
  `theta_pos=0.5` gives `dt_pos=0.25`, limiting cell `0`.
- Combined timestep selection returns CFL, positivity, equal and none
  restrictions deterministically.
- Timestep above the supplied stable bound is rejected transactionally.
- Accepted wet-dry update dries one wet cell and wets one dry cell in the
  two-cell flux fixture.
- Threshold removed-volume diagnostic: `2.5e-7 m3`.
- Negative-tolerance correction diagnostic: `2.5e-9 m3`.
- Public residual, spectral, outgoing-rate, maximum-speed, destination-state
  and diagnostics outputs are preserved on tested failure paths.
- Residual and wet-dry workspaces keep stable staging-storage pointers across
  failure and reuse.

## Architecture Validator Evidence

- `validate_regional_2d_mesh.py`: passed.
- `validate_regional_2d_fields.py`: passed.
- `validate_regional_2d_boundaries.py`: passed.
- `validate_target_graph.py`: passed.
- `validate_layer_ownership.py`: passed.

## Diagnostic Codes

Representative covered codes include:

```text
r2d.bathymetry.entity_count_mismatch
r2d.bathymetry.value_nonfinite
r2d.free_surface.destination_incompatible
r2d.hydrostatic.state_invalid
r2d.hydrostatic.result_nonfinite
r2d.well_balanced.boundary_not_executable
r2d.well_balanced.destination_incompatible
r2d.positivity.safety_factor_invalid
r2d.positivity.outgoing_rate_negative
r2d.positivity.outgoing_rate_nonfinite
r2d.positivity.dry_cell_outflow
r2d.timestep.estimate_invalid
r2d.wet_dry.timestep_exceeds_bound
r2d.wet_dry.candidate_invalid
```

All expected validation failures use numerical diagnostics with
`state_changed=false`.

## Scope Evidence

- `tsunami_r2d` remains a static target.
- Target dependencies remain `tsunami_core`, `tsunami_fvm` and `tsunami_data`.
- No dependency manifest, vcpkg baseline, preset or machine-specific path was
  changed.
- No GUI, Qt, QML, Python validator, Lucidchart map, new policy JSON, friction,
  Coriolis, radiation, sponge, Local3D or coupling implementation was added.
- Research stash remains present by message:
  `wip: preserve unrelated research HTML deletions before SWE-ARC-SVC-WP1`.
- `docs/Latex/Proposed Model/` remains untracked and unstaged.
