# Project Scope Change: Tohoku Impact and Barrier-Class Comparison

Date: 2026-07-19

Status: Research in progress

## Decision

The studentship research scope is revised from an event-conditioned
surrogate workflow toward hydrodynamic impact modelling for the 2011
Tohoku tsunami, two regional-to-local corridor comparisons and
fixed-rigid barrier-class comparison.

## Previous Direction

The previous direction considered documented existing defences,
reinforcement interventions, ML-guided geometry refinement,
event-conditioned supervised learning, unrestricted design optimisation,
generative geometry, inverse design, cross-event generalisation and a
broad multi-event ML dataset.

## Revised Direction

The revised active project concentrates on:

- selecting the 2011 Tohoku event as the active regional case;
- defining two evidence-based trajectories/corridors: epicentre to
  Kamaishi and epicentre to the Sendai coastal plain;
- finalising a source-data hierarchy for bathymetry/topography,
  earthquake/finite-fault data and tsunami observations;
- using a regional 2D depth-averaged NLSWE finite-volume propagation
  model to drive local 3D incompressible two-phase URANS--VOF
  fixed-barrier simulations by one-way replay;
- retaining simultaneous regional/local coupling only where reflection,
  resonance, blocking or other feedback materially changes outputs;
- comparing wall-type and obstacle/dissipating/array-type fixed rigid
  barrier classes through run-up, overtopping, transmission/reflection,
  pressure, force, impulse, moment and energy metrics;
- recording hydrodynamic load histories for later structural, damage
  or FSI studies without making those studies active baseline work.

ML, surrogate optimisation, generative geometry, material damage,
deformable structures, scour and two-way FSI remain deferred until
hydrodynamic load extraction and validation are stable.

## Rationale

The revised scope reduces evidence, data-volume and validation risk by
making the hydrodynamic modelling pipeline the first deliverable. It
also prevents geometry generation, ML and structural damage from being
over-claimed before the source data, corridor definitions, local load
extraction and validation hierarchy exist.

## Included Scope

- 2011 Tohoku source-to-coast event reconstruction.
- Kamaishi and Sendai coastal-plain corridors.
- Source-data manifest, CRS/datum provenance, terrain clipping and
  replay-boundary data requirements.
- Regional 2D NLSWE and local 3D URANS--VOF fixed-rigid barrier
  modelling specifications.
- Wall-type and obstacle/dissipating/array barrier-class comparison.
- Hydrodynamic metrics and load histories for later structural handoff.

## Excluded Scope

- Unrestricted free-form geometry generation.
- ML training and surrogate optimisation.
- Reinforcement learning.
- Unrestricted inverse design.
- Claims of cross-event generalisation.
- Direct training from before/after geometry alone.
- Replacement of high-fidelity confirmation by the surrogate.
- Material damage, deformable-structure response, scour and two-way FSI
  as baseline outputs.
- Final optimisation algorithm selection during this scope-change edit.

## Methodological Qualification

Existing or reconstructed defence geometries are useful evidence where
they are citable and recoverable, but they are not required for the
baseline class comparison and are not direct causal labels for an
optimal barrier.

Differences between historical geometries may reflect revised design standards,
changed acceptable-risk levels, construction constraints, budgets, land-use
decisions, standardisation, political decisions, or protection against hazards
beyond the selected event.

Historical changes may define realistic future comparison cases and
provide context for Kamaishi or other defences. Controlled validated
hydrodynamic simulations provide the principal barrier-performance
evidence.

## Project Gates

| Gate | Meaning | Deferred-work implication |
| --- | --- | --- |
| G1 | Data-source feasibility: candidate topobathymetry, source and observation datasets have licence, CRS/datum, resolution and provenance records. | ML/FSI/damage remain deferred. |
| G2 | Corridor definition: Kamaishi and Sendai corridors have final GIS coordinates, bearings, widths, lengths and sponge zones. | Geometry generation remains deferred. |
| G3 | Event reconstruction: the regional Tohoku model and replay histories are ready for validation against independent observations. | No local impact claims before this gate. |
| G4 | Local hydrodynamic capability: fixed-rigid URANS--VOF runs emit boundedness, CFL, residual, run-up, overtopping, energy and load metrics. | Structural studies may consume loads only after checks pass. |
| G5 | V1--V2--V3 validation: solver capability, Tohoku hydrodynamics and local impact/loading evidence are separated and reported. | ML/surrogate training remains future unless validated outputs exist. |
| G6 | Demonstration: barrier classes are compared in the two corridors with full provenance. | Demonstration only, not certification or generalisation. |

## Affected WBS IDs

RES-ML, RES-ML-ROL, RES-ML-DAT, RES-ML-REP, RES-ML-ALG, RES-ML-OBJ,
RES-ML-SUR, RES-ML-TRN, RES-ML-GEN, RES-ML-INT, RES-ML-SPEC,
RES-DAT-OBS, RES-DAT-SCN, RES-DAT-CAS, RES-GEO-TAX, RES-GEO-PAR,
RES-GEO-MET, RES-GEO-REP, RES-GEO-CST, RES-GEO-SPEC, RES-OPT,
RES-OPT-PRB, RES-OPT-OBJ, RES-OPT-CST, RES-OPT-DOE, RES-OPT-BUD and
RES-OPT-SPEC.

## Implications

Simulation: event reconstruction and defence-model credibility are mandatory
before impact-data generation. Simulation provenance, convergence and quality
state become dataset fields.

Geometry: geometry work moves from unrestricted candidate generation to
fixed-rigid wall-type and obstacle/dissipating/array class descriptors,
placement rules and software representation.

Data: the initial dataset is a manifest-driven single-event case
record, with source-data provenance, corridor polygons,
topobathymetry products, observation bundles and replay-boundary
metadata.

Optimisation: optimisation is deferred; current outputs are
comparison metrics and software-facing data requirements.

ML: supervised learning and surrogate use are deferred future work,
dependent on validated hydrodynamic outputs.

## Risks

- Event observations may be insufficient for the intended local validation.
- Corridor target coordinates or widths may change after GIS selection.
- Kamaishi bay-mouth breakwater source geometry may remain missing.
- Barrier geometry bounds may be limited by terrain coverage or mesh quality.
- Simulation cost may restrict data volume and algorithm complexity.
- Hydrodynamic metrics may be over-interpreted as structural damage
  evidence without the deferred structural model.

## Future Extension Path

Future work may add reconstructed real defences, additional events,
broader optimisation, cross-event generalisation, generative geometry,
reinforcement learning, deformable structures, scour or two-way FSI
only after the Tohoku hydrodynamic workflow has been completed,
validated and reviewed.
