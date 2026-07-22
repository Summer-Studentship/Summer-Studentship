# Target Catalogue v0.1

- **Work Package:** `SWE-ARC-TGT-WP1`
- **Issues:** `#122`, `#123`
- **Policy:** `architecture/target_dependency_policy_v0.1.json`
- **Document date:** 2026-07-22

## Naming Rules

- Implementation targets use lowercase snake-case names in the form
  `tsunami_<responsibility>`.
- Link-facing aliases use the `Tsunami::` namespace only after the concrete
  target exists.
- Target names describe responsibility rather than directory location.
- Executables remain lowercase implementation names.
- Third-party package names do not leak into project target names.
- Target names must not encode a corridor, case or numerical benchmark.
- Implementation targets must not be named after temporary prototypes.
- One target may own several closely related WBS Deliverables only when that
  ownership is explicit.
- Avoid target proliferation where a target would have no coherent independent
  responsibility.

## Active Scaffold

| Target | Alias | Role | WBS owner | Dependencies | External ownership |
|---|---|---|---|---|---|
| `tsunami_core` | `Tsunami::core` | Common foundation types, status/error primitives, cancellation primitives and small platform-independent utilities | `SWE-ARC-ERR`, `SWE-ENV-BLD` | None | None |
| `tsunami_cli` | None | Transitional CLI scaffold | `SWE-ARC-SVC`, `SWE-ENV-BLD` | Temporary: `tsunami_core`; final: `tsunami_application` | CLI11 |
| `tsunami_gui` | None | Transitional Qt Quick shell | `SWE-GUI-SHL`, `SWE-ENV-BLD` | Temporary: `tsunami_core`; final: `tsunami_application` | Qt Core, Gui, Qml, Quick and related Qt tools |
| `tsunami_tests` | None | Catch2 smoke/unit-test scaffold | `SWE-VER-UNIT`, `SWE-ENV-BLD` | Project libraries under test | Catch2 |

## Planned G0/G1

| Target | Reserved alias | Role | WBS owner | Allowed direct project dependencies | External ownership |
|---|---|---|---|---|---|
| `tsunami_application` | `Tsunami::application` | Case lifecycle, validation/preparation/run orchestration, status and cancellation boundary | `SWE-ARC-CASE`, `SWE-ARC-SVC`, `SWE-ARC-ERR` | `tsunami_core`, `tsunami_data`, `tsunami_geo`, `tsunami_r2d`, `tsunami_l3d`, `tsunami_coupling`; concrete adapters only at composition boundaries | None |
| `tsunami_fvm` | `Tsunami::fvm` | Mesh topology, fields, boundary abstractions, discrete operators, linear-system and timestep infrastructure | `SWE-FVM-*` | `tsunami_core` | Eigen, subject to later public-contract acceptance |
| `tsunami_data` | `Tsunami::data` | Versioned configuration, dataset/provenance records, result/checkpoint schema models and persistence-neutral data contracts | `SWE-DAT-CFG`, `SWE-DAT-MAN`, `SWE-DAT-SCH` | `tsunami_core` | yaml-cpp, nlohmann-json |
| `tsunami_io_hdf5` | None | HDF5 result/checkpoint persistence implementation | `SWE-DAT-SCH`, `SWE-DAT-CHK` | `tsunami_core`, `tsunami_data`, `tsunami_fvm` | HDF5, zlib transitively |
| `tsunami_io_xdmf` | None | XDMF descriptor generation | `SWE-DAT-XDMF` | `tsunami_core`, `tsunami_data`, `tsunami_fvm` | pugixml |
| `tsunami_geo` | `Tsunami::geo` | Method-neutral corridor, terrain, barrier, geometry and mesh-transfer contracts | `SWE-GEO-*` | `tsunami_core`, `tsunami_data`, `tsunami_fvm` | None |
| `tsunami_geo_gdal` | None | GDAL/PROJ raster, vector, CRS and datum implementation | `SWE-GEO-IMP`, `SWE-GEO-CRS` | `tsunami_core`, `tsunami_data`, `tsunami_geo` | GDAL, PROJ |
| `tsunami_mesh_gmsh` | None | External Gmsh process invocation, `.geo` generation and MSH 4.1 import | `SWE-GEO-MSH` | `tsunami_core`, `tsunami_data`, `tsunami_geo`, `tsunami_fvm` | Gmsh executable only |

## Planned Later Gate

| Target | Reserved alias | Role | WBS owner | Allowed direct project dependencies | External ownership |
|---|---|---|---|---|---|
| `tsunami_r2d` | `Tsunami::r2d` | Regional two-dimensional NLSWE solver implementation | `SWE-R2D-*` | `tsunami_core`, `tsunami_fvm`, `tsunami_data` | None |
| `tsunami_l3d` | `Tsunami::l3d` | Local three-dimensional URANS-VOF solver implementation | `SWE-L3D-*` | `tsunami_core`, `tsunami_fvm`, `tsunami_data` | None |
| `tsunami_coupling` | `Tsunami::coupling` | Replay, mapping, inlet reconstruction and cross-model metric coordination | `SWE-CPL-*` | `tsunami_core`, `tsunami_fvm`, `tsunami_data`, `tsunami_r2d`, `tsunami_l3d` | None |

## Optional Adapter

| Target | Role | WBS owner | Allowed direct project dependencies | External ownership |
|---|---|---|---|---|
| `tsunami_diagnostics_matplot` | Optional plotting diagnostics adapter | `SWE-VER-ACC`, `SWE-REL-DOC` | `tsunami_core`, `tsunami_application` or a future observer contract | Matplot++ |

## Test/Development Target

| Target | Role | Dependency rule |
|---|---|---|
| `tsunami_tests` | Test executable and CTest registration owner | May depend on project libraries under test; no production target may depend on it |

## External Tool

| Tool | Owning target | Boundary |
|---|---|---|
| Gmsh executable | `tsunami_mesh_gmsh` | External process, `.geo` and MSH 4.1 files; no accepted direct Gmsh library link |
| ParaView | None | XDMF/HDF5 interoperability boundary only; no project target links ParaView |
