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
MINGW_ROOT
NINJA_EXE
QT_ROOT
VCPKG_ROOT
```

## 3. Build During Development

The existing MinGW commands are pre-WBS development routes:

```powershell
cmake --preset windows-mingw-vcpkg
cmake --build --preset windows-mingw-vcpkg-debug
```

Do not use `windows-mingw-vcpkg-all`: it still names removed vcpkg
`visualization;gui` features. Shared developer/release/CI preset selection and
the accepted MSVC Windows route are pending `SWE-ENV-PRS-WP1`; target migration
is pending `SWE-ENV-BLD-WP1`.

The Docker build is also pre-WBS evidence, not yet the authorised clean-clone
smoke test:

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

Run the native build relevant to the change. The existing MinGW GUI command is
available only as historical prototype validation until the preset Work Package
replaces it:

```powershell
cmake --build --preset windows-mingw-vcpkg-debug
```

Do not use the historical `windows-mingw-vcpkg-all-debug` route. Inspect optional
dependency groups directly with the manifest commands in `dependencies.md`
until `SWE-ENV-PRS-WP1` provides supported feature-aware presets.

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
