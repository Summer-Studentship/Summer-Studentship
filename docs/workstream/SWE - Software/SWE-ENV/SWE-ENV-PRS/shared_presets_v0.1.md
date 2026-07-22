# Shared Presets v0.1

- **Work Package:** `SWE-ENV-PRS-WP1`
- **Issues:** `#111`, `#112`, `#113`
- **Document date:** 2026-07-22

## Naming Scheme

Configure presets use:

```text
<platform>-<compiler>-<purpose>
```

Build presets append `-build`. Test presets append `-run` or `-test`.
Workflow presets append `-workflow`.

## Inheritance Graph

```text
base
  -> vcpkg-base
       -> linux-base
            -> linux-gcc-base
                 -> linux-gcc-dev
                 -> linux-gcc-test    + tests-base
                 -> linux-gcc-release
                 -> linux-gcc-gui-dev + gui-base
                 -> linux-gcc-ci      + tests-base
            -> linux-clang-base
                 -> linux-clang-dev
                 -> linux-clang-test  + tests-base
       -> windows-msvc-base
            -> windows-msvc-dev
            -> windows-msvc-test      + tests-base
            -> windows-msvc-release
            -> windows-msvc-gui-dev   + gui-base
            -> windows-msvc-ci        + tests-base
```

`base` owns Ninja and `${sourceDir}/build/${presetName}`. `vcpkg-base` owns the
manifest toolchain expression derived from `VCPKG_ROOT`. `gui-base` owns
external Qt discovery through `QT_ROOT`.

## Compiler Authority

Linux GCC is the validated Linux developer/release/CI route. Linux Clang is
defined and validated for developer and test coverage on this host.

Windows MSVC is the authoritative Windows family. It uses `Ninja Multi-Config`,
the `x64-windows` vcpkg triplet and an already activated Visual Studio
Developer PowerShell or command prompt. Native Windows execution is not claimed
from Linux.

MinGW is retired from shared preset authority. Historical MinGW notes remain as
prototype evidence only.

## Configure Families

| Configure preset | CLI | GUI | Testing | Build type/config | vcpkg features | Qt route |
|---|---:|---:|---:|---|---|---|
| `linux-gcc-dev` | ON | OFF | OFF | Debug | default | none |
| `linux-gcc-test` | ON | OFF | ON | Debug | `tests` | none |
| `linux-gcc-release` | ON | OFF | OFF | Release | default | none |
| `linux-gcc-gui-dev` | ON | ON | OFF | Debug | default | `QT_ROOT` |
| `linux-gcc-ci` | ON | OFF | ON | RelWithDebInfo | `tests` | none |
| `linux-clang-dev` | ON | OFF | OFF | Debug | default | none |
| `linux-clang-test` | ON | OFF | ON | Debug | `tests` | none |
| `windows-msvc-dev` | ON | OFF | OFF | Debug build preset config | default | none |
| `windows-msvc-test` | ON | OFF | ON | Debug build preset config | `tests` | none |
| `windows-msvc-release` | ON | OFF | OFF | Release build preset config | default | none |
| `windows-msvc-gui-dev` | ON | ON | OFF | Debug build preset config | default | `QT_ROOT` |
| `windows-msvc-ci` | ON | OFF | ON | RelWithDebInfo build preset config | `tests` | none |

## Command Table

| Purpose | Command |
|---|---|
| GCC developer configure | `cmake --preset linux-gcc-dev` |
| GCC developer build | `cmake --build --preset linux-gcc-dev-build` |
| GCC tests | `cmake --workflow --preset linux-gcc-test-workflow` |
| Clang tests | `cmake --workflow --preset linux-clang-test-workflow` |
| GCC GUI configure | `cmake --preset linux-gcc-gui-dev` |
| GCC GUI build | `cmake --build --preset linux-gcc-gui-dev-build` |
| GCC release configure | `cmake --preset linux-gcc-release` |
| GCC release build | `cmake --build --preset linux-gcc-release-build` |
| GCC CI workflow | `cmake --workflow --preset linux-gcc-ci-workflow` |
| MSVC tests | `cmake --workflow --preset windows-msvc-test-workflow` on native Windows |
| MSVC CI | `cmake --workflow --preset windows-msvc-ci-workflow` on native Windows |

## Local Overrides

Create a local file by copying:

```sh
cp CMakeUserPresets.json.example CMakeUserPresets.json
```

Then replace:

```text
<path-to-vcpkg>
<path-to-qt-prefix-or-/usr>
```

`CMakeUserPresets.json` is ignored. Do not commit local compiler paths, Qt
paths, vcpkg paths, caches or credentials.

Linux developers may also avoid a user preset by exporting:

```sh
export VCPKG_ROOT=/path/to/vcpkg
export QT_ROOT=/usr
```

Use an actual Qt installer prefix instead of `/usr` when Qt is installed outside
the system CMake search path.

For Windows MSVC, activate the Visual Studio Developer environment before
running CMake. Do not encode Visual Studio installation paths in shared presets.

## Binary Directories and Cache Invalidation

Every configure preset writes to:

```text
build/<preset-name>
```

To reconfigure cleanly, remove the specific preset directory:

```sh
cmake -E rm -rf build/linux-gcc-test
cmake --preset linux-gcc-test
```

Do not delete unrelated build directories merely to switch between GCC, Clang,
GUI, release or CI presets.

## CI and Release Handoff

`linux-gcc-ci-workflow` is the local Linux CI-equivalent workflow for the G0
scaffold. Actual GitHub Actions workflow creation remains `SWE-REL-CI-WP1`.

Clean-clone, cache, runtime and multi-platform smoke evidence remains
`SWE-ENV-SMK-WP1`. Native Windows execution is required before Windows support
is accepted for release evidence.

## Unsupported or Unvalidated Platforms

No macOS preset is defined because no native macOS evidence exists in this Work
Package. Windows MSVC presets are structurally validated from Linux but not
natively executed.
