# Regional Radiation and Relaxation Boundaries v0.1

## Scope

This note records the `SWE-R2D-BC` Regional2D boundary increment.

The generic FVM layer still owns patch identity, patch ordering, fixed scalar
values, zero-gradient scalar values and named non-executable references.
Regional2D owns shallow-water exterior-state construction, transmissive open
boundaries, characteristic radiation boundaries and sponge relaxation zones.

## Boundary Model

`RegionalBoundaryConditionSet` owns one operation per mesh boundary patch:

```text
componentwise
transmissive
radiation
```

Patches without a Regional2D override use componentwise mode and execute the
four scalar FVM boundary sets for depth, x-momentum, y-momentum and bed
elevation. Overridden physical patches may retain named scalar references
because those scalar conditions are metadata only and are not executed.

`RegionalExteriorStateWorkspace` provides reusable patch fields for exterior
depth, x-momentum, y-momentum and bed elevation. Population stages all patch
values before publishing them, so failed boundary evaluation does not expose a
partially updated successful workspace.

## Far-Field Reference

`RegionalFarFieldState` stores:

```text
free_surface_elevation  m
velocity_x              m/s
velocity_y              m/s
```

Depth is never stored directly. For each local bed elevation `z_b`, the
reference conserved state is derived as:

```text
h_ref  = max(0, eta_ref - z_b)
qx_ref = h_ref u_ref
qy_ref = h_ref v_ref
```

The resulting state is canonicalised through the accepted shallow-water state
policy.

## Radiation Boundary

The radiation condition uses the owner-oriented outward face normal and the
local tangent `t = (-n_y, n_x)`.

For wet subcritical flow:

```text
c_L = sqrt(g h_L)
c_ref = sqrt(g h_ref)
R_out = u_n,L + 2 c_L
R_in  = u_n,ref - 2 c_ref
u_n,R = 0.5 (R_out + R_in)
c_R   = 0.25 (R_out - R_in)
h_R   = c_R^2 / g
```

Tangential velocity comes from the interior when the reconstructed normal
velocity is outgoing and from the reference state when it is incoming.

Supercritical outflow copies the interior state. Supercritical inflow uses the
full reference state. Dry/dry returns a canonical dry exterior state; dry/wet
uses the reference state; wet/dry is accepted only when the characteristic
reconstruction remains admissible.

## Relaxation Zones

`PatchRelaxationZoneSpecification` defines a patch-aligned sponge:

```text
patch_tag
width
maximum_rate
profile_exponent
reference_state
```

Each `RegionalRelaxationZone` owns a mesh-bound cell field of damping rates in
`1/s`. The implemented profile is deterministic and patch aligned:

```text
sigma_i = sigma_max (1 - d_i / width)^p
```

for cells with centroid distance `d_i < width`; other cells have zero rate.
Overlapping zones contribute additively.

The source residual is:

```text
R_i += A_i sigma_i (U_i - U_ref,i)
```

This matches the existing update convention:

```text
U_i^(n+1) = U_i^n - (dt / A_i) R_i
```

Outgoing mass accounting includes relaxation removal:

```text
D_i += A_i max(sigma_i (h_i - h_ref,i), 0)
```

## Time Integration

The physical residual overload accepts simulation time and is integrated into
Forward Euler, SSPRK2 and SSPRK3. Stage evaluation times are:

```text
Forward Euler: t_n
SSPRK2:        t_n, t_n + dt
SSPRK3:        t_n, t_n + dt, t_n + 0.5 dt
```

The solve loop accepts exactly one boundary mode:

```text
legacy scalar boundary sets
Regional2D physical boundary set
```

Ambiguous mixed requests are rejected before state mutation. Physical requests
may omit relaxation zones; the loop constructs an empty mesh-bound set once.

## Timestep Restrictions

Stable explicit timestep selection now considers:

```text
CFL
positivity
relaxation
multiple
none
```

The relaxation estimate is `theta_relax / max_i sigma_i` when any active
relaxation rate exists. `multiple` is reported when active bounds are equal
within the configured comparison tolerance.

## Deferred

This increment does not implement friction, Coriolis, wind stress, atmospheric
pressure, earthquake source modelling, Local3D coupling, geospatial production
cases or GUI integration.
