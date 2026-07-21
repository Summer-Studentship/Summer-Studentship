# Studentship Software Workstream WBS

**Version:** 0.3  
**Date:** 2026-07-19  
**Status:** Confirmed Domain/Deliverable baseline with authorised G0/G1 decomposition  
**Workstream code:** `SWE`  
**Hierarchy:** Workstream -> Domain -> Deliverable -> Work Package -> Task  
**Current decomposition depth:** All Domains and Deliverables; Work Packages and Tasks only for G0 and G1.

---

## 1. Purpose

This WBS defines the Software workstream required to implement the research-selected tsunami modelling methodology while the remaining Research work continues.

The active software baseline is:

1. a custom C++ regional two-dimensional, depth-averaged, unstructured finite-volume NLSWE solver;
2. a custom C++ local three-dimensional, incompressible, immiscible, two-phase URANS--VOF finite-volume solver;
3. one-way regional-to-local replay coupling;
4. evidence-driven Kamaishi and Sendai corridor preprocessing;
5. fixed terrain and fixed rigid barrier-class comparison;
6. hydrodynamic impact extraction, including run-up, overtopping, pressure, force, moment, impulse, transmission and energy metrics;
7. HDF5-based storage with XDMF/ParaView interoperability;
8. a Qt Quick/QML application around Qt-independent numerical and data libraries.

Deferred or stretch scope:

- machine learning, surrogate modelling, generative geometry and full optimisation;
- deformable structural response, material damage, scour and two-way FSI;
- LES as a production baseline;
- MPI and GPU acceleration before verified serial implementations exist;
- OpenFOAM as the production solver.

---

## 2. Ownership rules

The Software domains have the following non-overlapping ownership:

- `SWE-ENV` owns toolchains, packages, dependency acquisition and reproducible builds.
- `SWE-ARC` owns architecture, target boundaries, lifecycle contracts and application orchestration.
- `SWE-FVM` owns the shared finite-volume computational data model and reusable discrete infrastructure.
- `SWE-DAT` owns case configuration, dataset manifests, scientific-data records and persistent I/O schemas.
- `SWE-GEO` owns geospatial preprocessing, corridor construction, terrain/barrier representation and mesh adapters.
- `SWE-R2D` owns regional NLSWE-specific algorithms.
- `SWE-L3D` owns local URANS--VOF-specific algorithms.
- `SWE-CPL` owns regional/local replay, mapping, inlet reconstruction and cross-case impact comparison.
- `SWE-GUI` owns the Qt application and user-facing workflows.
- `SWE-VER` owns software verification, validation-support harnesses and numerical acceptance diagnostics.
- `SWE-HPC` owns performance optimisation after verified serial baselines.
- `SWE-STR` owns later structural response and FSI extensions.
- `SWE-REL` owns CI, documentation, packaging and reproducible releases.

Research remains authoritative for the physical models, numerical-method decisions, data-source selection, geometry classes and validation criteria. Software implements those accepted decisions.

---

## 3. Domain baseline

| Domain | Purpose | Scope class | Current state |
|---|---|---|---|
| `SWE-ENV` — Toolchain, Packages and Builds | Establish compilers, CMake, dependency management, licences and reproducible builds. | Active baseline | Start immediately |
| `SWE-ARC` — Architecture and Contracts | Define layering, ownership, stable interfaces, case lifecycle and application orchestration. | Active baseline | Start immediately |
| `SWE-FVM` — Shared Finite-Volume Core | Provide common mesh, field, boundary, operator, linear-system and runtime-control infrastructure. | Active baseline | Start immediately |
| `SWE-DAT` — Configuration, Data and I/O | Define case files, dataset manifests, scientific-data records, HDF5 schemas and restart data. | Active baseline | Start immediately |
| `SWE-GEO` — Geospatial, Terrain and Geometry Pipeline | Produce reproducible corridors, topobathymetry, barrier geometry and tagged solver meshes. | Active baseline | Start immediately |
| `SWE-R2D` — Regional 2D Solver | Implement the regional finite-volume NLSWE model. | Active baseline | After shared core/data slice |
| `SWE-L3D` — Local 3D Solver | Implement the transient segregated URANS--VOF local impact model. | Active baseline | After shared core and regional interfaces |
| `SWE-CPL` — Replay Coupling and Impact Analysis | Transfer regional results into the local model and execute barrier comparisons. | Active baseline | After regional/local interfaces stabilise |
| `SWE-GUI` — Application and Visualisation | Implement case editing, run control, diagnostics, previews and result access. | Active baseline | Shell early; integration staged |
| `SWE-VER` — Verification and Validation Support | Implement automated tests, benchmarks, convergence studies and runtime acceptance checks. | Continuous baseline | Continuous |
| `SWE-HPC` — Performance and Parallelism | Profile and introduce justified acceleration without changing the mathematical interfaces. | Deferred | After serial verification |
| `SWE-STR` — Structural Response and FSI | Extend stable hydrodynamic loading into deformable structural response and feedback. | Stretch | Only after load extraction is stable |
| `SWE-REL` — CI, Documentation and Deployment | Automate builds/tests and produce documented, reproducible application releases. | Continuous baseline | Basic CI early; packaging later |

**Confirmed Domain count:** 13

---

## 4. Deliverables by Domain

### 4.1 `SWE-ENV` — Toolchain, Packages and Builds

