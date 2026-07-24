# GUI Shell Validation v0.1

Policy versions: GUI shell `0.1`, target `0.1`, layer `0.1`, interface `0.1`, application service `0.1`, diagnostic failure `0.1`.

QML module URI: `Tsunami.Studentship`. Resource count: 14 QML files. Component count: 6. Navigation count: 7. Navigation order: `data`, `domain`, `mesh`, `physics`, `solver`, `run`, `post_processing`.

Qt discovery route: system Qt through the accepted local GUI preset. Qt version: 6.11.1. CMake: 4.3.3. GCC: 16.1.1. Clang: 22.1.6. GUI target links `tsunami_application`, `Qt6::Quick` and `Qt6::QuickControls2`.

Validation results:

- GUI configure/build: passed.
- Normal offscreen startup: passed.
- Shell smoke: passed.
- Combined diagnostic shell smoke: passed.
- QML lint: `tsunami_gui_qmllint` target available and returned 0; warnings remain for context-property/delegate qualification.
- Architecture validators: target, layer, case, interface, application-service, diagnostic and GUI-shell validators passed.
- Invalid GUI-shell fixtures rejected: duplicate navigation ID, incorrect order, missing page, missing service-status component, direct GUI-to-`tsunami_r2d`, Qt token in application header, QML hard-coded solver availability, QML-generated diagnostic severity, absent navigation accessibility name and missing navigation count smoke field.
- Target graph: `linux-gcc-dev`, `linux-gcc-test` and `linux-gcc-gui-dev` Graphviz outputs passed. GUI graph shows `tsunami_gui -> tsunami_application` and private Qt imports only.
- GCC regression workflow: passed, 15/15 tests.
- Clang regression workflow: passed, 15/15 tests.
- CLI regression: `--service-status` and `--diagnostic-smoke` outputs unchanged.
- Desktop manual review: direct non-offscreen smoke launch attempted in a Wayland/X11 environment and aborted with exit 134 without stderr; interactive visual review is unavailable in this execution context. Offscreen startup, shell and diagnostic shell smokes are the acceptance evidence.

Qt isolation: core, data and application public headers remain free of Qt tokens. Research stash confirmation: `wip: preserve unrelated research HTML deletions before SWE-ARC-SVC-WP1`.

Limitations and handoffs: no case UI, run control UI, diagnostic log UI, visualisation, result post-processing, final branding, persistence or scientific workflow is implemented.
