# CMake Scaffold Validation v0.1

- **Work Package:** `SWE-ENV-BLD-WP1`
- **Validation date:** 2026-07-22
- **Host:** Linux, `x64-linux`
- **Branch:** `build/g0-cmake-scaffold`

## Preflight

Commands:

```sh
git switch main
git pull --ff-only origin main
git status -sb
git remote -v
git branch --show-current
gh auth status
gh repo view Summer-Studentship/Summer-Studentship \
  --json nameWithOwner,defaultBranchRef,viewerPermission
git merge-base --is-ancestor \
  8c603cb84c250710100747a4efb62c97595a16a5 main
git switch -c build/g0-cmake-scaffold
```

Results:

- repository: `Summer-Studentship/Summer-Studentship`;
- default branch: `main`;
- viewer permission: `ADMIN`;
- pre-branch worktree: clean after restoring the confirmed automatic `vexp`
  metadata-only update;
- dependency/licence squash commit
  `8c603cb84c250710100747a4efb62c97595a16a5` is contained in `main`.

## Tool Versions and Discovery

| Tool or package | Version/discovery |
|---|---|
| CMake | `4.3.3` |
| Generator | Ninja `1.13.2` |
| Compiler | GCC `/usr/bin/c++`, `16.1.1 20260430` |
| vcpkg tool | `/tmp/tsunami-g0-vcpkg`, checkout `cea592f4772491abdb7c483387a59ea89889f4be`, executable version `2026-05-27-d5b6777d666efc1a7f491babfcdab37794c1ae3e` |
| vcpkg registry baseline | `d015e31e90838a4c9dfa3eed45979bc70d9357fc` from `vcpkg-configuration.json` |
| vcpkg triplet | `x64-linux` |
| Qt | Qt `6.11.1`, discovered from system install at `/usr` using `CMAKE_PREFIX_PATH=/usr` |

The host did not provide a persistent `VCPKG_ROOT`. Validation used a temporary
official vcpkg checkout under `/tmp`, consistent with the accepted dependency
evidence. No dependency or licence metadata was changed.

## Core and CLI Validation

Configure:

```sh
cmake -S . -B build/g0-cli -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/tsunami-g0-vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DTSUNAMI_BUILD_CLI=ON \
  -DTSUNAMI_BUILD_GUI=OFF \
  -DBUILD_TESTING=OFF
```

Result: configuration succeeded. vcpkg restored/built the default manifest
packages, including CLI11 `2.6.2`.

Build:

```sh
cmake --build build/g0-cli --target tsunami_core tsunami_cli
```

Result: `tsunami_core` and `tsunami_cli` built successfully.

Smoke:

```sh
./build/g0-cli/apps/tsunami_cli/tsunami_cli --version
```

Result:

```text
Tsunami Barrier Simulation 0.1.0
```

Exit status: `0`.

## Test Validation

Configure:

```sh
cmake -S . -B build/g0-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/tsunami-g0-vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_MANIFEST_FEATURES=tests \
  -DTSUNAMI_BUILD_CLI=ON \
  -DTSUNAMI_BUILD_GUI=OFF \
  -DBUILD_TESTING=ON
```

Result: configuration succeeded. Catch2 `3.15.0` resolved through the vcpkg
`tests` feature.

Build:

```sh
cmake --build build/g0-tests --target tsunami_tests
```

Result: `tsunami_tests` built successfully.

CTest:

```sh
ctest --test-dir build/g0-tests --output-on-failure
```

Result:

```text
100% tests passed, 0 tests failed out of 1
```

## Qt Quick GUI Validation

Configure:

```sh
cmake -S . -B build/g0-gui -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/tsunami-g0-vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DCMAKE_PREFIX_PATH=/usr \
  -DTSUNAMI_BUILD_GUI=ON \
  -DBUILD_TESTING=OFF
```

Result: configuration succeeded. Qt was discovered from `/usr`; Qt reported
missing optional Vulkan headers, but they are not required by the Qt Quick
scaffold.

Build:

```sh
cmake --build build/g0-gui --target tsunami_gui
```

Result: `tsunami_gui` built successfully. The build ran QML import scanning,
QML type registration and Qt resource generation for `qml/Main.qml`.

Offscreen startup:

```sh
env QT_QPA_PLATFORM=offscreen \
  ./build/g0-gui/apps/tsunami_gui/tsunami_gui --smoke-startup
```

Result: QML loaded and the executable exited with status `0`.

## Static Inspection

Commands:

```sh
rg -n "include_directories|link_directories|add_compile_options|CMAKE_CXX_STANDARD|TSUNAMI_BUILD_TESTS|TSUNAMI_BUILD_MATPLOT_SMOKE" \
  CMakeLists.txt CMakePresets.json src apps tests cmake
rg -n "Qt6|Catch2|CLI11" \
  build/g0-gui/src/CMakeFiles/tsunami_core.dir \
  build/g0-tests/src/CMakeFiles/tsunami_core.dir \
  build/g0-cli/src/CMakeFiles/tsunami_core.dir
cmake --build build/g0-gui --target help
```

Results:

- no global `include_directories`;
- no global `link_directories`;
- no global `add_compile_options`;
- no active global `CMAKE_CXX_STANDARD` setting;
- no active `TSUNAMI_BUILD_TESTS` or `TSUNAMI_BUILD_MATPLOT_SMOKE` option;
- `tsunami_core` build metadata contains no Qt, CLI11 or Catch2 link;
- active build targets include `tsunami_core`, `tsunami_cli` and
  `tsunami_gui`; `tsunami_tests` is present in the test configuration;
- no source file is intentionally shared by multiple executable targets.

Historical `apps/TsunamiGUI/CMakeLists.txt` still names `TsunamiGUI` and
`Qt6::Widgets`, but that directory is not added by the active root build and is
documented as a preserved prototype.

## General Checks

Commands run after implementation and documentation:

```sh
python3 -m json.tool CMakePresets.json >/tmp/cmake-presets-json-check.txt
git status --short
git diff --check
git diff --stat
git diff
```

Results are recorded in the final PR/commit review. No Windows or macOS
validation was performed or claimed from this Linux host.

## Acceptance Summary

| Requirement | Result |
|---|---|
| `tsunami_core` configures and builds | Pass |
| `tsunami_cli` configures and builds | Pass |
| `tsunami_cli --version` succeeds | Pass |
| Catch2 resolves only with `BUILD_TESTING=ON` and `tests` feature | Pass |
| `tsunami_tests` builds | Pass |
| CTest passes | Pass |
| Qt Quick target configures and builds | Pass |
| QML startup/offscreen smoke succeeds | Pass |
| No global include/link/warning state | Pass |
| `tsunami_core` remains Qt-free | Pass |
| No solver or final architecture implementation added | Pass |