- `SWE-ENV-STACK` — Software stack and dependency decision register.
- `SWE-ENV-BLD` — Target-based CMake build system.
- `SWE-ENV-DEP` — Reproducible dependency manifest and version baseline.
- `SWE-ENV-PRS` — Shared configure, build and test presets.
- `SWE-ENV-LIC` — Dependency licence and redistribution register.
- `SWE-ENV-SMK` — Clean-clone dependency and build smoke test.

### 4.2 `SWE-ARC` — Architecture and Contracts

- `SWE-ARC-TGT` — CMake target and dependency graph.
- `SWE-ARC-LAY` — Core, application, adapter and UI layer boundaries.
- `SWE-ARC-CASE` — Simulation-case lifecycle and directory convention.
- `SWE-ARC-API` — Mesh, field, solver, I/O, coupling and observer interfaces.
- `SWE-ARC-SVC` — Shared application-service and run-orchestration contract for CLI and GUI.
- `SWE-ARC-ERR` — Error, status, logging, cancellation and failure-propagation model.

### 4.3 `SWE-FVM` — Shared Finite-Volume Core

- `SWE-FVM-MSH` — Dimension-independent control-volume topology and discrete geometry model, including cells, faces, owner/neighbour connectivity, patches, centroids, volumes, face areas and normals.
- `SWE-FVM-FLD` — Typed scalar, vector and tensor field containers with cell-, face- and patch-location semantics.
- `SWE-FVM-BC` — Generic boundary-patch and boundary-condition framework.
- `SWE-FVM-NUM` — Shared interpolation, reconstruction, limiter, gradient, divergence and Laplacian infrastructure.
- `SWE-FVM-LIN` — Sparse linear-system, solver and preconditioner abstraction.
- `SWE-FVM-CTL` — Common residual, corrector, timestep-control and accept/reject infrastructure.

### 4.4 `SWE-DAT` — Configuration, Data and I/O

- `SWE-DAT-CFG` — Versioned simulation-case configuration schema and validation.
- `SWE-DAT-MAN` — Versioned dataset and provenance manifest covering provider, source, licence, resolution, CRS, vertical datum, preprocessing and generated artefacts.
- `SWE-DAT-ING` — Canonical ingestion records for earthquake-source, observation, gauge, run-up, inundation and other scientific datasets not owned by the geospatial raster/vector adapters.
- `SWE-DAT-SCH` — Versioned HDF5 result, diagnostic and checkpoint schema.
- `SWE-DAT-XDMF` — XDMF descriptors for ParaView-compatible inspection.
- `SWE-DAT-CHK` — Restart/checkpoint read, write and compatibility implementation.

### 4.5 `SWE-GEO` — Geospatial, Terrain and Geometry Pipeline

- `SWE-GEO-IMP` — GeoTIFF, NetCDF and vector-source import adapters.
- `SWE-GEO-CRS` — Horizontal CRS, vertical datum and coordinate-transformation handling with provenance.
- `SWE-GEO-COR` — Evidence-driven Kamaishi and Sendai trajectory/corridor construction, clipping and intermediate-artefact export.
- `SWE-GEO-TER` — Bathymetry/topography conditioning, merging, resampling and interpolation to the computational representation.
- `SWE-GEO-MSH` — Reproducible mesh-generation/import workflow and transfer into `SWE-FVM-MSH`.
- `SWE-GEO-TAG` — Boundary, terrain, barrier, material and region tagging.
- `SWE-GEO-BAR` — Method-neutral fixed terrain and rigid barrier-class descriptors, placement and computational representation.
- `SWE-GEO-CHK` — Corridor, terrain, geometry and mesh-validity/quality checks.

### 4.6 `SWE-R2D` — Regional 2D Solver

- `SWE-R2D-STA` — Conservative state, primitive-variable recovery and state admissibility.
- `SWE-R2D-FLX` — Rusanov baseline numerical flux with controlled HLL/HLLC extension points.
- `SWE-R2D-WB` — Hydrostatic reconstruction and lake-at-rest well balancing.
- `SWE-R2D-WD` — Positivity preservation and wetting--drying treatment.
- `SWE-R2D-SRC` — Bathymetry, Manning friction, Coriolis and accepted optional source terms.
- `SWE-R2D-BC` — Open/radiation boundaries and sponge/relaxation layers.
- `SWE-R2D-TIM` — Regional CFL control and accepted explicit time integration.
- `SWE-R2D-EQK` — Earthquake, finite-fault and seabed-deformation initialisation interface.
- `SWE-R2D-SOL` — Integrated regional solve loop, diagnostics and regional-output production.

### 4.7 `SWE-L3D` — Local 3D Solver

- `SWE-L3D-VOF` — Bounded compressive VOF transport, phase-state update and mixture-property update.
- `SWE-L3D-MOM` — Collocated cell-centred URANS momentum-predictor discretisation.
- `SWE-L3D-PRS` — Rhie--Chow pressure-consistent face fluxes and PISO-like pressure, velocity and flux correction.
- `SWE-L3D-SST` — `k`--`omega` SST turbulence transport and turbulent-viscosity update.
- `SWE-L3D-WLF` — Scalable wall-function treatment for terrain and barrier walls.
- `SWE-L3D-BC` — Regional-forced inlet, open/radiation outlet, lateral open-ocean, atmospheric-top, terrain and barrier boundary conditions.
- `SWE-L3D-TIM` — CFL, interface-CFL, gravity-wave and diffusion timestep constraints with timestep rejection.
- `SWE-L3D-SOL` — Exact transient segregated URANS--VOF solve sequence and field-acceptance loop.
- `SWE-L3D-FRC` — Barrier pressure, shear, traction, force and moment integration.

