# Regional Earthquake Initialisation v0.1

## Scope

This note records the `SWE-R2D-EQK` Regional2D earthquake-initialisation
increment.

The implementation adds mesh-bound seabed displacement contracts, earthquake
source provenance metadata, bed-deformation mapping, free-surface transfer,
post-event bathymetry/state construction, diagnostics and CSV output. It does
not add Okada evaluation, finite-fault parsing, production earthquake datasets,
dynamic bottom motion, GUI controls, Local3D coupling or new Python validators.

## Displacement Contract

`RegionalSeabedDisplacement` owns three immutable cell fields:

```text
eastward displacement   m
northward displacement  m
upward displacement     m
```

All components are bound to exactly one `FiniteVolumeMesh`, must have one finite
value per cell and use metre units. The value object is move-only and cloneable,
matching the Regional2D convention for mesh-bound solver inputs.

Factory helpers cover full three-component construction, vertical-only
construction, filled synthetic fields and zero displacement fields.

## Source Provenance

`RegionalEarthquakeSourceMetadata` records:

```text
source_kind
event_id
model_id
source_format
coordinate_reference
subfault_count
```

The current source kinds are `synthetic`, `gridded_displacement` and
`finite_fault`. Required strings must be non-empty and contain no embedded null
characters. `finite_fault` metadata requires at least one subfault. The metadata
is preserved in diagnostics and CSV output so generated initial states remain
traceable even when the displacement was produced externally.

## Bed Mapping

`calculate_effective_seabed_displacement` maps the 3-component displacement to
the scalar bed-elevation change used by the 2D solver:

```text
vertical_only:
  dz_eff = dz

horizontal_slope_corrected:
  dz_eff = dz - dx * dB/dx - dy * dB/dy
```

The horizontal-slope-corrected path uses the existing finite-volume linear
interpolation and Green-Gauss gradient workspaces. Bathymetry boundary data is
mandatory for this mapping so the gradient has a complete boundary stencil.
Destination fields are updated transactionally; failed validation leaves the
destination unchanged.

## Surface Transfer

`initialise_regional_earthquake_state` supports two free-surface transfer modes:

```text
passive_equal_to_effective_bed:
  eta+ = eta0 + dz_eff

prescribed:
  eta+ = eta0 + prescribed_surface_perturbation
```

Passive transfer preserves cell depth, total water volume and wet/dry
classification within tolerance. Prescribed transfer accepts an independent
mesh-bound metre-valued perturbation field and may intentionally create wet/dry
changes through the post-event depth clamp.

## Post-Event State

The initialised state uses:

```text
bed+   = bed0 + dz_eff
eta0   = bed0 + depth0
eta+   = eta0 + surface_perturbation
depth+ = max(0, eta+ - bed+)
qx+    = 0
qy+    = 0
time   = 0
step   = 0
```

Pre-event momentum must be zero within the configured tolerance. Any small
roundoff accepted by the tolerance is canonicalised to zero in the resulting
state.

## Diagnostics and CSV

`RegionalEarthquakeInitialisationDiagnostics` records source metadata, mapping
and transfer modes, cell counts, displacement extrema and integrals, pre/post
water volume, water-volume change, wet/dry counts and maximum post-event
momentum.

`RegionalCsvOutputWriter` writes `earthquake_initialisation.csv` when a
benchmark case carries earthquake diagnostics. The standalone benchmark also
prints a machine-readable `earthquake_*` summary line.

## Synthetic Benchmarks

The benchmark catalogue adds:

```text
earthquake_uniform_vertical_translation
earthquake_localised_vertical_uplift
earthquake_uplift_subsidence_dipole
earthquake_horizontal_slope_correction
earthquake_prescribed_surface_perturbation
```

The cases exercise vertical passive transfer, compact uplift propagation,
balanced uplift/subsidence, horizontal-slope correction over a planar bed and
prescribed free-surface perturbation across wet/dry cells.
