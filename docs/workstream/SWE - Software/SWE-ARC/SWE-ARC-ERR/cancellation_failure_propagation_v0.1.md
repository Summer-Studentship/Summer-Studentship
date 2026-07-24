# Cancellation and Failure Propagation v0.1

Cancellation remains cooperative. `stop_requested()` means cancellation has been requested; it does not prove execution has stopped.

The semantic stages are `requested`, `acknowledged` and `completed`. Operations check cancellation before dispatch or irreversible accepted commits. A pre-cancelled operation returns a cancellation diagnostic with category `cancellation`, normally severity `info` or `warning`, and context keys `operation`, `state_changed` and `cancellation_stage`.

Cancellation must not be rewritten into numerical, validation or solver failure. It never reopens a terminal run, and restart still creates a new run. Partial artefact disposition is operation-specific and deferred to the owner of that operation.

The representative no-solver path returns `application.service.cancelled` before envelope validation when the token is already requested, preserving cancellation semantics ahead of validation semantics.