### 4.8 `SWE-CPL` — Replay Coupling and Impact Analysis

- `SWE-CPL-IFC` — Regional/local state, coordinate, convention and interface specification.
- `SWE-CPL-RPL` — Versioned regional-output and local replay-boundary format.
- `SWE-CPL-MAP` — Spatial mapping and temporal interpolation from regional output to the local interface.
- `SWE-CPL-BC` — Reconstruction of three-dimensional local inlet conditions from depth-averaged regional variables.
- `SWE-CPL-MET` — Run-up, overtopping, transmission, pressure, force, moment, impulse and energy metric implementation.
- `SWE-CPL-CMP` — Reproducible wall-type versus obstacle/dissipating barrier comparison runner.

### 4.9 `SWE-GUI` — Application and Visualisation

- `SWE-GUI-SHL` — Qt Quick/QML application shell and navigation structure.
- `SWE-GUI-CAS` — Data, corridor, domain, mesh, physics, solver and output case editor.
- `SWE-GUI-RUN` — Headless-process launch, monitoring, cancellation and run-state control.
- `SWE-GUI-LOG` — Structured log, warning, residual and diagnostic presentation.
- `SWE-GUI-VIS` — Lightweight terrain/mesh/field preview and ParaView hand-off.
- `SWE-GUI-POST` — Result catalogue, case comparison and metric-history views.

### 4.10 `SWE-VER` — Verification and Validation Support

- `SWE-VER-UNIT` — Unit tests for data structures, geometry, operators, schemas and solver components.
- `SWE-VER-ACC` — Runtime acceptance checks for mass conservation, bounded volume fraction, positive turbulence variables, residual tolerances, CFL limits and timestep acceptance.
- `SWE-VER-BMK` — Regional and local solver-capability benchmark suite.
- `SWE-VER-REG` — Regression, restart reproducibility, schema round-trip and reference-result tests.
- `SWE-VER-CONV` — Mesh- and timestep-convergence study harness.
- `SWE-VER-VAL` — Observation, Tohoku hydrodynamic and impact/loading comparison harness; execution and acceptance remain governed by `RES-VER`.

### 4.11 `SWE-HPC` — Performance and Parallelism

- `SWE-HPC-PROF` — Serial performance, memory and scalability baseline.
- `SWE-HPC-SIMD` — Vectorisation and data-layout optimisation.
- `SWE-HPC-OMP` — Shared-memory parallel execution.
- `SWE-HPC-MPI` — Distributed-memory decomposition and execution.
- `SWE-HPC-GPU` — GPU execution backend after profiling and interface stabilisation.
- `SWE-HPC-AIO` — Bounded asynchronous and parallel I/O optimisation.

### 4.12 `SWE-STR` — Structural Response and FSI

- `SWE-STR-LOD` — Fluid-to-structure hydrodynamic load-transfer interface.
- `SWE-STR-MAT` — Material and structural-model adapter.
- `SWE-STR-FEM` — Deformable structural discretisation and solver.
- `SWE-STR-DMG` — Damage, failure and material-response extension.
- `SWE-STR-FSI` — Two-way fluid--structure coupling and feedback.

### 4.13 `SWE-REL` — CI, Documentation and Deployment

- `SWE-REL-CI` — Automated configure, build, test and artefact workflows.
- `SWE-REL-PKG` — Cross-platform application and runtime packaging.
- `SWE-REL-DOC` — Developer, user, case-schema and scientific-software documentation.
- `SWE-REL-REP` — Versioned reproducibility bundle containing configuration, manifests, environment, test evidence and release artefacts.

**Confirmed Deliverable count:** 83

---

## 5. Revision from v0.1

The following changes are accepted in v0.2:

1. Added `SWE-FVM` as a dedicated shared finite-volume core.
2. Moved the generic field/state model from `SWE-DAT-FLD` to `SWE-FVM-FLD`.
3. Consolidated generic regional and local mesh representations into `SWE-FVM-MSH`; `SWE-GEO-MSH` now owns generation/import rather than solver topology.
4. Added `SWE-DAT-CFG`, `SWE-DAT-MAN` and `SWE-DAT-ING` to implement the research-to-software data handoff.
5. Added explicit corridor, terrain-conditioning and method-neutral barrier-representation Deliverables under `SWE-GEO`.
6. Retired `SWE-L3D-IBM` as a premature commitment to immersed boundaries/cut cells; its replacement is the method-neutral `SWE-GEO-BAR`.
7. Expanded the local solver identity to explicitly include Rhie--Chow interpolation, PISO-like pressure correction, `k`--`omega` SST, scalable wall functions, local timestep constraints and the accepted solve sequence.
8. Added `SWE-CPL-RPL` as the versioned regional-to-local replay format.
9. Expanded verification to include runtime numerical acceptance and validation-support harnesses without claiming that validation has been completed.
10. Moved asynchronous I/O optimisation exclusively into deferred `SWE-HPC-AIO`.

---

## 6. Confirmed implementation order

1. `SWE-ENV` + `SWE-ARC`
2. `SWE-FVM` + `SWE-DAT` + `SWE-GEO`
3. `SWE-R2D` + continuous `SWE-VER`
4. staged `SWE-GUI` integration
5. `SWE-L3D` + continuous `SWE-VER`
6. `SWE-CPL`
7. `SWE-HPC` only after profiling verified serial baselines
8. `SWE-STR` only as stretch scope after stable hydrodynamic load extraction

The first implementation slice remains:

