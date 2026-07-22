# Layer Dependency Rules v0.1

- **Work Package:** `SWE-ARC-LAY-WP1`
- **Issues:** `#126`, `#128`
- **Policy:** `architecture/layer_ownership_policy_v0.1.json`
- **Document date:** 2026-07-22

## Rules

1. **Inward dependency rule:** dependencies move toward stable inner contracts. Outer layers may depend on inner layers; inner layers must not depend on outer layers.
2. **Domain ownership rule:** each behaviour has one owning WBS domain and one governing layer. Consumers use interfaces rather than duplicate implementation.
3. **Adapter inversion rule:** concrete adapters depend on domain/application ports. Domain/application contracts do not depend on concrete adapters.
4. **UI isolation rule:** Qt and QML remain within `tsunami_gui` and explicit GUI adapters. They do not enter `tsunami_core`, `tsunami_fvm`, `tsunami_data`, `tsunami_geo`, `tsunami_r2d`, `tsunami_l3d`, `tsunami_coupling` or `tsunami_application`.
5. **CLI isolation rule:** CLI11 remains in `tsunami_cli`.
6. **Persistence isolation rule:** HDF5 and XDMF implementation details remain in adapters. Numerical and domain targets consume persistence-neutral models or ports.
7. **Geospatial isolation rule:** GDAL, PROJ and Gmsh implementation types remain in concrete adapters.
8. **Solver separation rule:** R2D and L3D do not depend on each other. Cross-model relationships are owned by coupling.
9. **Shared-numerics rule:** generic finite-volume infrastructure belongs to `tsunami_fvm`; solver-specific equations remain in their solver targets.
10. **Application orchestration rule:** the application layer coordinates domains and approved adapters but does not implement numerical schemes.
11. **Frontend leaf rule:** GUI and CLI are leaf consumers. No production library depends on either frontend.
12. **Verification rule:** verification may depend on production code; production code must not depend on tests, Catch2 or benchmark harnesses.
13. **Infrastructure/runtime rule:** build and release tooling may operate on runtime targets; runtime targets do not depend on CI, packaging or project-management scripts.
14. **Deferred extension rule:** HPC and structural extensions use stable extension points and must not force dependencies into the serial hydrodynamic baseline.

## Prohibited Coupling Cases

| Prohibited case | Why prohibited | Correct owning layer | Correct dependency or adapter path |
|---|---|---|---|
| `tsunami_core -> Qt6::Quick` | Foundation must be UI-free | L9 Frontends | `tsunami_gui -> tsunami_application/core contracts` |
| `tsunami_core -> CLI11::CLI11` | Foundation must be CLI-free | L9 Frontends | `tsunami_cli -> tsunami_application/core contracts` |
| `tsunami_fvm -> tsunami_r2d` | Shared numerics must not depend on a solver | L5 Solver Domains | `tsunami_r2d -> tsunami_fvm` |
| `tsunami_fvm -> tsunami_l3d` | Shared numerics must not depend on a solver | L5 Solver Domains | `tsunami_l3d -> tsunami_fvm` |
| `tsunami_data -> tsunami_io_hdf5` | Data contracts must be persistence-neutral | L8 Infrastructure Adapters | `tsunami_io_hdf5 -> tsunami_data` |
| `tsunami_geo -> tsunami_geo_gdal` | Geospatial domain must not depend on concrete GDAL | L8 Infrastructure Adapters | `tsunami_geo_gdal -> tsunami_geo` |
| `tsunami_geo -> tsunami_mesh_gmsh` | Mesh domain must not depend on concrete Gmsh adapter | L8 Infrastructure Adapters | `tsunami_mesh_gmsh -> tsunami_geo` |
| `tsunami_r2d -> tsunami_l3d` | Solvers are independent siblings | L6 Coupling | `tsunami_coupling` consumes both |
| `tsunami_l3d -> tsunami_r2d` | Solvers are independent siblings | L6 Coupling | `tsunami_coupling` consumes both |
| `tsunami_r2d -> tsunami_coupling` | Solver must not depend on replay/coupling owner | L6 Coupling | `tsunami_coupling -> tsunami_r2d` |
| `tsunami_l3d -> tsunami_coupling` | Solver must not depend on replay/coupling owner | L6 Coupling | `tsunami_coupling -> tsunami_l3d` |
| `tsunami_application -> Qt6::Quick` | Application must be frontend-neutral | L9 Frontends | `tsunami_gui -> tsunami_application` |
| `tsunami_application -> CLI11::CLI11` | Application must be frontend-neutral | L9 Frontends | `tsunami_cli -> tsunami_application` |
| `tsunami_gui -> HDF5` | GUI must not own persistence | L8 Infrastructure Adapters | GUI asks application for results; `tsunami_io_hdf5` persists |
| `tsunami_gui -> GDAL` | GUI must not own geospatial import | L8 Infrastructure Adapters | GUI asks application; `tsunami_geo_gdal` imports |
| `tsunami_gui -> Gmsh library` | GUI must not own mesh generation implementation | L8 Infrastructure Adapters | `tsunami_mesh_gmsh` invokes external Gmsh process |
| `production target -> tsunami_tests` | Tests must not leak into production | L10 Verification | `tsunami_tests -> production target` |
| `runtime target -> CI or packaging script` | Runtime must not depend on delivery tooling | L0 Development and Delivery Infrastructure | Tooling invokes runtime builds/tests |
