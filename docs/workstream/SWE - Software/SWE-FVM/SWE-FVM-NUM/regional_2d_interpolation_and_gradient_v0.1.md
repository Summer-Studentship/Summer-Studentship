# Regional2D Interpolation and Gradient v0.1

`SWE-FVM-NUM-WP1` provides the first reusable finite-volume numerical operators for the standalone Stage 1 Regional2D solver. The implementation consumes the accepted mesh, field and boundary contracts from `SWE-FVM-MSH-WP1`, `SWE-FVM-FLD-WP1` and `SWE-FVM-BC-WP1`.

The operators live in `tsunami_fvm`, remain Qt-free and link only to `tsunami_core`.

## Scope

This work package introduces:

- immutable internal-face linear interpolation stencils;
- scalar and `Vector3` cell-to-face interpolation;
- boundary-face population through executable fixed-value and zero-gradient conditions;
- reusable interpolation and gradient workspaces;
- transactional destination updates;
- scalar Green-Gauss gradients from complete face scalar fields.

It does not implement shallow-water state, fluxes, residuals, divergence, limiters, non-orthogonal correction, skewness correction, vector-field gradients, Local3D, coupling, persistence, Python tooling or GUI integration.

## Interpolation

For each internal face `f`, owner cell `P`, neighbour cell `N`, face centroid `x_f` and cell centroids `x_P`, `x_N`:

```text
d_Pf = |x_f - x_P|
d_Nf = |x_f - x_N|

w_P = d_Nf / (d_Pf + d_Nf)
w_N = d_Pf / (d_Pf + d_Nf)

phi_f = w_P phi_P + w_N phi_N
```

For `Vector3`, the same weights are applied component-wise. Construction rejects nonfinite distances, degenerate denominators, nonfinite weights, weights outside `[0, 1]` and failed partition of unity.

The selected scheme is uncorrected distance-weighted linear interpolation. It is exact for the accepted two-triangle manufactured verification fixture, but it is not claimed to be skewness-corrected or globally linearly exact on arbitrary skewed unstructured meshes.

## Stencil

`LinearInterpolationStencil` owns a `MeshBinding` and contiguous `InternalFaceInterpolationEntry` records sorted by ascending `FaceId`.

Public inspection is limited to:

- `binding()`;
- `entries()`;
- `size()`;
- `empty()`;
- `is_bound_to(mesh)`.

Entries are immutable after construction. Weights are precomputed once by `make_linear_interpolation_stencil(const FiniteVolumeMesh&)`, not recalculated during each operator call.

## Boundary Integration

`interpolate_cell_to_face(...)` validates all mesh bindings, field counts, unit identifiers, boundary completeness, executable boundary kinds and patch-workspace layout before destination mutation.

Execution uses reusable workspace storage:

1. clear face-coverage markers;
2. populate internal faces from stencil weights into staging;
3. apply each executable boundary condition into its patch workspace;
4. scatter `patch.faces[local_index]` into the matching global face slot;
5. confirm every face was populated exactly once;
6. commit staging into the destination with `copy_values_from()`.

Named boundary references are intentionally non-executable and fail with `fvm.numerics.interpolation.boundary_not_executable`. They are never silently converted to zero-gradient boundaries.

## Transactionality

Interpolation and gradient operators stage all computed values in workspace storage and commit only after validation and complete evaluation succeed.

On success, the public destination contains the complete new field. On failure, destination values remain unchanged and diagnostics include `rule_id=SWE-FVM-NUM-WP1` and `state_changed=false`.

Workspace contents may be overwritten during a failed interpolation call, but the workspace remains reusable and deterministic for later valid calls.

## Workspaces

`LinearInterpolationWorkspace<Value>` owns:

- `MeshBinding`;
- immutable unit identifier;
- reusable global face staging field;
- one reusable boundary patch field per patch in `BoundaryPatchId` order;
- fixed-size face coverage markers.

`GreenGaussGradientWorkspace` owns:

- `MeshBinding`;
- contiguous `Vector3` staging values sized to the mesh cell count.

Both workspace types are move-only, fixed-size after construction, expose no public resize operation and keep storage pointers stable across repeated successful calls.

## Green-Gauss Gradient

The scalar gradient operator implements:

```text
grad(phi)_P = (1 / A_P) sum_f sigma_Pf phi_f S_f
```

where `A_P` is the accepted cell area, `S_f` is the accepted owner-oriented face-area vector and:

```text
sigma_Pf = +1 when P is owner(f)
sigma_Pf = -1 when P is neighbour(f)
```

For the accepted planar Regional2D model the stored result is:

```text
[d phi / dx, d phi / dy, 0]
```

The operator validates mesh bindings, face and cell counts, positive finite cell areas, finite face-area vectors, finite scalar face values and a nonempty destination unit before mutating the destination.

## Units

No unit algebra is performed. Interpolation requires source, destination, workspace and boundary units to match.

For gradients, the caller supplies the destination unit and is responsible for choosing the physically correct metadata. For example, an input `m` field over metre coordinates may use a dimensionless destination, while an input `m2/s` field over metre coordinates may use `m/s`.

## OpenFOAM Classification

Adopted:

- separate interpolation and gradient responsibilities;
- geometry-based internal-face weights;
- Green-Gauss accumulation;
- owner/neighbour sign convention;
- static-mesh geometric precomputation;
- dense contiguous destination fields.

Adapted:

- runtime scheme selection becomes concrete typed C++20 operators;
- temporary returned fields become preallocated destination fields plus reusable workspaces;
- arbitrary polyhedra become the accepted static planar triangular mesh;
- field algebra becomes explicit scalar and `Vector3` operations.

Deferred:

- skewness and non-orthogonal correction;
- least-squares gradients;
- slope limiters;
- runtime scheme selection;
- tensor gradients and vector-field gradients.

## Downstream Readiness

Regional2D state, flux and residual work packages can now request complete face fields and scalar gradients without redesigning mesh geometry, field storage, boundary execution or diagnostic interfaces.
