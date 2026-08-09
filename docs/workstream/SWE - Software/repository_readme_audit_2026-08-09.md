# Repository README Audit 2026-08-09

Scope: root README and meaningful subordinate README files present in the
repository during R11 Lane B.

| Path | Current claim | Stale? | Required correction | Edit? |
| --- | --- | --- | --- | --- |
| `README.md` | Project entering G0 baseline; limited current numerical/storage status. | Yes | Refresh to current Tohoku/Kamaishi, Regional2D, R9/R10, h300 gate, result-storage and visualisation status. | Yes |
| `apps/TsunamiGUI/README.md` | Historical Qt Widgets prototype, not production shell. | No | None. | No |
| `data/README.md` | Bulk data ignored; prefer HDF5 plus JSON metadata. | No | Root README now links current result workflow. | No |
| `data/source/earthquake/README.md` | Manual USGS finite-fault acquisition. | No | None. | No |
| `docs/workstream/README.md` | Research workstream LaTeX conventions. | Mostly | "No Git commit" wording is historical for the initial research scaffold, but not directly in the active result-storage scope. | No |
| `docs/workstream/RES - Research/README.md` | Research progress, including R10 approaching spatial qualification. | No | None. | No |
| `docs/workstream/SWE - Software/README.md` | Software WBS and baseline document index. | Partly | Add result-system, HDF5 and XDMF documents. | Yes |
| `docs/workstream/SWE - Software/SWE-DAT/README.md` | Data-domain index. | Partly | Add result layout, HDF5 schema, XDMF handoff and workflow links. | Yes |
| `tests/fixtures/cases/README.md` | Synthetic case fixtures only. | No | None. | No |
| `tests/fixtures/geospatial/illustrative/README.md` | Generated GDAL fixtures, no binary fixture commits. | No | None. | No |
| `tests/historical/README.md` | Historical prior-model sandbox, not active CTest graph. | No | None. | No |

Outcome: only stale or newly incomplete indexes were edited. No protected
abstract, poster, LaTeX build, research-paper or primary numerical-lane files
were modified in this worktree.
