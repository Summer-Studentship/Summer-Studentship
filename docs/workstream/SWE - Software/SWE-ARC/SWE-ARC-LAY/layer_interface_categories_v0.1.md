# Layer Interface Categories v0.1

- **Work Package:** `SWE-ARC-LAY-WP1`
- **Issues:** `#126`, `#127`, `#128`
- **Document date:** 2026-07-22

This document defines categories only. It intentionally does not create final
C++ class or interface names.

| Category | Owner | Permitted consumers | Permitted implementations | Prohibited types | Public/private expectation | Downstream WP |
|---|---|---|---|---|---|---|
| Foundation value contracts | L1 Foundation | All runtime and verification layers | `tsunami_core` | Qt, CLI11, Catch2, HDF5, GDAL, Matplot++, solver-specific records | Public | `SWE-ARC-ERR-WP1`, `SWE-ARC-API-WP1` |
| Domain model contracts | Owning domain layer: L2, L3, L4, L5 or L6 | Application, adapters, tests and dependent domain layers | Owning domain target | Concrete adapter, UI and plotting types | Public where used across targets | `SWE-ARC-API-WP1` plus domain WPs |
| Service/use-case contracts | L7 Application | CLI, GUI, tests and release tooling through stable entry points | `tsunami_application` | Qt, QML, CLI11, numerical scheme implementation | Public frontend boundary | `SWE-ARC-SVC-WP1`, `SWE-ARC-CASE-WP1` |
| Adapter ports | Inward contract layer; concrete implementation in L8 | Application composition, tests and owning domain contracts | Concrete adapter targets | Reverse dependency from domain to adapter | Port public, implementation private where possible | `SWE-ARC-API-WP1`, adapter WPs |
| Solver extension contracts | L5 solvers and L6 coupling | Application, tests and owning solver/coupling internals | Solver/coupling target implementations | GUI, persistence, GDAL/Gmsh and plotting types | Public only when selected by composition | `SWE-ARC-API-WP1`, solver WPs |
| Observer and diagnostic contracts | L5, L6 and L7 | Application, verification and optional diagnostics adapters | Domain/application observers, diagnostics adapters | Qt and Matplot++ in core contracts | Public neutral records, private concrete sinks | `SWE-ARC-API-WP1`, `SWE-VER-UNIT-WP1` |
| Composition contracts | L7 Application | Application startup and frontends | Application composition boundary | Inner domain selecting concrete adapters directly | Mostly private to composition | `SWE-ARC-SVC-WP1` |
