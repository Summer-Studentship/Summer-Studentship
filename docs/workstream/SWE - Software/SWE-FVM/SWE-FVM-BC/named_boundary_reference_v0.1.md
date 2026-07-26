# Named Boundary Reference v0.1

`NamedBoundaryReference<Value>` preserves an accepted but not-yet-implemented boundary type by name.

Examples in the fixtures include:

- `radiation`
- `relaxation`

Named references are descriptor-visible and have `executable=false`. Applying one always fails with `fvm.boundary.named_condition_not_executable`, includes `requested_type` in diagnostic context and leaves the target field unchanged.

This gives GUI and case-schema work a stable placeholder without inventing solver behavior.
