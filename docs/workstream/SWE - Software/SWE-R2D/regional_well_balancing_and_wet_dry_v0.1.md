# Regional Well Balancing and Wet-Dry Treatment v0.1

## Scope

This note records the first-order Regional2D bathymetry, well-balancing and
wet-dry implementation for `SWE-R2D-WB` and `SWE-R2D-WD`, with partial reusable
source-term infrastructure for `SWE-R2D-SRC`.

The authoritative conserved state remains:

```text
U = [h, hu, hv]^T
```

Free-surface elevation is derived and is not an independently mutable authority:

```text
eta = h + z_b
```

Bed elevation `z_b` is positive upward, measured in metres (`m`) and may be
negative, zero or positive. Only nonfinite bed values are invalid.

## Bathymetry and Boundaries

`RegionalBathymetry` owns one fixed-size `CellScalarField` named
`bed_elevation`. The field is mesh-bound, move-only, explicitly cloneable and
uses unit `m`.

The well-balanced residual consumes executable scalar boundary-condition sets
for depth (`m`), x-momentum (`m2/s`), y-momentum (`m2/s`) and bed elevation
(`m`). Named non-executable boundary references fail the complete residual
operation; they are not replaced by an implicit zero-gradient condition.

## Hydrostatic Reconstruction

For owner/right states and bed elevations:

```text
eta_L = h_L + z_L
eta_R = h_R + z_R
z_f* = max(z_L, z_R)
h_L* = max(0, eta_L - z_f*)
h_R* = max(0, eta_R - z_f*)
```

Dry-safe primitive recovery supplies the original velocities. Reconstructed
momenta preserve those velocities when the reconstructed depth is wet:

```text
q_x,L* = h_L* u_L
q_y,L* = h_L* v_L
q_x,R* = h_R* u_R
q_y,R* = h_R* v_R
```

When a reconstructed depth is dry, both reconstructed momentum components are
exactly zero.

## Pressure Corrections

For owner-oriented unit normal `n = (n_x, n_y)`, the pressure corrections are:

```text
C_L = [0,
       0.5 g (h_L^2 - (h_L*)^2) n_x,
       0.5 g (h_L^2 - (h_L*)^2) n_y]^T

C_R = [0,
       0.5 g (h_R^2 - (h_R*)^2) n_x,
       0.5 g (h_R^2 - (h_R*)^2) n_y]^T
```

The mass component is zero. The existing homogeneous `rusanov_flux` is reused
unchanged on the reconstructed states.

## Residual Equations

For internal face `f`, owner `P`, neighbour `N` and face length `A_f`:

```text
R_P += (F*_f + C_L) A_f
R_N -= (F*_f + C_R) A_f
```

For a boundary face owned by `P`:

```text
R_P += (F*_f + C_L) A_f
```

Internal mass remains conservative because the correction mass components are
zero:

```text
R_h,P^(f) + R_h,N^(f) = 0
```

Momentum cancellation is not asserted when bathymetry is present; the pressure
correction difference represents the discrete bed source.

## Lake At Rest

The preserved equilibrium is:

```text
q_x = q_y = 0
eta = h + z_b = eta_0
```

The well-balanced residual is zero within deterministic tolerance for wet,
bed-step and partially dry lake-at-rest fixtures when boundary exterior states
are consistent.

## Wet-Dry Interfaces

Hydrostatic reconstruction is the first-order shoreline treatment.

If the wet-side free surface does not exceed the dry-side bed, both
reconstructed interface depths are zero and the mass flux is zero. If the
wet-side free surface exceeds the dry-side bed, the wet-side reconstructed
depth is positive and the Rusanov flux may transfer water into the dry cell.
No wall-reflection override is applied.

## Spectral Sums and Outgoing Mass

The residual reports reconstructed-state spectral sums:

```text
Lambda_i = sum_f alpha_f A_f
alpha_f = max(|u_n,L*| + sqrt(g h_L*), |u_n,R*| + sqrt(g h_R*))
```

The unit is `m2/s`.

Outgoing mass-discharge rate is:

```text
D_i = sum_f max(Q_if^out, 0)
```

For an owner-oriented internal mass flux `Q_f = F_h,f* A_f`, owner outflow is
`Q_f` and neighbour outflow is `-Q_f`. Boundary faces contribute only to the
owner. The unit is `m3/s`.

## Positivity and Timesteps

The draining-time positivity restriction is:

```text
dt_i^drain = A_i h_i / D_i,  D_i > 0
dt_pos = theta_pos min_i dt_i^drain,  0 < theta_pos <= 1
```

Cells with zero outflow impose no positivity restriction. Dry cells with
positive outflow are rejected.

In this slice, the selected stable explicit timestep is the minimum of present
CFL and positivity estimates. When both are absent, the restriction kind is
`none` and a finite positive externally supplied timestep may still be used
with a compatible finite residual. Later boundary work extends the selector
with relaxation and multi-limiter reporting.

## Wet-Dry Update

The transactional forward-Euler update uses:

```text
U_i^(n+1) = U_i^n - (dt / A_i) R_i
```

The implementation validates the supplied stable bound, rejects timesteps above
that bound, computes every candidate, rejects depths below
`-depth_tolerance`, then canonicalises accepted dry states through the existing
state policy. Positivity is controlled by conservative residuals, outgoing
mass accounting and the draining-time timestep bound; post-update clipping is
not the primary method.

Diagnostics report wetted, dried, remaining wet, remaining dry and
canonicalised cells. They separately expose water volume removed by dry-depth
thresholding and volume corrected for small negative depths inside tolerance:

```text
0 < h_candidate <= h_dry      -> removed volume += A_i h_candidate
-eps_h <= h_candidate < 0     -> correction volume += A_i (-h_candidate)
```

## Transactionality and Workspaces

Bathymetry, free-surface, residual and wet-dry update operations stage their
results before mutating public outputs. Expected validation failures return
structured diagnostics with `state_changed=false`.

`WellBalancedResidualWorkspace` owns reusable patch fields for all four
boundary components, a staging residual, a staging spectral sum and a staging
outgoing mass-rate field. `WetDryUpdateWorkspace` owns reusable candidate-state
storage. Both workspaces are mesh-bound, move-only and keep stable storage
pointers across repeated calls.

## Reference Classification

Adopted from OpenFOAM-style practice: mesh-associated bathymetry fields,
source treatment separated from generic storage, dense workspaces and
patch-supplied boundary values.

Adapted: generic source machinery is replaced by explicit first-order
hydrostatic reconstruction over the accepted planar triangular mesh.

Deferred: friction, Coriolis, wind stress, atmospheric pressure, moving
bathymetry, dispersive terms, HLL/HLLC, MUSCL, slope limiters, SSPRK,
runtime source registration, solve-loop orchestration, persistence, Local3D,
coupling and GUI integration.

## Downstream Handoff

The next source-term slice can add accepted optional sources beside, not
inside, this hydrostatic reconstruction path. The next time-integration slice
can consume the combined timestep primitive without changing the first-order
wet-dry update contract. The solve-loop slice can use the well-balanced
residual, outgoing mass-rate and diagnostics as production primitives.
