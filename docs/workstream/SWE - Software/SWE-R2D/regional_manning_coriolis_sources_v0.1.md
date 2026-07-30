# Regional Manning and Coriolis Sources v0.1

## Scope

This note records the `SWE-R2D-SRC` Regional2D physical-source increment.

The implementation adds mesh-bound local source data for Manning bottom
friction and Coriolis rotation. It does not add earthquake forcing, wind stress,
atmospheric pressure, geospatial ingestion, Local3D coupling or GUI controls.

## Source Data

`RegionalSourceTermSet` owns optional cell fields:

```text
Manning coefficient n   s m^(-1/3)
Coriolis parameter f    1/s
```

The set is move-only, cloneable and bound to one `FiniteVolumeMesh`. Manning
values must be finite and nonnegative. Coriolis values must be finite and may
be signed. Empty source sets are valid and preserve the pre-source solve path.

`RegionalSolveRequest::local_sources` is appended after the stop token. A null
pointer is treated as an empty mesh-bound source set. Non-empty local sources
require the Regional2D physical boundary path so the source-enabled integration
is not mixed with the older scalar-boundary overload.

## Exact Cell Map

For each wet cell, the local source update keeps depth fixed and updates
momentum exactly for the cell-local model:

```text
dq/dt = -k |q| q + f J q
k     = g n^2 / h^(7/3)
```

where `J(qx, qy) = (qy, -qx)` under the implemented sign convention. Over a
substep `dt`:

```text
gamma = 1 / (1 + k |q| dt)
theta = f dt
qx'   = gamma ( cos(theta) qx + sin(theta) qy )
qy'   = gamma (-sin(theta) qx + cos(theta) qy )
```

Dry cells are canonicalised without evaluating `h^(7/3)`.

## Diagnostics

`RegionalStepDiagnostics` now includes `sources`. Source diagnostics report:

```text
manning_active_cell_count
coriolis_active_cell_count
maximum_manning_coefficient
maximum_coriolis_magnitude
maximum_manning_rate
maximum_coriolis_rate
manning_limiting_cell
coriolis_limiting_cell
momentum_x_change
momentum_y_change
initial_kinetic_energy
final_kinetic_energy
friction_kinetic_energy_removed
coriolis_kinetic_energy_error
```

Strang-split integration combines the two half-step source diagnostics by
summing additive quantities and taking maxima for maximum quantities and their
limiting cells.

## Timestep Restriction

`TimestepRestrictionKind` now includes:

```text
source
```

The existing `equal` enumerator remains present. The source timestep estimate
uses:

```text
dt_source = theta_source / max(max_i(g n_i^2 |q_i| / h_i^(7/3)), max_i(|f_i|))
```

No source restriction is emitted when all source rates are zero. Ties within
the comparison tolerance report `multiple` and use deterministic lowest-cell
selection.

## Split Integration

The source-enabled step applies:

```text
source half-step
hydrodynamic explicit step
source half-step
```

The hydrodynamic step remains Forward Euler, SSPRK2 or SSPRK3 according to the
existing policy. Source half-steps stage into workspace-owned conserved states,
so rejected attempts do not mutate the accepted simulation state.

## Benchmarks and CLI

The benchmark catalogue adds:

```text
uniform_manning_decay
uniform_coriolis_oscillation
uniform_manning_coriolis
frictional_wet_dry_dam_break
```

The standalone benchmark accepts:

```text
--manning
--coriolis
--source-safety
```

These flags replace the case source set with uniform mesh-bound fields and
configure the source timestep safety factor.

CSV diagnostics append source columns after the existing relaxation columns so
the prior column order remains stable.

