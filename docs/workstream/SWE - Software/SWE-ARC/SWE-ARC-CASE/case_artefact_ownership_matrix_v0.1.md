# Case Artefact Ownership Matrix v0.1

- **Work Package:** `SWE-ARC-CASE-WP1`
- **Policy:** `architecture/case_lifecycle_policy_v0.1.json`

| Path | Owner | Mutability |
|---|---|---|
| `case.json` | `SWE-DAT-CFG` | versioned |
| `README.md` | `SWE-ARC-CASE` | mutable |
| `inputs/` | `SWE-DAT-CFG` | versioned |
| `inputs/data/` | `SWE-DAT-MAN` | versioned |
| `validation/` | `SWE-VER` | append-only |
| `manifests/` | `SWE-DAT-MAN` | append-only |
| `prepared/` | `SWE-ARC-SVC` | versioned |
| `runs/` | `SWE-ARC-SVC` | append-only |
| `runs/<run-id>/run.json` | `SWE-ARC-SVC` | versioned |
| `runs/<run-id>/logs/` | `SWE-REL` | append-only |
| `runs/<run-id>/checkpoints/` | `SWE-DAT-CHK` | append-only |
| `runs/<run-id>/outputs/` | `SWE-DAT-SCH` | immutable |
| `runs/<run-id>/derived/` | `SWE-VER` | versioned |
| `runs/<run-id>/manifests/` | `SWE-DAT-MAN` | append-only |
| `archive/` | `SWE-REL` | immutable |

The matrix assigns placement and ownership only. Detailed file content remains with the downstream WBS owner.
