# Repository Layout

This repository should grow around a small number of stable ownership areas:

```text
.
|-- apps/
|   `-- gui/                 # Final Qt application entry point
|-- cmake/                   # Project CMake helper modules
|-- data/                    # Data staging convention; bulk data ignored by Git
|-- docs/                    # Human docs, Doxygen config, build notes
|-- external/                # Vendored third-party source/header projects
|-- matlab/                  # MATLAB analysis and visualisation scripts
|-- python/                  # ML experiments, training scripts, notebooks
|-- src/
|   |-- core/                # Shared constants, types, error handling
|   |-- gui/                 # Current Qt Widgets prototype
|   |-- io/                  # HDF5/JSON/file format readers and writers
|   |-- models/              # Physical/numerical models
|   `-- visualization/       # C++ plotting adapters, mathplot integration
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

## External Header Projects

If an external dependency has its own `CMakeLists.txt`, do not automatically
`add_subdirectory()` it unless we really want to build that whole upstream
project. For header-heavy libraries such as mathplot, create a project-owned
`INTERFACE` target instead:

```cmake
add_library(tsunami_mathplot INTERFACE)
target_include_directories(tsunami_mathplot INTERFACE
    ${PROJECT_SOURCE_DIR}/external/mathplot
    ${PROJECT_SOURCE_DIR}/external/mathplot/maths
)
target_compile_definitions(tsunami_mathplot INTERFACE
    MPLOT_FONTS_DIR="${PROJECT_SOURCE_DIR}/external/mathplot/fonts"
)
target_link_libraries(tsunami_mathplot INTERFACE
    OpenGL::GL
    Freetype::Freetype
    glfw
    nlohmann_json::nlohmann_json
)
```

This keeps our build predictable while still allowing mathplot to remain
restored into `external/`.

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