`versioned case configuration -> dataset manifest -> tagged Gmsh mesh and sample raster -> internal FVM mesh/field representation -> HDF5/XDMF output -> ParaView inspection -> visibility in the Qt Quick shell`

---

## 7. Baseline decision

The Domain and Deliverable levels in this document are now the authoritative Software WBS baseline for subsequent GitHub Project and Issue creation.

Work Package and Task decomposition is authorised only for G0 and G1 as defined below. Later gates remain at Deliverable level.

---

## 8. G0 and G1 Work Package and Task Decomposition
This section authorises Work Package and Task decomposition only for Gates `G0` and `G1`. All later Deliverables remain at Deliverable level.

### 8.1 `G0` — Repository and Build Baseline

**Objective:** Produce a clean-clone-reproducible C++20/CMake repository with explicit dependency, architecture, diagnostics, test, CI and Qt Quick shell boundaries.

**Exit criteria:**
- A clean clone configures using documented presets and the selected dependency workflow.
- Core library, CLI stub, Qt Quick shell and test targets build without undocumented machine state.
- Architecture, case lifecycle, interfaces, diagnostics and orchestration contracts are versioned.
- A baseline CI workflow configures, builds and runs smoke/unit tests.
- Licence and redistribution obligations are recorded.

**Authorised Work Packages:**

#### `SWE-ENV-STACK-WP1` — Confirm the production software stack

- **Parent Deliverable:** `SWE-ENV-STACK`
- **Dependencies:** None within the authorised gates
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - Every active dependency has a project role, owner, version policy and fallback decision.
- **Tasks:**
  - `SWE-ENV-STACK-WP1-T1` — Map required capabilities to mandatory and optional dependencies.
  - `SWE-ENV-STACK-WP1-T2` — Record selected libraries, versions, alternatives and deferred components.
  - `SWE-ENV-STACK-WP1-T3` — Approve the stack decision register and downstream ownership.

#### `SWE-ENV-BLD-WP1` — Create the target-based CMake scaffold

- **Parent Deliverable:** `SWE-ENV-BLD`
- **Dependencies:** `SWE-ENV-STACK-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - All skeleton targets configure and build without global include/link state.
- **Tasks:**
  - `SWE-ENV-BLD-WP1-T1` — Define the CMake/C++20 baseline and root project configuration.
  - `SWE-ENV-BLD-WP1-T2` — Create library, CLI, Qt application and test target skeletons.
  - `SWE-ENV-BLD-WP1-T3` — Apply target-scoped include paths, compile features and warnings.

#### `SWE-ENV-DEP-WP1` — Establish reproducible dependency acquisition

- **Parent Deliverable:** `SWE-ENV-DEP`
- **Dependencies:** `SWE-ENV-STACK-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - Dependency restoration is deterministic from repository-controlled metadata.
- **Tasks:**
  - `SWE-ENV-DEP-WP1-T1` — Create the dependency manifest and pinned baseline.
  - `SWE-ENV-DEP-WP1-T2` — Separate mandatory, GUI, geospatial, test and optional feature groups.
  - `SWE-ENV-DEP-WP1-T3` — Document supported acquisition and cache behaviour.

#### `SWE-ENV-PRS-WP1` — Define shared configure, build and test presets

- **Parent Deliverable:** `SWE-ENV-PRS`
- **Dependencies:** `SWE-ENV-BLD-WP1`, `SWE-ENV-DEP-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - The documented preset commands reproduce configuration, build and test workflows.
- **Tasks:**
  - `SWE-ENV-PRS-WP1-T1` — Create developer, release and CI configure presets.
  - `SWE-ENV-PRS-WP1-T2` — Create matching build and test presets with platform inheritance.
  - `SWE-ENV-PRS-WP1-T3` — Document preset selection and local override rules.

#### `SWE-ENV-LIC-WP1` — Create the dependency licence register

- **Parent Deliverable:** `SWE-ENV-LIC`
- **Dependencies:** `SWE-ENV-STACK-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - No selected dependency lacks an identified licence and redistribution decision.
- **Tasks:**
  - `SWE-ENV-LIC-WP1-T1` — Capture licence identifier, source and notice requirements for each dependency.
  - `SWE-ENV-LIC-WP1-T2` — Record linking, redistribution and runtime-library implications.
  - `SWE-ENV-LIC-WP1-T3` — Define release-time licence verification.

#### `SWE-ENV-SMK-WP1` — Implement the clean-clone smoke test

- **Parent Deliverable:** `SWE-ENV-SMK`
- **Dependencies:** `SWE-ENV-PRS-WP1`, `SWE-GUI-SHL-WP1`, `SWE-VER-UNIT-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - A clean clone completes configure, build and smoke-test steps using documented commands.
- **Tasks:**
  - `SWE-ENV-SMK-WP1-T1` — Define a clean environment and dependency restoration procedure.
  - `SWE-ENV-SMK-WP1-T2` — Build the core, CLI, Qt shell and test targets from that environment.
  - `SWE-ENV-SMK-WP1-T3` — Record pass/fail evidence and remediate undocumented prerequisites.

#### `SWE-ARC-TGT-WP1` — Lock the target and dependency graph

- **Parent Deliverable:** `SWE-ARC-TGT`
- **Dependencies:** `SWE-ENV-BLD-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - The target graph has no UI-to-core or adapter-to-domain ownership inversion.
- **Tasks:**
  - `SWE-ARC-TGT-WP1-T1` — Define named CMake targets and allowed dependency directions.
  - `SWE-ARC-TGT-WP1-T2` — Identify public/private interfaces and adapter boundaries.
  - `SWE-ARC-TGT-WP1-T3` — Add a machine-checkable or reviewable dependency map.

