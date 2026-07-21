# Build, Dependencies and Packaging Workstream

## Initial Hierarchy

```text
SW-BDP - Build, Dependencies and Packaging

SW-BDP-BLD - Build-System Configuration
|-- SW-BDP-BLD-01 - Root CMake Foundation
|-- SW-BDP-BLD-02 - Component Target Architecture
|-- SW-BDP-BLD-03 - Include and Linkage Model
|-- SW-BDP-BLD-04 - Cross-Platform Presets and Toolchains
|-- SW-BDP-BLD-05 - Build Options and Feature Flags
|-- SW-BDP-BLD-06 - Resources and Generated Files
|-- SW-BDP-BLD-07 - Testing Integration
`-- SW-BDP-BLD-08 - Installation Rules
```

## Provisional Execution Order

1. `SW-BDP-BLD-04` - Preset reconciliation.
2. `SW-BDP-BLD-01` and `SW-BDP-BLD-05` - Root option cleanup.
3. `SW-BDP-BLD-02` and `SW-BDP-BLD-03` - Component and linkage architecture.
4. Dependency target integration.
5. GUI architecture decision and target normalisation.
6. `SW-BDP-BLD-07` - Minimal CTest suite.
7. Docker verification strengthening.
8. `SW-BDP-BLD-08` - Installation and packaging design.

Later items remain provisional until their preceding work is reviewed.

## Current Executable Task

```text
SW-BDP-BLD-04-01 - Reconcile cross-platform presets, scripts and build documentation
```

This task is the first executable item in the build workstream. It reconciles
shared CMake preset names, vcpkg toolchain expectations, wrapper scripts, and
build documentation without changing solver, GUI, test, packaging, or CI
implementation.
