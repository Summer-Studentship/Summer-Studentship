# R17 Deliverables Register

This register lists the final studentship deliverables and their closure status.

## Final Entry Points

| Deliverable | Path | Status |
| --- | --- | --- |
| Public README | `README.md` | Updated for R17 closure |
| Studentship status | `docs/project/r17_studentship_status.md` | Complete |
| Repository audit | `docs/project/r17_repository_audit.md` | Complete, non-destructive |
| Repository map | `docs/project/r17_repository_map.md` | Complete |
| Review register | `docs/project/r17_repository_review_register.md` and `.json` | Complete |
| Proposed structure | `docs/project/r17_proposed_repository_structure.md` | Complete, recommendations only |

## Figure Packages

| Package | Path | Status | Caveat |
| --- | --- | --- | --- |
| R16 publication GIS figures | `deliverables/figures/r16_publication/publication/` | Complete | Uses frozen R16B GIS/QGIS package. |
| R17 3D bathymetry PNG | `deliverables/figures/r17_closure/publication/figure_C_corridor_bathymetry_3d.png` | Complete, 6200 x 3600 px | Terrain visualisation only, not 3D fluid simulation. |
| R17 3D bathymetry PDF | `deliverables/figures/r17_closure/publication/figure_C_corridor_bathymetry_3d.pdf` | Complete | Generated from final PNG. |
| R17 vertical exaggeration contact sheet | `deliverables/figures/r17_closure/previews/figure_C_vertical_exaggeration_contact_sheet.png` | Complete | 4x selected after 2x/4x/6x inspection. |
| R17 Blender scene | `deliverables/figures/r17_closure/sources/blender/figure_C_corridor_bathymetry_3d.blend` | Complete | Source scene retained; 9,305,523 bytes at render time. |

## Scientific Evidence Packages

| Evidence | Path | Status |
| --- | --- | --- |
| Model traceability | `docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/regional2d_model_implementation_traceability.md` | Accepted with documentation fixes |
| R13 fidelity diagnosis | `docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/regional2d_r13_fidelity_hybrid_replay.md` | Complete |
| R14 poster handoff | `docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/regional2d_r14_poster_handoff.md` | Complete |
| R15 validation framework | `docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/regional2d_r15_validation_framework.json` | Complete; no direct validation claim |
| R16 publication figure manifest | `deliverables/figures/r16_publication/provenance/publication_figure_manifest.md` | Complete |
| R17 3D figure provenance | `deliverables/figures/r17_closure/provenance/figure_C_corridor_bathymetry_3d.provenance.json` | Complete |

## Frozen Caveats

- R10 h400 `limited_linear` remains `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`.
- Real-event Regional2D is not spatially qualified.
- Temporal convergence and calibration are not claimed.
- Historical validation is not claimed.
- Current-forcing Local3D replay remains `REPLAY_VOF_BEHAVIOUR_UNRESOLVED`.