#### `SWE-ARC-LAY-WP1` — Lock architectural layers and ownership

- **Parent Deliverable:** `SWE-ARC-LAY`
- **Dependencies:** `SWE-ARC-TGT-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - Every v0.2 Domain maps to one owning layer and documented interfaces.
- **Tasks:**
  - `SWE-ARC-LAY-WP1-T1` — Define core, numerical, data, geospatial, application, adapter and UI layers.
  - `SWE-ARC-LAY-WP1-T2` — Write dependency rules and prohibited coupling cases.
  - `SWE-ARC-LAY-WP1-T3` — Record extension points for regional, local and coupling solvers.

#### `SWE-ARC-CASE-WP1` — Define the simulation-case lifecycle

- **Parent Deliverable:** `SWE-ARC-CASE`
- **Dependencies:** `SWE-ARC-LAY-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - A case has a deterministic directory layout and explicit lifecycle transitions.
- **Tasks:**
  - `SWE-ARC-CASE-WP1-T1` — Define case states from creation through validation, execution and archival.
  - `SWE-ARC-CASE-WP1-T2` — Define the canonical case-directory and artefact layout.
  - `SWE-ARC-CASE-WP1-T3` — Define schema-version, provenance and restart compatibility rules.

#### `SWE-ARC-API-WP1` — Define stable core interface contracts

- **Parent Deliverable:** `SWE-ARC-API`
- **Dependencies:** `SWE-ARC-LAY-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - Interfaces expose no Qt types in numerical/data core contracts.
- **Tasks:**
  - `SWE-ARC-API-WP1-T1` — Specify mesh, field, I/O, solver, coupling and observer interface responsibilities.
  - `SWE-ARC-API-WP1-T2` — Specify ownership, mutability, lifetime and error contracts.
  - `SWE-ARC-API-WP1-T3` — Create interface stubs or headers sufficient for dependent target compilation.

#### `SWE-ARC-SVC-WP1` — Define the shared application-service contract

- **Parent Deliverable:** `SWE-ARC-SVC`
- **Dependencies:** `SWE-ARC-API-WP1`, `SWE-ARC-CASE-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - CLI and Qt shell can invoke the same application-service boundary.
- **Tasks:**
  - `SWE-ARC-SVC-WP1-T1` — Specify case validation, run preparation, launch, cancellation and result-discovery operations.
  - `SWE-ARC-SVC-WP1-T2` — Define identical orchestration entry points for CLI and GUI adapters.
  - `SWE-ARC-SVC-WP1-T3` — Create a no-solver service stub for smoke testing.

#### `SWE-ARC-ERR-WP1` — Define diagnostics and failure propagation

- **Parent Deliverable:** `SWE-ARC-ERR`
- **Dependencies:** `SWE-ARC-API-WP1`, `SWE-ARC-SVC-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - A representative validation failure reaches CLI and GUI with preserved context.
- **Tasks:**
  - `SWE-ARC-ERR-WP1-T1` — Define error categories, status objects and severity levels.
  - `SWE-ARC-ERR-WP1-T2` — Define structured logging, cancellation and contextual diagnostic rules.
  - `SWE-ARC-ERR-WP1-T3` — Implement a minimal diagnostic path from core to CLI and GUI.

#### `SWE-GUI-SHL-WP1` — Create the Qt Quick application shell

- **Parent Deliverable:** `SWE-GUI-SHL`
- **Dependencies:** `SWE-ARC-SVC-WP1`, `SWE-ARC-ERR-WP1`, `SWE-ENV-DEP-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - The shell launches and displays service status without linking Qt into core libraries.
- **Tasks:**
  - `SWE-GUI-SHL-WP1-T1` — Create the Qt Quick/QML application target and resource structure.
  - `SWE-GUI-SHL-WP1-T2` — Implement navigation placeholders for data, domain, mesh, physics, solver, run and post-processing.
  - `SWE-GUI-SHL-WP1-T3` — Connect the shell to the application-service and diagnostic stubs.

#### `SWE-VER-UNIT-WP1` — Establish the unit and smoke-test framework

- **Parent Deliverable:** `SWE-VER-UNIT`
- **Dependencies:** `SWE-ENV-BLD-WP1`, `SWE-ARC-API-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - CTest discovers and executes tests through the shared presets.
- **Tasks:**
  - `SWE-VER-UNIT-WP1-T1` — Select and integrate the C++ test framework.
  - `SWE-VER-UNIT-WP1-T2` — Create representative core, configuration and adapter smoke tests.
  - `SWE-VER-UNIT-WP1-T3` — Register tests through CTest and presets.

#### `SWE-REL-CI-WP1` — Create the baseline CI workflow

- **Parent Deliverable:** `SWE-REL-CI`
- **Dependencies:** `SWE-ENV-PRS-WP1`, `SWE-VER-UNIT-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - A pull request triggers reproducible configure, build and test checks.
- **Tasks:**
  - `SWE-REL-CI-WP1-T1` — Configure clean checkout, dependency restore and preset-based build.
  - `SWE-REL-CI-WP1-T2` — Run smoke/unit tests and retain diagnostic artefacts on failure.
  - `SWE-REL-CI-WP1-T3` — Document branch and pull-request quality gates.

### 8.2 `G1` — Data and Mesh Vertical Slice

