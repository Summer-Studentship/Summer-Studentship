# Pre-WBS Repository Inventory v0.1

This inventory records the repository state observed before formal Software WBS execution. An artefact's presence is not evidence that a related WBS issue is accepted or complete.

| Path | Observed purpose | Content class | Current maturity | Probable WBS relationship | Commit group | Decision |
|---|---|---|---|---|---|---|
| `docs/Latex/` | Project notes, proposal sources, figures, bibliography and retained rendered reports | Research documentation | Historical and actively reusable | Research context; informs SWE requirements and architecture | Research documentation | Retain; ignore LaTeX intermediates |
| `docs/Research Papers/` | Curated literature and research staging notes | Research evidence library | Historical/reusable, with explicit rejected and excess subsets | Inputs to architecture, modelling and verification decisions | Research documentation | Retain |
| `docs/workstream/RES - Research/` | Structured Research WBS sources, decisions, evidence and rendered reports | Research workstream documentation | Substantial but not Software acceptance evidence | Cross-workstream inputs to SWE-ARC, SWE-VER and model scope | Research documentation | Retain; ignore LaTeX intermediates |
| `docs/Markdowns/` | Existing dependency, layout, build and workflow notes | Repository/developer documentation | Prototype guidance; may predate formal Software governance | SWE-ENV-BLD, SWE-ENV-DEP, SWE-ENV-PRS | Research/documentation baseline | Retain; reassess against G0 later |
| `docs/project-management/` | Earlier project setup, workflow and build-audit material | Administrative documentation | Historical/partial | Project governance and SWE environment evidence | Research/documentation baseline | Retain; investigate mapping later |
| `src/` | C++20 application/core scaffold and prototype entry points | Software source | Early prototype | SWE-ARC-LAY and future solver/application work | GUI/application prototype | Retain; do not treat as accepted architecture |
| `apps/TsunamiGUI/` | Qt Widgets GUI experiment | GUI prototype source | Early prototype | SWE-GUI-SHL | GUI prototype | Retain; ignore local Qt build/IDE state |
| `tests/Reworked Prior Model/` | Recreation of a prior vorticity/stream-function finite-difference model | C++ sandbox/reference implementation | Experimental historical sandbox | Potential SWE-VER reference only; not application verification | C++ sandbox and tests | Retain; classify as sandbox |
| `python/` | Minimal Python exploration entry point | Experimental source | Placeholder | Possible future tooling or modelling support; no accepted mapping | C++ sandbox and tests | Retain; investigate later |
| `matlab/` | Minimal MATLAB exploration entry point | Experimental source | Placeholder | Possible historical modelling reference; no accepted mapping | C++ sandbox and tests | Retain; investigate later |
| `data/` | Policy/readme for future curated datasets | Data scaffold | Placeholder | Future data ownership and fixtures | Build/repository tooling | Retain |
| `.github/` | Issue forms and repository contributor instructions | Repository metadata | Scaffold | SWE-ENV-PRS and governance support | Build/repository tooling | Retain |
| `scripts/` | External bootstrap and platform build helper scripts | Build automation scaffold | Prototype | SWE-ENV-BLD and SWE-ENV-DEP | Build/repository tooling | Retain; validate later |
| `README.md` | Repository landing page | Repository documentation | Historical stub plus local draft | G0 repository orientation | Repository landing page | Update truthfully for baseline |
| `CMakeLists.txt`, `src/CMakeLists.txt`, `CMakePresets.json` | CMake build definition and presets | Build configuration | Prototype scaffold | SWE-ENV-BLD | Build/repository tooling | Retain unchanged in tooling capture |
| `vcpkg.json`, `vcpkg-configuration.json` | C++ dependency declarations and registry configuration | Dependency configuration | Prototype scaffold | SWE-ENV-DEP | Build/repository tooling | Retain unchanged; formal revision deferred |
| `Dockerfile`, `.dockerignore` | Linux container build environment | Environment configuration | Prototype scaffold | SWE-ENV-BLD and SWE-ENV-PRS | Build/repository tooling | Retain unchanged in tooling capture |
| `.gitattributes`, `.gitignore`, `.vscode/extensions.json` | Repository hygiene, line-ending and editor recommendation metadata | Administrative metadata | Baseline | SWE-ENV-PRS | Repository hygiene | Retain; ignore only confirmed generated/local state |
| `build/`, `apps/TsunamiGUI/build/` | CMake/compiler output | Generated build content | Local/generated | None as committed evidence | Excluded | Ignore |
| `apps/TsunamiGUI/.qtcreator/`, local `.vscode/` state | Machine-specific IDE state | Generated/local configuration | Local/generated | None as committed evidence | Excluded | Ignore |
| LaTeX intermediates (`*.aux`, `*.bcf`, `*.blg`, `*.fdb_latexmk`, `*.fls`, `*.log`, `*.nlo`, `*.run.xml`, `*.synctex.gz`, `*.toc`, `.latex-aux/`) | Typesetting compiler state | Generated documentation output | Local/generated | None as committed evidence | Excluded | Ignore |

## Safety observations

- No file larger than 50 MB was observed outside `.git`.
- No `.env`, private-key file, GitHub token pattern or other probable credential was found by the baseline scan.
- Authored PDFs and third-party research papers are retained as intentional documentation; LaTeX compiler intermediates are excluded.
- No pre-WBS artefact is represented here as satisfying a Software WBS acceptance condition.
