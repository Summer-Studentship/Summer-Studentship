# R16 Publication Figure Manifest

Generated: `2026-08-11T21:24:09Z`
Branch: `feat/r16-publication-gis`
Git SHA: `6be53a5f357261c077ec52fd598e47cbdad1b163`

## QGIS Status

Status: **AVAILABLE** using `QGIS 4.2.0-Belém do Pará 'Belém do Pará' (exported)`.
The original R16 package recorded QGIS unavailability; R16B completes those previously blocked cartographic items with QGIS 4.2.

| Figure | Status | QC class | Output summary |
|---|---|---|---|
| A | COMPLETE | POSTER_READY_HERO | deliverables/figures/r16_publication/publication/figure_A_tohoku_kamaishi_corridor.pdf, deliverables/figures/r16_publication/publication/figure_A_tohoku_kamaishi_corridor.svg, deliverables/figures/r16_publication/publication/figure_A_tohoku_kamaishi_corridor.png |
| B | COMPLETE | POSTER_READY_HERO | deliverables/figures/r16_publication/publication/figure_B_corridor_bathymetry_plan.pdf, deliverables/figures/r16_publication/publication/figure_B_corridor_bathymetry_plan.svg, deliverables/figures/r16_publication/publication/figure_B_corridor_bathymetry_plan.png |
| C | COMPLETE | REPORT_READY_SECONDARY | deliverables/figures/r16_publication/publication/figure_C_corridor_bathymetry_oblique.pdf, deliverables/figures/r16_publication/publication/figure_C_corridor_bathymetry_oblique.svg, deliverables/figures/r16_publication/publication/figure_C_corridor_bathymetry_oblique.png |
| D1 | COMPLETE | POSTER_READY_PRIMARY | deliverables/figures/r16_publication/publication/figure_D1_eta_space_time.pdf, deliverables/figures/r16_publication/publication/figure_D1_eta_space_time.svg, deliverables/figures/r16_publication/publication/figure_D1_eta_space_time.png |
| D2 | COMPLETE | REPORT_READY_COMPANION | deliverables/figures/r16_publication/publication/figure_D2_wave_profiles_to_shore.pdf, deliverables/figures/r16_publication/publication/figure_D2_wave_profiles_to_shore.svg, deliverables/figures/r16_publication/publication/figure_D2_wave_profiles_to_shore.png |
| E | COMPLETE | POSTER_READY_PRIMARY | deliverables/figures/r16_publication/publication/figure_E_hybrid_domain_framework.pdf, deliverables/figures/r16_publication/publication/figure_E_hybrid_domain_framework.svg, deliverables/figures/r16_publication/publication/figure_E_hybrid_domain_framework.png |
| F | COMPLETE | POSTER_READY_PRIMARY | deliverables/figures/r16_publication/publication/figure_F_validation_geometry.pdf, deliverables/figures/r16_publication/publication/figure_F_validation_geometry.svg, deliverables/figures/r16_publication/publication/figure_F_validation_geometry.png |
| S1 | COMPLETE | REPORT_ONLY_SUPPORTING | deliverables/figures/r16_publication/publication/figure_S1_longitudinal_bathymetry.pdf, deliverables/figures/r16_publication/publication/figure_S1_longitudinal_bathymetry.svg, deliverables/figures/r16_publication/publication/figure_S1_longitudinal_bathymetry.png |

## Data Authority

- Bathymetry/topography source: ETOPO 2022 WGS84 + EGM2008 tile already used by the simulation preprocessing lineage.
- Regional result: frozen R10 h400 limited_linear HDF5, no rerun.
- Validation register: 29 observations, 0 DIRECT, 1 PROXY, 28 TARGET_ONLY.
- Figure C uses a direct VTK offscreen terrain render because PyVista is unavailable and QGIS 4.2 headless 3D layout export is not exposed in this runtime.
