# Linux And Docker Build

Docker is historical portability evidence for the non-GUI core. It exercises a
clean Linux image, but it is not yet the authorised clean-clone smoke test;
replacement smoke evidence belongs to `SWE-ENV-SMK-WP1`.

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
- configures a headless Linux preset with
  `CMAKE_TOOLCHAIN_FILE=$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`
- builds the matching headless target set
- invokes `ctest`

The accepted registered test suite is the `tsunami_tests` CTest target selected
by `BUILD_TESTING=ON` and the vcpkg `tests` feature.

Docker does not validate Windows builds, GUI runtime behaviour, release
packaging, or installer generation.

## Native Linux Equivalent

If working directly on Linux instead of Docker:

```sh
export VCPKG_ROOT=/opt/vcpkg
export VCPKG_MAX_CONCURRENCY=2

cmake --workflow --preset linux-gcc-test-workflow
```

For a developer build without tests:

```sh
cmake --preset linux-gcc-dev
cmake --build --preset linux-gcc-dev-build
```

Optional plotting comes from the `diagnostics` manifest group and remains
disabled until its external backend is approved and smoke tested. The Linux GUI
preset, `linux-gcc-gui-dev`, uses external Qt while the shared Linux vcpkg
toolchain remains available for non-Qt manifest dependencies.
