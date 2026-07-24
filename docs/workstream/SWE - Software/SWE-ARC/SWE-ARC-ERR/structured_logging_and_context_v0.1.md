# Structured Logging and Context v0.1

Structured logging is a neutral record and sink contract derived from `Diagnostic` and `OperationStatus`. No concrete file logger or spdlog adapter is introduced by this Work Package.

Required serialised fields are `timestamp_utc`, `sequence`, `severity`, `category`, `code`, `message`, `operation`, `operation_state`, optional `progress` and `context`.

Recommended context keys are `case_id`, `case_revision`, `preparation_id`, `run_id`, `checkpoint_id`, `rule_id`, `target`, `component`, `state_changed` and `correlation_id`.

Timestamps are supplied by the logging boundary, not by pure error construction. Sequence values are monotonic within one producer stream. Logging sinks do not own lifecycle state and logging failure must not silently alter a scientific result. Frontends may render diagnostics but do not become logging authorities. Secrets, unrestricted environment dumps, multiline binary payloads and large file contents are prohibited unless explicitly sanitised later.
