# Regional 2D NLSWE Research Gate Closure

Date: 2026-07-07

Status: Research specification complete; implementation/validation pending

## Decision

The regional 2D NLSWE mathematical and numerical model is established at
research-specification level. The selected model is a horizontal,
depth-averaged, cell-centred finite-volume solver for the nonlinear
shallow-water equations with conserved state `[h, q_x, q_y]^T`.

The model lock covers the governing equations, positive-upward bathymetry
convention, source-term catalogue, dynamic moving-bed source treatment,
polygonal finite-volume update, reconstruction and limiter, hydrostatic
reconstruction, HLLC/HLL flux strategy, wetting/drying, SSPRK(3,3)
transport, Strang-split Manning friction, CFL timestep, open/radiation
boundary treatment, sponge-layer damping and mesh-refinement principles.

## Closed Research Gate

The adopted process for this gate was:

WBS item -> evidence review -> decision gate -> locked specification -> repo-native documentation update.

The initial regional-model research gate is closed. Future changes to the
regional mathematical model should be treated as revisions to this locked
specification, not as continuation of the initial research-selection task.

## Not Closed

The following remain separate workstreams:

- QGIS/geospatial case-study domain lock;
- final bathymetry/topography dataset selection;
- final fault-source dataset selection;
- numerical verification and validation runs;
- C++ implementation;
- local 3D/interface/FSI modelling;
- optimisation and ML workflow.

## Primary WBS Alignment

Research-specification status applies to the regional-model portions of:

- RES-PHY-SRC, RES-PHY-PRO, RES-PHY-NRS and RES-PHY-SYN;
- RES-DAT-GEO and RES-DAT-SRC, as data requirements rather than final data selections;
- RES-MOD-EQS, RES-MOD-SRC, RES-MOD-IBC and RES-MOD-SPEC;
- RES-NUM-FRM, RES-NUM-MSH, RES-NUM-FLX, RES-NUM-SRC, RES-NUM-TIM, RES-NUM-WET, RES-NUM-IBC and RES-NUM-SPEC;
- RES-VER-SPEC, as required tests rather than executed verification.

## Handoff

Next workstreams are QGIS/geospatial domain lock, data-source selection,
regional verification/validation, software implementation, and local
3D/interface modelling.
