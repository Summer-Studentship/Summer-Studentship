# Schema and Compatibility Rules v0.1

- **Work Package:** `SWE-ARC-CASE-WP1`
- **Policy:** `architecture/case_lifecycle_policy_v0.1.json`

## Schema Versioning

Schema versions use `major.minor.patch`. Unknown major versions are rejected. Minor versions may be read forward only when the owning schema class declares compatible extension behaviour. Patch versions are treated as bugfix-equivalent unless the owning schema says otherwise.

## Ownership Handoff

`SWE-DAT-CFG` owns the actual user configuration schema. `SWE-DAT-MAN` owns dataset and provenance manifest content. `SWE-DAT-SCH` and `SWE-DAT-CHK` own persistent scientific output and checkpoint schemas.

`SWE-ARC-API-WP1` owns concrete C++ contracts. `SWE-ARC-SVC-WP1` owns orchestration semantics that execute the lifecycle transitions.

## Restart Compatibility

Restart compatibility is classified as `exact_resume`, `compatible_resume`, `migration_required`, `replay_only` or `incompatible`. A restart creates a new run identity and never mutates the source run.
