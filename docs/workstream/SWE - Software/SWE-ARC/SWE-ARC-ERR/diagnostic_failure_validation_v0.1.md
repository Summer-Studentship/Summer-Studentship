# Diagnostic Failure Validation v0.1

## Policy Evidence

Policy versions: diagnostic `0.1`, service `0.1`, interface `0.1`, case `0.1`, target `0.1`, layer `0.1`.

Public diagnostic headers: `Diagnostic.hpp`, `Status.hpp`, `Error.hpp`, `Observer.hpp`, `Result.hpp`, `Cancellation.hpp`.

Severity count: 6. Category count: 13. Operation-state count: 6.

Representative code: `application.validation.case_location_required`.

Preserved context keys: `operation`, `rule_id`, `case_location`, `case_revision`, `state_changed`.

Observer/result equivalence: covered by Catch2 application tests and CLI/Qt diagnostic smoke.

Cancellation result: pre-cancelled validation and launch return category `cancellation` with `cancellation_stage=requested`.

Logging-rule review: structured record and sink rules are documented; concrete spdlog integration is deferred.

## Command Evidence

GCC workflow: passed, 15/15 tests.

Clang workflow: passed, 15/15 tests.

CLI service smoke:

```text
service_backend=no-solver
service_boundary=available
solver_available=false
validation_available=false
preparation_available=false
launch_available=false
cancellation_available=false
result_discovery_available=false
```

CLI diagnostic smoke:

```text
diagnostic_operation=validate_case
diagnostic_state=failed
diagnostic_severity=error
diagnostic_category=validation
diagnostic_code=application.validation.case_location_required
diagnostic_message=case location is required
diagnostic_context.operation=validate_case
diagnostic_context.rule_id=case.location.required
diagnostic_context.case_location=<empty>
diagnostic_context.case_revision=0
diagnostic_context.state_changed=false
diagnostic_context_preserved=true
```

Qt offscreen smoke: `QT_QPA_PLATFORM=offscreen tsunami_gui --diagnostic-smoke --smoke-startup` passed with exit code 0.

Architecture validators: JSON parsing, Python compilation, target graph, layer ownership, case lifecycle, interface contracts, application service and diagnostic failure validators passed.

Invalid-fixture rejection: duplicate severity, unknown category, invalid error-code format, missing `rule_id`, missing `state_changed`, Qt type in diagnostic header, application error without validation category, observer context mismatch, CLI missing severity, GUI missing code, cancellation as numerical failure and logging sink as lifecycle owner were rejected.

Target graph: `linux-gcc-dev`, `linux-gcc-test` and `linux-gcc-gui-dev` Graphviz outputs passed. `tsunami_application` still depends only on `tsunami_core` and `tsunami_data`; CLI and GUI depend on `tsunami_application`; graphs remain acyclic.

Research stash: `stash@{0}: On main: wip: preserve unrelated research HTML deletions before SWE-ARC-SVC-WP1`.

## Handoffs

`SWE-GUI-SHL-WP1` can replace the minimal QML smoke text with final diagnostic presentation. `SWE-DAT-CFG-WP1` owns scientific/user case-schema rules. Solver, coupling and verification WPs inherit the diagnostic taxonomy and cancellation distinctions.

Unresolved decisions: concrete logging sink, user/internal context split, localisation policy and diagnostic retention policy.
