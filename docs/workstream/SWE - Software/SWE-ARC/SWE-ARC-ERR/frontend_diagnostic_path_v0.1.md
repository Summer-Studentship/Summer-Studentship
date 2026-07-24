# Frontend Diagnostic Path v0.1

Core owns semantic diagnostic values: code, message, category, severity, context and operation status. The application service owns operation context and propagation through returned `Result` and observer `OperationStatus`.

The CLI owns terminal formatting and exit-code policy. `--diagnostic-smoke` creates the shared no-solver service, sends an invalid validation request and returns zero only when returned error and observer status preserve equivalent semantics.

The GUI owns standard-to-Qt conversion. `--diagnostic-smoke --smoke-startup` exposes `diagnosticStatusModel` to QML and verifies the required QML-visible properties during offscreen startup. QML displays the code and message but does not determine severity or category.

Frontends do not rewrite stable codes. Future localisation may alter displayed message text, but code and context remain authoritative. Truncation or visual grouping must not alter the underlying diagnostic record.
