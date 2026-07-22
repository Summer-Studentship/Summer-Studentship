# Preset Validation v0.1

- **Work Package:** `SWE-ENV-PRS-WP1`
- **Validation date:** 2026-07-22
- **Host:** Linux, `x64-linux`
- **Branch:** `build/g0-shared-presets`

## Tool Versions

| Tool | Version |
|---|---|
| CMake | `4.3.3` |
| Ninja | `1.13.2` |
| GCC | `g++ 16.1.1 20260430` |
| Clang | `clang++ 22.1.6` |
| vcpkg | `2026-05-27-d5b6777d666efc1a7f491babfcdab37794c1ae3e` from `/tmp/tsunami-g0-vcpkg` |
| vcpkg registry baseline | `d015e31e90838a4c9dfa3eed45979bc70d9357fc` |
| Qt | `6.11.1`, discovered through `/usr` with `QT_ROOT=/usr` |

Validation used local environment values:

```sh
VCPKG_ROOT=/tmp/tsunami-g0-vcpkg
QT_ROOT=/usr
```

These paths are not committed and are represented in shared presets only as
environment-variable references.

## JSON and Enumeration

Commands:

```sh
python3 -m json.tool CMakePresets.json >/dev/null
python3 -m json.tool CMakeUserPresets.json.example >/dev/null
cmake --list-presets=configure
cmake --list-presets=build
cmake --list-presets=test
cmake --list-presets=workflow
```

Result: pass. Configure, build, test and workflow presets enumerated without
broken inheritance or duplicate names.

## Linux GCC Validation

Developer configure/build:

```sh
env VCPKG_ROOT=/tmp/tsunami-g0-vcpkg QT_ROOT=/usr \
  cmake --preset linux-gcc-dev
cmake --build --preset linux-gcc-dev-build
./build/linux-gcc-dev/apps/tsunami_cli/tsunami_cli --version
```

Result: pass. CLI output:

```text
Tsunami Barrier Simulation 0.1.0
```

Test workflow:

```sh
env VCPKG_ROOT=/tmp/tsunami-g0-vcpkg QT_ROOT=/usr \
  cmake --workflow --preset linux-gcc-test-workflow
```

Result: pass. CTest reported `100% tests passed, 0 tests failed out of 1`.

Release configure/build:

```sh
env VCPKG_ROOT=/tmp/tsunami-g0-vcpkg QT_ROOT=/usr \
  cmake --preset linux-gcc-release
cmake --build --preset linux-gcc-release-build
```

Result: pass.

GUI configure/build/offscreen startup:

```sh
env VCPKG_ROOT=/tmp/tsunami-g0-vcpkg QT_ROOT=/usr \
  cmake --preset linux-gcc-gui-dev
cmake --build --preset linux-gcc-gui-dev-build
env QT_QPA_PLATFORM=offscreen \
  ./build/linux-gcc-gui-dev/apps/tsunami_gui/tsunami_gui --smoke-startup
```

Result: pass. Qt's optional Vulkan header probe was not satisfied on this host,
but the Qt Quick scaffold does not require Vulkan headers.

CI workflow:

```sh
env VCPKG_ROOT=/tmp/tsunami-g0-vcpkg QT_ROOT=/usr \
  cmake --workflow --preset linux-gcc-ci-workflow
```

Result: pass. CTest reported `100% tests passed, 0 tests failed out of 1`.

## Linux Clang Validation

Developer configure/build:

```sh
env VCPKG_ROOT=/tmp/tsunami-g0-vcpkg QT_ROOT=/usr \
  cmake --preset linux-clang-dev
cmake --build --preset linux-clang-dev-build
```

Result: pass.

Test workflow:

```sh
env VCPKG_ROOT=/tmp/tsunami-g0-vcpkg QT_ROOT=/usr \
  cmake --workflow --preset linux-clang-test-workflow
```

Result: pass. CTest reported `100% tests passed, 0 tests failed out of 1`.

## Windows MSVC Structural Validation

From Linux, validation was structural only:

- `windows-msvc-dev`, `windows-msvc-test`, `windows-msvc-release`,
  `windows-msvc-gui-dev` and `windows-msvc-ci` enumerate;
- matching build presets enumerate and use `Debug`, `Release` or
  `RelWithDebInfo` configurations for `Ninja Multi-Config`;
- test and workflow presets enumerate;
- `VCPKG_TARGET_TRIPLET` is `x64-windows`;
- Qt discovery remains external through `QT_ROOT`;
- no MinGW compiler variables or Qt-via-vcpkg feature route remains.

Native Windows configure/build/test execution is not claimed from this Linux
host. That evidence remains downstream release/smoke work.

## Static Inspection

Commands:

```sh
rg -n 'windows-mingw-vcpkg|linux-vcpkg-headless|TSUNAMI_BUILD_TESTS|TSUNAMI_BUILD_MATPLOT_SMOKE|TsunamiGUI|Qt Widgets|visualization;gui|linux-gui-debug' \
  docs/Markdowns CMakePresets.json CMakeUserPresets.json.example
rg -n 'windows-msvc|x64-windows|Ninja Multi-Config|CMAKE_PREFIX_PATH|QT_ROOT|VCPKG_TARGET_TRIPLET|CMAKE_TOOLCHAIN_FILE' \
  CMakePresets.json
```

Result: pass. No stale preset target names or Qt Widgets descriptions remain in
the shared preset files or active developer Markdown guidance. Windows MSVC
presets keep Qt external and use the `x64-windows` triplet.

## Diff Checks

Commands:

```sh
git status --short
git diff --check
git diff --stat
git diff
```

Result: inspected before commit. No whitespace errors were reported.

## Limitations

- No native Windows command was executed on Linux.
- No macOS preset is defined or validated.
- GitHub Actions workflow creation remains out of scope for this Work Package.
- Clean-clone smoke and runtime packaging evidence remain `SWE-ENV-SMK-WP1`.
