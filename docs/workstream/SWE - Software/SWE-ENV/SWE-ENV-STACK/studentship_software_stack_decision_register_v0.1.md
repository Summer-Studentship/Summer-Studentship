# Studentship Software Stack Decision Register

**Document ID:** `SWE-ENV-STACK-DR-v0.1`  
**Date:** 2026-07-19  
**WBS parent:** `SWE-ENV-STACK-WP1`  
**Primary task:** `SWE-ENV-STACK-WP1-T2`  
**Decision state:** Architectural stack accepted; exact dependency resolution remains provisional until `SWE-ENV-DEP-WP1` and `SWE-ENV-SMK-WP1` pass.  
**Project:** Summer Studentship — tsunami-energy-dissipating barrier modelling

---

## 1. Decision principles

1. The numerical and data core shall remain independent of Qt, plotting libraries and external applications.
2. Dependencies shall be acquired through a repository-controlled vcpkg manifest wherever technically and legally appropriate.
3. The vcpkg registry shall be pinned by an exact `builtin-baseline` commit; floating dependency resolution is prohibited.
4. Qt shall be acquired through the Qt installer/Maintenance Tool rather than built through vcpkg.
5. Gmsh shall initially be invoked as an external executable and exchange `.msh` files; the production application shall not link directly to Gmsh.
6. ParaView and QGIS are external scientific tools, not embedded application dependencies.
7. Matplot++ shall be isolated behind a plotting/diagnostics adapter.
8. MATLAB shall initially consume HDF5 outputs. Direct MATLAB Engine integration is deferred.
9. Every accepted dependency requires a licence entry, owner, package source and clean-clone verification.
10. Upstream “latest” versions are references only. The repository baseline is authoritative after the dependency smoke test.

---

## 2. Classification

| Classification | Meaning |
|---|---|
| **Mandatory — production** | Required to build or run the active software baseline. |
| **Mandatory — development** | Required for build, testing, documentation or preprocessing, but not necessarily distributed with the application. |
| **Optional retained** | Supported or planned, but the baseline must build without it. |
| **Deferred** | Architecturally anticipated but excluded from G0/G1 implementation. |
| **Rejected/superseded** | Considered but not selected for the current architecture. |

---

## 3. Core language, build and package infrastructure

| Capability | Selected component | Baseline/reference | Classification | Acquisition | Licence | Decision and constraints | WBS owner |
|---|---|---:|---|---|---|---|---|
| Implementation language | C++ | C++20 | Mandatory — production | Compiler toolchain | ISO standard | Use the C++20 language level. Do not require experimental language extensions. | `SWE-ENV-BLD`, `SWE-ARC` |
| Build system | CMake | Minimum 3.28; test through current 4.x, reference 4.4.0 | Mandatory — development | System/package installer | BSD-3-Clause | Use target-based CMake, presets and target-scoped properties. No global include or link directories. | `SWE-ENV-BLD`, `SWE-ENV-PRS` |
| Build executor | Ninja | vcpkg/system-resolved stable | Mandatory — development | System package | Apache-2.0 | Default single-configuration generator on Linux and CI; retain multi-config compatibility. | `SWE-ENV-BLD` |
| C++ package manager | vcpkg manifest mode | Registry reference `2026.05.25`; exact commit to be pinned | Mandatory — development | Repository bootstrap | MIT | Use `vcpkg.json`, feature groups and `builtin-baseline`. Overrides require a documented compatibility reason. | `SWE-ENV-DEP` |
| Source control | Git | Supported stable system version | Mandatory — development | System package | GPL-2.0-only | Repository is the authority for source, manifests, presets, schemas and documentation. | `SWE-REL-CI` |
| CI | GitHub Actions | Versioned action major tags or commit SHAs | Mandatory — development | GitHub service | Service/action-specific | CI must call the same CMake presets used locally. Third-party actions require licence and security review. | `SWE-REL-CI` |
| Primary Linux compiler | GCC | GCC 13 or newer | Mandatory — development | System toolchain | GPL with runtime exceptions | Primary local/CI compiler family. Exact CI image/compiler shall be recorded. | `SWE-ENV-BLD` |
| Secondary Linux compiler | Clang | Clang 17 or newer | Mandatory — development | System toolchain | Apache-2.0 with LLVM exceptions | Used for portability, diagnostics and sanitizer coverage. | `SWE-ENV-BLD`, `SWE-VER-UNIT` |
| Windows compiler | MSVC | Visual Studio 2022 toolset | Mandatory — development | Visual Studio Build Tools | Microsoft terms | Primary supported Windows compiler. MinGW may remain an experimental compatibility target but is not the release authority. | `SWE-ENV-BLD`, `SWE-REL-PKG` |

