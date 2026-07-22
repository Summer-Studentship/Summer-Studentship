# Case State Machine v0.1

- **Work Package:** `SWE-ARC-CASE-WP1`
- **Policy:** `architecture/case_lifecycle_policy_v0.1.json`

## States

The case lifecycle starts at `created` and reaches user-editable work through `draft`. Validation moves through `validating` to either `invalid` or `validated`. Execution preparation moves through `preparing` to either `preparation_failed` or `prepared`.

`archiving` records archive evidence and returns to the previous stable state if packaging fails. `archived` is the only terminal case state and has no outgoing edit, validation, preparation or run transition.

## Acceptance Rule

A case may be edited only in mutable states that create or preserve explicit revision history. A prepared case may start runs through later orchestration work, but this policy does not implement that service.
