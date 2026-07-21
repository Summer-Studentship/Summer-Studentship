# Historical Windows Build With Qt MinGW

This records the pre-WBS native Windows prototype route. The accepted stack uses
MSVC as the primary supported Windows compiler; shared replacement presets are
owned by `SWE-ENV-PRS-WP1`. Do not treat this page as release authority.

## Required Tools

- Git
- Qt 6.11.1, including the MinGW kit
- CMake from Qt or a recent standalone CMake
- Ninja
- vcpkg

The local machine currently uses:

```text
MINGW_ROOT=C:/Qt/Tools/mingw1310_64
NINJA_EXE=C:/Qt/Tools/Ninja/ninja.exe
QT_ROOT=C:/Qt/6.11.1/mingw_64
VCPKG_ROOT=C:/vcpkg
```

These paths are supplied through environment variables. Shared presets derive
the vcpkg CMake toolchain from `VCPKG_ROOT` and do not contain absolute local
toolchain paths.

## Bootstrap

From PowerShell:

```powershell
$env:MINGW_ROOT = "C:/Qt/Tools/mingw1310_64"
$env:NINJA_EXE = "C:/Qt/Tools/Ninja/ninja.exe"
$env:QT_ROOT = "C:/Qt/6.11.1/mingw_64"
$env:VCPKG_ROOT = "C:/vcpkg"
$env:VCPKG_MAX_CONCURRENCY = "2"
$env:PATH = "$env:MINGW_ROOT/bin;C:/Qt/Tools/Ninja;$env:PATH"
```

Restore package dependencies only through `vcpkg.json`, following
[`dependencies.md`](dependencies.md). The superseded external mathplot checkout
has no bootstrap step. Optional plotting is the disabled-by-default
`diagnostics` manifest group and has a separately recorded Gnuplot backend
decision.

## Configure And Build

Normal Qt GUI build:

```powershell
cmake --preset windows-mingw-vcpkg
cmake --build --preset windows-mingw-vcpkg-debug
```

The existing `windows-mingw-vcpkg-all` configure/build presets and the
`-Visualization` switch in `scripts/build_windows_qt.ps1` select removed
`visualization;gui` features. They are unsupported historical names pending
`SWE-ENV-PRS-WP1`; Qt is now acquired only through the Qt installer/Maintenance
Tool, never through a vcpkg GUI feature.

Run/debug from VS Code through CMake Tools or through the checked launch
configuration. Avoid the C++ extension's raw "build active file" action because
it bypasses CMake include paths and linked libraries.

## Release Packaging

For a Windows release, build in Release mode, run Qt's `windeployqt` on the
final executable, then copy any vcpkg DLLs required by the dynamic MinGW triplet
from:

```text
build/<preset>/vcpkg_installed/x64-mingw-dynamic/bin
```

This repository does not yet automate portable archives or installers.
