# Architectural Layers v0.1

- **Work Package:** `SWE-ARC-LAY-WP1`
- **Issues:** `#126`, `#127`
- **Policy:** `architecture/layer_ownership_policy_v0.1.json`
- **Document date:** 2026-07-22

## Canonical Layers

| ID | Layer | Classification | Owning WBS domains | Primary targets/tooling | Namespace roots | Provisional paths |
|---|---|---|---|---|---|---|
| L0 | Development and Delivery Infrastructure | Build/release | `SWE-ENV`, `SWE-REL` | CMake, vcpkg, presets, CI, packaging, docs, validators | None | `cmake/`, `.github/`, `tools/`, `docs/` |
| L1 | Foundation | Runtime | `SWE-ARC` foundation contracts | `tsunami_core` | `tsunami::core` | `src/core/` |
| L2 | Shared Numerical Core | Runtime | `SWE-FVM` | `tsunami_fvm` | `tsunami::fvm` | `src/fvm/` |
| L3 | Data Contracts | Runtime | `SWE-DAT` | `tsunami_data` | `tsunami::data` | `src/data/` |
| L4 | Geospatial and Geometry Domain | Runtime/preprocessing | `SWE-GEO` | `tsunami_geo` | `tsunami::geo` | `src/geo/` |
| L5 | Solver Domains | Runtime | `SWE-R2D`, `SWE-L3D` | `tsunami_r2d`, `tsunami_l3d` | `tsunami::r2d`, `tsunami::l3d` | `src/r2d/`, `src/l3d/` |
| L6 | Coupling and Analysis Domain | Runtime | `SWE-CPL` | `tsunami_coupling` | `tsunami::coupling` | `src/coupling/` |
| L7 | Application and Orchestration | Runtime | `SWE-ARC` case/service/error | `tsunami_application` | `tsunami::application` | `src/application/` |
| L8 | Infrastructure Adapters | Runtime adapters | `SWE-DAT`, `SWE-GEO`, `SWE-VER` subordinate placement | `tsunami_io_hdf5`, `tsunami_io_xdmf`, `tsunami_geo_gdal`, `tsunami_mesh_gmsh`, `tsunami_diagnostics_matplot` | `tsunami::adapters::*` | `src/adapters/...` |
| L9 | Frontends | Runtime leaves | `SWE-GUI`; CLI under architecture/application concerns | `tsunami_cli`, `tsunami_gui` | `tsunami::cli`, `tsunami::gui` | `apps/tsunami_cli/`, `apps/tsunami_gui/` |
| L10 | Verification | Development/test | `SWE-VER` | `tsunami_tests`, future harnesses | Test-local | `tests/unit/`, `tests/acceptance/`, `tests/benchmarks/`, `tests/regression/`, `tests/convergence/`, `tests/validation/` |
| L11 | Deferred Execution and Structural Extensions | Deferred | `SWE-HPC`, `SWE-STR` | Future accepted targets only | `tsunami::hpc`, `tsunami::structural` | No G0 production path |

## Direction

Dependencies move toward stable inner contracts unless a documented composition
boundary explicitly selects an adapter. Frontends are leaves. Verification may
consume production code. Build/release infrastructure may inspect runtime
artefacts but is not part of the runtime target graph.

## Handoffs

This document fixes placement and ownership. Concrete C++ interface class names
belong to `SWE-ARC-API-WP1`; simulation-case lifecycle details belong to
`SWE-ARC-CASE-WP1`.
