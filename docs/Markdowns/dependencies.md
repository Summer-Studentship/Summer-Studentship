# Dependency Strategy

This project should treat CMake as the source of truth for C++ builds and
vcpkg as the source of truth for third-party C++ packages.

## Current Toolchain

- C++ standard: C++20
- Build system: CMake
- Windows compiler route currently used locally: Qt MinGW GCC at
  `C:/Qt/Tools/mingw1310_64`
- Qt route currently used locally: Qt installer at `C:/Qt/6.11.1/mingw_64`
- Package manager: vcpkg manifest mode through `vcpkg.json`
- vcpkg registry pin: `vcpkg-configuration.json`
- Shared vcpkg toolchain convention:
  `$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`

## C++ Package Groups

Default vcpkg dependencies are for the numerical/data layer:

- `hdf5[cpp,hl,zlib]`: bulk simulation and processed data storage
- `highfive`: modern C++ wrapper over HDF5
- `eigen3`: core linear algebra/numerics
- `nlohmann-json`: structured metadata and configuration

Optional vcpkg features:

- `visualization`: mathplot/OpenGL support through `armadillo`, `freetype`,
  `glfw3`, `opengl`, and `rapidxml`
- `gui`: Qt packages when Qt is installed by vcpkg instead of the Qt installer

Mathplot is restored into `external/mathplot` by bootstrap scripts and ignored
by Git. Prefer treating it as a header dependency from our own CMake targets
instead of adding mathplot's full CMake project by default, because its upstream
build also tries to build its examples, tests, fonts, and optional GUI
integrations.

If `external/mathplot` is missing after cloning this repository, restore it with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap_external.ps1
```

That script clones `https://github.com/sebsjames/mathplot.git` recursively,
checks out the pinned commit, checks that the `mplot` and `maths/sm` headers
exist, and applies the local MinGW font-header patch required by the current Qt
kit.

On Linux/macOS, use:

```sh
sh scripts/bootstrap_external.sh
```

## Windows Bootstrap

In PowerShell, set the two paths used by `CMakePresets.json`:

```powershell
$env:MINGW_ROOT = "C:/Qt/Tools/mingw1310_64"
$env:NINJA_EXE = "C:/Qt/Tools/Ninja/ninja.exe"
$env:VCPKG_ROOT = "C:/vcpkg"
$env:QT_ROOT = "C:/Qt/6.11.1/mingw_64"  # adjust to your installed Qt path
$env:VCPKG_MAX_CONCURRENCY = "2"
$env:PATH = "$env:MINGW_ROOT/bin;C:/Qt/Tools/Ninja;$env:PATH"
```

Keep the compiler, Qt build, and vcpkg triplet aligned. For the current Windows
route, use the `x64-mingw-dynamic` triplet and make sure `MINGW_ROOT` points to
the MinGW toolchain that should compile the project. Put `MINGW_ROOT/bin` first
on `PATH` before running vcpkg so vcpkg does not accidentally pick another
MinGW installation.

`VCPKG_MAX_CONCURRENCY=2` is intentionally conservative. HDF5 can fail during
highly parallel MinGW builds on this machine, while a low-concurrency build has
completed successfully.

Install the default dependencies:

```powershell
& "$env:VCPKG_ROOT/vcpkg.exe" install --triplet x64-mingw-dynamic
```

Add native C++ visualisation dependencies when mathplot targets are enabled:

```powershell
& "$env:VCPKG_ROOT/vcpkg.exe" install --triplet x64-mingw-dynamic --x-feature=visualization
```

Configure and build using Qt from the Qt installer:

```powershell
cmake --preset windows-mingw-vcpkg
cmake --build --preset windows-mingw-vcpkg-debug
```

Use the all-dependencies package route when vcpkg should also provide the GUI
and visualisation manifest features:

```powershell
cmake --preset windows-mingw-vcpkg-all
cmake --build --preset windows-mingw-vcpkg-all-debug
```

The shared Windows presets read `MINGW_ROOT`, `NINJA_EXE`, `QT_ROOT`, and
`VCPKG_ROOT` from the environment. Linux vcpkg presets also read `VCPKG_ROOT`
and use the system compiler and Ninja discovery. A local
`CMakeUserPresets.json` may set `VCPKG_ROOT` for a developer machine, but it
should remain uncommitted.

## Documentation Tools

Doxygen is a developer tool rather than a linked library. Install Doxygen and
Graphviz through the platform package manager or official installers, then add
a root documentation target later with `find_package(Doxygen)`.

## Python and MATLAB

Keep Python and MATLAB dependencies separate from the C++ manifest:

- Python: add `pyproject.toml` or `requirements.txt` under a future `python/`
  directory when the machine-learning layer starts.
- MATLAB: keep scripts under a future `matlab/` directory and document required
  toolboxes in `matlab/README.md`.

Do not commit virtual environments, generated model checkpoints, or large
intermediate datasets.
