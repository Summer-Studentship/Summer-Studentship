# G6 — Theoretical Hybrid Model Complete

Suggested GitHub issue title: `[G6] Theoretical 2D-3D hybrid model complete`.

## Gate Rule

G6 is merge-ready once every required theoretical-model capability has accepted implementation evidence and each G6 baseline gap below is resolved. Calibration, observational validation and convergence studies are explicitly outside this gate.

## Required Capabilities

### Regional2D mathematical model

- depth-averaged NLSWE
- bathymetry/topography
- wet/dry treatment
- gravity
- Manning friction
- numerical flux and time integration
- Regional2D boundaries
- runtime physical acceptance

### Earthquake source

- real USGS finite-fault input
- projected-grid displacement generation
- vertical seabed displacement
- passive free-surface transfer
- source provenance

### Geospatial corridor

- epicentre
- Kamaishi target/interface
- projected CRS
- corridor construction
- terrain conditioning
- tagged mesh
- nearshore interface

### Coupling

- versioned regional export
- coordinate and datum conventions
- spatial mapping
- temporal handling
- 3D inlet reconstruction
- normal and tangential velocity
- free-surface/alpha reconstruction
- dry treatment
- discharge preservation

### Local3D mathematical model

- incompressible immiscible two-phase Navier-Stokes
- VOF free surface
- URANS
- k-omega SST
- pressure-velocity coupling
- wall functions
- coupling-enabled inlet
- outlet
- lateral boundary policy
- atmospheric top
- terrain and barrier walls
- adaptive timestep/CFL controls

### End-to-end execution

- real-data Regional2D run
- coupling export
- complete replay window
- no-defence Local3D run
- rigid-wall Local3D run
- finite and bounded fields
- force and probe output
- reproducible command

## Exclusions

- observational calibration
- observation validation
- mesh-convergence study
- timestep-convergence study
- HDF5 result container
- XDMF/VTKHDF visualisation layer
- CPU/GPU acceleration
- adaptive mesh refinement
- official CAD barrier geometry
- buildings
- full impact metric suite
- obstacle/dissipating barrier comparison
- GUI
- FSI
- scour
- structural damage
- machine learning
- publication figures

## Resolved G6 Baseline Gaps

- `G6-L3D-BC-001`: status `resolved`; acceptance `real_kamaishi_acceptance_passed`.
- `G6-L3D-BC-002`: status `resolved`; acceptance `real_kamaishi_acceptance_passed`.
- `G6-L3D-WLF-001`: status `resolved`; acceptance `real_kamaishi_acceptance_passed`.
- `G6-L3D-TIM-001`: status `resolved_for_g6_baseline`; acceptance `real_kamaishi_acceptance_passed`.

## R2 Evidence Update

- Replay schemas supported: `1.0.0` legacy `symmetry_test`; `1.1.0` production `open_ocean_damped`.
- Production boundary policy: patch-type outlet and laterals with `pressureInletOutletVelocity`, `prghTotalPressure`, bounded `variableHeightFlowRate`, ambient `inletOutlet` turbulence and `calculated` `nut`.
- Production damping: Foundation 11 `isotropicDamping` with `halfCosineRamp` outlet and lateral zones.
- Production wall policy: `kqRWallFunction`, `omegaWallFunction`, `nutUSpaldingWallFunction`, plus the Foundation 11 `yPlus` function object.
- Timestep disposition: repository owns pre-run maxDeltaT/maxCo/maxAlphaCo/minimum-timestep policy and post-run acceptance; Foundation 11 owns internal Courant/interface-Courant adaptive reduction and damped increase; exact rollback/retry remains post-G6.
- Closure evidence index: `docs/workstream/wbs-reconciliation/g6_closure_evidence.json`.
- Real Kamaishi acceptance: `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/evidence/g6_openfoam_acceptance.json`.

## Current Gate Status

G6 is merge-ready but is not closed by this branch before PR #274 merges. The
accepted evidence includes exact G5/G6 Regional2D prefix equivalence over
`0-600 s` and complete `300 s` no-defence and simple rigid barrier Local3D
acceptance. Calibration remains unstarted.
