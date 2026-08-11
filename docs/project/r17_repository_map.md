# R17 Repository Map

This map gives a reviewer a practical route through the repository at R17
closure.

## Start Here

1. `README.md` - public entry point and current status.
2. `docs/project/r17_studentship_status.md` - scientific closure statement.
3. `docs/project/r17_deliverables_register.md` - final deliverable inventory.
4. `docs/project/r17_repository_audit.md` - non-destructive repository audit.

## Scientific Evidence

| Area | Main paths |
| --- | --- |
| Model consistency | `docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/regional2d_model_implementation_traceability.md` |
| R7/R8/R9/R10/R13/R14/R15 evidence | `docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/` |
| Regional figures and convergence graphics | `deliverables/figures/convergence/` |
| R14 hybrid evidence | `deliverables/figures/r14_hybrid/`, `deliverables/video/r14_hybrid/` |
| R15 validation framework figures | `deliverables/figures/r15_validation/publication/` |
| R16 publication GIS package | `deliverables/figures/r16_publication/` |
| R17 final 3D bathymetry figure | `deliverables/figures/r17_closure/` |

## Software

| Path | Purpose |
| --- | --- |
| `src/r2d/` | Regional2D solver implementation. |
| `src/fvm/` | Finite-volume infrastructure. |
| `src/coupling/`, `src/l3d/` | Regional-to-local coupling and Local3D support. |
| `src/r2d_case_runner/` | File-driven Regional2D runner. |
| `apps/r2d_case/` | Production case-runner executable entry point. |
| `tools/verification/convergence/` | R-series convergence, order and fidelity scripts. |
| `tools/openfoam/` | Local3D/OpenFOAM replay generation and diagnostics. |
| `tools/results/` | HDF5, XDMF and ResultDataset tooling. |
| `tools/figures/qgis/` | QGIS publication project generation/export. |
| `tools/figures/blender/` | R17 Blender 3D terrain renderer. |

## Data And Provenance

Large simulation outputs and raw validation data remain outside Git. Checked-in
files record small fixtures, hashes, manifests and figure-ready outputs. The
important rule is that manifests and provenance files are scientific evidence,
not expendable clutter.

## Current Do-Not-Overclaim Boundaries

- Do not describe the real-event Regional2D solution as spatially converged.
- Do not describe the project as historically validated.
- Do not state that no Kamaishi validation data exist; the correct statement is
  that no `DIRECT` comparison exists for the frozen corridor output.
- Do not imply that the accepted G6 Local3D evidence came from R10 h400 forcing.
- Do not describe the R17 3D bathymetry render as a 3D fluid simulation.
