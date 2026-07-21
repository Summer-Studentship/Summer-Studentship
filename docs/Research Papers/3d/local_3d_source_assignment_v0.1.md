# Local 3D Source Assignment

Date: 2026-07-18

Status: Companion source-role manifest for the local 3D URANS--VOF
research integration.

This file assigns the PDFs stored in `docs/Research Papers/3d/` to
evidence roles used by the WBS-driven Research workstream. It is not a
standalone literature review. Each source should support, limit,
contradict, or defer a specific decision in `docs/workstream/RES -
Research`.

## Storage and citation rules

- Keep each PDF physically in one location. Do not duplicate PDFs into
  WBS Deliverable folders.
- Cite papers through the relevant Domain bibliography:
  `res-mod.bib`, `res-num.bib`, `res-phy.bib`, `res-ver.bib`, or
  `res-str.bib`.
- Use the source role when writing evidence, not only the citation key.
- Do not create placeholder bibliography entries. If bibliographic
  details have not been confirmed, keep the citation status as pending.

## Evidence roles

| Role | Meaning |
| --- | --- |
| `3D-FVM` | Finite-volume discretisation, pressure correction, source-term linearisation, residuals, wall functions |
| `3D-RANSVOF` | Local 3D RANS/VOF tsunami modelling |
| `3D-TOHOKU` | 2011 Tohoku, Kamaishi or real-event overtopping, loading or validation evidence |
| `3D-VOF` | VOF interface conservation, boundedness, sharpness, compression or geometric interface capture |
| `3D-SST` | `k`--`omega` SST and high-Reynolds-number turbulence closure |
| `3D-WAVEBC` | Wave generation, absorption, relaxation zones, radiation or open boundaries |
| `3D-COUPLING` | Regional-to-local, nested, replay or simultaneous coupling |
| `3D-VALIDATION` | Canonical benchmarks, Tohoku hydrodynamics, impact/load validation |
| `3D-DEFERRED` | FSI, porosity, damage, scour, deformable barriers, sediment, structural failure |

## Source assignment

