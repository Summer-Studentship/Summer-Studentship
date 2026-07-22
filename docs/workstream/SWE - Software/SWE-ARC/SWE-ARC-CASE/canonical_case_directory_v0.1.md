# Canonical Case Directory v0.1

- **Work Package:** `SWE-ARC-CASE-WP1`
- **Policy:** `architecture/case_lifecycle_policy_v0.1.json`

## Layout

```text
<case-root>/
  case.json
  README.md
  inputs/
    data/
  validation/
  manifests/
  prepared/
  runs/
    <run-id>/
      run.json
      logs/
      checkpoints/
      outputs/
      derived/
      manifests/
  archive/
```

## Determinism

The layout is deterministic: a given artefact class has one canonical location. Runtime work may choose filenames inside these directories only within the ownership and mutability rules in the policy.
