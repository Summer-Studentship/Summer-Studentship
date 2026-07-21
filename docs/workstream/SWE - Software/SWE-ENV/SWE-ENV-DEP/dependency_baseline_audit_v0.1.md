# Dependency Baseline Audit v0.1

- **Document ID:** `SWE-ENV-DEP-WP1-AUD-v0.1`
- **Work Package:** `SWE-ENV-DEP-WP1`
- **Baseline date:** 2026-07-21

**Decision state:** Accepted dependency-acquisition baseline; build-target and preset integration remain assigned to their owning Work Packages.

## Authority and scope

This audit reconciles the pre-WBS dependency mechanism with the accepted
[software stack decision register](../SWE-ENV-STACK/studentship_software_stack_decision_register_v0.1.md).
The repository-controlled manifest is `vcpkg.json`; its only registry authority
is the exact `default-registry.baseline` in `vcpkg-configuration.json`. The
historical CMake and preset files were inspected but are intentionally unchanged
by this Work Package.

The words **direct** and **transitive** are used deliberately. A package selected
by another port is not a project-owned API and must not be linked or included by
project code unless the manifest and stack register are amended first.

## Pre-WBS dependency disposition

| Existing package or route | Existing acquisition route | Accepted-stack status | Decision and replacement | Owning WBS Deliverable | Required later CMake or preset work | Baseline class |
|---|---|---|---|---|---|---|
| `eigen3` | Default vcpkg manifest dependency | Accepted serial linear-algebra baseline | **Retain** as a direct package | `SWE-FVM-LIN` | `SWE-ENV-BLD` must create/link the owning numerical target without leaking Eigen types across public boundaries | Default |
| `hdf5[cpp,hl,zlib]` | Default vcpkg dependency | HDF5 1.14.6 C and high-level APIs are accepted; the old feature spelling does not match the pinned port | **Replace declaration** with `hdf5`, default features disabled, `zlib` enabled, and the supported `1.14.6` override. The pinned port builds the C/high-level libraries without an `hl` feature | `SWE-DAT-SCH`, `SWE-DAT-CHK` | `SWE-ENV-BLD` must discover and link the required HDF5 C/high-level targets and keep the serial baseline | Default |
| `highfive` | Default vcpkg manifest dependency | Not selected by the authoritative stack | **Remove**; project code will use the HDF5 C/high-level APIs directly | `SWE-DAT-SCH`, `SWE-DAT-CHK` | `SWE-ENV-BLD` must not add a HighFive target; any future wrapper choice requires a stack amendment | Removed |
| `nlohmann-json` | Default vcpkg manifest dependency | Accepted for JSON interchange | **Retain** as a direct package | `SWE-DAT-CFG`, `SWE-DAT-MAN` | `SWE-ENV-BLD` must link only targets that own JSON interchange | Default |
| `armadillo` | Direct member of historical `visualization` feature | Not selected as a project API | **Remove** as a direct dependency; do not infer an API from plotting-port internals | `SWE-GUI-POST`, `SWE-VER-CONV` | `SWE-ENV-BLD` must isolate Matplot++ behind a diagnostics adapter | Removed; transitive use only if a future port requires it |
| `freetype` | Direct member of historical `visualization` feature | Direct font rendering is rejected | **Remove**; Qt or an optional plotting backend owns any font stack it selects | `SWE-GUI`, `SWE-GUI-POST` | No direct CMake discovery; re-review only if it appears in a resolved transitive plan | Removed; possible transitive |
| `glfw3` | Direct member of historical `visualization` feature | Explicit GLFW application dependency is rejected | **Remove**; Qt owns application windowing and Matplot++ OpenGL is disabled | `SWE-GUI`, `SWE-GUI-POST` | Remove any future/historical direct link during `SWE-ENV-BLD`; do not enable the experimental plotting backend | Removed; possible transitive |
| `opengl` | Direct member of historical `visualization` feature | Direct OpenGL in the numerical core is rejected | **Remove**; graphics belongs to Qt or an approved optional adapter | `SWE-GUI`, `SWE-GUI-POST` | `SWE-ENV-BLD` must not give core targets an OpenGL dependency | Removed; possible transitive |
| `rapidxml` | Direct member of historical `visualization` feature | Superseded for project-owned XML/XDMF generation | **Replace** with direct `pugixml` | `SWE-DAT-XDMF` | `SWE-ENV-BLD` must link pugixml only to the XDMF/data target | Removed; replacement is default |
| `qtbase` | Historical vcpkg `gui` feature | Qt is accepted, but vcpkg acquisition is rejected | **Externalise** accepted Core, Gui and Concurrent modules to the Qt installer/Maintenance Tool | `SWE-GUI`, `SWE-ENV-LIC` | `SWE-ENV-BLD` must discover the external Qt installation; `SWE-ENV-PRS` must pass portable hints without absolute repository paths | External mandatory production dependency |
| `qtdeclarative` | Historical vcpkg `gui` feature | Qml, Quick and Quick Controls are accepted, but vcpkg acquisition is rejected | **Externalise** to the Qt installer/Maintenance Tool | `SWE-GUI-SHL`, `SWE-ENV-LIC` | Qt Quick target integration belongs to `SWE-ENV-BLD`/`SWE-GUI-SHL`; shared selection belongs to `SWE-ENV-PRS` | External mandatory production dependency |
| `qtquick3d` | Historical vcpkg `gui` feature | Not in the accepted Qt module set | **Remove and defer**; add only when an owning GUI Deliverable and licence review approve it | `SWE-GUI-VIS`, `SWE-ENV-LIC` | No target or preset integration in the baseline | Deferred external module |
| `qtquicktimeline` | Historical vcpkg `gui` feature | Not in the accepted Qt module set | **Remove and defer** pending an explicit GUI requirement and licence review | `SWE-GUI`, `SWE-ENV-LIC` | No target or preset integration in the baseline | Deferred external module |
| `qtshadertools` | Historical vcpkg `gui` feature | Not in the accepted Qt module set | **Remove and defer** pending an explicit GUI requirement and licence review | `SWE-GUI-VIS`, `SWE-ENV-LIC` | No target or preset integration in the baseline | Deferred external module |
| `external/mathplot` | Git clone of `sebsjames/mathplot` at a script-pinned revision, patched locally | Superseded; accepted plotting component is Matplot++ | **Replace** with vcpkg `matplotplusplus` in the optional `diagnostics` feature; retire both bootstrap scripts and all active clone instructions | `SWE-GUI-POST`, `SWE-VER-CONV` | `SWE-ENV-BLD` must remove the dead external-header/smoke route and create an isolated adapter. `SWE-ENV-SMK` must validate a supported headless backend | Optional feature; old vendored route removed |

