# R18 Poster Visual Handoff

R18 freezes poster-ready figures only. No simulation, calibration, source, mesh, corridor, Local3D or poster/report editing was performed.

| Figure | Role | Final path | Question | Interpretation | Caption | Allowed claim | Required caveat | Suggested A0 width |
|---|---|---|---|---|---|---|---|---|
| A1 | HERO | `deliverables/figures/r18_poster/publication/figure_A1_tohoku_kamaishi_corridor_bathymetry.png` | Where is the accepted Kamaishi corridor, and what bathymetry does the simulated wave traverse? | Main poster geographic anchor | Combined context, corridor and longitudinal bathymetry figure using the existing R16 ETOPO 2022 EGM2008 GIS sources and frozen R10 h400 centreline sampling. | Shows the unchanged accepted R10/G6 corridor, event/Kamaishi context and along-corridor bathymetry from existing publication sources. | Geographic and bathymetric context only; does not establish calibration, historical validation or real-event spatial qualification. | 32-36 cm |
| C | PRIMARY | `deliverables/figures/r18_poster/publication/figure_C_bathymetry_3d_annotated.png` | What does the real Kamaishi corridor bathymetry and coastal relief look like in an interpretable oblique view? | Best visual intuition for the corridor relief | Oblique Blender terrain visualisation of the accepted Kamaishi Regional2D corridor from the R16/R17 ETOPO 2022 EGM2008 terrain lineage. Vertical relief is exaggerated 4x for interpretation; the blue plane marks z=0 and the muted coral outline is the unchanged computational corridor. | Shows the accepted corridor over the existing ETOPO 2022 bathymetry/topography lineage as a visual terrain context figure. | Terrain visualisation only; not a Local3D, OpenFOAM, calibrated inundation or historically validated result. | 28-32 cm |
| D1 | PRIMARY | `deliverables/figures/r18_poster/publication/figure_D1_eta_space_time_publication.png` | How does the frozen h400 simulated free surface evolve toward Kamaishi along the accepted corridor? | Best scientific-result panel | Frozen R10 h400 limited_linear free-surface elevation sampled along the accepted centreline toward the nearshore interface. | Shows the existing R10 h400 limited_linear eta evolution along the accepted corridor sampling path. | Best available numerically uncertain real-event result; not spatially qualified, calibrated or historically validated. | 22-26 cm |
| F | SECONDARY | `deliverables/figures/r18_poster/publication/figure_F_validation_geometry_publication.png` | Why do the available historical observations not directly validate the current Kamaishi corridor result? | Validation caveat panel | R15 observation geometry relative to the unchanged Kamaishi corridor. NOWPHAS 802G is the priority offshore target but lies about 12.3 km outside the corridor; DART 21418 lies about 545 km outside in the open-ocean context inset. | Shows observation geometry and R15 eligibility relative to the accepted corridor while preserving the R15 classifications. | Historical validation is not complete: R15 has 29 observations with 0 DIRECT, 1 PROXY and 28 TARGET_ONLY. | 18-22 cm |

## Poster Classification

- HERO: A1.
- PRIMARY: C and D1.
- SECONDARY: F.
- REPORT_ONLY: D2 and S1.
- DROP_FROM_POSTER: E.

## Two-Page Report Recommendation

Use A1 as the Page-2 figure because it combines the geographic corridor, regional context and bathymetry profile in one compact asset. Use D1 as the companion figure where space allows because it is the strongest scientific-result visual.

## Supporting Package

- Contact sheet: `deliverables/figures/r18_poster/publication/r18_poster_contact_sheet.png`.
- D2: REPORT_ONLY (Useful explanatory companion for a report page but redundant beside D1 on a crowded poster.)
- E: DROP_FROM_POSTER (R18 explicitly drops the hybrid schematic from the poster visual package.)
- S1: REPORT_ONLY (Folded into A1 as an inset; standalone profile remains report/supporting material only.)
