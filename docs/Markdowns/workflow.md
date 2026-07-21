# Final Development Workflow

The repository should support three repeatable workflows: local Windows GUI
development, headless Docker/CI validation, and release packaging.

## 1. Clone

```sh
git clone https://github.com/Helios-MEOW/Summer-Studentship.git
cd Summer-Studentship
```

If using mathplot targets:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap_external.ps1
```

or on Linux/macOS:

```sh
sh scripts/bootstrap_external.sh
```

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
MINGW_ROOT
NINJA_EXE
QT_ROOT
VCPKG_ROOT
```

## 3. Build During Development

For the Windows GUI:

```powershell
cmake --preset windows-mingw-vcpkg
cmake --build --preset windows-mingw-vcpkg-debug
```

For the all-dependencies package route:

```powershell
cmake --preset windows-mingw-vcpkg-all
cmake --build --preset windows-mingw-vcpkg-all-debug
```

For the portable headless core:

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

Run the native build you changed. For the usual GUI path:

```powershell
cmake --build --preset windows-mingw-vcpkg-debug
```

Use `windows-mingw-vcpkg-all-debug` only when validating the heavier vcpkg GUI
or visualisation manifest feature set.

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
