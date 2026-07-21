# Research Workstream PDF Directory Structure and Placement Register

**Version:** 0.3  
**Date:** 26 June 2026  
**Root folder:** `Workstream PDFs/`

## Storage rules

1. Store each PDF physically once under its primary owning Deliverable.
2. Record secondary relevance through WBS tags and Mendeley tags rather than duplicate files.
3. Keep exact duplicates and rejected sources in quarantine folders for traceability.
4. Keep methodological and contextual-validation evidence separate within each solver-family branch.
5. Treat the local 3D and structural-solver branches as open until their numerical families are selected.

## Directory structure

```text
Workstream PDFs/
├── 00_ADMIN/
│   └── Project_Proposal/
├── RES-DAT/
│   └── RES-DAT-OBS/
│       ├── Tohoku_Observations/
│       └── Tohoku_Runup_and_Inundation/
├── RES-ML/
│   ├── RES-ML-REP/
│   │   └── 90_Cross_Domain_Context/
│   └── RES-ML-SUR/
│       ├── 90_Cross_Domain_Context/
│       └── Tsunami_Force_Surrogates/
├── RES-MOD/
│   ├── RES-MOD-ASM/
│   │   ├── Conditional_Air_Compressibility/
│   │   ├── Porous_and_Permeable_Barriers/
│   │   └── Porous_and_Rubble_Mound_Barriers/
│   ├── RES-MOD-EQS/
│   │   ├── 02D_vs_03D_Model_Rationale/
│   │   └── Local_3D_Free_Surface_Models/
│   ├── RES-MOD-MUL/
│   │   ├── 02D-03D_Coupling/
│   │   ├── 90_Cross_Domain_Context/
│   │   └── Regional_to_Coastal_Model_Chains/
│   └── RES-MOD-SRC/
│       └── Turbulence_Closures/
│           └── LES/
├── RES-NUM/
│   ├── RES-NUM-FRM/
│   │   ├── 01_Regional_NLSWE/
│   │   │   ├── 01_FDM/
│   │   │   │   ├── 01_Methodological/
│   │   │   │   └── 02_Contextual_Validation/
│   │   │   ├── 02_FVM/
│   │   │   │   ├── 01_Methodological/
│   │   │   │   └── 02_Contextual_Validation/
│   │   │   ├── 03_LBM_DBM/
│   │   │   │   └── 01_Methodological/
│   │   │   └── 04_Comparative_Reviews/
│   │   └── 02_Local_3D_Navier_Stokes/
│   │       ├── 01_Eulerian_FVM_VOF/
│   │       │   ├── 01_Methodological/
│   │       │   └── 03_Alternative_Interface_Capture/
│   │       ├── 02_FEM_ALE/
│   │       ├── 03_FDM_MAC_Cut_Cell/
│   │       ├── 04_SPH/
│   │       └── 05_MPS/
│   └── RES-NUM-STB/
│       └── 01_Regional_NLSWE/
├── RES-PHY/
│   ├── RES-PHY-PRO/
│   │   ├── Far_Field_Propagation/
│   │   └── Tohoku_Dispersion_and_Propagation/
│   └── RES-PHY-SRC/
│       ├── Scenario_and_Fault_Context/
│       └── Tohoku_Source_Reconstruction/
├── RES-STR/
│   ├── RES-STR-FSI/
│   │   ├── One_Way_Load_Transfer/
│   │   ├── Two_Way_FSI/
│   │   └── Moving_Boundary/
│   │       ├── ALE/
│   │       └── Immersed_Boundary_Cut_Cell/
│   ├── RES-STR-LOD/
│   │   └── Hydrodynamic_Loads_and_Drag/
│   └── RES-STR-SIM/
│       └── 03D_Solid_Dynamics/
│           ├── Generalized_Alpha_and_Nonlinear_Dynamics/
│           ├── Updated_Lagrangian_Finite_Deformation/
│           └── Alternative_Solid_FVM/
├── RES-VER/
│   ├── RES-VER-BMK/
│   │   ├── Local_Impact_Experiments/
│   │   └── Local_Impact_Validation/
│   ├── RES-VER-FRM/
│   │   └── Standards_and_Guides/
│   └── RES-VER-TST/
│       └── Blind_Validation_and_Model_Comparison/
├── 98_REJECTED/
│   ├── Low_Relevance/
│   └── Superseded_Observation_Sources/
└── 99_DUPLICATES/
    ├── DUP-QIN-2018/
    └── DUP-WEI-2013/
```

## Current decision-sensitive branches

| Branch | Current status | Primary evidence placement |
|---|---|---|
| Regional 2D NLSWE | Finite volume selected | `RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/02_FVM/` |
| Finite difference | Retained as reference and alternative | `RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/01_FDM/` |
| LBM / DBM | Removed from active shortlist | `RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/03_LBM_DBM/` |
| Local 3D two-phase Navier–Stokes | Eulerian polyhedral FVM with VOF selected at family level | `RES-NUM/RES-NUM-FRM/02_Local_3D_Navier_Stokes/01_Eulerian_FVM_VOF/` |
| Structural dynamics | 3D FEM with Updated-Lagrangian finite deformation and generalized-alpha time integration selected at high level | `RES-STR/RES-STR-SIM/03D_Solid_Dynamics/` |
| Moving structural boundary | Sharp immersed boundary with conservative cut-cell treatment selected; ALE retained as comparator | `RES-STR/RES-STR-FSI/Moving_Boundary/` |

