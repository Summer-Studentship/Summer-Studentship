# Regional2D State, Flux and Residual v0.1

This document records the first equation-level Stage 1 Regional2D vertical slice. It builds on the accepted FVM mesh, field, boundary and numerical-operator contracts and keeps the GUI, persistence, Local3D and coupling layers out of scope.

## Conserved State

The homogeneous nonlinear shallow-water conserved variables are:

```text
U = [h, q_x, q_y]^T = [h, hu, hv]^T
```

Units:

- `h`: `m`;
- `q_x`, `q_y`: `m2/s`.

`RegionalConservedState` owns three fixed-size `CellScalarField` components for those variables. The component fields share one mesh binding, are move-only, cloneable through explicit deep copy and are copied transactionally from compatible states.

## State Policy

`ShallowWaterStatePolicy` contains:

- gravity `g`;
- dry-depth threshold `h_dry`;
- depth tolerance `epsilon_h`;
- normal tolerance `epsilon_n`.

The validated invariants are `g > 0`, `h_dry > 0`, `epsilon_h >= 0`, `epsilon_h <= h_dry`, `epsilon_n > 0`, and all values finite.

## Dry-State Interpretation

Local state validation rejects nonfinite depth, nonfinite momentum and depth below `-epsilon_h`.

When `h <= h_dry`, the canonical state is:

```text
(h, q_x, q_y) = (0, 0, 0)
```

This includes small negative depths inside the accepted tolerance. Wet states retain finite momenta unchanged.

Primitive recovery uses:

```text
u = q_x / h, v = q_y / h   when h > h_dry
u = 0,       v = 0         when h <= h_dry
```

The implementation never divides by a dry or zero depth.

## Physical Normal Flux

For owner-oriented face-area vector `S_f = (S_x, S_y, 0)`:

```text
A_f = |S_f|
n_f = S_f / A_f
```

The normal is validated as finite, positive length, planar and unit length within `epsilon_n`.

For normal velocity `u_n = u n_x + v n_y`, the physical flux per unit face length is:

```text
F_n(U) = [
  h u_n,
  q_x u_n + 0.5 g h^2 n_x,
  q_y u_n + 0.5 g h^2 n_y
]
```

Canonical dry states return zero physical flux.

## Signal Speed and Rusanov Flux

The shared characteristic speed is:

```text
lambda(U,n) = |u_n| + sqrt(g h)
```

Dry states have zero normal velocity, zero wave speed and zero signal speed.

For left and right states:

```text
alpha_f = max(lambda(U_L,n), lambda(U_R,n))
```

The baseline Rusanov flux is:

```text
F_hat = 0.5 [F_n(U_L) + F_n(U_R)] - 0.5 alpha_f (U_R - U_L)
```

The implementation verifies consistency, reversal conservation and finite dry/wet combinations. HLL, HLLC, reconstruction, limiters and runtime scheme selection are deferred.

## Boundary Exterior States

Residual evaluation receives three scalar boundary condition sets:

- depth boundaries with unit `m`;
- x-momentum boundaries with unit `m2/s`;
- y-momentum boundaries with unit `m2/s`.

Before face traversal, each executable boundary set is applied into reusable patch fields. For a boundary face, the exterior state is assembled from patch-local `h`, `q_x`, and `q_y` values using the boundary patch face order. The assembled exterior state is then validated and canonicalised.

Named boundary references fail the residual evaluation explicitly. They are not replaced with zero-gradient states.

## Residual and Spectral Sum

Every face is evaluated exactly once.

For an internal face with owner `P` and neighbour `N`:

```text
R_P += F_hat A_f
R_N -= F_hat A_f
Lambda_P += alpha_f A_f
Lambda_N += alpha_f A_f
```

For a boundary face with owner `P`:

```text
R_P += F_hat A_f
Lambda_P += alpha_f A_f
```

The residual is an integrated outward residual:

```text
R_i = sum sigma_if F_hat_f A_f
```

The semidiscrete homogeneous equation is:

```text
dU_i/dt = -R_i / A_i
```

Cell spectral sums use unit `m2/s`. The maximum face signal speed is retained separately in `m/s`.

## CFL Estimate

For `0 < CFL <= 1`:

```text
dt_i = CFL A_i / Lambda_i
```

Cells with `Lambda_i = 0` impose no restriction. If all cells have zero spectral sum, the global stable timestep and limiting cell are absent rather than infinite. Negative or nonfinite spectral sums are errors.

## Forward Euler

The reusable explicit stage is:

```text
U_i^{n+1} = U_i^n - dt R_i / A_i
```

Each candidate cell state is finite-checked, validated and canonicalised before commit. The destination state is unchanged on failure.

## Workspaces and Transactionality

Residual evaluation uses `RegionalResidualWorkspace`, which owns reusable patch fields, staged residual components and staged spectral sums. Forward Euler uses `RegionalStateUpdateWorkspace`, which owns reusable local-state staging.

The public destinations are committed only after complete validation and evaluation. Workspaces are fixed-size, move-only and remain reusable after failures.

## OpenFOAM Classification

Adopted:

- one numerical flux evaluation per face;
- opposite owner/neighbour residual contributions;
- separate state, flux and residual responsibilities;
- preallocated field storage;
- reusable timestep-stage kernels.

Adapted:

- general equation fields become explicit Regional2D state and residual types;
- runtime flux selection becomes a concrete Rusanov implementation;
- broad dimension handling becomes explicit unit identifiers;
- polyhedral volume handling becomes accepted planar triangular cell area.

Deferred:

- bathymetry and source terms;
- hydrostatic reconstruction and well balancing;
- wet-front algorithms and positivity-preserving reconstruction;
- higher-order reconstruction and limiters;
- HLL/HLLC;
- SSPRK and adaptive solve loop;
- Local3D and coupling.

## Downstream Handoff

The next vertical slice can add bathymetry, well-balanced source treatment or wetting-drying methods on top of the accepted state, flux, residual, spectral and update interfaces without redesigning mesh, field, boundary or diagnostic contracts.