**Objective:** Demonstrate a reproducible path from case configuration and dataset provenance through geospatial/corridor preprocessing, tagged mesh ingestion, internal FVM representation, HDF5/XDMF output, ParaView inspection and Qt visibility.

**Exit criteria:**
- A versioned case and dataset manifest validates with actionable diagnostics.
- A sample raster/vector corridor workflow produces provenance-bearing intermediate artefacts.
- A tagged Gmsh mesh imports into the shared FVM topology and field model.
- Mesh and sample fields round-trip through HDF5 and are described by valid XDMF.
- The output opens in ParaView and is discoverable or previewable from the Qt shell.
- Unit, schema round-trip and clean-clone CI checks pass.

**Authorised Work Packages:**

#### `SWE-DAT-CFG-WP1` — Implement the versioned case schema

- **Parent Deliverable:** `SWE-DAT-CFG`
- **Dependencies:** `SWE-ARC-CASE-WP1`, `SWE-ARC-ERR-WP1`
- **Research inputs:** `RES-MOD`, `RES-NUM`
- **Work Package acceptance:**
  - G1 case fixtures validate deterministically and invalid cases identify exact fields.
- **Tasks:**
  - `SWE-DAT-CFG-WP1-T1` — Define the minimum G1 case structure and schema version.
  - `SWE-DAT-CFG-WP1-T2` — Implement parsing, semantic validation and contextual diagnostics.
  - `SWE-DAT-CFG-WP1-T3` — Create valid, invalid and migration fixture cases.

#### `SWE-DAT-MAN-WP1` — Implement the dataset and provenance manifest

- **Parent Deliverable:** `SWE-DAT-MAN`
- **Dependencies:** `SWE-DAT-CFG-WP1`
- **Research inputs:** `RES-DAT-GEO`, `RES-DAT-SRC`, `RES-DAT-OBS`, `RES-DAT-UNC`, `RES-DAT-CAS`
- **Work Package acceptance:**
  - Every G1 source and generated artefact has machine-readable provenance.
- **Tasks:**
  - `SWE-DAT-MAN-WP1-T1` — Define source, provider, licence, CRS, vertical datum, resolution and processing records.
  - `SWE-DAT-MAN-WP1-T2` — Implement manifest validation and generated-artefact lineage.
  - `SWE-DAT-MAN-WP1-T3` — Create a sample Tohoku/corridor manifest fixture.

#### `SWE-DAT-SCH-WP1` — Define and implement the G1 HDF5 schema

- **Parent Deliverable:** `SWE-DAT-SCH`
- **Dependencies:** `SWE-FVM-MSH-WP1`, `SWE-FVM-FLD-WP1`, `SWE-DAT-MAN-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - The G1 mesh and fields round-trip without topology, tag or metadata loss.
- **Tasks:**
  - `SWE-DAT-SCH-WP1-T1` — Define mesh, patch, field, metadata and provenance groups.
  - `SWE-DAT-SCH-WP1-T2` — Implement write/read adapters for the G1 topology and sample fields.
  - `SWE-DAT-SCH-WP1-T3` — Version the schema and create compatibility checks.

#### `SWE-DAT-XDMF-WP1` — Generate XDMF descriptors for G1 outputs

- **Parent Deliverable:** `SWE-DAT-XDMF`
- **Dependencies:** `SWE-DAT-SCH-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - ParaView opens the generated XDMF/HDF5 pair with correct mesh and fields.
- **Tasks:**
  - `SWE-DAT-XDMF-WP1-T1` — Map HDF5 topology, geometry and field datasets into XDMF.
  - `SWE-DAT-XDMF-WP1-T2` — Generate temporal/static metadata required by ParaView.
  - `SWE-DAT-XDMF-WP1-T3` — Validate the descriptor against the sample HDF5 case.

#### `SWE-FVM-MSH-WP1` — Implement the shared control-volume topology

- **Parent Deliverable:** `SWE-FVM-MSH`
- **Dependencies:** `SWE-ARC-API-WP1`
- **Research inputs:** `RES-NUM`
- **Work Package acceptance:**
  - The reference mesh preserves connectivity, orientation, volumes, areas, normals and patches.
- **Tasks:**
  - `SWE-FVM-MSH-WP1-T1` — Define cell, face, owner/neighbour, patch and discrete-geometry records.
  - `SWE-FVM-MSH-WP1-T2` — Implement topology construction and invariant checks from imported mesh data.
  - `SWE-FVM-MSH-WP1-T3` — Create a small tagged reference mesh fixture.

#### `SWE-FVM-FLD-WP1` — Implement typed field containers

- **Parent Deliverable:** `SWE-FVM-FLD`
- **Dependencies:** `SWE-FVM-MSH-WP1`
- **Research inputs:** `RES-MOD`, `RES-NUM`
- **Work Package acceptance:**
  - Fields reject incompatible topology and retain location/unit/name metadata.
- **Tasks:**
  - `SWE-FVM-FLD-WP1-T1` — Define scalar/vector field storage with cell, face and patch locations.
  - `SWE-FVM-FLD-WP1-T2` — Implement sizing, metadata, access and topology-compatibility checks.
  - `SWE-FVM-FLD-WP1-T3` — Populate deterministic sample fields on the reference mesh.

#### `SWE-FVM-BC-WP1` — Implement the boundary-patch framework

- **Parent Deliverable:** `SWE-FVM-BC`
- **Dependencies:** `SWE-FVM-MSH-WP1`, `SWE-FVM-FLD-WP1`
- **Research inputs:** `RES-NUM`
- **Work Package acceptance:**
  - Imported patch tags map deterministically to generic boundary objects.
