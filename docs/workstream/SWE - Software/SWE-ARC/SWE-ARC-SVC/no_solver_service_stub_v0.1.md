# No-Solver Service Stub v0.1

- **Work Package:** `SWE-ARC-SVC-WP1`

The no-solver implementation identifies itself as `no-solver`, version `0.1.0`. It reports the shared service boundary as available and every scientific operation capability as unavailable.

Unsupported validation, preparation, launch and cancellation return `Result` failure with stable provisional service codes. Launch does not create or reuse a run ID. Result discovery returns a successful empty no-backend response and does not fabricate artefacts.

If cancellation is already requested before dispatch, operations return `application.service.cancelled`. The stub emits at most one neutral observer event for a no-solver rejection and does not log, print, create files or mutate case/run state.
