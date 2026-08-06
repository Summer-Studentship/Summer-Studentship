# G6 — Theoretical Hybrid Model Complete

Suggested GitHub issue title: `[G6] Theoretical 2D-3D hybrid model complete`.

## Gate Rule

G6 is blocked until every required theoretical-model capability has accepted implementation evidence and each remaining G6 gap below is resolved. Calibration, observational validation and convergence studies are explicitly outside this gate.

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

## Remaining G6 Gaps

- `G6-L3D-BC-001`: Define the production Local3D lateral open-ocean boundary policy instead of relying only on symmetryPlane test-mode sides.
- `G6-L3D-BC-002`: Provide boundary-reflection evidence for the selected Local3D outlet/lateral treatment.
- `G6-L3D-WLF-001`: Record baseline wall-function applicability evidence, including y+ or an approved surrogate rationale.
- `G6-L3D-TIM-001`: Formally dispose of timestep rejection/recovery and diffusion-constraint coverage under the OpenFOAM adopted backend.