- **Tasks:**
  - `SWE-FVM-BC-WP1-T1` — Define patch identity, type, entity membership and metadata.
  - `SWE-FVM-BC-WP1-T2` — Define a generic boundary-condition interface without solver-specific equations.
  - `SWE-FVM-BC-WP1-T3` — Create fixed-value, zero-gradient and named-placeholder fixtures.

#### `SWE-FVM-NUM-WP1` — Implement G1 interpolation and gradient primitives

- **Parent Deliverable:** `SWE-FVM-NUM`
- **Dependencies:** `SWE-FVM-MSH-WP1`, `SWE-FVM-FLD-WP1`
- **Research inputs:** `RES-NUM`
- **Work Package acceptance:**
  - Constant-field invariance and defined linear-field tolerances pass.
- **Tasks:**
  - `SWE-FVM-NUM-WP1-T1` — Implement cell-to-face interpolation required for inspection fixtures.
  - `SWE-FVM-NUM-WP1-T2` — Implement a basic gradient operator on the reference mesh.
  - `SWE-FVM-NUM-WP1-T3` — Verify exactness/consistency using constant and linear manufactured fields.

#### `SWE-GEO-IMP-WP1` — Implement sample raster/vector import adapters

- **Parent Deliverable:** `SWE-GEO-IMP`
- **Dependencies:** `SWE-DAT-MAN-WP1`
- **Research inputs:** `RES-DAT-GEO`, `RES-DAT-OBS`
- **Work Package acceptance:**
  - Imported fixtures preserve source metadata and fail clearly on unsupported inputs.
- **Tasks:**
  - `SWE-GEO-IMP-WP1-T1` — Read the selected G1 raster and vector fixture formats.
  - `SWE-GEO-IMP-WP1-T2` — Extract CRS, extent, resolution, nodata and source metadata.
  - `SWE-GEO-IMP-WP1-T3` — Emit canonical import records linked to the dataset manifest.

#### `SWE-GEO-CRS-WP1` — Implement coordinate and datum transformation handling

- **Parent Deliverable:** `SWE-GEO-CRS`
- **Dependencies:** `SWE-GEO-IMP-WP1`
- **Research inputs:** `RES-DAT-UNC`, `RES-DAT-SCN`
- **Work Package acceptance:**
  - Known control points satisfy the accepted transformation tolerance.
- **Tasks:**
  - `SWE-GEO-CRS-WP1-T1` — Define the WGS84-to-metric corridor transformation workflow.
  - `SWE-GEO-CRS-WP1-T2` — Apply transformations while retaining source/target CRS provenance.
  - `SWE-GEO-CRS-WP1-T3` — Create round-trip and known-point transformation tests.

#### `SWE-GEO-COR-WP1` — Implement evidence-driven corridor construction

- **Parent Deliverable:** `SWE-GEO-COR`
- **Dependencies:** `SWE-GEO-CRS-WP1`, `SWE-DAT-CFG-WP1`
- **Research inputs:** `RES-DAT-SCN`, `RES-DAT-CAS`
- **Work Package acceptance:**
  - The two corridor fixtures reproduce configured geometry and record all defining inputs.
- **Tasks:**
  - `SWE-GEO-COR-WP1-T1` — Create epicentre/target centreline and local rotated basis construction.
  - `SWE-GEO-COR-WP1-T2` — Apply L_pre, L_inland, width and flat-ended buffering parameters.
  - `SWE-GEO-COR-WP1-T3` — Export Kamaishi and Sendai sample corridor polygons with provenance.

#### `SWE-GEO-TER-WP1` — Implement G1 terrain conditioning

- **Parent Deliverable:** `SWE-GEO-TER`
- **Dependencies:** `SWE-GEO-COR-WP1`, `SWE-GEO-IMP-WP1`
- **Research inputs:** `RES-DAT-GEO`, `RES-DAT-UNC`
- **Work Package acceptance:**
  - The conditioned raster has no unexplained nodata and records transformation/resampling steps.
- **Tasks:**
  - `SWE-GEO-TER-WP1-T1` — Clip sample bathymetry/topography to the corridor.
  - `SWE-GEO-TER-WP1-T2` — Handle nodata, merging and resampling with explicit policies.
  - `SWE-GEO-TER-WP1-T3` — Export a conditioned terrain fixture with lineage metadata.

#### `SWE-GEO-MSH-WP1` — Implement the tagged Gmsh import workflow

- **Parent Deliverable:** `SWE-GEO-MSH`
- **Dependencies:** `SWE-FVM-MSH-WP1`, `SWE-GEO-TER-WP1`
- **Research inputs:** `RES-NUM`, `RES-GEO`
- **Work Package acceptance:**
  - All expected cells, faces and physical groups are preserved in the FVM mesh.
- **Tasks:**
  - `SWE-GEO-MSH-WP1-T1` — Define the G1 Gmsh physical-group naming convention.
  - `SWE-GEO-MSH-WP1-T2` — Import nodes, elements and physical tags into canonical adapter records.
  - `SWE-GEO-MSH-WP1-T3` — Transfer the fixture into the shared FVM topology.

#### `SWE-GEO-TAG-WP1` — Implement canonical region and boundary tagging

- **Parent Deliverable:** `SWE-GEO-TAG`
- **Dependencies:** `SWE-GEO-MSH-WP1`, `SWE-FVM-BC-WP1`
- **Research inputs:** `RES-MOD`, `RES-GEO`
- **Work Package acceptance:**
  - The reference mesh passes tag validation and exported tags remain identifiable.
