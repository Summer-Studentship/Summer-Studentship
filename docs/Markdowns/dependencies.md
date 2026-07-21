# Dependency Acquisition

This guide is the supported dependency-acquisition contract established by
`SWE-ENV-DEP-WP1`. CMake target wiring and shared preset migration are separate
work under `SWE-ENV-BLD-WP1` and `SWE-ENV-PRS-WP1`.

## Prerequisites

Install these tools independently of the C++ manifest:

- Git;
- CMake 3.28 or newer;
- Ninja;
- a supported C++20 compiler;
- a vcpkg executable compatible with manifest mode; and
- the Qt installer/Maintenance Tool only when working on the GUI.

Set `VCPKG_ROOT` to the local vcpkg checkout or installation. Do not commit that
path, compiler paths, Qt paths, cache paths or credentials to shared manifests,
scripts or presets.

The package registry and the vcpkg executable are related but distinct:

- `vcpkg-configuration.json` is the sole authority for package/port resolution;
- it pins the Microsoft vcpkg registry release `2026.05.25` to exact commit
  `d015e31e90838a4c9dfa3eed45979bc70d9357fc`; and
- the vcpkg executable has its own version. A Docker or workstation tool checkout
  does not override the manifest's registry baseline.

## Package groups

An ordinary manifest restore installs the direct default dependencies:

```text
cli11
eigen3
fmt
hdf5[zlib] at the compatible 1.14.6 line
nlohmann-json
pugixml
spdlog[fmt]
yaml-cpp
```

The remaining direct dependencies are additive named features:

| Feature | Direct ports | Use |
|---|---|---|
| `geospatial` | `gdal`, `proj[tiff]` | Accepted production geospatial stack, disabled by default until its owning import/CRS targets exist |
| `tests` | `catch2` | Unit and benchmark development support |
| `diagnostics` | `matplotplusplus` | Optional diagnostic plots behind an adapter; not the production field visualiser |
| `netcdf` | `netcdf-c[netcdf-4]` | Optional direct multidimensional ingestion only when GDAL is demonstrably insufficient |

Features are additive. For example, a test plan contains both the default ports
and Catch2. `geospatial` being disabled by default is a foundation-build boundary,
not a reversal of its accepted production status.

The `netcdf-c` override selects upstream version 4.9.3 at port revision 0. The
later port revisions at this registry baseline request an HDF5 feature that does
not exist in the accepted HDF5 1.14.6 port. This is a repository-supported
registry version, not a local overlay.

Matplot++ 1.2.1 is the version resolved by the exact registry. Its selected
non-OpenGL backend requires an external Gnuplot 5.2.6+ executable at runtime.
Gnuplot is not silently installed by vcpkg: diagnostics stays off by default
until `SWE-ENV-SMK` and the stack/licence review approve and verify that external
runtime route. Experimental Matplot++ OpenGL support remains disabled.

## Manifest restoration

From the repository root on Linux, restore the default set into a build-local
installed tree:

```sh
"${VCPKG_ROOT}/vcpkg" install \
  --triplet x64-linux \
  --x-install-root=build/dependency-check/vcpkg_installed
```

Inspect a plan without building or installing it:

```sh
"${VCPKG_ROOT}/vcpkg" install --dry-run --triplet x64-linux
"${VCPKG_ROOT}/vcpkg" install --dry-run --triplet x64-linux --x-feature=geospatial
"${VCPKG_ROOT}/vcpkg" install --dry-run --triplet x64-linux --x-feature=tests
"${VCPKG_ROOT}/vcpkg" install --dry-run --triplet x64-linux --x-feature=diagnostics
"${VCPKG_ROOT}/vcpkg" install --dry-run --triplet x64-linux --x-feature=netcdf
```

Multiple features can be selected by repeating `--x-feature`. On Windows
PowerShell the equivalent form is:

```powershell
& "$env:VCPKG_ROOT/vcpkg.exe" install --dry-run `
  --triplet x64-windows `
  --x-feature=geospatial `
  --x-feature=tests
