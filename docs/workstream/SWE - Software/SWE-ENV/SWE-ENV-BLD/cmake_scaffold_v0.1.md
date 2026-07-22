# CMake Scaffold v0.1

- **Work Package:** `SWE-ENV-BLD-WP1`
- **Issues:** `#103`, `#104`, `#105`
- **Document date:** 2026-07-22

## Target Scaffold

| Target | Type | Purpose | Condition | Allowed provisional links |
|---|---|---|---|---|
| `tsunami_core` | Static library | Qt-independent core skeleton | Always configured | No UI framework; no CLI11; no Catch2; no Matplot++ |
| `tsunami_cli` | Executable | Minimal command-line scaffold with `--help` and `--version` | `TSUNAMI_BUILD_CLI=ON` | `tsunami_core`, `CLI11::CLI11` |
| `tsunami_gui` | Executable | Minimal Qt Quick/QML startup shell | `TSUNAMI_BUILD_GUI=ON` | `tsunami_core`, `Qt6::Quick` |
| `tsunami_tests` | Executable | Catch2 foundation smoke test registered with CTest | `BUILD_TESTING=ON` | `tsunami_core`, `Catch2::Catch2WithMain` |

## Provisional Target Graph

```text
tsunami_cli   -> tsunami_core
tsunami_gui   -> tsunami_core + Qt
tsunami_tests -> tsunami_core + Catch2
tsunami_core  -> no UI framework
```

Final dependency direction and target decomposition are owned by
`SWE-ARC-TGT-WP1`.

## Build Options

| Option | Default | Meaning |
|---|---:|---|
| `TSUNAMI_BUILD_CLI` | `ON` | Configure `tsunami_cli` and discover CLI11 |
| `TSUNAMI_BUILD_GUI` | `OFF` | Configure `tsunami_gui` and discover Qt 6 Quick |
| `BUILD_TESTING` | CTest default `ON` | Configure `tsunami_tests` and discover Catch2 |
| `TSUNAMI_WARNINGS_AS_ERRORS` | `OFF` | Add warning-as-error flags to project-owned targets only |

## Compiler-Warning Policy

`cmake/TsunamiCompilerWarnings.cmake` provides
`tsunami_apply_compiler_warnings(<target>)`. The function applies warning flags
only to project-owned targets:

- MSVC: `/W4`, `/permissive-`;
- GCC: `-Wall`, `-Wextra`, `-Wpedantic`, `-Wconversion`,
  `-Wsign-conversion`;
- Clang: `-Wall`, `-Wextra`, `-Wpedantic`, `-Wconversion`,
  `-Wsign-conversion`.

`TSUNAMI_WARNINGS_AS_ERRORS=ON` adds `/WX` for MSVC or `-Werror` for GCC/Clang.
No global `add_compile_options` is used.

## Dependency Discovery

CLI11 is discovered only inside `apps/tsunami_cli` when
`TSUNAMI_BUILD_CLI=ON`.

Catch2 v3 is discovered only inside `tests` when `BUILD_TESTING=ON`.

Qt 6 is discovered only inside `apps/tsunami_gui` when
`TSUNAMI_BUILD_GUI=ON`. The accepted acquisition route is an external Qt
installation, not vcpkg Qt. On the validation host, Qt was discovered from
`CMAKE_PREFIX_PATH=/usr`, where `qmake6` reported Qt `6.11.1`.

## Configure and Build Commands

Core and CLI:

```sh
cmake -S . -B build/g0-cli -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/tsunami-g0-vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DTSUNAMI_BUILD_CLI=ON \
  -DTSUNAMI_BUILD_GUI=OFF \
  -DBUILD_TESTING=OFF
cmake --build build/g0-cli --target tsunami_core tsunami_cli
./build/g0-cli/apps/tsunami_cli/tsunami_cli --version
```

Tests:

```sh
cmake -S . -B build/g0-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/tsunami-g0-vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_MANIFEST_FEATURES=tests \
  -DTSUNAMI_BUILD_CLI=ON \
  -DTSUNAMI_BUILD_GUI=OFF \
  -DBUILD_TESTING=ON
cmake --build build/g0-tests --target tsunami_tests
ctest --test-dir build/g0-tests --output-on-failure
```

Qt Quick GUI:

```sh
cmake -S . -B build/g0-gui -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/tsunami-g0-vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DCMAKE_PREFIX_PATH=/usr \
  -DTSUNAMI_BUILD_GUI=ON \
  -DBUILD_TESTING=OFF
cmake --build build/g0-gui --target tsunami_gui
env QT_QPA_PLATFORM=offscreen \
  ./build/g0-gui/apps/tsunami_gui/tsunami_gui --smoke-startup
```

## Historical Disposition

`apps/TsunamiGUI` is preserved as a Qt Widgets prototype and is not included in
the active production build. New GUI work targets `apps/tsunami_gui`.

`tests/Reworked Prior Model` is preserved as a historical sandbox and is not
part of the accepted CTest suite. New tests target `tsunami_tests`.

`src/main.cpp` and `src/test.cpp` were removed from the active source tree
because they were obsolete, disconnected entrypoints. The new entrypoints are
`apps/tsunami_cli/main.cpp` and `apps/tsunami_gui/main.cpp`.

## Preset Compatibility

`CMakePresets.json` received only narrow compatibility maintenance:

- CMake preset minimum updated to 3.28;
- global `CMAKE_CXX_STANDARD` cache values removed;
- removed `TSUNAMI_BUILD_TESTS` and `TSUNAMI_BUILD_MATPLOT_SMOKE` cache entries;
- Linux GUI preset retargeted from `TsunamiGUI` to `tsunami_gui`;
- historical `visualization;gui` manifest feature selection corrected to
  `diagnostics`.

The final developer, release, CI, sanitizer and cross-platform preset families
remain `SWE-ENV-PRS-WP1`.

## Handoff

This scaffold proves target-local include, link, feature and warning rules for
the four G0 target names. It does not define the final domain target graph,
solver architecture, GUI navigation, case services or data/geospatial adapters.
Those decisions remain with `SWE-ARC-TGT-WP1` and the owning implementation
Work Packages.
