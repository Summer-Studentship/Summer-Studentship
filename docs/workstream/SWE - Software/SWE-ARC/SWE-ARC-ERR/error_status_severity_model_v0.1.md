# Error, Status and Severity Model v0.1

## Taxonomy

Severity levels are `trace`, `debug`, `info`, `warning`, `error` and `critical`. Severity describes impact, not component ownership. Ordinary invalid user input is `error`, not `critical`.

Diagnostic categories are `validation`, `configuration`, `input_data`, `preparation`, `execution`, `persistence`, `geospatial`, `numerical`, `coupling`, `cancellation`, `resource`, `unsupported` and `internal`. Categories describe the responsibility that detected the condition and do not encode severity.

Operation states are `idle`, `accepted`, `running`, `succeeded`, `failed` and `cancelled`. `OperationStatus` is a transient presentation/status record and is not the authoritative case or run lifecycle state.

## Error Contract

`tsunami::core::Error` carries code, message, category, severity, deterministic context and optional cause code. Codes follow `<domain>.<component>.<condition>`, remain lowercase/dot-separated and never contain dynamic IDs or paths. Dynamic values belong in context.

Context keys are lower snake case, unique within one diagnostic and deterministically ordered. Repeated insertion replaces the prior value. `context_value(key)` provides canonical lookup for frontends and observers.

## Result and Observer

`Result<T>` and `Result<void>` retain the complete `Error`. `IObserver` consumes the canonical `OperationStatus` payload synchronously. Error creation does not log, throw or terminate automatically.