```

The Windows triplet above is illustrative of the accepted MSVC route. Platform-
native restoration is verified by later preset/smoke Work Packages; do not infer
Windows or macOS support solely from the recorded Linux dependency plan.

## Direct and transitive dependencies

Only top-level `dependencies` and named-feature entries in `vcpkg.json` are
project-selected direct packages. Libraries such as zlib, TIFF, CImg or
nodesoup may also appear because a selected port needs them. Such a transitive
package:

- is still included in release-time security and licence review;
- is not automatically an API that project source may include or link; and
- must become an explicit direct dependency, with an owning WBS decision, before
  project code relies on it directly.

Inspect the complete plan after every baseline, triplet, feature or port-option
change. Release verification must additionally inspect installed metadata under
`<install-root>/<triplet>/share/<port>/copyright`.

## Qt and other external software

Qt is not acquired through vcpkg. Use the Qt installer/Maintenance Tool and
install only the accepted Qt 6 modules required by the GUI targets:

```text
Qt Core
Qt Gui
Qt Qml
Qt Quick
Qt Quick Controls
Qt Concurrent
```

Keep the installed Qt version and kit compatible with the compiler selected for
that machine. Record them in local build provenance. Additional Qt modules,
including Quick 3D, Quick Timeline and Shader Tools, require a new owning
requirement and module-level licence review.

These components also remain outside `vcpkg.json`:

| Component | Acquisition and baseline use |
|---|---|
| Gmsh | Separate upstream/system installation; invoked as an external executable with `.geo`/MSH file exchange |
| ParaView | Separate scientific visualisation application; consumes XDMF/HDF5 results |
| QGIS | Separate GIS inspection/preprocessing application |
| MATLAB | Separately licensed application; initially consumes HDF5 files and is never required to build/run the C++ application |
| Doxygen | System/upstream documentation generator; development-only |
| LaTeX | Supported TeX distribution for research/scientific documentation |
| Python 3.12+ | System/managed interpreter for reproducible preprocessing; not a silent product runtime |
| CMake, Ninja and Git | Supported system, package-manager or official installations |
| Gnuplot | Unapproved conditional runtime for the optional Matplot++ backend; do not bundle until the recorded stack/licence/smoke decision completes |

External applications are not application-package contents by default. See the
licence register before redistributing any executable, runtime, plugin, dataset
or development image.

## Cache behaviour

vcpkg maintains source downloads, build trees and binary packages separately
from the manifest. These caches improve speed but are not dependency authority;
the manifest, feature selection, triplet and exact registry commit still define
the graph.

Use a developer-controlled binary cache without putting a machine path in the
repository. For example:

```sh
export VCPKG_DEFAULT_BINARY_CACHE="<cache-directory>"
export VCPKG_BINARY_SOURCES="clear;files,<cache-directory>,readwrite"
```

PowerShell uses the same environment-variable names:

```powershell
$env:VCPKG_DEFAULT_BINARY_CACHE = "<cache-directory>"
$env:VCPKG_BINARY_SOURCES = "clear;files,<cache-directory>,readwrite"
```

CI may use an authenticated remote binary cache, but credentials and endpoints
belong in CI configuration/secrets. Cache keys must distinguish the registry
baseline, triplet, feature set, compiler/toolchain and relevant port options.

For clean-cache evidence, use a new empty install root, downloads directory and
binary-cache directory, then run the same manifest command. Removing a previous
build-local `vcpkg_installed` tree proves installation is reproducible; it does
not change the registry pin. Preserve the command, vcpkg version, triplet,
features and plan in the smoke evidence.

For example, after creating three empty directories outside the source tree:

```sh
VCPKG_DOWNLOADS="<empty-downloads-directory>" \
VCPKG_BINARY_SOURCES="clear;files,<empty-binary-cache-directory>,readwrite" \
"${VCPKG_ROOT}/vcpkg" install \
  --triplet x64-linux \
  --x-install-root="<empty-install-directory>"
```

Offline restoration works only when all of the following are already available:

- the exact Git registry commit and port trees;
- every required source archive or asset; and
- either compatible binary packages or the complete local build toolchain.

`--no-downloads` is a useful offline check after populating those stores. A bare
clean clone without cached registry/source data cannot restore from the network
while offline, and the repository does not claim otherwise.

## Updating the baseline

Never update dependencies incidentally. On a dedicated dependency branch:

1. Choose a published vcpkg registry release and resolve its annotated tag to an
   immutable commit:

   ```sh
   git ls-remote https://github.com/microsoft/vcpkg.git \
     'refs/tags/<release>^{}'
   ```

2. Replace only `default-registry.baseline` in `vcpkg-configuration.json` with
   the full peeled commit. Do not add a second `builtin-baseline` authority.
3. Run `vcpkg format-manifest vcpkg.json`, validate both JSON files, and inspect
   the default and every feature plan independently.
4. Review direct and transitive version changes, installed copyright metadata,
   runtime implications and the dependency licence register.
5. Obtain platform CI and clean-clone smoke evidence before accepting the update.

`vcpkg x-update-baseline --dry-run` may be used to inspect the current upstream
proposal, but it targets upstream state rather than proving that a chosen release
was reviewed. A floating branch name is never a valid baseline.

## Presets and local paths

`CMakeUserPresets.json` is the supported place to inherit shared presets and set
developer-specific paths or cache variables. It stays uncommitted; use
`CMakeUserPresets.json.example` only as a template. Shared files must use
environment variables, preset inheritance or tool discovery rather than a
developer's absolute paths.

The existing `windows-mingw-vcpkg-all` preset and the `-Visualization` path in
`scripts/build_windows_qt.ps1` still select the removed historical
`visualization;gui` features. They are unsupported pending `SWE-ENV-PRS-WP1` and
must not be used as dependency-restoration instructions. This Work Package does
not alter presets or target implementation.