---

## 4. Application and interface stack

| Capability | Selected component | Baseline/reference | vcpkg port | Classification | Licence | Decision and constraints | WBS owner |
|---|---|---:|---|---|---|---|---|
| Desktop framework | Qt 6 | Qt 6.11.1 reference; minimum supported minor determined by smoke test | Not managed through vcpkg | Mandatory — production | Commercial or LGPL-3.0/GPL, module-dependent | Install through the Qt installer. Dynamically link open-source LGPL modules. Maintain a module-level licence register. | `SWE-GUI`, `SWE-ENV-LIC` |
| UI technology | Qt Quick/QML | Same Qt baseline | `qtdeclarative` if ever package-managed | Mandatory — production | Qt module licence | Selected over Qt Widgets and ImGui for the primary application shell. | `SWE-GUI-SHL` |
| Qt foundation modules | Qt Core, Gui, Qml, Quick, Quick Controls, Concurrent | Same Qt baseline | Not managed through vcpkg | Mandatory — production | Qt module licence | Add modules only when an owning Deliverable requires them. Avoid GPL-only modules unless project licensing explicitly permits them. | `SWE-GUI`, `SWE-ENV-LIC` |
| CLI parsing | CLI11 | vcpkg-baseline resolved stable | `cli11` | Mandatory — production | BSD-3-Clause | Used only by the CLI adapter. Application services must not depend on CLI11 types. | `SWE-ARC-SVC` |
| Structured logging | spdlog | 1.17.0 reference | `spdlog` | Mandatory — production | MIT | Use external `fmt`, structured categories and sink adapters. Core emits diagnostics without Qt types. | `SWE-ARC-ERR`, `SWE-GUI-LOG` |
| Formatting | fmt | 12.1.0 reference | `fmt` | Mandatory — production | MIT | Selected instead of relying solely on inconsistent cross-toolchain `std::format` implementations. Build spdlog against external fmt. | `SWE-ARC-ERR` |
| JSON interchange | nlohmann/json | 3.12.0 reference | `nlohmann-json` | Mandatory — production | MIT | Used for structured metadata, diagnostic payloads and lightweight interchange; YAML remains the human-edited case format. | `SWE-DAT-CFG`, `SWE-DAT-MAN` |
| YAML configuration | yaml-cpp | 0.8.0 reference | `yaml-cpp` | Mandatory — production | MIT | Human-authored case files. Semantic validation remains project-owned rather than delegated to the parser. | `SWE-DAT-CFG` |
| XML/XDMF generation | pugixml | vcpkg-baseline resolved stable | `pugixml` | Mandatory — production | MIT | Selected for deterministic XDMF generation and validation. Supersedes RapidXML for new project code. | `SWE-DAT-XDMF` |

---

## 5. Scientific data and geospatial stack

| Capability | Selected component | Baseline/reference | vcpkg port | Classification | Licence | Decision and constraints | WBS owner |
|---|---|---:|---|---|---|---|---|
| Scientific storage | HDF5 | **Selected line: 1.14.6**; HDF5 2.1.1 deferred for compatibility evaluation | `hdf5` | Mandatory — production | HDF5 BSD-style licence | Use serial HDF5 with C and high-level APIs for G0/G1. Enable compression only where reproducibility and reader compatibility are verified. | `SWE-DAT-SCH`, `SWE-DAT-CHK` |
| ParaView descriptor | XDMF | Project-owned schema profile | No direct dependency | Mandatory — production | Specification/file format | Generate minimal deterministic XDMF referencing HDF5 datasets. Do not adopt the XDMF library unless manual generation becomes unmanageable. | `SWE-DAT-XDMF` |
| Raster/vector I/O | GDAL | 3.13.1 reference | `gdal` | Mandatory — production | MIT-style | Canonical C++ raster/vector ingestion layer. Driver set shall be minimised and verified against selected project datasets. | `SWE-GEO-IMP` |
| Coordinate transformations | PROJ | 9.8.1 reference; PROJ-data recorded separately | `proj` | Mandatory — production | MIT | All horizontal/vertical transformations must record source CRS, target CRS, pipeline and grid resources. | `SWE-GEO-CRS` |
| Mesh generation | Gmsh | 4.15.2 stable reference | External executable; `gmsh` port not linked initially | Mandatory — development | GPL-2.0-or-later with Gmsh exception; commercial option | Invoke externally using versioned `.geo`/configuration files and import MSH 4.1. Direct library integration is deferred because of licence and coupling implications. | `SWE-GEO-MSH`, `SWE-ENV-LIC` |
| Optional direct NetCDF ingestion | netCDF-C | vcpkg-baseline resolved stable | `netcdf-c` | Optional retained | BSD-style | Add only if GDAL cannot preserve required metadata or multidimensional structure for selected datasets. | `SWE-DAT-ING`, `SWE-GEO-IMP` |
| Compression | zlib | vcpkg-baseline resolved stable | `zlib` | Mandatory — production, transitive/direct | zlib | Required for common HDF5/GDAL compression paths. Treat as an explicit manifest dependency if the schema mandates compression. | `SWE-DAT-SCH` |

