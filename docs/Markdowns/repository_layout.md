# Repository Layout

This repository should grow around a small number of stable ownership areas:

```text
.
|-- apps/
|   |-- tsunami_cli/         # Active command-line scaffold
|   `-- tsunami_gui/         # Active Qt Quick application scaffold
|-- cmake/                   # Project CMake helper modules
|-- data/                    # Data staging convention; bulk data ignored by Git
|-- docs/                    # Human docs, Doxygen config, build notes
|-- matlab/                  # MATLAB analysis and visualisation scripts
|-- python/                  # ML experiments, training scripts, notebooks
|-- src/
|   |-- core/                # Active tsunami_core foundation target
|   |-- gui/                 # Historical GUI prototype
|   |-- io/                  # Planned data adapters, for example HDF5/XDMF
|   |-- models/              # Historical model organisation; planned solver targets are policy-led
|   `-- visualization/       # Plotting-neutral output and optional diagnostics adapters
`-- tests/                   # C++ tests
```

Treat the layout above as the intended direction. The historical GUI prototype
is not part of the active CMake graph; new GUI work targets `apps/tsunami_gui`.

## CMake Structure

Keep the root `CMakeLists.txt` short. Its job should be:

- set the project name, C++ standard, and global options
- load dependency helpers from `cmake/`
- add subdirectories for libraries, apps, examples, and tests

Each major component should own its own `CMakeLists.txt`. The accepted target
catalogue is recorded in
[`target_catalogue_v0.1.md`](../workstream/SWE%20-%20Software/SWE-ARC/SWE-ARC-TGT/target_catalogue_v0.1.md).
For example:

```text
src/core/CMakeLists.txt       -> tsunami_core library
src/io_hdf5/CMakeLists.txt    -> tsunami_io_hdf5 adapter
apps/tsunami_gui/CMakeLists.txt -> tsunami_gui executable
tests/CMakeLists.txt          -> test executables
```

Prefer linking targets together instead of sharing global include directories.
For example, the GUI should link to the application boundary once it exists:

```cmake
target_link_libraries(tsunami_gui PRIVATE tsunami_application Qt6::Quick)
```

## Third-party boundaries

Package-managed libraries are restored from `vcpkg.json`; external applications
use the routes in `dependencies.md`. Do not vendor or clone third-party source
into `external/` as an undocumented alternative acquisition path.

When `SWE-ENV-BLD-WP1` creates the target scaffold, optional Matplot++ use must
sit behind a project-owned diagnostics adapter and be enabled only with the
`diagnostics` dependency group. Numerical code emits plotting-neutral arrays and
metadata and must not include plotting APIs. Matplot++ is not a replacement for
the externally installed ParaView field-visualisation workflow.

## Data

Use `data/` for local data organisation, but keep large files out of Git. Store
small, curated examples under `data/examples/` only when they are useful for
tests or documentation.

The recommended staging pattern is:

```text
data/
|-- raw/           # Immutable source data
|-- interim/       # Converted or cleaned data
|-- processed/     # Model-ready datasets
|-- results/       # Simulation/ML outputs
|-- figures/       # Generated visual outputs
`-- examples/      # Tiny tracked samples only
```

Inside each stage, use your model and subcategory taxonomy consistently, for
example `data/processed/<model>/<subcategory>/<run_id>/`.