- **Tasks:**
  - `SWE-GEO-TAG-WP1-T1` — Define terrain, inlet, outlet, side, top, barrier and material tag semantics.
  - `SWE-GEO-TAG-WP1-T2` — Validate required/forbidden tag combinations for 2D and 3D meshes.
  - `SWE-GEO-TAG-WP1-T3` — Persist tags through HDF5/XDMF export.

#### `SWE-GEO-BAR-WP1` — Implement method-neutral rigid barrier descriptors

- **Parent Deliverable:** `SWE-GEO-BAR`
- **Dependencies:** `SWE-DAT-CFG-WP1`, `SWE-GEO-TAG-WP1`
- **Research inputs:** `RES-GEO`, `RES-PHY-BAR`
- **Work Package acceptance:**
  - Barrier class and placement are reproducible independently of the eventual discretisation method.
- **Tasks:**
  - `SWE-GEO-BAR-WP1-T1` — Define wall-type and obstacle/dissipating-class descriptors.
  - `SWE-GEO-BAR-WP1-T2` — Define placement, orientation, dimensions and case-reference metadata.
  - `SWE-GEO-BAR-WP1-T3` — Attach a sample rigid barrier descriptor to the G1 case without selecting IBM/cut-cell methods.

#### `SWE-GEO-CHK-WP1` — Implement corridor, geometry and mesh checks

- **Parent Deliverable:** `SWE-GEO-CHK`
- **Dependencies:** `SWE-GEO-COR-WP1`, `SWE-GEO-MSH-WP1`, `SWE-GEO-TAG-WP1`
- **Research inputs:** `RES-VER`
- **Work Package acceptance:**
  - Injected invalid fixtures fail the intended checks with specific diagnostics.
- **Tasks:**
  - `SWE-GEO-CHK-WP1-T1` — Check corridor validity, raster coverage and CRS consistency.
  - `SWE-GEO-CHK-WP1-T2` — Check mesh topology, face orientation, positive measures and required patches.
  - `SWE-GEO-CHK-WP1-T3` — Emit structured diagnostics consumable by CLI and Qt.

#### `SWE-GUI-CAS-WP1` — Expose G1 case and manifest inspection

- **Parent Deliverable:** `SWE-GUI-CAS`
- **Dependencies:** `SWE-DAT-CFG-WP1`, `SWE-DAT-MAN-WP1`, `SWE-GUI-SHL-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - The Qt shell can inspect valid G1 cases and explain invalid ones.
- **Tasks:**
  - `SWE-GUI-CAS-WP1-T1` — Load and display case-schema and dataset-manifest summaries.
  - `SWE-GUI-CAS-WP1-T2` — Present validation errors with field and source context.
  - `SWE-GUI-CAS-WP1-T3` — Expose generated artefact locations without implementing full editing.

#### `SWE-GUI-VIS-WP1` — Expose the G1 mesh/field result

- **Parent Deliverable:** `SWE-GUI-VIS`
- **Dependencies:** `SWE-DAT-XDMF-WP1`, `SWE-GUI-SHL-WP1`
- **Research inputs:** None directly; governed by the accepted Software baseline
- **Work Package acceptance:**
  - A user can locate and open the G1 result from the application workflow.
- **Tasks:**
  - `SWE-GUI-VIS-WP1-T1` — Catalogue the generated HDF5/XDMF result.
  - `SWE-GUI-VIS-WP1-T2` — Display a lightweight metadata/mesh summary in the Qt shell.
  - `SWE-GUI-VIS-WP1-T3` — Provide a controlled ParaView hand-off for the XDMF output.

#### `SWE-VER-REG-WP1` — Implement G1 round-trip and regression tests

- **Parent Deliverable:** `SWE-VER-REG`
- **Dependencies:** `SWE-DAT-SCH-WP1`, `SWE-GEO-CHK-WP1`
- **Research inputs:** `RES-VER`
- **Work Package acceptance:**
  - The complete G1 fixture reproduces the accepted reference metadata and checksums/tolerances.
- **Tasks:**
  - `SWE-VER-REG-WP1-T1` — Create case/manifest/schema round-trip tests.
  - `SWE-VER-REG-WP1-T2` — Create mesh/tag/field HDF5 reference checks.
  - `SWE-VER-REG-WP1-T3` — Create a deterministic vertical-slice regression fixture.

---

## 9. Gate dependency summary

`G0` is a prerequisite for `G1`.

The critical G1 chain is:

`SWE-DAT-CFG-WP1` -> `SWE-DAT-MAN-WP1` -> `SWE-GEO-IMP-WP1` -> `SWE-GEO-CRS-WP1` -> `SWE-GEO-COR-WP1` -> `SWE-GEO-TER-WP1` -> `SWE-GEO-MSH-WP1` -> `SWE-GEO-TAG-WP1` -> `SWE-DAT-SCH-WP1` -> `SWE-DAT-XDMF-WP1` -> `SWE-GUI-VIS-WP1`.

`SWE-FVM-MSH-WP1`, `SWE-FVM-FLD-WP1` and `SWE-FVM-BC-WP1` form the parallel shared-core chain required by mesh transfer and HDF5 output.

---

## 10. v0.3 authority

Version 0.3 preserves the confirmed v0.2 Domain and Deliverable baseline and adds only the authorised G0/G1 Work Packages, Tasks, dependencies, research inputs and acceptance conditions. It is the authoritative source for initial GitHub Issue creation.
