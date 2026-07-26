# Boundary GUI Handoff v0.1

The GUI should treat `IBoundaryConditionView` and `BoundaryDescriptor` as the inspection contract.

Recommended GUI-visible fields:

- boundary id and display name;
- patch name and patch id;
- kind;
- value kind and component count;
- entity count;
- unit id;
- executable flag.

The GUI must not infer solver semantics from `named_reference`. It may display the requested type once case-schema ownership exposes it, but execution remains blocked until numerics implements the named behavior.
