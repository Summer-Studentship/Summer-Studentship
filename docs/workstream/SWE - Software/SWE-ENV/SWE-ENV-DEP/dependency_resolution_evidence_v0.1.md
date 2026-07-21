# Dependency Resolution Evidence v0.1

- **Document ID:** `SWE-ENV-DEP-WP1-EVD-v0.1`
- **Work Package:** `SWE-ENV-DEP-WP1`
- **Evidence date:** 2026-07-21
- **Host:** Linux `x64-linux`; compiler detected by vcpkg as `/usr/bin/c++`

**Result:** Manifest syntax, exact registry resolution, direct port names and all
independent/additive feature plans passed. No packages were built or installed.

## Tool and baseline provenance

No pre-installed vcpkg executable or `VCPKG_ROOT` was available in `PATH`, the
repository, `/opt`, or the ordinary user tool locations inspected on this host.
Validation therefore used an isolated official vcpkg checkout under a temporary
directory; it did not add a tool or generated state to the repository.

The isolated tool checkout used the repository's pre-existing bootstrap/tool
commit so that the package-registry change could be evaluated independently:

```text
vcpkg tool checkout: cea592f4772491abdb7c483387a59ea89889f4be
vcpkg executable version: 2026-05-27-d5b6777d666efc1a7f491babfcdab37794c1ae3e
metrics: disabled during bootstrap
```

Representative bootstrap commands were:

```sh
git clone https://github.com/microsoft/vcpkg.git <temporary-vcpkg-root>
git -C <temporary-vcpkg-root> checkout \
  cea592f4772491abdb7c483387a59ea89889f4be
<temporary-vcpkg-root>/bootstrap-vcpkg.sh -disableMetrics
VCPKG_EXE=<temporary-vcpkg-root>/vcpkg
"${VCPKG_EXE}" version
```

The accepted registry release tag was resolved independently:

```sh
git ls-remote https://github.com/microsoft/vcpkg.git \
  'refs/tags/2026.05.25^{}'
```

Result:

```text
d015e31e90838a4c9dfa3eed45979bc70d9357fc
release/commit date: 2026-05-25
```

`vcpkg-configuration.json` contains that full commit as its sole default-registry
baseline. No floating registry reference or duplicate manifest
`builtin-baseline` is present.

## Syntax and format validation

Commands:

```sh
"${VCPKG_EXE}" format-manifest vcpkg.json
python3 -m json.tool vcpkg.json >/dev/null
python3 -m json.tool vcpkg-configuration.json >/dev/null
```

Results:

- vcpkg reported `Succeeded in formatting the manifest files.`;
- both JSON parsing commands exited successfully; and
- formatting retained the manifest's exact HDF5 and netCDF-C overrides.

## Compatibility investigation

Two invalid combinations were discovered and resolved against metadata already
recorded in the exact upstream registry; no overlay or invented version was
used.

### HDF5 feature schema

An initial plan using the historical `hdf5[hl]` spelling failed because the
registry's HDF5 1.14.6 port has no `hl` feature. Inspection of the exact port
manifest and portfile showed that its core build includes the C high-level
libraries. The final declaration is therefore:

```text
hdf5[core,zlib] 1.14.6
```

This plan passed. The obsolete C++ wrapper feature and HighFive dependency were
not restored.

### netCDF-C port revision

The baseline's newest 4.9.3 port revision requests `hdf5[hl,zlib]`, which is
incompatible with the selected HDF5 1.14.6 port schema. The registry versions
database also records upstream netCDF-C 4.9.3 at port revision 0, whose
`netcdf-4` dependency uses `hdf5[zlib]`. A manifest override to version 4.9.3
(implicit port revision 0) produced the exact tree
`b588ae3ed78f7cdb5a4afaba85678d8a91082422` and passed with HDF5 1.14.6.

## Final dependency plans

Each command used the repository root as the manifest root and a separate empty
temporary install root. `--dry-run` generated the resolution/build plan without
building or installing packages.

### Default dependencies

```sh
"${VCPKG_EXE}" install --dry-run --triplet x64-linux \
  --x-install-root=<temporary-plan-root>/default \
  --downloads-root=<temporary-vcpkg-downloads>
```

Exit status: 0.

| Direct port | Resolved version/features |
|---|---|
| `cli11` | 2.6.2 |
| `eigen3` | 5.0.1 |
| `fmt` | 12.1.0 |
| `hdf5` | 1.14.6, `core,zlib` |
| `nlohmann-json` | 3.12.0#2 |
| `pugixml` | 1.15#1 |
| `spdlog` | 1.17.0, `core,fmt` |
| `yaml-cpp` | 0.9.0#1 |

