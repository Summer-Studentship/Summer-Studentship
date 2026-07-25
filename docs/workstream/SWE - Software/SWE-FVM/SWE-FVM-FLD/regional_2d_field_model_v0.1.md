# Regional2D Field Model v0.1

`SWE-FVM-FLD-WP1` defines the fixed-size typed field subsystem for the standalone Regional2D finite-volume model.

## Role

Regional2D state will initially represent conserved quantities as separate scalar cell fields:

- water depth `h`;
- x-momentum `hu`;
- y-momentum `hv`.

Separate scalar fields keep storage simple for early operators and output adapters. Vector fields are also supported for gradients, velocity-like inspection values and geometry-related test data.

## Associations

- Cell field: one value per mesh cell.
- Face field: one value per mesh face.
- Boundary-patch field: one value per ordered boundary face in one selected patch.

## Values

- Scalar: `tsunami::core::Real`.
- Vector: `tsunami::fvm::Vector3`.

The generic field container permits any representable value, including negative and nonfinite values. Physical admissibility and finite-state checks belong to Regional2D state/operator layers.

## Exclusions

This work package does not implement interpolation, gradients, divergence, fluxes, NLSWE state objects, boundary-condition algorithms, persistence, GUI controls, Local3D fields, MPI or GPU storage.
