# Diagnostic Model Audit v0.1

## Existing Contracts

`Error` already provided a stable code, message, context vector, optional cause code and validity check. `Result<T>` and `Result<void>` used that same `Error` for expected failures. `ICancellationToken::stop_requested()` exposed cooperative cancellation intent only. `IObserver` carried a separate `Observation` record with topic code, message, optional progress and context. The no-solver service returned `application.service.operation_unavailable`, `application.service.solver_unavailable` and `application.service.cancelled`.

## Frontend Baseline

The CLI owned `--help`, `--version` and `--service-status` formatting. The GUI created the same service factory product, converted service status into `QVariantMap` and exposed it to QML as `serviceStatus`.

## Gaps

The baseline did not define severity, diagnostic category, operation status, stable context replacement, cancellation category, or logging-record rules. Returned errors and observer events used different shapes, so context could drift across the core to application to frontend path.

## Compatibility

The accepted `Error` type remains the sole error family. Its two-string constructor remains source-compatible and defaults to `internal`/`error`. `Observation` remains as an alias to the canonical `OperationStatus`, so observers move to a shared diagnostic payload without introducing Qt, CLI11, spdlog, fmt or third-party expected types.

## Handoffs

GUI shell work can build richer presentation over the QML-visible status map. `SWE-DAT-CFG-WP1` owns actual schema validation. Solver and verification work inherit category/severity/context rules without adding frontend formatting to domain targets.
