# Repository Issue Reconciliation 2026-08-09

Scope: R11 audit of priority Software WBS issues #150-153, #166-173 and
#226-229, plus protected parent issues #81, #82 and #96.

Remote repository: `Summer-Studentship/Summer-Studentship`

Default branch at audit time: `main`

Important constraint: the Lane B evidence in this branch is local and
unmerged. Remote issue closure is therefore limited to issues satisfied by
already reachable remote evidence. No remote issues were closed in this audit.

| Issue | WBS | Classification | Acceptance evidence | Action | Remaining blocker |
| --- | --- | --- | --- | --- | --- |
| #151 | `SWE-VER-UNIT-WP1-T1` | `UPDATE_ONLY` | C++ Catch2/CTest infrastructure exists in repository history, but acceptance requires committed, reviewed and linked task output. | Do not close. | Confirm exact PR/review linkage on reachable remote history. |
| #152 | `SWE-VER-UNIT-WP1-T2` | `UPDATE_ONLY` | Representative core/configuration/adapter smoke tests exist locally and in broader history. | Do not close. | Confirm exact remote acceptance linkage. |
| #153 | `SWE-VER-UNIT-WP1-T3` | `UPDATE_ONLY` | CTest registration and presets exist, and focused CTest sweeps pass locally. | Do not close. | Confirm exact remote acceptance linkage. |
| #150 | `SWE-VER-UNIT-WP1` | `STILL_OPEN` | Parent acceptance requires CTest through shared presets and child completion. | Do not close. | Children #151-153 remain open. |
| #166 | `SWE-DAT-SCH-WP1` | `READY_TO_CLOSE_AFTER_MERGE` | Local commit `adb0485` adds `tsunami.regional2d.result` HDF5 schema, writer/reader, synthetic fixture and round-trip tests; local follow-up fixes legacy conversion. | Do not close. | Evidence is not merged/remotely reachable. |
| #167 | `SWE-DAT-SCH-WP1-T1` | `READY_TO_CLOSE_AFTER_MERGE` | Local schema docs define mesh, field, metadata, coupling, diagnostics and provenance groups. | Do not close. | Evidence is local branch only. |
| #168 | `SWE-DAT-SCH-WP1-T2` | `READY_TO_CLOSE_AFTER_MERGE` | Local writer/reader adapters round-trip HDF5 fixture values; R10 h400 legacy CSV converts to HDF5 externally. | Do not close. | Evidence is local branch only. |
| #169 | `SWE-DAT-SCH-WP1-T3` | `READY_TO_CLOSE_AFTER_MERGE` | Local validation rejects unsupported major versions and missing required datasets. | Do not close. | Evidence is local branch only. |
| #170 | `SWE-DAT-XDMF-WP1` | `READY_TO_CLOSE_AFTER_MERGE` | Local commit `7ffe6d1` adds XDMF generation/validation; external R10 h400 converted XDMF opened with `pvpython`, showing 7598 points, 14536 cells and `h,qx,qy`. | Do not close. | Evidence is local/unmerged. |
| #171 | `SWE-DAT-XDMF-WP1-T1` | `READY_TO_CLOSE_AFTER_MERGE` | XDMF maps HDF5 topology, geometry and cell-centred field datasets. | Do not close. | Evidence is local branch only. |
| #172 | `SWE-DAT-XDMF-WP1-T2` | `READY_TO_CLOSE_AFTER_MERGE` | XDMF temporal collection is generated from `/time/values`. | Do not close. | Evidence is local branch only. |
| #173 | `SWE-DAT-XDMF-WP1-T3` | `READY_TO_CLOSE_AFTER_MERGE` | Structural validator checks XML, paths, shapes, temporal grid count and field attributes. | Do not close. | Evidence is local branch only. |
| #227 | `SWE-GUI-VIS-WP1-T1` | `READY_TO_CLOSE_AFTER_MERGE` | Local result-system workflow and external R10 converted HDF5/XDMF validation record catalogue a generated result. | Do not close. | Evidence is local/unmerged; no application catalogue yet. |
| #228 | `SWE-GUI-VIS-WP1-T2` | `STILL_OPEN` | No Qt shell metadata/mesh summary was implemented. | Do not close. | Requires GUI workflow work, outside R11 PoC. |
| #229 | `SWE-GUI-VIS-WP1-T3` | `READY_TO_CLOSE_AFTER_MERGE` | Local XDMF handoff opens in `pvpython`; docs define controlled handoff route. | Do not close. | Evidence is local/unmerged and parent GUI workflow remains incomplete. |
| #226 | `SWE-GUI-VIS-WP1` | `STILL_OPEN` | Parent acceptance requires user to locate and open the result from the application workflow. | Do not close. | #228 remains open; Qt workflow not implemented. |
| #81 | `SWE-VER-CONV` | `STILL_OPEN` | C1A convergence evidence exists, but temporal convergence remains gated/unresolved. | Do not close. | R11 h300 and possible temporal gate still pending. |
| #82 | `SWE-VER-VAL` | `STILL_OPEN` | No formal observational validation or calibration has been performed. | Do not close. | Validation scope not reached. |
| #96 | `SWE-REL-DOC` | `STILL_OPEN` | README and result docs improved locally. | Do not close. | Documentation umbrella requires authorised child completion, not just README refresh. |

## Remote Actions

- Issues closed: none.
- Evidence comments posted: none.

Reason: no audited issue satisfied all closure rules using already reachable
remote evidence. HDF5, XDMF and visualisation evidence is local to
`feat/results-storage-visualisation` and should be reconsidered after merge.
