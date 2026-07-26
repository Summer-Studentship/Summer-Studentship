# Boundary Numerics Handoff v0.1

`SWE-FVM-BC-WP1` provides validated boundary data movement only.

Numerics may depend on:

- complete `BoundaryConditionSet<Value>` coverage for every mesh patch;
- deterministic patch ordering;
- fixed-value prescribed patch fields;
- zero-gradient owner-cell copies;
- non-executable named placeholders that fail deterministically.

Numerics still owns:

- flux equations;
- residual/operator assembly;
- time integration;
- physical interpretation of named boundary types;
- finite-state and value-range validation.