---

## 6. Numerical and verification stack

| Capability | Selected component | Baseline/reference | vcpkg port | Classification | Licence | Decision and constraints | WBS owner |
|---|---|---:|---|---|---|---|---|
| Serial linear algebra baseline | Eigen | 5.0.1 reference | `eigen3` | Mandatory — production initially | MPL-2.0 | Use behind `SWE-FVM-LIN` abstractions. Do not expose Eigen types across public solver/application boundaries. Reassessment required before HPC scaling. | `SWE-FVM-LIN` |
| Unit/benchmark framework | Catch2 | 3.15.0 reference | `catch2` | Mandatory — development | BSL-1.0 | Selected over GoogleTest for the initial single-repository scientific test suite. Use CTest integration. | `SWE-VER-UNIT`, `SWE-VER-BMK` |
| Shared-memory parallelism | OpenMP | Compiler-provided implementation | Toolchain feature | Optional retained | Runtime/toolchain-dependent | Disabled by default until serial correctness and profiling are complete. | `SWE-HPC-OMP` |
| Distributed-memory parallelism | MPI | Implementation selected later | `openmpi`/platform-specific | Deferred | Implementation-dependent | No MPI dependency may enter active solver interfaces before domain decomposition is designed. | `SWE-HPC-MPI` |
| GPU acceleration | CUDA or alternative backend | Not selected | Not selected | Deferred | Vendor/toolchain-dependent | Introduce only after profiling and backend-neutral kernels/interfaces exist. | `SWE-HPC-GPU` |
| Sanitizers | ASan, UBSan; TSan where supported | Compiler-provided | Toolchain feature | Mandatory — development | Toolchain-dependent | Dedicated non-release presets and CI jobs. | `SWE-VER-UNIT`, `SWE-REL-CI` |
| Static analysis | clang-tidy | Toolchain-matched | System tool | Mandatory — development | Apache-2.0 with LLVM exceptions | Warning baseline and selected checks shall be versioned. | `SWE-VER-UNIT`, `SWE-REL-CI` |

---

## 7. Visualisation and analysis allocation

| Capability | Selected component | Baseline/reference | Acquisition | Classification | Licence | Decision and constraints | WBS owner |
|---|---|---:|---|---|---|---|---|
| Full scientific visualisation | ParaView | 6.1.1 stable reference | External installation | Mandatory — development | BSD-3-Clause | Primary system for large unstructured meshes, 3D fields, VOF data, vector fields and temporal results. | `SWE-GUI-VIS`, `SWE-VER-VAL` |
| GIS inspection/preprocessing | QGIS | 4.2.0 current reference; workflows must remain portable | External installation | Mandatory — development | GPL-2.0-or-later | Used for corridor construction, source inspection, clipping validation and cartographic checks. Not embedded. | `SWE-GEO-COR`, `SWE-GEO-CHK` |
| C++ diagnostic plotting | Matplot++ | 1.2.2 reference | vcpkg port `matplotplusplus` | Optional retained | MIT | MATLAB-style residual, gauge, force, moment, energy and convergence plots. Isolate behind a diagnostics adapter. Disable experimental OpenGL initially. | `SWE-GUI-POST`, `SWE-VER-CONV` |
| MATLAB analysis | MATLAB | R2026a reference | External licensed installation | Optional retained | Proprietary | Initial interface is HDF5 file exchange. MATLAB is not required to build or run the C++ application. | `SWE-DAT-SCH`, `SWE-VER-VAL` |
| MATLAB direct integration | MATLAB Engine/Data API for C++ | Match installed MATLAB release | MathWorks installation | Deferred | Proprietary | Add only if file-based interoperability is insufficient. Must remain an optional adapter and build feature. | Future adapter under `SWE-DAT`/`SWE-VER` |
| In-application preview | Qt Quick scene/item layer | Qt baseline | Qt | Mandatory — production | Qt module licence | Lightweight previews only. Do not recreate ParaView inside the application. | `SWE-GUI-VIS` |