The pinned Matplot++ 1.2.1 port resolves only its own `cimg` and `nodesoup`
dependencies and does not make Armadillo, FreeType, GLFW, OpenGL or RapidXML
direct dependencies. Its non-OpenGL backend expects an external Gnuplot 5.2.6+
runtime. Therefore `diagnostics` remains disabled by default. Gnuplot acquisition,
licence verification and headless output are explicit `SWE-ENV-SMK`/stack-review
items; the rejected experimental OpenGL backend has not been enabled as a
shortcut.

## Accepted direct package groups

| Group | Direct vcpkg ports | Status and rationale | Primary owner |
|---|---|---|---|
| Default/core | `cli11`, `eigen3`, `fmt`, `hdf5[zlib]`, `nlohmann-json`, `pugixml`, `spdlog`, `yaml-cpp` | Restored on every manifest install. HDF5 is overridden to 1.14.6; its high-level libraries are part of that port's core build | `SWE-ENV-DEP` plus capability owners in the stack register |
| `geospatial` | `gdal`, `proj[tiff]` | Accepted production stack, feature-gated until `SWE-GEO-IMP`/`SWE-GEO-CRS` targets exist. GDAL default features are disabled to prevent an unreviewed driver/dependency expansion | `SWE-GEO-IMP`, `SWE-GEO-CRS` |
| `tests` | `catch2` | Development-only test/benchmark framework | `SWE-VER-UNIT`, `SWE-VER-BMK` |
| `diagnostics` | `matplotplusplus` | Optional plotting adapter only; not the production field visualiser. Disabled by default pending backend smoke evidence | `SWE-GUI-POST`, `SWE-VER-CONV` |
| `netcdf` | `netcdf-c[netcdf-4]` | Optional direct ingestion only when GDAL cannot preserve required multidimensional data or metadata. The same upstream 4.9.3 line is pinned to port revision 0 because later port revisions require an HDF5 feature absent from the accepted 1.14.6 port | `SWE-DAT-ING`, `SWE-GEO-IMP` |

