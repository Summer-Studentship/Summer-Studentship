# CMake Scaffold Audit v0.1

- **Work Package:** `SWE-ENV-BLD-WP1`
- **Issues:** `#103`, `#104`, `#105`
- **Audit date:** 2026-07-22
- **Branch:** `build/g0-cmake-scaffold`

## Purpose

This audit records how the pre-WBS CMake prototype was reconciled with the G0
target-scoped scaffold baseline. It is not the final target architecture review;
that remains owned by `SWE-ARC-TGT-WP1`.

## Build-Structure Audit

| Existing artefact | Current purpose | Reusable element | Incompatibility with accepted baseline | Action taken | Deferred work |
|---|---|---|---|---|---|
| Root `CMakeLists.txt` | Repository configure entry point | Existing project entry point and compile-command export | Required CMake 3.21, set global C++ standard, exposed historical Qt Widgets and Matplot smoke switches | Raised minimum to 3.28; moved C++20 to target compile features; retained compile-command export; added `CTest`; created independent CLI/GUI options; retired active Matplot path | Full preset families and target graph remain `SWE-ENV-PRS-WP1` and `SWE-ARC-TGT-WP1` |
| `CMAKE_CXX_STANDARD` settings | Global language baseline | C++20 intent | Global cache/source settings could leak into all targets | Removed from active root build and compatibility presets; each active target now uses `target_compile_features` | Cross-platform enforcement policy remains preset/toolchain work |
| `src/CMakeLists.txt` | Builds `tsunami_core` | Existing Qt-independent core skeleton and target-scoped include pattern | Include path lacked install/build interface distinction and no warning policy was applied | Retained `tsunami_core`, added build/install interface include paths, C++20 feature and target-local warnings | Public header/API shape remains architecture work |
| `tsunami_core` | Initial core library | Lightweight constants/types headers and empty source | Must not acquire Qt, CLI11, Catch2 or Matplot links | Preserved as Qt-independent static library with no third-party links | Numerical/data/geospatial targets deferred |
| `src/main.cpp` | Historical root GUI entry point | None for G0 scaffold | Included missing `gui/MainWindow.hpp` and duplicated GUI ownership | Deleted from active source tree; disposition documented here | Any future GUI entry point belongs under `apps/tsunami_gui` |
| `src/test.cpp` | Historical plotting experiment | Historical evidence only | Referenced missing/non-baseline plotting headers and was not a Catch2 test | Deleted from active source tree; disposition documented here | Diagnostics adapter and smoke tests remain later WBS work |
| `apps/TsunamiGUI` | Pre-WBS Qt Widgets prototype | Prototype history | Uses Qt Widgets and historical target `TsunamiGUI`; production shell must be Qt Quick/QML | Preserved directory; removed from active root build; added status README | Migration/reuse requires later GUI decision |
| Qt Widgets dependency | Historical GUI dependency | None for accepted shell | Accepted scaffold requires Qt Quick/QML, not Widgets | Root no longer discovers `Qt6::Widgets`; `tsunami_gui` discovers Qt Quick only when enabled | Qt deployment and richer GUI shell remain later GUI/preset work |
| `TSUNAMI_BUILD_GUI` | Historical Widgets toggle | Useful independent GUI switch name | Previously defaulted ON and selected Widgets | Retained as GUI switch, default OFF, now selects Qt Quick scaffold | Final GUI workflow presets remain `SWE-ENV-PRS-WP1` |
| `TSUNAMI_BUILD_TESTS` | Project-specific test toggle | None | Duplicated CTest's standard `BUILD_TESTING` | Removed from active build and presets | None |
| `BUILD_TESTING` | CTest standard switch | Standard CMake behaviour | Not used by historical test route | Adopted for `tsunami_tests` | Test taxonomy remains later verification work |
| `TSUNAMI_BUILD_MATPLOT_SMOKE` | Historical plotting smoke toggle | Recorded dependency context | Dead path referenced historical plotting work outside accepted scaffold | Removed from active root build and presets | Diagnostics adapter belongs to later work |
| `tests/Reworked Prior Model` | Historical prior-model sandbox | Historical solver experimentation | Not an accepted Studentship test suite | Left untouched and excluded from CTest; added `tests/historical/README.md` | Any migration requires explicit verification/architecture scope |
| `CMakePresets.json` | Historical configure/build presets | Useful names and vcpkg toolchain pattern | Referenced global C++ standard cache values, removed options, removed feature names and target `TsunamiGUI` | Made narrow compatibility edits: CMake min 3.28, removed standard cache values, replaced removed options with `BUILD_TESTING`, corrected diagnostics feature and `tsunami_gui` target | Complete preset redesign remains `SWE-ENV-PRS-WP1` |

## Accepted Scaffold Targets

| Target | Condition | Purpose |
|---|---|---|
| `tsunami_core` | Always configured | Qt-independent core library skeleton |
| `tsunami_cli` | `TSUNAMI_BUILD_CLI=ON` | Minimal CLI11 command-line scaffold |
| `tsunami_gui` | `TSUNAMI_BUILD_GUI=ON` | Minimal Qt Quick/QML application scaffold |
| `tsunami_tests` | `BUILD_TESTING=ON` | Catch2/CTest smoke-test scaffold |

## Audit Conclusion

The active build now proves the G0 scaffold names without creating solver,
FVM, geospatial, data, production service or final architecture targets. The
historical Qt Widgets prototype and prior-model sandbox remain preserved but
inactive.
