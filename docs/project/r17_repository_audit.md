# R17 Repository Audit

This is a non-destructive closure audit. It classifies the repository and
records review candidates; it does not delete, move, rename, untrack, ignore or
rewrite anything.

Audit snapshot: `27fe493` on `feat/r17-studentship-closure`, after the R17
Figure C commit and before this documentation commit.

Starting authority for the worktree: R16B HEAD
`adee22b326297d9b626f90c279e5ff2ca30c4438`.

## Safety Position

The R17 rationalisation lane obeyed the safety amendment:

- no `rm`, `git rm`, `mv`, `git mv`, `git clean`, reset, stash or history
  rewrite was used for repository rationalisation;
- no protected research PDFs, LaTeX build artifacts, Copilot instructions or
  unrelated user files were modified;
- all clean-up topics are documented as review recommendations only.

## Inventory

| Metric | Value |
| --- | ---: |
| Tracked files inspected | 1,598 |
| Tracked bytes inspected | 683,417,200 |
| Git pack size reported by `git count-objects -vH` | 546.50 MiB |
| Duplicate groups >= 1 KiB | 23 |
| Duplicate tracked paths in those groups | 48 |
| Potential redundant bytes if all duplicates were human-approved for deduplication | 22,005,955 |
| Tracked `.aux.xml` sidecars | 9 |
| Tracked PDF files | 147 |
| Tracked PDF bytes | 595,357,566 |
| Tracked PNG files | 52 |
| Tracked PNG bytes | 51,013,187 |
| Tracked TIFF files | 9 |
| Tracked Blender scenes | 1 |

`git count-objects -vH` also reported one zero-byte local Git metadata garbage
entry under the worktree refs area. This is not a tracked repository file and
was not modified.

## Primary Classification Counts

These counts assign each tracked file to one primary conservative class.
Review topics are listed separately in the review register.

| Classification | Files | Bytes | Interpretation |
| --- | ---: | ---: | --- |
| `KEEP_AUTHORITATIVE` | 675 | 5,569,832 | Source code, tests, schemas, tools, build config and current repository control files. |
| `KEEP_PROVENANCE` | 642 | 224,111,113 | Workstream evidence, verification outputs, manifests and provenance records. |
| `KEEP_DELIVERABLE` | 123 | 62,451,829 | Poster/report/video/publication outputs intended to be consumed directly. |
| `KEEP_RESEARCH_INPUT` | 115 | 374,478,591 | Research PDFs, proposal material and local data-source notes. |
| `KEEP_GENERATED_REPRODUCIBLE` | 42 | 14,905,677 | Reproducible GIS/render source artifacts that should remain until an artifact policy exists. |
| `REVIEW_REQUIRED` | 1 | 1,900,158 | `docs/UI Inspiration.png`, a pre-WBS loose image with unclear current role. |

## Top-Level Map

| Path | Files | Bytes | Primary disposition |
| --- | ---: | ---: | --- |
| `.github/` | 3 | 4,605 | `KEEP_AUTHORITATIVE` |
| `.vscode/` | 1 | 136 | `KEEP_AUTHORITATIVE` |
| `apps/` | 31 | 61,760 | `KEEP_AUTHORITATIVE` |
| `architecture/` | 20 | 127,254 | `KEEP_AUTHORITATIVE` |
| `cases/` | 1 | 3,988 | `KEEP_AUTHORITATIVE` |
| `cmake/` | 1 | 1,230 | `KEEP_AUTHORITATIVE` |
| `data/` | 2 | 1,433 | `KEEP_RESEARCH_INPUT` |
| `deliverables/` | 352 | 79,046,559 | Mixed `KEEP_DELIVERABLE`, `KEEP_PROVENANCE`, `KEEP_GENERATED_REPRODUCIBLE` |
| `docs/` | 569 | 598,799,376 | Mixed `KEEP_RESEARCH_INPUT`, `KEEP_PROVENANCE`, `REVIEW_REQUIRED` |
| `schemas/` | 7 | 67,059 | `KEEP_AUTHORITATIVE` |
| `src/` | 218 | 2,011,523 | `KEEP_AUTHORITATIVE` |
| `tests/` | 308 | 1,291,565 | `KEEP_AUTHORITATIVE` |
| `tools/` | 71 | 1,969,831 | `KEEP_AUTHORITATIVE` |
| Root config and README files | 11 | 30,411 | `KEEP_AUTHORITATIVE` |

## Review Findings

The detailed register is
`docs/project/r17_repository_review_register.md` and
`docs/project/r17_repository_review_register.json`.

High-signal findings:

- R16 preview PNGs and publication PNGs contain intentional byte-identical
  duplicates for several figures. Keep for now; future policy may decide
  whether previews should be generated, linked or retained.
- Some research PDFs are duplicated between broad research folders and
  workstream-staged folders. These are research inputs and must not be removed
  without a citation/bibliography review.
- Nine `.aux.xml` GIS/render sidecars are tracked. They may be future
  `.gitignore` candidates, but current R16/R17 provenance still benefits from
  keeping them.
- One tracked research PDF path contains a newline, and 24 tracked paths contain
  non-ASCII characters. These are path hygiene review topics only.
- `docs/UI Inspiration.png` is the only primary `REVIEW_REQUIRED` item because
  its current role is unclear.

## Closure Conclusion

The repository is suitable for studentship handoff without destructive
rationalisation. The next maintainer should treat all review-register entries
as candidates for a separate, human-reviewed clean-up branch after the
scientific closure package is accepted.
