# Architecture Layer Audit v0.1

- **Work Package:** `SWE-ARC-LAY-WP1`
- **Issues:** `#126`, `#127`, `#128`, `#129`
- **Layer policy:** `architecture/layer_ownership_policy_v0.1.json`
- **Target policy:** `architecture/target_dependency_policy_v0.1.json`
- **Document date:** 2026-07-22

## Scope

This audit records current source placement against the accepted target graph.
No files are moved and no planned source directories are created by this work.

| Current source path | Current target | Current responsibility | Proposed owning layer | Target-graph compatibility | Status | Later migration | Responsible downstream WP |
|---|---|---|---|---|---|---|---|
| `src/core/*.hpp`, `src/core/File_System.cpp` | `tsunami_core` | Foundation constants, errors, result/status-style records, types and filesystem utility | L1 Foundation | Compatible: no project or external framework dependency | Active production scaffold | Keep in `src/core/`; refine namespaces and contracts later | `SWE-ARC-ERR-WP1`, `SWE-ARC-API-WP1` |
| `src/CMakeLists.txt` | `tsunami_core` | CMake definition and `Tsunami::core` alias | L1 Foundation plus L0 build metadata | Compatible with active library target | Active production scaffold | Split target files only when real targets exist | `SWE-ARC-LAY-WP1`, later implementation WPs |
| `apps/tsunami_cli/main.cpp` | `tsunami_cli` | CLI help/version scaffold | L9 Frontends | Compatible: transitional dependency on `tsunami_core`, owns CLI11 | Active scaffold | Migrate from direct core use to `tsunami_application` when services exist | `SWE-ARC-SVC-WP1` |
| `apps/tsunami_gui/main.cpp`, `apps/tsunami_gui/qml/Main.qml` | `tsunami_gui` | Qt Quick startup shell | L9 Frontends | Compatible: transitional dependency on `tsunami_core`, owns Qt | Active scaffold | Route workflows through `tsunami_application`; keep Qt in frontend | `SWE-GUI-SHL-WP1` |
| `tests/smoke/core_smoke.cpp` | `tsunami_tests` | Catch2 foundation smoke test | L10 Verification | Compatible: test depends inward on `tsunami_core` | Active scaffold | Expand into test taxonomy when owning targets exist | `SWE-VER-UNIT-WP1` |
| `tests/Reworked Prior Model/` | None | Historical finite-difference sandbox | Historical evidence outside active graph | Not part of active target policy | Historical | Mine ideas only through future accepted WPs; do not import as architecture | Future solver WPs |
| `apps/TsunamiGUI/` | None | Historical Qt Widgets prototype | Historical evidence outside active graph | Not part of active target policy | Historical | Do not migrate directly; new GUI work remains in `apps/tsunami_gui` | `SWE-GUI-SHL-WP1` |
| `cmake/`, `CMakePresets.json`, `vcpkg.json` | Build metadata | Toolchain, dependency and preset control | L0 Development and Delivery Infrastructure | Compatible: runtime targets do not depend on tooling | Active infrastructure | Continue under SWE-ENV/SWE-REL ownership | `SWE-ENV-*`, `SWE-REL-*` |
| `tools/architecture/` | None at runtime | Architecture policy validators | L0 Development and Delivery Infrastructure | Compatible: tooling inspects runtime policies only | Active infrastructure | May become CI input later without runtime dependency | `SWE-REL-CI-WP1` |

## Audit Result

The current source tree is compatible with the accepted target graph. Existing
active files map to L1, L9 or L10. Historical files remain outside the active
graph. Planned directories for FVM, data, geospatial, solver, coupling,
application and adapter layers are documented only; they are not created here.
