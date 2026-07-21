# Linux And Docker Build

Docker is the reproducibility check for the non-GUI core. It checks that a
clean Linux image can configure the project with the vcpkg toolchain, discover
manifest dependencies, compile the configured targets, and invoke CTest without
depending on a developer workstation.

## Build The Image

From the repository root:

```sh
docker build -t summer-studentship-build-check .
```

The Dockerfile:

- installs compiler, CMake, Ninja, Git, and archive tools
- clones vcpkg to `/opt/vcpkg`
- checks out the pinned vcpkg baseline
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

That is intentional. GUI builds and interactive plotting should be checked
natively on developer machines, while Docker checks the portable numerical core.
The Linux GUI preset, `linux-gui-debug`, uses system Qt while the shared Linux
vcpkg toolchain remains available for non-Qt manifest dependencies.
