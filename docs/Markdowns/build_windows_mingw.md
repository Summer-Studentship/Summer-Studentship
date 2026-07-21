# Windows Build With Qt MinGW

This is the native Windows route for day-to-day GUI development.

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

Restore external source dependencies when using mathplot:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap_external.ps1
```

## Configure And Build

Normal Qt GUI build:

```powershell
cmake --preset windows-mingw-vcpkg
cmake --build --preset windows-mingw-vcpkg-debug
```

All-dependencies package route, including the vcpkg GUI and visualisation
manifest features:

```powershell
cmake --preset windows-mingw-vcpkg-all
cmake --build --preset windows-mingw-vcpkg-all-debug
```

The all-dependencies preset makes vcpkg responsible for additional packages. It
does not add new CMake targets by itself.

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