Qt is deliberately absent from every vcpkg group. The accepted external Qt 6
module set is Core, Gui, Qml, Quick, Quick Controls and Concurrent. Gmsh,
ParaView, QGIS, MATLAB, Doxygen, LaTeX, Python, Ninja, CMake and Git also remain
external tools or applications and are not manifest ports.

### Reference versus resolved versions

The stack register deliberately marked exact resolution provisional pending this
Work Package. The selected registry resolves yaml-cpp 0.9.0#1 rather than its
0.8.0 reference, GDAL 3.12.4#2 rather than its later 3.13.1 reference, and
Matplot++ 1.2.1 rather than its 1.2.2 reference. These are supported versions in
the accepted exact registry; no unsupported overlay was added merely to mimic a
reference. HDF5 is different because the stack explicitly selects 1.14.6, so it
has a compatibility override. The optional netCDF-C override is the same upstream
4.9.3 release at port revision 0 and exists solely to remain compatible with
that selected HDF5 port. Any move toward a newer registry/reference is governed
by the update policy below and requires dependency, licence and smoke review.

## Registry pin decision

`vcpkg-configuration.json` is the sole package-registry authority:

```text
repository: https://github.com/microsoft/vcpkg
release reference: 2026.05.25
exact peeled commit: d015e31e90838a4c9dfa3eed45979bc70d9357fc
release commit date: 2026-05-25
```

The exact commit was obtained with:

```sh
git ls-remote https://github.com/microsoft/vcpkg.git \
  'refs/tags/2026.05.25^{}'
```

It was selected because it is the immutable commit behind the stack register's
accepted `2026.05.25` registry reference and resolves the direct ports without an
overlay. It replaces the pre-WBS post-release commit
`cea592f4772491abdb7c483387a59ea89889f4be`. There is no duplicate
`builtin-baseline` in `vcpkg.json`.

Baseline updates must use a dedicated dependency branch. The updater must choose
and record an upstream release tag, resolve it to a full commit, change the one
configuration authority, format the manifest, inspect direct and transitive
plans, update the licence diff and obtain CI/smoke evidence before merge. A
floating branch or unreviewed `x-update-baseline` result is not acceptable.

## Deferred integration ledger

This Work Package changes metadata and documentation only. The following
pre-existing inconsistencies are recorded rather than silently expanding scope:

| Existing artefact | Current condition | Required owner/action |
|---|---|---|
| `CMakeLists.txt` | Contains the pre-WBS external-mathplot smoke option/route and does not yet consume the accepted target set | `SWE-ENV-BLD-WP1`: create target-scoped discovery/linkage, remove the obsolete route, and add feature-appropriate adapters |
| `CMakePresets.json` | `windows-mingw-vcpkg-all` still names removed `visualization;gui` manifest features | `SWE-ENV-PRS-WP1`: replace historical feature selections with supported groups and external Qt discovery after the target scaffold exists |
| `CMakeUserPresets.json.example` | Illustrates local paths for the historical presets | `SWE-ENV-PRS-WP1`: update the example together with shared preset semantics; keep actual machine paths uncommitted |
| `scripts/build_windows_qt.ps1` | Its `-Visualization` switch selects the historical all-dependencies preset and therefore removed feature names | `SWE-ENV-PRS-WP1`: replace or retire the switch together with the shared preset migration; it is not a supported acquisition command meanwhile |
| `Dockerfile` | `VCPKG_BASELINE` currently names a vcpkg **tool checkout** (`cea592f...`), while package resolution is governed by `vcpkg-configuration.json` (`d015e31...`) | `SWE-ENV-BLD-WP1`/`SWE-ENV-SMK-WP1`: rename the argument to make the tool/registry distinction explicit, select a verified tool release, and run clean-clone restoration |

The historical presets are not evidence that removed feature names are
supported. Until `SWE-ENV-PRS-WP1` completes, developers should restore manifest
groups directly as documented in `docs/Markdowns/dependencies.md` and treat those
preset names as pending migration.

## Audit conclusion

The accepted package set now has one repository-controlled manifest, one exact
registry authority, explicit default/optional boundaries, and no active
`external/mathplot` acquisition route. Resolution evidence is recorded in
`dependency_resolution_evidence_v0.1.md`. Build-target, preset and clean-clone
execution remain separate authorised Work Packages.
