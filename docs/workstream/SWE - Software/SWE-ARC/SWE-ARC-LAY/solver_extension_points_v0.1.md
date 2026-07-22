# Solver Extension Points v0.1

- **Work Package:** `SWE-ARC-LAY-WP1`
- **Issues:** `#126`, `#129`
- **Document date:** 2026-07-22

This document records extension categories only. Concrete C++ interface names
belong to `SWE-ARC-API-WP1`.

## Regional 2D Solver: `tsunami_r2d`

| Extension category | Owning target | Inward dependencies | Data consumed | Data produced | Multiple implementations? | Prohibited exposure | Related WBS | Downstream API WP |
|---|---|---|---|---|---|---|---|---|
| state/admissibility policy | `tsunami_r2d` | core, fvm, data | regional state and constraints | accepted/rejected state | Yes | Qt, HDF5, GDAL | `SWE-R2D-STA` | `SWE-ARC-API-WP1` |
| numerical-flux policy | `tsunami_r2d` | core, fvm | face states, normals, parameters | flux contributions | Yes | UI/persistence types | `SWE-R2D-FLX` | `SWE-ARC-API-WP1` |
| reconstruction and limiter policy | `tsunami_r2d` | fvm | fields, topology | reconstructed states | Yes | adapter types | `SWE-R2D-FLX` | `SWE-ARC-API-WP1` |
| well-balancing treatment | `tsunami_r2d` | fvm, data | bathymetry and state | balanced source/flux terms | Yes | GDAL/HDF5 | `SWE-R2D-WB` | `SWE-ARC-API-WP1` |
| wetting/drying treatment | `tsunami_r2d` | fvm | depth/state fields | admissible wet/dry state | Yes | GUI types | `SWE-R2D-WD` | `SWE-ARC-API-WP1` |
| source-term contribution | `tsunami_r2d` | data, fvm | source parameters | residual/source increments | Yes | persistence adapters | `SWE-R2D-SRC` | `SWE-ARC-API-WP1` |
| boundary-condition implementation | `tsunami_r2d` | fvm, data | patch metadata, state | boundary flux/state | Yes | Qt/GDAL | `SWE-R2D-BC` | `SWE-ARC-API-WP1` |
| sponge/relaxation treatment | `tsunami_r2d` | fvm, data | relaxation zones | source increments | Yes | adapter types | `SWE-R2D-BC` | `SWE-ARC-API-WP1` |
| time-integration policy | `tsunami_r2d` | fvm | residuals, state | advanced state | Yes | UI/persistence types | `SWE-R2D-TIM` | `SWE-ARC-API-WP1` |
| CFL/timestep policy | `tsunami_r2d` | fvm | mesh, wave speeds | timestep proposal | Yes | Qt | `SWE-R2D-TIM` | `SWE-ARC-API-WP1` |
| earthquake/source initialisation | `tsunami_r2d` | data, fvm | accepted source records | initial state | Yes | GDAL/HDF5 concrete types | `SWE-R2D-EQK` | `SWE-ARC-API-WP1` |
| diagnostic observer | `tsunami_r2d` | core | residuals and run metrics | neutral diagnostic records | Yes | Qt/Matplot++ | `SWE-R2D-SOL`, `SWE-VER-ACC` | `SWE-ARC-API-WP1` |
| result/output observer | `tsunami_r2d` | data contracts | fields and metadata | neutral output records | Yes | HDF5 concrete APIs | `SWE-R2D-SOL`, `SWE-DAT-SCH` | `SWE-ARC-API-WP1` |

Generic discrete operators remain in `tsunami_fvm`; R2D-specific algorithms
remain in `tsunami_r2d`; configuration selections originate from `tsunami_data`;
concrete persistence remains outside the solver.

## Local 3D Solver: `tsunami_l3d`