## Current local PDF placements

| ID | Local filename | Recommended path | Decision/status |
|---|---|---|---|
| `ADM-001` | `Studentship_Proposal.pdf` | `Workstream PDFs/00_ADMIN/Project_Proposal/` | Authoritative project input |
| `P006` | `Japan’s 2011 Tsunami: Characteristics of Wave Propagation from Observations and Numerical Modelling.pdf` | `Workstream PDFs/RES-DAT/RES-DAT-OBS/Tohoku_Observations/` | Active reviewed evidence |
| `P027` | `Role of Trapped Air on the Tsunami-Induced Transient Loads and Response of Coastal Bridges.pdf` | `Workstream PDFs/RES-MOD/RES-MOD-ASM/Conditional_Air_Compressibility/` | Active reviewed evidence |
| `P029` | `Numerical Modeling of Tsunami Waves Interaction with Porous and Impermeable Vertical Barriers.pdf` | `Workstream PDFs/RES-MOD/RES-MOD-ASM/Porous_and_Permeable_Barriers/` | Active reviewed evidence |
| `P025` | `Numerical assessment of tsunami attack on a rubble mound breakwater using openfoam.pdf` | `Workstream PDFs/RES-MOD/RES-MOD-ASM/Porous_and_Rubble_Mound_Barriers/` | Consideration only |
| `P032` | `A comparison of a two-dimensional depth-averaged flow model and a three-dimensional RANS model for predicting tsunami inundation and fluid forces.pdf` | `Workstream PDFs/RES-MOD/RES-MOD-EQS/02D_vs_03D_Model_Rationale/` | Selected 2D–3D model-rationale evidence |
| `P022` | `A Simplified 3-D Navier-Stokes Numerical Model for Landslide-tsunami.pdf` | `Workstream PDFs/RES-MOD/RES-MOD-EQS/Local_3D_Free_Surface_Models/` | Consideration only |
| `P023` | `3D numerical simulation of tsunami generation and propagation,case study.pdf` | `Workstream PDFs/RES-MOD/RES-MOD-EQS/Local_3D_Free_Surface_Models/` | Consideration only |
| `P012` | `Modelling Propogation and innundation of tohoku tsunami.pdf` | `Workstream PDFs/RES-MOD/RES-MOD-MUL/Regional_to_Coastal_Model_Chains/` | Active reviewed evidence |
| `P030` | `Influence of Turbulence Effects on the Runup of Tsunami Waves on the Shore within the Framework of the Navier–Stokes Equations .pdf` | `Workstream PDFs/RES-MOD/RES-MOD-SRC/Turbulence_Closures/` | Supports retaining URANS and LES modes |
| `P042` | `Numerical Methods in Fluids - 30 September 1992 - Casulli - Semi‐implicit finite difference methods for three‐dimensional.pdf` | `Workstream PDFs/RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/01_FDM/01_Methodological/` | Retained FDM methodological reference |
| `P043` | `on_the_construction_of_computational_methods_for_shallow_water_flow_problems.pdf` | `Workstream PDFs/RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/01_FDM/01_Methodological/` | Retained FDM supporting reference |
| `P009` | `Modeling of the 2011 Japan Tsunami: Lessons for Near-Field Forecast.pdf` | `Workstream PDFs/RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/01_FDM/02_Contextual_Validation/` | Retained FDM contextual reference |
| `P046` | `A SECOND-ORDER WELL-BALANCED POSITIVITY PRESERVING.pdf` | `Workstream PDFs/RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/02_FVM/01_Methodological/` | SELECTED — regional FVM methodological reference |
| `P047` | `Discrete Boltzmann model of shallow water equations with polynomial equilibria.pdf` | `Workstream PDFs/RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/03_LBM_DBM/01_Methodological/` | SCREENED OUT — LBM/DBM removed from active shortlist |
| `P044` | `Numerical Methods for NSLWE.pdf` | `Workstream PDFs/RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/04_Comparative_Reviews/` | Reviewed comparative foundation |
| `P024` | `Numerical Solutions of the Nonlinear Dispersive Shallow Water Wave Equations Based on the Space–Time Coupled Generalized Finite Difference Scheme.pdf` | `Workstream PDFs/RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/04_Other_Discretisations/` | Consideration only |
| `P045` | `Numerical Accuracy in the solutions of SWE.pdf` | `Workstream PDFs/RES-NUM/RES-NUM-STB/01_Regional_NLSWE/` | Reviewed verification support |
| `P003` | `Accurate numerical Asimulation of the far-field tsunami caused by the 2011 Tohoku earthquake.pdf` | `Workstream PDFs/RES-PHY/RES-PHY-PRO/Far_Field_Propagation/` | Active reviewed evidence |
| `P002` | `JGR Oceans - 2014 - Saito - Dispersion and nonlinear effects in the 2011 Tohoku‐Oki earthquake tsunami.pdf` | `Workstream PDFs/RES-PHY/RES-PHY-PRO/Tohoku_Dispersion_and_Propagation/` | Active governing-physics evidence |
| `P021` | `Exploring the tsunami generation potential of major faults in the sicilian channel using 3D numerical modelling.pdf` | `Workstream PDFs/RES-PHY/RES-PHY-SRC/Scenario_and_Fault_Context/` | Consideration only |
| `P005` | `Numerical Simulation of the 2011 Tohoku Tsunami Based on a New Transient FEM Co-seismic Source: Comparison to Far- and Near-Field Observations.pdf` | `Workstream PDFs/RES-PHY/RES-PHY-SRC/Tohoku_Source_Reconstruction/` | Active reviewed evidence |
| `P011` | `Tsunami source of the 2011 off the Pacific coast of Tohoku Earthquake.pdf` | `Workstream PDFs/RES-PHY/RES-PHY-SRC/Tohoku_Source_Reconstruction/` | Active reviewed evidence |
| `P028` | `Baragamage_Wu_2024_Tsunami_Bridge_FSI.pdf` | `Workstream PDFs/RES-STR/RES-STR-FSI/Moving_Boundary/Immersed_Boundary_Cut_Cell/` | Active tsunami FSI and cut-cell contextual evidence |
| `P031` | `Validation of tsunami numerical simulation models for an idealized coastal industrial site.pdf` | `Workstream PDFs/RES-VER/RES-VER-TST/Blind_Validation_and_Model_Comparison/` | Active reviewed evidence |

