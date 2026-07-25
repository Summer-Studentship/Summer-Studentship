# Mesh GUI Handoff v0.1

`tsunami_fvm` remains independent of the Qt GUI.

## Boundary

- Public FVM headers must not include or mention Qt, QObject, QML or GUI-owned types.
- `FiniteVolumeMesh` exposes inspection through immutable topology and geometry accessors.
- Future GUI mesh display must use an application-service adapter or presentation model.
- GUI controls, QML views and direct mesh editing are outside `SWE-FVM-MSH-WP1`.

This keeps the mesh model reusable by solvers, tests and future adapters without making the GUI an owner of numerical storage.
