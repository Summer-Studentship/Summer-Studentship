# R17 Proposed Repository Structure

This is a recommendation document, not an executed clean-up. No files were
deleted, moved, renamed, untracked or ignored in R17.

## Goals For A Future Rationalisation Branch

- Preserve all scientific authority and provenance.
- Separate live code from frozen source snapshots.
- Keep deliverables discoverable without duplicating large previews unless
  there is a presentation reason.
- Keep protected research inputs intact unless bibliography review approves a
  different storage policy.
- Make generated outputs reproducible from scripts and manifests.

## Suggested Future Shape

```text
apps/
src/
tests/
tools/
schemas/
cases/
data/
docs/
  project/
  workstream/
  research-library/
  historical/
deliverables/
  figures/
    publication/
    evidence/
    generated-sources/
  report/
  poster/
  video/
external-data-manifests/
```

## Candidate Moves For Human Review

| Candidate | Possible destination | R17 action |
| --- | --- | --- |
| `docs/UI Inspiration.png` | `docs/historical/gui/` or an app-specific design archive | None |
| R16 source snapshot script | Keep as snapshot or replace with provenance link to `tools/figures/r16_publication.py` | None |
| R16 preview/publication duplicate PNGs | Keep both, generate previews on demand, or document preview-as-copy policy | None |
| Research PDFs duplicated across folders | Bibliographic source library with workstream citation links | None |
| `.aux.xml` sidecars | Ignore future transient sidecars only if provenance remains sufficient | None |
| Paths with newline/non-ASCII characters | Bibliographic path hygiene pass | None |

## Required Review Gates

1. Scientific owner confirms no evidence chain depends on a candidate path.
2. Bibliography/citation owner confirms research-source references are intact.
3. Reproducibility owner confirms regenerated deliverables match registered
   hashes or intentionally supersede them.
4. Git owner performs any movement in a separate branch with explicit approval.

Until those gates exist, the repository should remain as delivered by R17.
