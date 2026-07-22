# Domain Layer Ownership Matrix v0.1

- **Work Package:** `SWE-ARC-LAY-WP1`
- **Issues:** `#126`, `#127`
- **Policy:** `architecture/layer_ownership_policy_v0.1.json`
- **Document date:** 2026-07-22

| Domain | Governing owning layer | Primary targets or tooling | Runtime/build-time | Interface boundary | Notes |
|---|---|---|---|---|---|
| `SWE-ENV` | L0 Development and Delivery Infrastructure | CMake, vcpkg, presets, licence/dependency docs | Build-time | Produces build configuration | No runtime ownership |
| `SWE-ARC` | L7 Application and Orchestration | `tsunami_application`; subordinate foundation in `tsunami_core` | Runtime | Cross-cutting contracts, case/service/error use cases | Governing layer is L7; foundation primitives live in L1 |
| `SWE-FVM` | L2 Shared Numerical Core | `tsunami_fvm` | Runtime | Generic numerical abstractions | Solver-independent |
| `SWE-DAT` | L3 Data Contracts | `tsunami_data`; subordinate I/O adapters in L8 | Runtime | Persistence-neutral models and adapter ports | HDF5/XDMF remain concrete adapters |
| `SWE-GEO` | L4 Geospatial and Geometry Domain | `tsunami_geo`; subordinate GDAL/Gmsh adapters in L8 | Runtime/preprocessing | Method-neutral domain contracts | External types remain private to adapters |
| `SWE-R2D` | L5 Solver Domains | `tsunami_r2d` | Runtime | Regional solver contract | No L3D dependency |
| `SWE-L3D` | L5 Solver Domains | `tsunami_l3d` | Runtime | Local solver contract | No R2D dependency |
| `SWE-CPL` | L6 Coupling and Analysis Domain | `tsunami_coupling` | Runtime | Replay, mapping and comparison boundary | Owns one-way cross-model coupling |
| `SWE-GUI` | L9 Frontends | `tsunami_gui` | Runtime | Application use-case boundary | Qt isolated |
| `SWE-VER` | L10 Verification | `tsunami_tests`; optional diagnostics adapter in L8 | Development/test | May consume production interfaces | No reverse dependency |
| `SWE-HPC` | L11 Deferred Execution and Structural Extensions | Future execution backends | Deferred | Stable execution-policy seams | Post-verification |
| `SWE-STR` | L11 Deferred Execution and Structural Extensions | Future structural targets | Deferred | Hydrodynamic-load interface | Stretch scope |
| `SWE-REL` | L0 Development and Delivery Infrastructure | CI, packaging, release docs | Build/release | Consumes build artefacts | No numerical ownership |

All 13 Software WBS domains have exactly one governing layer. Domains with
adapter or foundation sub-placement record that placement without creating a
second governing owner.
