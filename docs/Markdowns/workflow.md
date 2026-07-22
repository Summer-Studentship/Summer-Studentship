# Final Development Workflow

The repository should support three repeatable workflows: local Windows GUI
development, headless Docker/CI validation, and release packaging.

## 1. Clone

```sh
git clone https://github.com/Summer-Studentship/Summer-Studentship.git
cd Summer-Studentship
```

Restore C++ packages from the repository manifest by following
[`dependencies.md`](dependencies.md). There is no external mathplot checkout or
bootstrap step. Optional diagnostic plotting uses the `diagnostics` manifest
feature and remains disabled until its external backend is approved and smoke
tested.

## 2. Configure Locally

Each developer supplies local tool paths through environment variables or an
ignored `CMakeUserPresets.json`. That file is ignored by Git.

Shared behaviour belongs in:

```text
CMakePresets.json
vcpkg.json
vcpkg-configuration.json
```

Local machine paths belong in:

```text
CMakeUserPresets.json
.vscode/settings.json
```

Shared presets use these environment variables:

```text
QT_ROOT
VCPKG_ROOT
```

## 3. Build During Development

Use the shared G0 presets for ordinary Linux development:

```sh
cmake --preset linux-gcc-dev
cmake --build --preset linux-gcc-dev-build
```

Use `linux-gcc-test-workflow` or `linux-clang-test-workflow` for the registered
CTest suite. GUI work uses `linux-gcc-gui-dev` and requires an external Qt
installation exposed through `QT_ROOT` or an equivalent local CMake prefix.
Windows release-authority presets use MSVC and must be run from an activated
Visual Studio environment on Windows.

The Docker build is historical portability evidence, not yet the authorised
clean-clone smoke test:

```sh
docker build -t summer-studentship-build-check .
```

## 4. Code Organisation

Keep the solver core independent from Qt:

```text
src/core        shared constants, types, errors, results
src/models      shallow water, Navier-Stokes, interface models
src/solvers     numerical schemes and time stepping
src/io          HDF5, JSON, filesystem, run metadata
src/gui         Qt interface layer only
```

The GUI should call the core through clean C++ interfaces. The core should not
include Qt headers.

## 5. Data Organisation

Use the same model/category taxonomy everywhere:

```text
data/raw/<model>/<subcategory>/<run_id>/
data/interim/<model>/<subcategory>/<run_id>/
data/processed/<model>/<subcategory>/<run_id>/
data/results/<model>/<subcategory>/<run_id>/
data/figures/<model>/<subcategory>/<run_id>/
```

Bulk HDF5, MATLAB, and generated result files stay out of Git unless they are
tiny curated examples under `data/examples/`.

## 6. Before Sharing Work

Run the native build relevant to the change. For headless Linux test changes:

```sh
cmake --workflow --preset linux-gcc-test-workflow
```

For GUI scaffold changes:

```sh
cmake --preset linux-gcc-gui-dev
cmake --build --preset linux-gcc-gui-dev-build
```

Optional dependency groups remain governed by `dependencies.md`; do not enable
geospatial, diagnostics or netCDF features merely because a preset exists.

Run the portability check:

```sh
docker build -t summer-studentship-build-check .
```

Commit related changes together:

```text
build/dependencies
source architecture
docs/workflow
tests
```
