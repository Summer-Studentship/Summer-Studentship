# Regional2D Boundary Framework v0.1

`SWE-FVM-BC-WP1` defines boundary-condition identity, patch mapping and generic executable placeholders for Stage 1 Regional2D finite-volume work.

The framework lives in `tsunami_fvm`, remains Qt-free and links only to `tsunami_core`. It consumes the accepted mesh and field contracts from `SWE-FVM-MSH-WP1` and `SWE-FVM-FLD-WP1`.

## Public Types

- `BoundaryConditionId` is the stable string identity.
- `BoundaryConditionKind` has `fixed_value`, `zero_gradient` and `named_reference`.
- `BoundaryDescriptor` carries id, name, mesh id, patch id/name, kind, value kind, component count, entity count, unit id and executable flag.
- `IBoundaryConditionView` exposes descriptor inspection only.
- `BoundaryCondition<Value>` owns one concrete operation variant.
- `BoundaryConditionSet<Value>` owns exactly one condition for every mesh boundary patch.

## Exclusions

No governing equations, flux operators, solver assembly, GUI controls, CLI parsing, persistence, Local3D coupling or registration system is introduced here.
