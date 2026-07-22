# Run State Machine v0.1

- **Work Package:** `SWE-ARC-CASE-WP1`
- **Policy:** `architecture/case_lifecycle_policy_v0.1.json`

## States

A run starts at `created`, is queued through `queued`, executes in `running`, may drain through `cancelling`, and can finish as `cancelled`, `failed`, `completed`, `postprocess_failed` or `postprocessed` depending on execution and postprocessing evidence.

`cancelled`, `failed` and `postprocessed` are terminal run states. `completed` is immutable for primary outputs but may still enter `postprocessing`.

## Restart Rule

Restart never reopens a terminal or completed source run. A restart creates a new run identity under `runs/<new-run-id>/` and records the source run in `runs/<new-run-id>/manifests/restart_from.json`.
