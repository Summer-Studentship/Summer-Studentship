# Research Workstream Chat Handoff — High-Level FSI Architecture

**Version:** 0.1  
**Date:** 26 June 2026  
**Project:** Generating Tsunami Energy-Dissipating Barriers Using ML

## How to continue

Continue from the next unresolved high-level decision only:

> Compare monolithic and partitioned fluid–structure interaction coupling for the selected tsunami-barrier architecture. If partitioned coupling remains preferable, then compare loose and strong coupling, added-mass stability, relaxation and interface quasi-Newton acceleration.

Do not jump to code, detailed APIs, exact element implementations, ML algorithms or full software architecture before the coupling family is selected.

## Working style

- Resolve one conceptual layer at a time.
- Keep responses technically rigorous but controlled in length.
- Use primary research papers and authoritative sources.
- Distinguish provisional decisions from final selections.
- OpenFOAM is an architectural and mathematical reference only; it is not the production solver.
- The custom implementation remains a C++ solver framework.
- Future architecture diagrams should use Lucidchart-supported importable mind-map text rather than Mermaid.

## Project model hierarchy

```text
Regional 2D tsunami propagation
→ local 3D run-up, breaking, overtopping and impact
→ hydrodynamic traction on candidate defence
→ nonlinear 3D structural response
→ structural-motion feedback to the local fluid when required
```

### Regional model

- Cell-centred unstructured finite-volume nonlinear shallow-water model selected.
- Well-balanced, positivity-preserving, wetting/drying-capable formulation required.
- Exact flux, limiter, Runge–Kutta order and dry tolerance remain unresolved.

### Local fluid model

Selected at family level:

```text
Eulerian cell-centred polyhedral finite volume
+ incompressible immiscible two-phase Navier–Stokes
+ VOF free-surface capture
+ pressure correction
```

Still unresolved within the local fluid solver:

- geometric versus algebraic VOF;
- sharp/Ghost-Fluid versus diffuse mixture pressure treatment;
- PISO/PIMPLE or alternative correction sequence;
- URANS and LES closure details;
- momentum and temporal schemes;
- mesh-refinement strategy.

## Reference descriptions

### Fluid

- Eulerian description for both regional and local fluid domains.
- Impact does not require a Lagrangian fluid formulation.
- A fixed background fluid mesh is preferred for breaking and topology-changing free surfaces.

### Solid

- Lagrangian description for the deformable defence.
- Current position:

```text
x = X + d(X,t)
```

- Secure fixed/bonded base.
- No gross rigid-body translation, sliding or overturning in the baseline.
- “Immovable” does not mean undeformable.

## Structural equation of motion

The 3D semi-discrete structural equation is

```text
M_s q¨ + C_s q˙ + f_int(q,z) = f_hyd(t) + f_body(t)
```

where:

- `q` contains three displacement degrees of freedom per structural node;
- `z` contains constitutive history variables;
- `f_int` is assembled from stresses throughout the solid;
- `f_hyd` is assembled from fluid traction over the current wetted interface.

Fluid traction is obtained from

```text
t_f→s = σ_f n_s
σ_f = −pI + τ_f
```

and mapped to structural nodal forces through the structural surface interpolation.

## Structural time integration

Selected provisionally:

```text
generalised-alpha implicit time integration
```

Rationale:

- second-order accuracy under the standard parameter conditions;
- controllable high-frequency numerical dissipation;
- appropriate for stiff 3D structural dynamics;
- compatible with nonlinear constitutive response and Newton iterations;
- supported by Liu and Marsden’s unified continuum formulation.

Important qualification:

```text
stable structural integration ≠ stable FSI coupling
```

Generalised-alpha must be embedded in a complete nonlinear structural solver.

## Required structural solver components

Current high-level structural stack:

```text
3D finite-element spatial discretisation
+ finite-deformation solid mechanics
+ constitutive model and history update
+ generalised-alpha time integration
+ Newton-type nonlinear equilibrium iteration
+ sparse linear solve
+ nonlinear convergence checks
```

FEM is the provisional structural spatial discretisation. Cell-centred finite-volume solid mechanics remains a recorded alternative and C++ architecture reference.

## Finite-deformation reference formulation

Selected baseline:

```text
Updated-Lagrangian finite-deformation formulation
```

Interpretation:

- Total Lagrangian refers all structural states to the initial geometry.
- Updated Lagrangian uses the latest converged configuration as the reference for the next increment.
- This choice concerns deformation during one tsunami simulation.
- It is separate from the ML optimisation loop; each candidate design begins a new simulation with its own initial geometry.

