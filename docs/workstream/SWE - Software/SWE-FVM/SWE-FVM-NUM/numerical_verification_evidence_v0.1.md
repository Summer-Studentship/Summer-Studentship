# Numerical Verification Evidence v0.1

## Environment

- Work package: `SWE-FVM-NUM-WP1`.
- Branch: `feat/g1-fvm-numerical-primitives`.
- Base commit before local commits: `5dbb7b1` (`feat(g1): implement complete Regional2D boundary framework`).
- Interpolation commit: `3086376c47066ea8e8bb6fe33927abc9b5118504`.
- Gradient commit: `a161f27e7aabb86ae4898dd7506bb75e72c6ab1b`.
- Verification and documentation commit: the commit containing this evidence document.
- CMake: `4.4.0`.
- GCC: `g++ (GCC) 16.1.1 20260625`.
- Clang: `clang version 22.1.8`.
- vcpkg root used for validation: `/tmp/tsunami-fvm-vcpkg`.
- vcpkg checkout: `b9db60d8be5cd434b195da8cffc2ee82e227fac4`.

## Commands

- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-gcc-test-workflow`
- `VCPKG_ROOT=/tmp/tsunami-fvm-vcpkg cmake --workflow --preset linux-clang-test-workflow`
- `ctest --test-dir build/linux-gcc-test --show-only`
- `ctest --test-dir build/linux-gcc-test --output-on-failure`
- `ctest --test-dir build/linux-gcc-test -R "Linear interpolation|Cell-to-face interpolation|Multi-face boundary interpolation|Interpolation rejects|Green-Gauss gradient|workspaces are reusable" --output-on-failure`
- `python3 tools/architecture/validate_regional_2d_mesh.py --root .`
- `python3 tools/architecture/validate_regional_2d_fields.py --root .`
- `python3 tools/architecture/validate_regional_2d_boundaries.py --root .`
- `python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json`
- `python3 tools/architecture/validate_layer_ownership.py --layers architecture/layer_ownership_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json`

## Test Discovery

- Previous discovered CTest count: 38.
- Final discovered CTest count: 46.
- New numerical CTest count: 8.
- GCC workflow: 46/46 tests passed.
- Clang workflow: 46/46 tests passed.
- Full GCC CTest: 46/46 tests passed.
- Numerical-only GCC CTest filter: 8/8 tests passed.

The numerical tests are:

- `Linear interpolation stencil is deterministic and geometry weighted`;
- `Cell-to-face interpolation preserves constant scalar and vector fields`;
- `Cell-to-face interpolation handles linear internal faces and boundary scatter`;
- `Multi-face boundary interpolation retains patch-local ordering`;
- `Interpolation rejects named and incompatible inputs transactionally`;
- `Interpolation and gradient workspaces are reusable with stable storage`;
- `Green-Gauss gradient preserves constants and rejects invalid inputs`;
- `Green-Gauss gradient recovers manufactured linear fields on the reference mesh`.

## Numerical Results

Reference two-triangle stencil:

- internal face: `FaceId{2}`;
- owner: `CellId{0}`;
- neighbour: `CellId{1}`;
- owner weight: `0.5`;
- neighbour weight: `0.5`;
- partition error: `0.0` within `1e-12`.

Skewed two-triangle stencil:

- owner weight approximately `0.5594`;
- neighbour weight approximately `0.4406`;
- weights remain finite, bounded and partitioned to one;
- test confirms the skewed weights are not equal to `0.5`.

Constant interpolation:

- scalar constants tested: `0.0`, `1.0`, `-3.25`;
- vector constant tested: `(2.0, -4.0, 0.0)`;
- maximum accepted face error: `0.0` within `1e-12`.

Boundary integration:

- scalar fixed-value scatter preserves multi-face patch local order: faces `0` and `1` receive `10.0` and `20.0`;
- scalar zero-gradient scatter uses the owner cell: north and west receive `7.0` on the multi-face fixture;
- vector fixed-value scatter preserves faces `0` and `1` as `(1,0,0)` and `(0,1,0)`;
- vector zero-gradient scatter uses the owner cell: north and west receive `(7,-7,0)`;
- every global face is populated exactly once.

Linear interpolation and gradient:

| Coefficients `(a,b,c)` | Internal face interpolation error | Gradient max error |
| --- | ---: | ---: |
| `(1,1,0)` | `0.0` within `1e-12` | `0.0` within `1e-12` |
| `(2,0,1)` | `0.0` within `1e-12` | `0.0` within `1e-12` |
| `(4,2,-3)` | `0.0` within `1e-12` | `0.0` within `1e-12` |

The manufactured gradient test verifies both cells recover `(b,c,0)`, so it would fail if the neighbour contribution used the owner sign.

Constant gradient:

- complete scalar face field value: `4.25`;
- every cell gradient: `(0,0,0)` within `1e-12`.

Failure and reuse:

- named boundary failure returns `fvm.numerics.interpolation.boundary_not_executable`;
- diagnostic context includes `patch_id=2`, `boundary_kind=named_reference`, `rule_id=SWE-FVM-NUM-WP1` and `state_changed=false`;
- source, destination, stencil, workspace, boundary-set and unit incompatibilities leave the destination unchanged;
- nonfinite scalar face values are rejected by the gradient operator and leave the destination unchanged;
- repeated interpolation and gradient calls keep destination, staging and patch-workspace data pointers stable.

## Architecture Validators

- Regional2D mesh validator: passed.
- Regional2D field validator: passed.
- Regional2D boundary validator: passed.
- Target dependency validator: passed.
- Layer ownership validator: passed.

## Scope Checks

- Research stash remains present by message: `wip: preserve unrelated research HTML deletions before SWE-ARC-SVC-WP1`.
- The stash was not applied, popped, dropped or inspected.
- `docs/Latex/Proposed Model/` remains untracked and was not staged or edited.
- No GUI, Qt, QML, Local3D, coupling, dependency-manifest, preset, policy JSON, Python or Lucidchart artefact was added for this work package.
