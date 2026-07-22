# Case Lifecycle Audit v0.1

- **Work Package:** `SWE-ARC-CASE-WP1`
- **Issues:** `#130`, `#131`, `#132`, `#133`
- **Policy:** `architecture/case_lifecycle_policy_v0.1.json`
- **Document date:** 2026-07-22

## Existing Evidence

The pre-WBS repository contained application prototypes, target scaffolding and research documentation, but no authoritative simulation-case lifecycle policy. No existing C++ class, GUI workflow or data file is treated as closing this work package.

## Accepted Boundary

`SWE-ARC-CASE-WP1` defines the architectural contract for how a simulation case moves from creation to validation, execution readiness, run evidence and archive. It also defines where artefacts belong and which downstream WBS owner must define their detailed contents.

## Non-Goals

This audit does not define the final `case.yaml` or `case.json` scientific schema, parser implementation, C++ interfaces, HDF5/XDMF layouts, checkpoint payloads, solver execution, GUI screens or CI jobs.

## Result

The work package now has a deterministic case directory, explicit case and run state machines, schema-version rules, provenance categories and restart-compatibility classes. These are validated by `tools/architecture/validate_case_lifecycle.py`.
