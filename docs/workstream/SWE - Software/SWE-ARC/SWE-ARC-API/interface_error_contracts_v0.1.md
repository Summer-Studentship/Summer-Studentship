# Interface Error Contracts v0.1

- **Work Package:** `SWE-ARC-API-WP1`

Expected operational failures use `tsunami::core::Result<T>` or `Result<void>`. Programming-invariant violations are distinct from user/data failures and may be handled by implementation-specific assertions before crossing a public boundary.

Public interfaces do not terminate the process for recoverable failures and do not log automatically. Errors contain stable codes, human messages and contextual key/value records. Cancellation is distinguishable from solver failure by stable error code or completion outcome.

Partial writes are aborted or remain unaccepted. A failed operation must document whether observable state changed. Implementations translate exceptions before they cross public target boundaries. Detailed severity, structured logging and failure propagation remain with `SWE-ARC-ERR-WP1`.
