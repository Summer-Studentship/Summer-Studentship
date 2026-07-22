# Target Dependency Rules v0.1

- **Work Package:** `SWE-ARC-TGT-WP1`
- **Issues:** `#122`, `#123`, `#125`
- **Policy:** `architecture/target_dependency_policy_v0.1.json`
- **Document date:** 2026-07-22

## Rule 1 - Inward Ownership

Outer targets may depend on inner targets. Inner targets must not depend on
outer targets.

Conceptual direction:

```text
Frontends
-> Application
-> Coupling and domain solvers
-> Data/geospatial/FVM contracts
-> Core
```

## Rule 2 - Adapters Implement Inward Contracts

A concrete adapter may depend on the contract or domain target it implements.
The contract or domain target must not depend on that concrete adapter.

Allowed examples:

```text
tsunami_io_hdf5 -> tsunami_data
tsunami_geo_gdal -> tsunami_geo
tsunami_mesh_gmsh -> tsunami_geo
```

Forbidden reverse edges:

```text
tsunami_data -> tsunami_io_hdf5
tsunami_geo -> tsunami_geo_gdal
tsunami_geo -> tsunami_mesh_gmsh
```

## Rule 3 - Frontends Are Leaves

`tsunami_cli` and `tsunami_gui` must not be linked by production library
targets.

## Rule 4 - Framework Isolation

- Qt targets may appear only in `tsunami_gui` and future explicit Qt adapters.
- CLI11 may appear only in `tsunami_cli` or a CLI-specific adapter.
- Catch2 may appear only in test targets.
- Matplot++ may appear only in `tsunami_diagnostics_matplot`.

## Rule 5 - Solver Independence

`tsunami_r2d` and `tsunami_l3d` must not depend directly on one another.
One-way replay ownership belongs to `tsunami_coupling`.

## Rule 6 - Persistence Inversion Prevention

Numerical and domain targets depend on persistence-neutral models or
interfaces, not concrete HDF5 implementation details. `tsunami_data` does not
depend on `tsunami_io_hdf5`.

## Rule 7 - Executable Isolation

No reusable project library may depend on an executable target.

## Rule 8 - Acyclic Project Graph

The project-owned target graph must remain a directed acyclic graph.

## Rule 9 - Tests Do Not Leak Into Production

Production targets must not depend on `tsunami_tests` or Catch2.

## Rule 10 - Public-Header Discipline

A target public header must not require consumers to link an undeclared
transitive dependency. Use `PUBLIC`, `PRIVATE` and `INTERFACE` links according
to actual API exposure. Do not use `PUBLIC` merely for convenience.

## Review and Machine Checks

`tools/architecture/validate_target_graph.py` enforces the machine-readable
policy. It checks unique target names, known dependency references, acyclicity,
prohibited edges, frontend/test leakage, adapter inversions, framework
ownership and active CMake graph edges from generated Graphviz files.
