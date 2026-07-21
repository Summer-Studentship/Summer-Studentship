# RES - Research

The Research workstream follows the hierarchy `Workstream -> Domain -> Deliverable`.

Research stops at Deliverable level. Implementation Tasks and Work Packages belong to Software or later execution workstreams, not to this Research scaffold.

Progress states are:

- Not started
- In progress
- Draft complete
- Reviewed
- Accepted

Deliverable acceptance requires the research questions to be answered, claims to be supported by appropriate evidence, assumptions and limitations to be explicit, project-specific conclusions to be stated, downstream requirements to be traceable, and unresolved decisions to be closed or formally deferred.

Expected scaffold counts:

- 10 Domain directories
- 74 Deliverable directories
- 10 Domain master `.tex` files
- 10 Domain `.bib` files
- 74 Deliverable `.tex` fragments

## Current Research-Phase Progress

Current scope decision:

- The active project scope is impact modelling for the 2011 Tohoku tsunami,
  local comparison between the Kamaishi and Sendai coastal-plain corridors,
  and fixed-rigid barrier-class comparison.
- The modelling baseline is regional 2D NLSWE finite-volume propagation
  driving local 3D incompressible two-phase URANS--VOF barrier interaction
  through one-way replay. Simultaneous coupling remains a higher-fidelity
  option only where barrier feedback materially affects outputs.
- Active barrier geometry classes are wall-type and obstacle, dissipating or
  array-type barriers. ML, surrogate optimisation, generative geometry,
  material damage, deformable structures, scour and two-way FSI are deferred
  until hydrodynamic load extraction and validation are stable.
- Data handling now begins in parallel with remaining Research through a
  manifest-driven workflow for source data, CRS/datum provenance, corridor
  polygons, terrain clipping/interpolation, structured outputs and replay
  boundary format.

Research gates:

- G1 -- Data-source and provenance feasibility.
- G2 -- Evidence-based Tohoku corridor definition.
- G3 -- Regional event reconstruction and replay-boundary readiness.
- G4 -- Local 3D hydrodynamic capability and fixed-barrier metric extraction.
- G5 -- V1--V2--V3 verification/validation evidence.
- G6 -- Barrier-class comparison demonstration.

Completed:

- Governing architecture consolidated into three equation systems.
- Regional NLSWE model selected.
- Local three-dimensional two-phase Navier-Stokes model selected.
- Structural dynamics and FSI hierarchy selected.
- Regional numerical method families screened.
- Spectral methods excluded from the active regional solver comparison.
- FDM reviewed and retained as a reference branch.
- FVM reviewed and selected as the regional numerical family.
- Regional 2D NLSWE mathematical and numerical model established at
  research-specification level.
- Regional HLLC/HLL flux strategy, MUSCL/least-squares/Barth--Jespersen
  reconstruction, hydrostatic reconstruction, wetting--drying,
  SSPRK(3,3), Strang-split Manning friction, CFL timestep, open/radiation
  boundaries, sponge damping and mesh-refinement principles specified.
- LBM/DBM reviewed and removed from the active regional shortlist.
- VOLNA contextual validation reviewed.
- OpenFOAM adopted as the local 3D software and architectural reference.

In progress:

- Local 3D lower-level numerical choices.
- Tohoku data-source finalisation and provenance manifest design.
- Kamaishi and Sendai corridor geometry lock.
- 2D-3D numerical coupling selection.
- Hydrodynamic load-output schema and structural handoff requirements.
- QGIS/geospatial domain lock and regional validation planning.

Not yet started or unresolved:

- Final regional dry-depth tolerance and velocity regularisation constants.
- Final regional HLL/HLLC wave-speed safeguards.
- Final regional mesh levels, CFL value and domain-specific sponge placement.
- Final bathymetry/topography and fault-source dataset selections.
- Local free-surface method.
- Local pressure-velocity algorithm.
- Final geometry file format and barrier-class parameter bounds.
- Structural element, damage model and FSI coupling algorithm are deferred.
- Numerical 2D-3D transfer operators.