### Plot ownership rule

Numerical code shall emit plotting-neutral arrays, histories and metadata. Matplot++, MATLAB and Qt visualisation code shall consume these through adapters. Solver classes shall not call plotting APIs.

---

## 8. Documentation and reproducibility tooling

| Capability | Selected component | Baseline/reference | Classification | Licence | Decision and constraints | WBS owner |
|---|---|---:|---|---|---|---|
| API documentation | Doxygen | 1.17.0 | Mandatory — development | GPL-2.0 | Generate HTML API/dependency documentation from annotated C++ headers. Doxygen is a tool, not linked code. | `SWE-REL-DOC` |
| Scientific documentation | LaTeX | TeX Live supported release | Mandatory — development | Distribution/package-dependent | Preserve the existing Research LaTeX system and mathematical notation. | Research + `SWE-REL-DOC` |
| General documentation | Markdown | GitHub-flavoured Markdown | Mandatory — development | Format | WBS, decisions, architecture, build and user guides. | `SWE-REL-DOC` |
| Preprocessing automation | Python | Python 3.12 or newer | Mandatory — development | PSF | Permitted for reproducible acquisition/preprocessing utilities where C++ implementation provides no project value. Product runtime must not silently depend on Python. | `SWE-GEO`, `SWE-DAT` |
| Performance cache | ccache or sccache | System/CI stable | Optional retained | Tool-specific | Build acceleration only; cache keys must include compiler, triplet, vcpkg baseline and feature set. | `SWE-HPC-PROF`, `SWE-REL-CI` |

---

## 9. Rejected or superseded choices

| Component/approach | Decision | Reason |
|---|---|---|
| Qt Widgets as the primary UI | Superseded | Qt Quick/QML better matches the selected application architecture and interface design. |
| ImGui as the primary application framework | Superseded | Useful for engineering tools, but not selected for the primary cross-platform application shell. |
| Matplot++ as the principal field visualiser | Rejected for that role | Not suitable as the main large-mesh/3D/VOF visualisation system; ParaView owns that role. |
| Embedded VTK/ParaView in G0/G1 | Deferred/rejected for baseline | Excessive integration and packaging burden for capabilities already provided by external ParaView. |
| Direct Gmsh library linkage | Deferred | External invocation reduces licence coupling and keeps mesh generation replaceable. |
| HDF5 2.x as the initial baseline | Deferred | Requires explicit ParaView, MATLAB, schema and vcpkg compatibility verification before migration. |
| RapidXML for new XDMF code | Superseded | Pugixml provides a clearer maintained XML interface. Existing unrelated RapidXML code need not be changed automatically. |
| GLFW as an explicit application dependency | Rejected | Qt owns the windowing and graphics context. Transitive use by optional packages does not make GLFW a project API. |
| Direct FreeType dependency | Rejected | Font handling is owned by Qt/plotting backends; no current project-owned font-rendering requirement exists. |
| Direct OpenGL dependency in the core | Rejected | Graphics backends belong to Qt or optional visualisation adapters. |
| Conan as a second package manager | Rejected | A single vcpkg manifest avoids duplicate dependency-resolution systems. |
| GoogleTest as the initial test framework | Not selected | Catch2 is selected. Reconsider only if an external component or organisational standard requires GoogleTest. |
| OpenFOAM as the production solver | Rejected for production | May remain a verification/reference tool, while the project implements the selected custom solver architecture. |

---

## 10. Version and update policy

### 10.1 Repository authority

The authoritative dependency resolution shall be:

1. exact vcpkg `builtin-baseline` commit;
2. `vcpkg.json` dependency and feature declarations;
3. explicit overrides only when documented;
4. Qt installation version recorded in the build provenance;
5. external-tool versions recorded in each generated case/reproducibility bundle.