The direct default plan additionally selected zlib 1.3.2 and vcpkg helper ports
as transitives.

### Geospatial feature

```sh
"${VCPKG_EXE}" install --dry-run --triplet x64-linux \
  --x-feature=geospatial \
  --x-install-root=<temporary-plan-root>/geospatial \
  --downloads-root=<temporary-vcpkg-downloads>
```

Exit status: 0. Direct additions were GDAL 3.12.4#2 with no default features and
PROJ 9.8.1 with `core,tiff`. The plan exposed its transitive surface, including
json-c, libgeotiff, libjpeg-turbo, liblzma, pkgconf, SQLite, TIFF, zlib and vcpkg
build helpers. Those packages are review/release inputs, not project-owned APIs.

### Test feature

```sh
"${VCPKG_EXE}" install --dry-run --triplet x64-linux \
  --x-feature=tests \
  --x-install-root=<temporary-plan-root>/tests \
  --downloads-root=<temporary-vcpkg-downloads>
```

Exit status: 0. The direct addition was Catch2 3.15.0.

### Diagnostics feature

```sh
"${VCPKG_EXE}" install --dry-run --triplet x64-linux \
  --x-feature=diagnostics \
  --x-install-root=<temporary-plan-root>/diagnostics \
  --downloads-root=<temporary-vcpkg-downloads>
```

Exit status: 0. The direct addition was Matplot++ 1.2.1. Its resolved transitives
were CImg 3.7.6 and nodesoup 2023-06-12; Armadillo, FreeType, GLFW, OpenGL and
RapidXML were not selected. Port metadata warns that the selected non-OpenGL
backend requires external Gnuplot 5.2.6+ at runtime, so dependency-plan success
is not backend smoke evidence.

### Optional netCDF feature

```sh
"${VCPKG_EXE}" install --dry-run --triplet x64-linux \
  --x-feature=netcdf \
  --x-install-root=<temporary-plan-root>/netcdf \
  --downloads-root=<temporary-vcpkg-downloads>
```

Exit status: 0. The direct addition was netCDF-C 4.9.3 port revision 0 with
`core,netcdf-4`. It resolved alongside HDF5 1.14.6; TinyXML2 11.0.0 was an
additional transitive.

### Combined feature compatibility

```sh
"${VCPKG_EXE}" install --dry-run --triplet x64-linux \
  --x-feature=geospatial \
  --x-feature=tests \
  --x-feature=diagnostics \
  --x-feature=netcdf \
  --x-install-root=<temporary-plan-root>/all-features \
  --downloads-root=<temporary-vcpkg-downloads>
```

Exit status: 0. The combined plan contained every direct default and feature
port above with the same versions and no version/feature conflict.

## Repository validation

The final repository checks for this evidence set are:

```sh
git status --short
git diff --check
git diff --stat
git diff
python3 -m json.tool vcpkg.json >/dev/null
python3 -m json.tool vcpkg-configuration.json >/dev/null
```

The working-tree check, the dependency commit's staged
`git diff --cached --check`, and both JSON checks passed. Status, statistics,
names and the complete staged dependency diff were inspected; the staged scope
contains no CMake target, preset, GUI, solver or test implementation change.
The applicable diff check is repeated immediately before each scoped commit.

## Constraints and unresolved platform evidence

- Only `x64-linux` planning was executed. Windows/MSVC, Windows/MinGW and macOS
  dependency plans remain unverified on their native platforms.
- `--dry-run` proves registry/port/feature resolution, not compilation, runtime
  discovery, binary compatibility or clean-clone restoration.
- A first repeat run inside the restricted execution sandbox could not refresh
  the Git registry and consequently reported ports as unavailable. Re-running
  the identical command with network access fetched registry metadata and
  passed. This is recorded as an execution-environment limitation, not a
  dependency-resolution failure.
- Matplot++ output remains runtime-unverified until the Gnuplot acquisition and
  licence decision is accepted and `SWE-ENV-SMK` supplies a headless smoke test.
- GDAL driver sufficiency, PROJ grid selection, Qt module deployment and actual
  linked runtime libraries are deliberately verified by their owning later
  implementation, preset, smoke and release Work Packages.

These limitations do not weaken the Work Package claim: repository-controlled
metadata deterministically produces a complete dependency plan for the tested
host and each selected manifest group. It does not claim a complete
multi-platform build.
