# Preset Audit v0.1

- **Work Package:** `SWE-ENV-PRS-WP1`
- **Issues:** `#111`, `#112`, `#113`
- **Audit date:** 2026-07-22
- **Branch:** `build/g0-shared-presets`

## Purpose

This audit records the transition from the scaffold-era presets to the shared
developer, test, release and CI preset families used by the accepted G0 target
scaffold. It does not define CI workflows or the final architecture target
graph.

## Existing Preset Audit

| Existing preset | Platform/compiler | Generator | Intended use | Target options | vcpkg features | Qt route | Reusable content | Stale or incompatible content | Replacement | Native validation |
|---|---|---|---|---|---|---|---|---|---|---|
| `windows-mingw-vcpkg` | Windows MinGW | Ninja | Historical Windows GUI/dev route | GUI inherited from project defaults unless set by user | default | External `QT_ROOT` | vcpkg toolchain pattern and binary-dir convention | MinGW was not accepted Windows release authority; used user-specific compiler variables | Retired from shared authority; Windows route is `windows-msvc-*` | Not validated on Linux |
| `windows-mingw-vcpkg-all` | Windows MinGW | Ninja | Historical all-dependency route | Historical GUI/dev route | `diagnostics` after BLD compatibility edit | Description still implied vcpkg GUI/Qt | None beyond historical compatibility | Qt-via-vcpkg description was stale; MinGW experimental only | Retired from shared authority | Not validated on Linux |
| `linux-vcpkg-headless` | Linux default compiler | Ninja | Headless Linux build | CLI ON, GUI OFF, testing OFF | default | none | Linux vcpkg-base structure and build dir | No test/CI/release family; generic compiler route; no workflow preset | `linux-gcc-dev`, `linux-gcc-release`, `linux-gcc-ci` | Replaced and validated |
| `linux-gui-debug` | Linux default compiler | Ninja | Qt Quick scaffold build | CLI ON, GUI ON, testing OFF | default | External Qt/system | GUI option shape and external Qt route | Name was one-off; no matching workflow/test families | `linux-gcc-gui-dev` | Replaced and validated |
| `linux-base` | Linux default compiler | Ninja | Hidden shared base | vcpkg toolchain, x64-linux | default | none | Inheritance and binary-dir convention | Did not distinguish GCC/Clang authority | `base`, `vcpkg-base`, `linux-base`, `linux-gcc-base`, `linux-clang-base` | Structurally validated |
| `windows-mingw-vcpkg-debug` | Windows MinGW | build preset | Historical build | Unscoped default build | default | External Qt | None | Historical target family not authoritative | Retired | Not validated on Linux |
| `windows-mingw-vcpkg-all-debug` | Windows MinGW | build preset | Historical all-dependency build | Unscoped default build | diagnostics | Description implied old all-dependency route | None | Not accepted release authority | Retired | Not validated on Linux |
| `linux-vcpkg-headless-build` | Linux default compiler | build preset | Headless build | Default build | default | none | Build-preset pattern | Did not name accepted target set | `linux-gcc-dev-build`, `linux-gcc-release-build`, `linux-gcc-ci-build` | Replaced and validated |
| `linux-gui-debug-build` | Linux default compiler | build preset | GUI build | `tsunami_gui` | default | External Qt | Correct target after BLD | One-off name | `linux-gcc-gui-dev-build` | Replaced and validated |

## Explicit Gaps Found

- There was no MSVC authority in the shared presets.
- There was no Linux Clang coverage.
- MinGW was still presented as a Windows development route, despite being
  experimental compatibility rather than release authority.
- Some descriptions still carried Qt-via-vcpkg or historical all-dependency
  wording.
- Test presets, CI presets and workflow presets were missing.
- `CMakeUserPresets.json.example` still targeted the historical `TsunamiGUI`
  build preset and described the GUI as Qt Widgets.
- Shared presets relied on machine-specific MinGW compiler and Ninja variables.
- No macOS evidence existed; macOS remains future native validation rather than
  an invented shared preset.

## Action Taken

`CMakePresets.json` now defines hidden bases plus Linux GCC, Linux Clang and
Windows MSVC configure/build/test/workflow families. Shared presets reference
only local environment variables such as `VCPKG_ROOT` and `QT_ROOT`; local paths
belong in ignored `CMakeUserPresets.json`.

MinGW shared presets were retired rather than renamed because the historical
route depended on machine-specific compiler/Ninja variables and is not the
accepted Windows release authority.