### 10.2 Update classes

| Update type | Default action |
|---|---|
| Patch release within selected line | Accept after CI, schema round-trip and clean-clone tests. |
| Minor release | Review API, ABI, licence, transitive dependencies and output compatibility. |
| Major release | Treat as an architectural decision requiring a new decision-register revision. |
| Security update | Prioritise, but retain verification and reproducibility evidence. |
| vcpkg baseline update | Perform on a dedicated branch with dependency diff, licence diff and full CI. |

### 10.3 Feature policy

Optional package features must be disabled unless an active Deliverable owns them. This applies particularly to:

- Matplot++ experimental OpenGL features;
- HDF5 parallel/asynchronous capabilities;
- GDAL drivers and optional database/cloud integrations;
- Qt modules beyond the accepted UI set;
- MPI, CUDA and other HPC backends.

---

## 11. Licence controls

1. Qt open-source deployment shall use dynamically linked LGPL-compatible modules unless a commercial licence is selected.
2. GPL-only Qt modules require explicit approval before addition.
3. Gmsh shall remain external for the baseline; distribution and integration implications must be reviewed before packaging.
4. Every vcpkg dependency and transitive dependency must be copied into the release licence register from its installed copyright metadata.
5. External tools such as QGIS, ParaView, Doxygen and MATLAB are not redistributed by default.
6. Dataset licences are governed separately by `SWE-DAT-MAN` and `RES-DAT`; software-package licensing does not replace data licensing.

---

## 12. Required smoke tests before exact version acceptance

| Test | Acceptance condition |
|---|---|
| Qt/CMake build | Qt Quick shell configures and builds through project presets on Linux and Windows. |
| vcpkg restore | Clean clone resolves the exact pinned baseline without manual port installation. |
| HDF5 round trip | Mesh, tags, fields and provenance write/read without loss. |
| ParaView interoperability | ParaView opens the XDMF/HDF5 fixture with correct topology and fields. |
| MATLAB interoperability | MATLAB reads the selected HDF5 schema without custom binary conversion. |
| GDAL/PROJ | Representative raster/vector fixtures retain CRS, extent, nodata and transformation metadata. |
| Gmsh | MSH 4.1 physical groups import into the shared FVM topology deterministically. |
| Matplot++ | Optional build produces saved diagnostic plots in headless CI and does not introduce Qt/core coupling. |
| Licence generation | The build produces a complete direct/transitive dependency licence inventory. |

---

## 13. WBS disposition

### `SWE-ENV-STACK-WP1-T1`

**Status:** Complete.

Required capabilities have been mapped to mandatory, optional, deferred and rejected components.

### `SWE-ENV-STACK-WP1-T2`

**Status:** Complete through this register.

Selected components, reference versions, acquisition methods, licences, constraints, alternatives and owning Deliverables have been recorded.

### `SWE-ENV-STACK-WP1-T3`

**Status:** Ready for acceptance.

Acceptance requires:

1. approval of this architectural stack;
2. commitment of this register to the repository;
3. creation of the dependency manifest under `SWE-ENV-DEP-WP1`;
4. successful G0 clean-clone smoke testing before exact package versions are promoted from provisional to verified.

---

## 14. Current upstream reference evidence

The following current references were checked on 2026-07-19:

- Qt 6.11.1 is the current Qt 6.11 patch release; Qt 6.11 is under standard support.
- CMake 4.4.0 is the current stable upstream release.
- vcpkg `2026.05.25` is the latest published registry release identified during this review.
- GDAL 3.13.1 and PROJ 9.8.1 are current stable releases.
- Gmsh 4.15.2 is the current stable release; Gmsh 5.0 is development-only.
- HDF5 2.1.1 is current, while 1.14.6 is the selected compatibility baseline.
- ParaView 6.1.1 is the current stable release; ParaView 6.2 was still scheduled for 2026-07-31.
- QGIS 4.2.0 is the current release.
- Doxygen 1.17.0 is the current release.
- MATLAB R2026a is the current MATLAB release line.
- Catch2 3.15.0, Eigen 5.0.1, spdlog 1.17.0, fmt 12.1.0, nlohmann/json 3.12.0, yaml-cpp 0.8.0 and Matplot++ 1.2.2 are the reference versions used by this register.

These are evidence references, not substitutes for the repository's eventual pinned dependency graph.
