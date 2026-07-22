# Historical Windows Build With Qt MinGW

This records the pre-WBS native Windows prototype route. The accepted shared
Windows preset family uses MSVC as the primary supported compiler. Do not treat
this MinGW page as release authority.

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

Historical MinGW builds are no longer represented by shared authoritative
presets. The current shared Windows route is structural until native execution
is performed on Windows:

```powershell
cmake --preset windows-msvc-dev
cmake --build --preset windows-msvc-dev-build
```

Run those commands from an activated Visual Studio Developer PowerShell or
command prompt. Qt is acquired only through the Qt installer/Maintenance Tool,
never through a vcpkg GUI feature.

Run/debug from VS Code through CMake Tools or through the checked launch
configuration. Avoid the C++ extension's raw "build active file" action because
it bypasses CMake include paths and linked libraries.

## Release Packaging

For a Windows release, use the MSVC release preset family on a native Windows
host, run Qt's deployment tooling on the final executable, then copy any vcpkg
runtime DLLs required by the selected MSVC triplet from:

```text
build/<preset>/vcpkg_installed/x64-windows/bin
```

This repository does not yet automate portable archives or installers.