| Local source | Evidence roles | Primary WBS use | Citation status |
| --- | --- | --- | --- |
| `VOF Method for the Dynamics of Free Boundaries.pdf` | `3D-VOF` | `RES-NUM-FRM`, `RES-NUM-SPEC` | Added as `hirt1981vof` where used |
| `New Property Averaging Scheme for Volume of Fluid Method for Two-Phase Flows with Large Viscosity Ratios.pdf` | `3D-VOF` | `RES-NUM-FRM`, `RES-NUM-SPEC` | Bibliographic confirmation pending |
| `Two-Equation Eddy-Viscosity Turbulence Models for Engineering Applications.pdf` | `3D-SST` | `RES-MOD-SRC`, `RES-NUM-SPEC` | Added as `menter1994twoequation` where used |
| `On the over-production of turbulence beneath surface waves in Reynolds-averaged Navier-Stokes models.pdf` | `3D-SST` | `RES-MOD-SRC`, `RES-MOD-ASM` | Bibliographic confirmation pending |
| `One-way coupled Eulerian-Langrangian strategy for wave propogation and impact on coastal areas.pdf` | `3D-COUPLING`, `3D-VALIDATION` | `RES-MOD-MUL`, `RES-NUM-IBC` | Bibliographic confirmation pending |
| `A two-way coupling 2D-3D hybrid finite element numerical model using overlapping method for tsunami simulation.pdf` | `3D-COUPLING` | `RES-MOD-MUL`, `RES-NUM-IBC` | Bibliographic confirmation pending |
| `Realistic wave generation and active wave absorption for Navier-Stokes models_ Application to OpenFOAM.pdf` | `3D-WAVEBC`, `3D-FVM` | `RES-MOD-IBC`, `RES-NUM-IBC` | Added as `higuera2013realistic` where used |
| `Beyond VoF: alternative OpenFOAM solvers for numerical wave tanks.pdf` | `3D-WAVEBC`, `3D-VOF` | `RES-NUM-SPEC`, `RES-VER-BMK` | Bibliographic confirmation pending |
| `A pretty good sponge_ Dealing with open boundaries in limited-area ocean models.pdf` | `3D-WAVEBC` | `RES-MOD-IBC`, `RES-NUM-IBC` | Bibliographic confirmation pending |
| `Experimentally validated numerical models to assess tsunami hydrodynamic force on an elevated structure.pdf` | `3D-RANSVOF`, `3D-VALIDATION` | `RES-PHY-LOD`, `RES-VER-MET` | Bibliographic confirmation pending |
| `Tsunami Wave Energy.pdf` | `3D-RANSVOF` | `RES-PHY-BAR`, `RES-OPT-OBJ` | Bibliographic confirmation pending |
| `Tsunami hydrodynamic force on a building using a SPH real scale numerical simulation.pdf` | `3D-VALIDATION`, `3D-DEFERRED` | `RES-NUM-SPEC`, `RES-VER-BMK` | Bibliographic confirmation pending |
| `Numerical modeling of non-breaking, impulsive breaking, and broken wave interaction with elevated coastal structures_ Laboratory validation and inter-model comparisons.pdf` | `3D-VALIDATION`, `3D-RANSVOF` | `RES-PHY-LOD`, `RES-VER-MET` | Bibliographic confirmation pending |
| `A comparison between the surface compression method and an interface reconstruction method for the VOF approach.pdf` | `3D-VOF` | `RES-NUM-FRM`, `RES-NUM-SPEC` | Bibliographic confirmation pending |
| `A Sharp Free Surface Finite Volume Method Applied to Gravity Wave Flows.pdf` | `3D-VOF`, `3D-FVM` | `RES-NUM-FRM`, `RES-NUM-SPEC` | Added as `vukcevic2018sharp` where used |
| `Solution of the implicitly discretised fluid flow equations by operator-splitting.pdf` | `3D-FVM` | `RES-NUM-FRM`, `RES-NUM-SPEC` | Added as `issa1986operator` where used |
| `Comparison of interface capturing methods for the simulation of two-phase flow in a unified low-Mach framework.pdf` | `3D-VOF` | `RES-NUM-SPEC`, `RES-NUM-ERR` | Bibliographic confirmation pending |
| `Verification and Validation of a Numerical Wave Tank Using Waves2FOAM.pdf` | `3D-WAVEBC`, `3D-VALIDATION` | `RES-VER-BMK`, `RES-VER-MET` | Bibliographic confirmation pending |
| `numerical-study-of-the-turbulent-flow-past-an-airfoil-with-trailing-edge-separation.pdf` | `3D-SST` | `RES-MOD-SRC` | Context only; bibliographic confirmation pending |
| `Wave_reflection_from_coastal_structures.pdf` | `3D-WAVEBC` | `RES-MOD-IBC`, `RES-VER-MET` | Bibliographic confirmation pending |
| `An Experimental and Computational Study of Breaking Wave Impact Forces.pdf` | `3D-VALIDATION`, `3D-DEFERRED` | `RES-PHY-LOD`, `RES-VER-BMK` | Bibliographic confirmation pending |
| `Numerical Methods in Fluids - 2011 - Jacobsen - A wave generation toolbox for the open-source CFD library OpenFoam.pdf` | `3D-WAVEBC`, `3D-FVM` | `RES-NUM-IBC`, `RES-VER-BMK` | Added as `jacobsen2012waves2foam` where used |
| `Validation of tsunami numerical simulation models for an idealised coastal industrial site.pdf` | `3D-VALIDATION`, `3D-RANSVOF` | `RES-VER-FRM`, `RES-VER-MET` | Added as `watanabe2022validation` where used |
| `Turbulence Model Effects on VOF Analysis of Breakwater Overtopping During the 2011 Great East Japan Tsunami.pdf` | `3D-TOHOKU`, `3D-SST`, `3D-RANSVOF` | `RES-MOD-SRC`, `RES-PHY-BAR` | Bibliographic confirmation pending |
| `Numerical Study on the Turbulent Structure of Tsunami Bottom Boundary Layer Using the 2011 Tohoku Tsunami Waveform\n.pdf` | `3D-TOHOKU`, `3D-SST` | `RES-PHY-SCL`, `RES-MOD-SRC` | Filename contains an embedded newline; bibliographic confirmation pending |
| `Influence of Turbulence Effects on the Runup of Tsunami Waves on the Shore within the Framework of the Navier-Stokes Equations.pdf` | `3D-SST`, `3D-RANSVOF` | `RES-MOD-SRC`, `RES-PHY-SCL` | Added as `kozelkov2022turbulence` where used |
| `Tsunami Wave Generation in NS Solver and the Effect of Leading trough on Wave Run-up.pdf` | `3D-RANSVOF`, `3D-VALIDATION` | `RES-PHY-BAR`, `RES-VER-BMK` | Bibliographic confirmation pending |

## Missing sources or metadata

The current source pool is sufficient to document the local 3D
URANS--VOF baseline at research-specification level. The following
items remain open before the local numerical method can be accepted:

- confirmed bibliographic metadata for sources marked pending above;
- a dedicated, project-approved source for scalable wall functions and
  rough-wall treatment under high-Reynolds-number tsunami impact;
- a dedicated source for the exact collocated pressure--velocity
  interpolation used to avoid checkerboarding if Rhie--Chow treatment is
  selected explicitly;
- validation data for the selected real defence and selected historical
  event after `RES-DAT-CAS` closes the case-study specification;
- evidence for any deferred physics that is later activated, including
  deformable barriers, structural failure, scour, sediment transport,
  unresolved porous media or trapped-air compressibility.
