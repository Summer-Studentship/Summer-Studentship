# State, Flux and Residual Verification v0.1

## Environment

- Branch: `feat/g2-r2d-state-flux-residual`.
- State commit: `3e9ef54307bd27a47e73693c1433a5e9f8f39f30`.
- Flux commit: `f31e9ce423f2bdc49907726da51597bb48dcf42f`.
- Residual/CFL commit: `112469b52f45b47cc2373564c42251c03394bfbc`.
- Forward-Euler verification commit: the commit containing this evidence document.
- Base commit: `91fd3bd173dce48407c0706df84f42e79743b256`.
- CMake: `4.4.0`.
- GCC: `g++ (GCC) 16.1.1 20260625`.
- Clang: `clang version 22.1.8`.
- vcpkg checkout: `b9db60d8be5cd434b195da8cffc2ee82e227fac4`.

## Commands

- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-gcc-test-workflow`
- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-clang-test-workflow`
- `ctest --test-dir build/linux-gcc-test --show-only`
- `ctest --test-dir build/linux-gcc-test --output-on-failure`
- `ctest --test-dir build/linux-gcc-test -R "Regional2D|Rusanov|Physical flux|CFL|Forward Euler|Regional residual" --output-on-failure`
- `python3 tools/architecture/validate_regional_2d_mesh.py --root .`
- `python3 tools/architecture/validate_regional_2d_fields.py --root .`
- `python3 tools/architecture/validate_regional_2d_boundaries.py --root .`
- `python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json`
- `python3 tools/architecture/validate_layer_ownership.py --layers architecture/layer_ownership_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json`

## Results

- Previous discovered CTest count: 46.
- Final discovered CTest count: 55.
- New Regional2D test cases: 9.
- Regional2D filter count: 10, including the existing Regional2D mesh test matched by the filter.
- GCC workflow: 55/55 tests passed.
- Clang workflow: 55/55 tests passed.
- Full GCC CTest: 55/55 tests passed.
- Regional2D-filtered GCC CTest: 10/10 tests passed.

## State Evidence

- Policy validation accepts finite positive gravity and dry depth, nonnegative depth tolerance no larger than dry depth and positive normal tolerance.
- Gravity zero, gravity negative, nonfinite gravity, dry depth zero, negative depth tolerance, excessive depth tolerance and invalid normal tolerance are rejected.
- Wet primitive recovery for `(h,q_x,q_y)=(2,4,-6)` gives `(h,u,v)=(2,2,-3)`.
- Exact dry, small positive dry and small negative within tolerance canonicalise to `(0,0,0)`.
- Depth below tolerance and nonfinite components are rejected.
- `RegionalConservedState` is move-only, cloneable by explicit deep copy, mesh-bound and uses units `m`, `m2/s`, `m2/s`.
- Failed mesh-wide canonicalisation preserves the invalid state values for caller inspection.

## Flux Evidence

- Still-water physical flux with `h=2`, `g=9.81`, `n=(1,0)` gives mass flux `0`, x-momentum flux `19.62`, y-momentum flux `0`.
- Uniform x-flow and y-flow analytical fluxes pass within `1e-12`.
- Oblique signal speed for `h=4`, `q_x=8`, `q_y=0`, `n=(1/sqrt(2),1/sqrt(2))` matches `|2/sqrt(2)| + sqrt(9.81*4)`.
- Normal reversal preserves characteristic speed.
- Rusanov consistency error is `0.0` within `1e-12`.
- Rusanov reversal-conservation error is `0.0` within `1e-12`.
- Dry/dry returns zero flux and zero signal speed.
- Wet/dry, dry/wet and wet/wet cases remain finite.

## Residual and CFL Evidence

- Constant wet state with matching fixed exterior states has maximum residual `0.0` within `1e-12`.
- Constant dry state has maximum residual `0.0` within `1e-12` and zero spectral sums.
- Named depth boundary fails with `r2d.residual.boundary_not_executable` and preserves residual, spectral sum and maximum-speed destinations.
- Internal face contribution cancellation error is `0.0` within `1e-12`.
- Cell spectral sums are finite and nonnegative; maximum signal speed is positive for wet states.
- CFL analytical estimate for spectral sums `[2,4]`, cell area `0.5` and `CFL=0.5` gives `0.0625` with limiting cell `1`.
- Lowering CFL from `0.5` to `0.25` halves the timestep.
- All-zero spectral sums return absent timestep and absent limiting cell.
- Negative and nonfinite spectral sums are rejected.

## Forward-Euler Evidence

- Zero residual preserves the current state.
- With `dt=0.25`, `A=0.5` and residual `(0.2,-0.4,0.8)`, cell `0` updates analytically to `(1.9,1.2,-1.4)`.
- Candidate depth below negative tolerance is rejected and preserves destination state.
- Candidate depth inside dry tolerance canonicalises to `(0,0,0)`.
- Destination and update-workspace storage pointers remain stable across successful calls.
- Nonpositive timestep is rejected.

## Architecture and Scope Evidence

- Regional2D mesh, field and boundary validators passed.
- Target dependency validator passed with `tsunami_r2d -> tsunami_fvm` represented.
- Layer ownership validator passed.
- `tsunami_r2d` is now a compiled static target and links `tsunami_core`, `tsunami_fvm` and the existing public-contract dependency `tsunami_data`.
- Research stash remains present by message: `wip: preserve unrelated research HTML deletions before SWE-ARC-SVC-WP1`.
- `docs/Latex/Proposed Model/` remains untracked and unstaged.
- No bathymetry, wetting-drying scheme, source terms, GUI, Python validator, Lucidchart map, new policy JSON, Local3D or coupling implementation was added.
