# Current Target Audit v0.1

- **Work Package:** `SWE-ARC-TGT-WP1`
- **Issues:** `#122`, `#123`, `#124`, `#125`
- **Policy:** `architecture/target_dependency_policy_v0.1.json`
- **Document date:** 2026-07-22

## Scope

This audit records the active G0 CMake target graph as implemented before any
planned architecture-layer targets exist. It confirms actual links from the
current `CMakeLists.txt` files rather than treating the planned catalogue as
implemented.

## Active Targets

| Target | Type | Source directory | Public include surface | Project dependencies | External dependencies | Compile-feature ownership | Build condition | Current role | Current limitations | Planned long-term role |
|---|---|---|---|---|---|---|---|---|---|---|
| `tsunami_core` | Static library | `src` | `src` via `BUILD_INTERFACE`; install placeholder `include` | None | None | `PUBLIC cxx_std_20` | Always configured | Foundation scaffold for common constants, errors, results, types and filesystem utility code | Not yet split into final layer directories; does not yet own complete status, cancellation or diagnostic contracts | Qt-free common foundation used by all production libraries |
| `tsunami_cli` | Executable | `apps/tsunami_cli` | None | `tsunami_core` | `CLI11::CLI11` | `PRIVATE cxx_std_20` | `TSUNAMI_BUILD_CLI=ON` | Minimal CLI scaffold with help/version path | Direct core link is transitional until `tsunami_application` exists | CLI frontend depending on `tsunami_application` and owning CLI11 |
| `tsunami_gui` | Executable | `apps/tsunami_gui` | None | `tsunami_core` | `Qt6::Quick` plus Qt-generated runtime/tool targets | `PRIVATE cxx_std_20` | `TSUNAMI_BUILD_GUI=ON` | Minimal Qt Quick/QML startup shell | Direct core link is transitional until `tsunami_application` exists; no case navigation or editing | GUI frontend depending on `tsunami_application` and owning Qt Quick/QML |
| `tsunami_tests` | Executable | `tests` | None | `tsunami_core` | `Catch2::Catch2WithMain` | `PRIVATE cxx_std_20` | `BUILD_TESTING=ON` | Catch2 smoke-test scaffold registered with CTest | Only foundation smoke coverage exists; historical solver sandbox is not part of this target | Test/development target that may link libraries under test while production targets never depend on Catch2 |

## Active Graph

Observed direct project-target edges:

```text
tsunami_cli   -> tsunami_core
tsunami_gui   -> tsunami_core
tsunami_tests -> tsunami_core
```

Observed external ownership:

```text
tsunami_cli   -> CLI11::CLI11
tsunami_gui   -> Qt6::Core, Qt6::Qml, Qt6::Quick and Qt-generated tools
tsunami_tests -> Catch2::Catch2WithMain
```

There is no active project edge from core to a frontend, no production edge to
tests and no concrete adapter target in the active graph.

## Notes

- `Tsunami::core` is a namespace alias for the active `tsunami_core` library.
- No aliases are created for planned targets until those implementation targets
  exist.
- The historical `apps/TsunamiGUI` prototype and `tests/Reworked Prior Model`
  sandbox are not part of the active CMake graph.