Reason for the selection:

- early candidate geometries may experience large rotation, yielding and substantial permanent shape change;
- the fluid-facing geometry may evolve materially during impact;
- Updated Lagrangian is the more natural baseline under the current severe-deformation assumption.

Limitation:

- Updated Lagrangian alone does not provide fracture, fragmentation, erosion or topology change.
- Initial scope: simulate large elastoplastic deformation until a defined failure condition is reached.
- Post-fracture debris interaction is outside the baseline.

## Moving structural boundary inside the fluid solver

Families compared:

### ALE

- Body-fitted fluid mesh follows the structural surface.
- Direct interface geometry and conventional wall treatment.
- Main weakness: severe mesh distortion, smoothing/remeshing and field-transfer complexity during large deformation.
- Retained as a comparator for moderate-deformation verification cases.

### Sharp immersed boundary with conservative cut cells

Selected baseline:

```text
sharp immersed-boundary representation
+ conservative cut-cell treatment
```

Rationale:

- fixed Eulerian background mesh;
- no global fluid-mesh deformation or repeated remeshing;
- better compatibility with large deformation, changing candidate geometries and VOF free-surface breaking;
- conservative cut cells can account for moving fluid volumes and interface fluxes.

This selection excludes a simple non-conservative immersed forcing method.

Future implementation requirements include:

- structural-surface intersection with the fluid grid;
- cut-cell volume and aperture updates;
- small-cell stabilisation, merging or flux redistribution;
- sharp wall reconstruction;
- pressure and traction recovery;
- conservative force, moment and interface-work transfer.

## Current high-level FSI architecture

```text
Eulerian polyhedral FVM two-phase fluid
+ VOF free-surface capture
+ Updated-Lagrangian 3D solid FEM
+ generalised-alpha structural time integration
+ Newton-type nonlinear structural solution
+ sharp immersed boundary with conservative cut cells
```

Interface conditions to enforce:

```text
u_f = d˙_s
σ_f n_f + σ_s n_s = 0
```

## Remaining high-level decisions

1. Monolithic versus partitioned FSI coupling.
2. Loose versus strong coupling if partitioned.
3. Added-mass stability treatment.
4. Relaxation or interface quasi-Newton acceleration.
5. Conservative mapping for force, moment, displacement and interface work.
6. Constitutive material family.
7. Failure criterion and termination policy.
8. Exact finite-element spaces and incompressibility treatment.
9. Generalised-alpha spectral radius and fluid/solid time-step coordination.

## Core evidence and paper IDs

- **P028 — Baragamage and Wu (2024):** three-dimensional tsunami flow, reduced structural EOM, generalised-alpha integration, immersed boundary and cut cells. Local and Drive copy available.
- **P050 — Liu and Marsden (2018):** unified continuum FSI, nonlinear 3D solid dynamics, VMS FEM, generalised-alpha and predictor multi-corrector nonlinear solution. Acquisition queue.
- **P051 — Nemer et al. (2021):** Updated-Lagrangian nonlinear solid dynamics on unstructured tetrahedral FEM. Acquisition queue.
- **P052 — Pasquariello et al. (2016):** conservative cut-cell FVM–FEM coupling, mortar transfer and a 3D large-deformation FSI benchmark. Acquisition queue.
- **P053 — Girfoglio et al. (2021):** ALE, strongly coupled partitioned 3D FSI and LES filtering. ALE comparator. Acquisition queue.
- **P054 — Cardiff et al. (2018):** finite-volume solid mechanics and fluid-solid interaction toolbox. Alternative spatial discretisation and architecture reference. Acquisition queue.
- **P055 — Chung and Hulbert (1993):** foundational generalised-alpha paper. Institutional acquisition required.

## Updated artefacts

- `fsi_high_level_progress_v0.1.tex`
- `fsi_high_level_progress_v0.1.bib`
- `fsi_high_level_progress_v0.1.pdf`
- `mendeley_paper_tag_register_v0.3.xlsx`
- `research_workstream_pdf_directory_structure_v0.3.md`
- `research_workstream_chat_handoff_fsi_v0.1.md`

## Physical PDF assignment completed

The local Baragamage and Wu PDF is assigned to:

```text
Workstream PDFs/
└── RES-STR/
    └── RES-STR-FSI/
        └── Moving_Boundary/
            └── Immersed_Boundary_Cut_Cell/
                └── Baragamage_Wu_2024_Tsunami_Bridge_FSI.pdf
```

The remaining P050–P055 records have target paths assigned in the v0.3 register and remain in the acquisition queue.
