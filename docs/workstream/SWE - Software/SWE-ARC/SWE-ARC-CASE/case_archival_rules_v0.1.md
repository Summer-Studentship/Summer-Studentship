# Case Archival Rules v0.1

- **Work Package:** `SWE-ARC-CASE-WP1`
- **Policy:** `architecture/case_lifecycle_policy_v0.1.json`

## Archive Transition

A case enters `archiving` only when an archive profile has been selected. If packaging succeeds, the case reaches `archived`. If packaging fails, the case returns to its previous stable non-archived state.

## Archived State

`archived` is terminal and immutable. It has no outgoing edit, validation, preparation, run or archive transition. External storage systems may move or retain packages, but that metadata is outside the simulation-case lifecycle.

## Profiles

`lightweight` retains identity, configuration, manifests, validation reports and run summaries. `reproducible` retains prepared snapshots, logs, outputs and checkpoint compatibility records. `full_evidence` retains all reproducible artefacts plus retained checkpoints, derived reports and archive packaging evidence.
