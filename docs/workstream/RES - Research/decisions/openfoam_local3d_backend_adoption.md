# OpenFOAM Local3D Backend Adoption

Date: 2026-08-06

Status: Accepted for the G6 theoretical hybrid model baseline

## Decision

OpenFOAM Foundation 11 is the numerical implementation authority for the
Local3D incompressible two-phase URANS-VOF baseline.

OpenFOAM is an adopted backend, not the governing research authority. The
research model, interface contracts and accepted formulation remain
authoritative for the studentship. Repository evidence must continue to show
how each mathematical component maps either to repository-native code, adopted
OpenFOAM behavior, or an external geospatial/scientific library.

## Repository Responsibilities

- coupling reconstruction;
- case generation;
- model selection;
- boundary-condition definition;
- execution orchestration;
- acceptance validation;
- result extraction;
- provenance.

## OpenFOAM Responsibilities

- VOF transport;
- momentum discretisation;
- pressure-velocity coupling;
- k-omega SST transport;
- wall-function evaluation;
- transient solver sequence.

## Advantages

- Provides a mature, inspectable implementation of incompressible two-phase
  URANS-VOF behavior without adding an unvalidated repository-native Local3D
  solver.
- Keeps the repository focused on the novel hybrid contract: source, regional
  propagation, coupling reconstruction, case generation, validation and
  provenance.
- Enables reproducible smoke and real-data runs through a pinned container image.
- Separates accepted backend behavior from research authority, so future model
  changes remain explicit.

## Limitations

- OpenFOAM internals are not repository-native code and must not be described as
  such in WBS closure evidence.
- Repository control over rejected-step recovery is limited to generated
  dictionaries, runtime log inspection and documented backend behavior.
- Current lateral side treatment uses symmetryPlane in the replay baseline and
  still needs production open-ocean policy evidence.
- Wall-function applicability requires y+ or approved surrogate evidence before
  the G6 gate can close.
- The current rigid wall is a simplified baseline, not an official CAD or full
  barrier-class comparison.

## Verification Obligations

- Record OpenFOAM image, digest and Foundation version for accepted runs.
- Validate generated dictionaries, field set, boundaryData coverage and
  function-object outputs.
- Reject runs with fatal OpenFOAM errors, floating-point exceptions, nonfinite
  final fields, incomplete replay windows, missing probe output or missing force
  output where required.
- Maintain theory-to-implementation traceability for every G6 mathematical row.

## Validation Obligations

Observation calibration, observation validation, mesh convergence, timestep
convergence and approved impact/load validation remain post-G6 work. They must
not be claimed as complete by backend adoption alone.

## Replacement Conditions

Replace or supplement the OpenFOAM backend if:

- the accepted research formulation diverges materially from available
  OpenFOAM behavior;
- required boundary, wall, turbulence or coupling evidence cannot be produced;
- licensing, reproducibility or container availability becomes unacceptable;
- a repository-native Local3D solver reaches equal or stronger acceptance
  evidence.

## Version Pinning

The accepted baseline uses:

- image: `docker.io/openfoam/openfoam11-paraview510:11`;
- observed digest from PR #270: `sha256:fd10956e0b1eb70f9808baf2857e4baf846a0f6f272f73b6d00546eae96be181`;
- observed image id from PR #270:
  `7f8a8af7c4c5884a41a61e42f5a18e037f46a114ae91196f4154a4cdac1e4f93`;
- solver: `foamRun -solver incompressibleVoF`;
- turbulence model: `kOmegaSST` in `constant/momentumTransport`.