| Extension category | Owning target | Inward dependencies | Data consumed | Data produced | Multiple implementations? | Prohibited exposure | Related WBS |
|---|---|---|---|---|---|---|---|
| VOF advection/compression policy | `tsunami_l3d` | core, fvm, data | phase field, fluxes | bounded phase update | Yes | UI/persistence types | `SWE-L3D-VOF` |
| mixture-property update | `tsunami_l3d` | fvm, data | phase state | material properties | Yes | Qt/HDF5 | `SWE-L3D-VOF` |
| momentum discretisation policy | `tsunami_l3d` | fvm | velocity, pressure, properties | momentum residual | Yes | adapter types | `SWE-L3D-MOM` |
| pressure/velocity coupling policy | `tsunami_l3d` | fvm | predicted velocity, pressure | corrected fields | Yes | GUI types | `SWE-L3D-PRS` |
| Rhie-Chow interpolation | `tsunami_l3d` | fvm | cell fields, face geometry | pressure-consistent flux | Yes | external libraries in API | `SWE-L3D-PRS` |
| PISO corrector policy | `tsunami_l3d` | fvm | residuals and fields | corrected state | Yes | Qt/HDF5 | `SWE-L3D-PRS` |
| turbulence model | `tsunami_l3d` | fvm, data | local flow fields | turbulence fields | Yes | adapter types | `SWE-L3D-SST` |
| wall treatment | `tsunami_l3d` | fvm, geo contracts | wall patches | wall fluxes/stresses | Yes | Gmsh/GDAL types | `SWE-L3D-WLF` |
| boundary-condition implementation | `tsunami_l3d` | fvm, data | patch and forcing records | boundary state/flux | Yes | GUI types | `SWE-L3D-BC` |
| timestep-constraint policy | `tsunami_l3d` | fvm | CFL/interface/diffusion data | timestep proposal | Yes | UI types | `SWE-L3D-TIM` |
| field acceptance/rejection policy | `tsunami_l3d` | core, fvm | candidate fields | accepted/rejected state | Yes | persistence APIs | `SWE-L3D-TIM` |
| force and moment extraction | `tsunami_l3d` | fvm, geo | pressure/shear fields | load records | Yes | Matplot++ | `SWE-L3D-FRC` |
| diagnostic observer | `tsunami_l3d` | core | solver metrics | neutral diagnostics | Yes | Qt/Matplot++ | `SWE-L3D-SOL` |
| result/output observer | `tsunami_l3d` | data contracts | fields and metadata | neutral output records | Yes | HDF5 concrete APIs | `SWE-L3D-SOL` |

Shared mesh/field/operator infrastructure remains in `tsunami_fvm`. Coupling
forced-inlet reconstruction is owned by `tsunami_coupling`. Solver-owned
force/moment extraction feeds coupling-owned cross-case comparison.

## Coupling: `tsunami_coupling`

| Extension category | Owning target | Inward dependencies | Data consumed | Data produced | Multiple implementations? | Prohibited exposure | Related WBS |
|---|---|---|---|---|---|---|---|
| replay reader/writer contract | `tsunami_coupling` | core, data | replay metadata | replay records | Yes | HDF5 concrete APIs | `SWE-CPL-RPL` |
| regional/local coordinate-convention contract | `tsunami_coupling` | data, fvm | solver conventions | mapping conventions | Yes | GUI/GDAL types | `SWE-CPL-IFC` |
| spatial mapping policy | `tsunami_coupling` | fvm, data | regional/local meshes | mapped values | Yes | adapter APIs | `SWE-CPL-MAP` |
| temporal interpolation policy | `tsunami_coupling` | data | time series | interpolated forcing | Yes | persistence APIs | `SWE-CPL-MAP` |
| depth-averaged to 3D inlet reconstruction | `tsunami_coupling` | r2d, l3d contracts | regional state | local inlet records | Yes | GUI types | `SWE-CPL-BC` |
| boundary forcing provider | `tsunami_coupling` | l3d contracts | inlet records | forcing sequence | Yes | UI/persistence types | `SWE-CPL-BC` |
| metric extraction | `tsunami_coupling` | r2d, l3d, data | fields/load records | metrics | Yes | Matplot++ | `SWE-CPL-MET` |
| barrier comparison runner | `tsunami_coupling` | data, solver contracts | case variants | comparison records | Yes | GUI workflow | `SWE-CPL-CMP` |
| regional/local compatibility validation | `tsunami_coupling` | core, data | solver metadata | validation results | Yes | Qt | `SWE-CPL-IFC` |
| diagnostic observer | `tsunami_coupling` | core | coupling progress/metrics | neutral diagnostics | Yes | Qt/Matplot++ | `SWE-CPL-*` |

Coupling may consume regional and local contracts. R2D and L3D do not implement
cross-model mapping. GUI consumes application-level status and results only.
Persistence implementation remains adapter-owned.
