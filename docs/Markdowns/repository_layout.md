# Repository Layout

This repository should grow around a small number of stable ownership areas:

```text
.
|-- apps/
|   `-- gui/                 # Final Qt application entry point
|-- cmake/                   # Project CMake helper modules
|-- data/                    # Data staging convention; bulk data ignored by Git
|-- docs/                    # Human docs, Doxygen config, build notes
|-- matlab/                  # MATLAB analysis and visualisation scripts
|-- python/                  # ML experiments, training scripts, notebooks
|-- src/
|   |-- core/                # Shared constants, types, error handling
|   |-- gui/                 # Historical GUI prototype
|   |-- io/                  # HDF5/JSON/file format readers and writers
|   |-- models/              # Physical/numerical models
|   `-- visualization/       # Plotting-neutral output and optional diagnostics adapters
`-- tests/                   # C++ tests
```

Treat the layout above as the intended direction. The old top-level `GUI/`
prototype has been retired in favour of `src/gui/` and future `apps/gui/`
targets.

## CMake Structure

Keep the root `CMakeLists.txt` short. Its job should be:

- set the project name, C++ standard, and global options
- load dependency helpers from `cmake/`
- add subdirectories for libraries, apps, examples, and tests

Each major component should own its own `CMakeLists.txt`. For example:

```text
src/core/CMakeLists.txt       -> tsunami_core library
src/io/CMakeLists.txt         -> tsunami_io library
apps/gui/CMakeLists.txt       -> Qt GUI executable
tests/CMakeLists.txt          -> test executables
```

Prefer linking targets together instead of sharing global include directories.
For example, the GUI should link to a reusable simulation/data library:

```cmake
target_link_libraries(tsunami_gui PRIVATE tsunami_core tsunami_io Qt6::Widgets)
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
