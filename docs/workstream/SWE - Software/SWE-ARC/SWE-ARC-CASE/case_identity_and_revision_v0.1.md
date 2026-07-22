# Case Identity and Revision v0.1

- **Work Package:** `SWE-ARC-CASE-WP1`
- **Policy:** `architecture/case_lifecycle_policy_v0.1.json`

## Identity Fields

Every case identity record requires `case_id`, `case_slug`, `created_at_utc`, `created_by`, `schema_version` and `policy_version`.

## Revision Rules

Every material input edit creates a new monotonically increasing case revision. Validated and prepared artefacts bind to the exact revision that produced them. A run binds to one case revision at creation and never changes revision afterward.

Archive identity records the final case revision and all retained run identities. Runtime services may refer to case and run identities, but later API work owns the concrete C++ type names and object contracts.