## Acquisition queue

| ID | Paper | Target path | Current role |
|---|---|---|---|
| `P048` | The VOLNA Code for the Numerical Modelling of Tsunami Waves: Generation, Propagation and Inundation | `Workstream PDFs/RES-NUM/RES-NUM-FRM/01_Regional_NLSWE/02_FVM/02_Contextual_Validation/` | SELECTED — regional FVM contextual-validation reference |
| `P049` | A Sharp Free Surface Finite Volume Method Applied to Gravity Wave Flows | `Workstream PDFs/RES-NUM/RES-NUM-FRM/02_Local_3D_Navier_Stokes/01_Eulerian_FVM_VOF/01_Methodological/` | LEADING CANDIDATE — local 3D solver unresolved |
| `P050` | A Unified Continuum and Variational Multiscale Formulation for Fluids, Solids, and Fluid-Structure Interaction | `Workstream PDFs/RES-STR/RES-STR-SIM/03D_Solid_Dynamics/Generalized_Alpha_and_Nonlinear_Dynamics/` | SELECTED SUPPORT — generalized-alpha and nonlinear structural solve |
| `P051` | Stabilized Finite Element Method for Incompressible Solid Dynamics Using an Updated Lagrangian Formulation | `Workstream PDFs/RES-STR/RES-STR-SIM/03D_Solid_Dynamics/Updated_Lagrangian_Finite_Deformation/` | SELECTED SUPPORT — Updated-Lagrangian finite deformation |
| `P052` | A Cut-Cell Finite Volume-Finite Element Coupling Approach for Fluid-Structure Interaction in Compressible Flow | `Workstream PDFs/RES-STR/RES-STR-FSI/Moving_Boundary/Immersed_Boundary_Cut_Cell/` | SELECTED — moving-boundary methodological reference |
| `P053` | Fluid-Structure Interaction Simulations with a LES Filtering Approach in solids4Foam | `Workstream PDFs/RES-STR/RES-STR-FSI/Moving_Boundary/ALE/` | REFERENCE ALTERNATIVE — ALE moving-boundary treatment |
| `P054` | An Open-Source Finite Volume Toolbox for Solid Mechanics and Fluid-Solid Interaction Simulations | `Workstream PDFs/RES-STR/RES-STR-SIM/03D_Solid_Dynamics/Alternative_Solid_FVM/` | REFERENCE ALTERNATIVE — solid FVM |
| `P055` | A Time Integration Algorithm for Structural Dynamics with Improved Numerical Dissipation: The Generalized-Alpha Method | `Workstream PDFs/RES-STR/RES-STR-SIM/03D_Solid_Dynamics/Generalized_Alpha_and_Nonlinear_Dynamics/` | FOUNDATIONAL ACQUISITION — generalized-alpha |

## Duplicate handling

| Duplicate ID | Quarantine path | Required action |
|---|---|---|
| `P041` | `Workstream PDFs/99_DUPLICATES/DUP-QIN-2018/` | Do not import; retain only the recommended copy |
| `P010` | `Workstream PDFs/99_DUPLICATES/DUP-WEI-2013/` | Do not import; retain only the recommended copy |

## Register relationship

The spreadsheet register is authoritative for:

- primary owning Deliverable;
- exact recommended path;
- local, Drive-only and acquisition status;
- selected, retained, screened-out and unresolved numerical decisions;
- duplicate groups and Mendeley import actions;
- secondary WBS relevance without additional file copies.

The `Directory Map` worksheet gives the leaf-path inventory and record counts. The `Workstream Placement` worksheet is the move/import manifest.