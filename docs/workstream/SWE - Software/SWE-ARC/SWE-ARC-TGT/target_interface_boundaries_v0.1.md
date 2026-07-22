# Target Interface Boundaries v0.1

- **Work Package:** `SWE-ARC-TGT-WP1`
- **Issues:** `#122`, `#124`
- **Policy:** `architecture/target_dependency_policy_v0.1.json`
- **Document date:** 2026-07-22

## Scope

This document defines target-level public/private boundaries: CMake visibility,
dependency ownership and target responsibility. It does not define final C++
namespaces, headers, abstract interfaces or class names. Detailed layer
ownership belongs to `SWE-ARC-LAY-WP1`; detailed C++ interface contracts belong
to `SWE-ARC-API-WP1`.

## Boundary Catalogue

| Target | Public responsibility | Private implementation responsibility | Permitted public dependency types | Prohibited public dependency types | Owned third-party dependency | External types in public headers | WBS owner | Implementing WP |
|---|---|---|---|---|---|---|---|---|
| `tsunami_core` | Common status, error, cancellation and utility primitives | Platform-independent helper implementation | C++ standard library only unless later accepted | Qt, CLI11, Catch2, GDAL, HDF5, Matplot++, solver/domain types | None | No | `SWE-ARC-ERR`, `SWE-ENV-BLD` | `SWE-ARC-ERR-WP1`, later layer/API work |
| `tsunami_fvm` | Mesh, field, boundary, operator and timestep contracts | Storage and reusable numerical infrastructure | `tsunami_core`; Eigen only if accepted by API work | Qt, CLI11, Catch2, GDAL, HDF5, Matplot++ | Eigen | Not by default | `SWE-FVM-*` | `SWE-FVM-MSH-WP1`, `SWE-FVM-FLD-WP1`, `SWE-FVM-BC-WP1`, `SWE-FVM-NUM-WP1` |
| `tsunami_data` | Persistence-neutral configuration, provenance and schema models | Parsing/validation internals and neutral schema utilities | `tsunami_core`; yaml/json types only where accepted | HDF5, GDAL, Qt, CLI11, Catch2, Matplot++ | yaml-cpp, nlohmann-json | Only after API acceptance | `SWE-DAT-CFG`, `SWE-DAT-MAN`, `SWE-DAT-SCH` | `SWE-DAT-CFG-WP1`, `SWE-DAT-MAN-WP1`, `SWE-DAT-SCH-WP1` |
| `tsunami_io_hdf5` | HDF5 persistence adapter boundary | HDF5 group/dataset/checkpoint implementation | `tsunami_data`, `tsunami_fvm`, `tsunami_core` | Qt, CLI11, Catch2, GDAL, Matplot++ | HDF5, zlib | Adapter headers only, not domain model headers | `SWE-DAT-SCH`, `SWE-DAT-CHK` | `SWE-DAT-SCH-WP1` |
| `tsunami_io_xdmf` | XDMF descriptor adapter boundary | XML emission and ParaView descriptor mapping | `tsunami_data`, `tsunami_fvm`, `tsunami_core` | ParaView libraries, Qt, CLI11, Catch2, HDF5 ownership | pugixml | Adapter headers only | `SWE-DAT-XDMF` | `SWE-DAT-XDMF-WP1` |
| `tsunami_geo` | Method-neutral corridor, terrain, geometry, barrier and mesh-transfer contracts | Geometry preparation algorithms without concrete library ownership | `tsunami_core`, `tsunami_data`, `tsunami_fvm` | GDAL, PROJ, Gmsh, HDF5, Qt, CLI11, Catch2 | None | No concrete GDAL/PROJ/Gmsh types | `SWE-GEO-*` | `SWE-GEO-*` WPs |
| `tsunami_geo_gdal` | GDAL/PROJ import and CRS adapter boundary | Raster/vector import, transformation and metadata extraction | `tsunami_geo`, `tsunami_data`, `tsunami_core` | Qt, CLI11, Catch2, HDF5, Matplot++ | GDAL, PROJ | Adapter headers only | `SWE-GEO-IMP`, `SWE-GEO-CRS` | `SWE-GEO-IMP-WP1`, `SWE-GEO-CRS-WP1` |
| `tsunami_mesh_gmsh` | Gmsh process and MSH import adapter boundary | `.geo` generation, process invocation and MSH 4.1 parsing | `tsunami_geo`, `tsunami_data`, `tsunami_fvm`, `tsunami_core` | Direct Gmsh library linkage, Qt, CLI11, Catch2, HDF5 | Gmsh executable | No direct Gmsh library types | `SWE-GEO-MSH` | `SWE-GEO-MSH-WP1` |
| `tsunami_r2d` | Regional NLSWE solver boundary | Regional numerical algorithms | `tsunami_core`, `tsunami_fvm`, `tsunami_data` | GUI, CLI11, concrete adapters, local solver, coupling, Catch2 | None | No adapter or UI types | `SWE-R2D-*` | Later `SWE-R2D-*` WPs |
| `tsunami_l3d` | Local URANS-VOF solver boundary | Local numerical algorithms | `tsunami_core`, `tsunami_fvm`, `tsunami_data` | GUI, CLI11, concrete adapters, regional solver, coupling, Catch2 | None | No adapter or UI types | `SWE-L3D-*` | Later `SWE-L3D-*` WPs |
| `tsunami_coupling` | Replay, mapping and cross-model metric boundary | Coupling-domain orchestration and analysis | `tsunami_core`, `tsunami_fvm`, `tsunami_data`, `tsunami_r2d`, `tsunami_l3d` | GUI, CLI11, concrete adapters, Catch2 | None | No adapter or UI types | `SWE-CPL-*` | Later `SWE-CPL-*` WPs |
| `tsunami_application` | Case lifecycle and run orchestration boundary | Implementation selection and composition | Domain contracts and adapters at composition boundary | Qt, CLI11, Catch2, Matplot++ | None | No UI framework types | `SWE-ARC-CASE`, `SWE-ARC-SVC`, `SWE-ARC-ERR` | `SWE-ARC-CASE-WP1`, `SWE-ARC-SVC-WP1`, `SWE-ARC-ERR-WP1` |
| `tsunami_cli` | Command-line frontend | CLI parsing and application-service invocation | `tsunami_application`; transitional `tsunami_core` | Qt, Catch2, concrete solver/FVM/data adapter ownership | CLI11 | CLI target only | `SWE-ARC-SVC`, `SWE-ENV-BLD` | `SWE-ARC-SVC-WP1` |
| `tsunami_gui` | Qt Quick frontend | QML shell, navigation and user workflows | `tsunami_application`; transitional `tsunami_core` | CLI11, Catch2, solver/FVM/HDF5/GDAL/Gmsh ownership | Qt Core, Gui, Qml, Quick | GUI target only | `SWE-GUI-SHL`, `SWE-ENV-BLD` | `SWE-GUI-SHL-WP1` |
| `tsunami_diagnostics_matplot` | Optional plotting diagnostics adapter | Plot generation from neutral diagnostics | `tsunami_core`, `tsunami_application` or observer contract | Required numerical/application dependency, Qt, CLI11, Catch2 | Matplot++ | Adapter headers only | `SWE-VER-ACC`, `SWE-REL-DOC` | Later diagnostics/documentation work |
| `tsunami_tests` | Verification executable and CTest owner | Test cases and smoke checks | Any project library under test | Production dependency on tests, frontend-to-test leakage | Catch2 | Test target only | `SWE-VER-UNIT`, `SWE-ENV-BLD` | `SWE-VER-UNIT-WP1` |
