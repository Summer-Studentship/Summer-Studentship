# Case Lifecycle Validation v0.1

- **Work Package:** `SWE-ARC-CASE-WP1`
- **Policy:** `architecture/case_lifecycle_policy_v0.1.json`
- **Validator:** `tools/architecture/validate_case_lifecycle.py`
- **Document date:** 2026-07-22

## Validator Commands

Environment:

| Tool | Version |
|---|---|
| Python | `3.14.5` |

```sh
python3 -m json.tool architecture/case_lifecycle_policy_v0.1.json
python3 -m py_compile tools/architecture/validate_case_lifecycle.py
python3 tools/architecture/validate_case_lifecycle.py \
  --policy architecture/case_lifecycle_policy_v0.1.json \
  --targets architecture/target_dependency_policy_v0.1.json \
  --layers architecture/layer_ownership_policy_v0.1.json
```

## Checks

The validator checks JSON shape, referenced target/layer policy versions, unique case and run state names, known transition endpoints, reachability from initial states, reachable terminal states, archived immutability, run restart identity rules, canonical path uniqueness, artefact ownership, mutability classes, schema-major rejection behaviour, mandatory provenance categories and restart-compatibility classes.

## Invalid Fixture Coverage

Temporary fixtures are used during validation and are not committed. The required rejection cases are unreachable state, archived-to-draft transition, completed-to-running transition, path without owner, restart that reuses the original run identity and missing schema-major rejection rule.

| Fixture | Injected fault | Result |
|---|---|---|
| `/tmp/case-lifecycle-invalid/unreachable-state.json` | Added unreachable `orphan` case state | `ERROR: case: unreachable state(s): ['orphan']` |
| `/tmp/case-lifecycle-invalid/archived-to-draft.json` | Added `archived -> draft` transition | `ERROR: case states: archived must not have outgoing transitions` |
| `/tmp/case-lifecycle-invalid/completed-to-running.json` | Added `completed -> running` transition | `ERROR: run states: completed must not return to running` |
| `/tmp/case-lifecycle-invalid/path-without-owner.json` | Removed owner from `case.json` path | `ERROR: canonical_paths: case.json lacks owner` |
| `/tmp/case-lifecycle-invalid/restart-reuses-run.json` | Restart reopens parent run | `ERROR: run restart: restart must create a new run` |
| `/tmp/case-lifecycle-invalid/missing-schema-major-rejection.json` | Removed unknown-major rejection rule | `ERROR: schema rules: case_configuration_envelope missing unknown_major version behaviour` |

## Handoff

The validator confirms the architectural lifecycle only. Runtime state persistence, C++ interfaces, parser behaviour, scientific schemas, checkpoint compatibility payloads and orchestration side effects remain with the downstream WBS owners listed in the policy.
