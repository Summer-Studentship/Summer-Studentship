# Service Lifecycle Contract v0.1

- **Work Package:** `SWE-ARC-SVC-WP1`
- **Case policy:** `architecture/case_lifecycle_policy_v0.1.json`

| Operation | Permitted source state | Expected effect |
|---|---|---|
| validate | `draft`, `invalid` | enters or requests `validating` |
| prepare | `validated`, eligible `preparation_failed` | enters or requests `preparing` |
| launch | case `prepared` | creates a new run and queues it |
| cancel | run `created`, `queued`, `running`, `cancelling` | requests cancellation |
| discover results | any readable non-corrupt case/run state | no lifecycle mutation |

The service cannot launch from an unvalidated case or without accepted preparation. Archived cases reject validation, preparation and launch. Completed, failed and cancelled runs are immutable. Restart creates a new run identity.

The no-solver stub does not perform real state transitions. Every unsupported state-changing operation reports that no state changed.
