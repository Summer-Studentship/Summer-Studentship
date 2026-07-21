# Linux And Docker Build

Docker is the pre-WBS portability check for the non-GUI core. It exercises a
clean Linux image, but it is not yet the authorised clean-clone smoke test;
replacement target/preset/smoke evidence belongs to `SWE-ENV-BLD-WP1`,
`SWE-ENV-PRS-WP1` and `SWE-ENV-SMK-WP1`.

## Build The Image

From the repository root:

```sh
docker build -t summer-studentship-build-check .
```

The Dockerfile:

- installs compiler, CMake, Ninja, Git, and archive tools
- clones the vcpkg tool to `/opt/vcpkg`
- checks out exact vcpkg **tool** commit
  `cea592f4772491abdb7c483387a59ea89889f4be`; package/port resolution is
  separately governed by the exact default-registry baseline
  `d015e31e90838a4c9dfa3eed45979bc70d9357fc` in
  `vcpkg-configuration.json`
- configures `linux-vcpkg-headless` with
  `CMAKE_TOOLCHAIN_FILE=$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`
- builds `linux-vcpkg-headless-build`
- invokes `ctest`

CTest invocation is not the same as registered test execution. The current
repository enables CTest but does not yet register test executables, so Docker
does not prove a non-empty test suite.

Docker does not validate Windows builds, GUI runtime behaviour, release
packaging, or installer generation.

## Native Linux Equivalent

If working directly on Linux instead of Docker:

```sh
export VCPKG_ROOT=/opt/vcpkg
export VCPKG_MAX_CONCURRENCY=2

cmake --preset linux-vcpkg-headless
cmake --build --preset linux-vcpkg-headless-build
ctest --test-dir build/linux-vcpkg-headless --output-on-failure
```

This preset disables:

```text
TSUNAMI_BUILD_GUI
TSUNAMI_BUILD_MATPLOT_SMOKE
TSUNAMI_BUILD_TESTS
```

That reflects the historical target scaffold. Optional plotting now comes from
the `diagnostics` manifest group and remains disabled until its external backend
is approved and smoke tested. The Linux GUI preset, `linux-gui-debug`, uses
system Qt while the shared Linux vcpkg toolchain remains available for non-Qt
manifest dependencies. Shared replacement presets are pending
`SWE-ENV-PRS-WP1`.
