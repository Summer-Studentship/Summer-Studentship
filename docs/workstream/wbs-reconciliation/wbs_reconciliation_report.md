# WBS Reconciliation Report

Baseline: `1ba6475 feat(g5): add Kamaishi hybrid delivery pipeline (#271)`.

This audit reconciles the GitHub WBS against the implemented real-data hybrid execution through the adopted OpenFOAM Foundation 11 Local3D backend. Calibration was not started and the numerical model was not modified.

## Inventory

- WBS issues audited: 233
- Hierarchy levels reconstructed: 5
- G6-required issues/items: 88
- Post-G6 issues/items: 28
- Excluded from current scope: 117

## Disposition Summary

- `complete_adopted_backend`: 5
- `complete_native`: 126
- `deferred`: 27
- `not_started`: 12
- `partial`: 63
- `superseded`: 0

## OpenFOAM Adoption

OpenFOAM Foundation 11 is treated as an adopted numerical backend for the Local3D incompressible two-phase URANS-VOF baseline. The repository owns the coupling reconstruction, generated dictionaries, execution wrapper, acceptance validation, extraction and provenance. The backend owns VOF transport, momentum discretisation, pressure-velocity coupling, k-omega SST transport, wall-function evaluation and transient solver sequencing.

Decision record: `docs/workstream/RES - Research/decisions/openfoam_local3d_backend_adoption.md`.

## Issues To Close As Adopted Backend

- #56 `SWE-L3D-VOF` [SWE-L3D-VOF] Bounded compressive VOF transport, phase-state update and mixture-property update.
- #57 `SWE-L3D-MOM` [SWE-L3D-MOM] Collocated cell-centred URANS momentum-predictor discretisation.
- #58 `SWE-L3D-PRS` [SWE-L3D-PRS] Rhie--Chow pressure-consistent face fluxes and PISO-like pressure, velocity and flux correction.
- #59 `SWE-L3D-SST` [SWE-L3D-SST] `k`--`omega` SST turbulence transport and turbulent-viscosity update.
- #63 `SWE-L3D-SOL` [SWE-L3D-SOL] Exact transient segregated URANS--VOF solve sequence and field-acceptance loop.

## Issues To Close As Native Coupling

- #65 `SWE-CPL-IFC` [SWE-CPL-IFC] Regional/local state, coordinate, convention and interface specification.
- #66 `SWE-CPL-RPL` [SWE-CPL-RPL] Versioned regional-output and local replay-boundary format.
- #67 `SWE-CPL-MAP` [SWE-CPL-MAP] Spatial mapping and temporal interpolation from regional output to the local interface.
- #68 `SWE-CPL-BC` [SWE-CPL-BC] Reconstruction of three-dimensional local inlet conditions from depth-averaged regional variables.

## Issues Retained As Partial

- #8 `SWE-L3D`: production boundary, wall-function and timestep evidence at baseline G6 level, full Local3D wall/load validation remains post-G6
- #9 `SWE-CPL`: full impact metrics, barrier-class comparison beyond simplified rigid wall
- #60 `SWE-L3D-WLF`: y+ evidence, wall-function applicability, roughness policy, mesh-resolution consistency, load sensitivity
- #61 `SWE-L3D-BC`: production lateral open-ocean treatment, reflection assessment, boundary-condition benchmark evidence, clear distinction between symmetry test mode and open-ocean mode
- #62 `SWE-L3D-TIM`: diffusive timestep constraint evidence, step-rejection/recovery disposition, formal timestep-convergence evidence
- #64 `SWE-L3D-FRC`: surface pressure distribution, wall shear distribution, traction-vector output, impulse integration, load convergence, approved validation comparison
- #69 `SWE-CPL-MET`: run-up, overtopping, transmission coefficient, impulse, energy metrics, approved validation context
- #70 `SWE-CPL-CMP`: wall-type versus obstacle/dissipating barrier comparison, second barrier-class geometry, identical-forcing comparison, comparison metrics

## Deferred Or Post-G6 Scope

- GUI remains deferred and excluded from the current gate.
- HPC remains open and post-G6 for MPI scaling, CPU affinity and GPU feasibility.
- HDF5/XDMF, convergence, validation, FSI/scour/structural damage, GUI and publication outputs are excluded from G6.

## Remaining G6 Blockers

- `G6-L3D-BC-001` (SWE-L3D-BC): Define the production Local3D lateral open-ocean boundary policy instead of relying only on symmetryPlane test-mode sides.
- `G6-L3D-BC-002` (SWE-L3D-BC): Provide boundary-reflection evidence for the selected Local3D outlet/lateral treatment.
- `G6-L3D-WLF-001` (SWE-L3D-WLF): Record baseline wall-function applicability evidence, including y+ or an approved surrogate rationale.
- `G6-L3D-TIM-001` (SWE-L3D-TIM): Formally dispose of timestep rejection/recovery and diffusion-constraint coverage under the OpenFOAM adopted backend.

## Verification

- `python3 -m json.tool <every-new-json-file>`: passed for all generated JSON files.
- `python3 tools/project-management/validate_wbs_reconciliation.py`: passed.
- `git diff --check`: passed.
- `python3 -m unittest tests.openfoam.test_openfoam_replay`: passed, 16 tests.
- `python3 -m unittest` for 10 dependency-free `tests.cases.test_kamaishi_delivery` cases: passed.
- `python3 -m unittest tests.openfoam.test_openfoam_replay tests.cases.test_kamaishi_delivery tests.cases.test_tohoku_producer_crs`: not fully runnable in this environment because optional preprocessing dependencies `pyproj` and `rasterio` are not installed.

## Live GitHub Mutation Status

The dry-run mutation plan validated locally and was applied through the GitHub CLI. Issues #56, #57, #58, #59, #63, #65, #66, #67 and #68 were closed as completed after evidence comments were posted. Issues #60, #61, #62, #64, #69, #70, #8 and #9 received partial-status comments and remain open.

The G6 milestone exists as milestone #7, and the G6 gate issue exists as #273. Details are recorded in `wbs_mutation_result.json`.

GitHub Project item/field/view mutation remains blocked because the current GitHub token lacks Project scope. Manual Project update instructions are recorded in `github_project_update.md`.
