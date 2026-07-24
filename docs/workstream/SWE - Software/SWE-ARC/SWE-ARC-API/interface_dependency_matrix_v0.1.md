# Interface Dependency Matrix v0.1

- **Work Package:** `SWE-ARC-API-WP1`

| Interface family | Owner | Permitted consumers | Prohibited dependencies |
|---|---|---|---|
| identity/version/result | `tsunami_core` | all runtime targets | Qt, CLI11, Catch2, Eigen, HDF5, GDAL |
| observer/cancellation | `tsunami_core` | application, solvers, coupling, tests | Qt, platform process handles |
| mesh/field views | `tsunami_fvm` | solvers, adapters, tests | Qt, HDF5, GDAL, Eigen public types |
| data/I/O ports | `tsunami_data` | application, adapters, solvers, tests | concrete adapters |
| regional solver | `tsunami_r2d` | application, coupling, tests | Qt, L3D implementation, adapters |
| local solver | `tsunami_l3d` | application, coupling, tests | Qt, R2D implementation, adapters |
| replay/coupling | `tsunami_coupling` | application, tests | GUI, concrete persistence |

`tsunami_coupling` is the sole cross-solver owner. `tsunami_r2d` and `tsunami_l3d` remain independent siblings.
