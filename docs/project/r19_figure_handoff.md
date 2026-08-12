# R19 Figure Handoff

Status: `COMPLETE`
Generated: `2026-08-12T21:52:53Z`
Branch: `feat/r19-tikz-domain-figures`
HEAD: `2e2f843ab55404e332a9780410f852705fa0ba95`

R19 replaces decorative or AI-illustration approaches with QGIS geography, real model geometry, TikZ engineering annotation and PGFPlots data plotting.

| Figure | Scientific purpose | Caption | Allowed claim | Required caveat | Poster/report role | QC class |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | Locate the 2011 event reference, Kamaishi, and the current Regional2D corridor geometry in real GIS geography. | QGIS/TikZ map of the 2011 Tohoku event reference, Kamaishi proxy, accepted delivery corridor, centreline, current length/width/orientation and selected wet nearshore interface. | Shows the accepted current R10/G6 delivery corridor geometry over real ETOPO/QGIS context. | The corridor is delivery integration geometry, not exact Kamaishi harbour reconstruction, spatial qualification, calibration or historical validation. | poster primary candidate | `POSTER_CANDIDATE` |
| T2 | Explain the full-width source-to-interface bathymetry and one-way 2D-to-3D framework. | Longitudinal hybrid-corridor schematic using bathymetry sampled from the real R10 h400 Regional2D mesh along the accepted Kamaishi centreline. The 2D->3D transition band is conceptual/framework; the selected wet nearshore interface is the current implementation choice and not a universal optimum. | Shows real R10 h400 mesh bathymetry along the accepted centreline with conceptual hybrid regions. | Transition band is conceptual/framework; selected interface is current implementation choice, not a universal optimum. | poster primary candidate | `POSTER_CANDIDATE` |
| T3 | Show the Regional2D and Local3D computational domains, boundary labels, and transfer quantities. | Engineering schematic of the accepted Regional2D corridor and G6 simple-rigid-barrier Local3D replay domain, with one-way transfer terms and boundary labels from case evidence. | Shows accepted current-case computational geometry and one-way replay coupling terms. | The Local3D box is a TikZ pseudo-3D engineering schematic derived from G6 dimensions, not a new OpenFOAM render or validation result. | report/detail candidate | `REPORT_CANDIDATE` |
| T0 | Preview T1, T3, and T2 in a poster-panel composition. | Composite preview combining the geographic corridor, computational-domain detail and longitudinal hybrid-corridor schematic. | Useful combined preview of the R19 domain figure package. | Composite is a layout preview, not an additional scientific result. | poster primary candidate | `POSTER_CANDIDATE` |

## Scientific Authority

- Regional method authority: `MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES`, `GLOBAL_FIRST_ORDER_VERIFIED`, `SECOND_ORDER_VERIFIED`.
- R10 h400 `limited_linear` remains `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`.
- The figures may say the 2011 Tohoku event was simulated with the verified formulation; they must also say it is not spatially qualified, physically calibrated or historically validated.
- Hybrid coupling was implemented/demonstrated through accepted G6 replay; the R10 h400 Local3D replay remains `REPLAY_VOF_BEHAVIOUR_UNRESOLVED`.
- Historical validation counts remain 29 observations, 0 DIRECT, 1 PROXY, 28 TARGET_ONLY; NOWPHAS 802G is about 12.273 km outside and DART 21418 about 545 km outside.

## Files

- T1: `deliverables/figures/r19_tikz/publication/figure_T1_tohoku_kamaishi_domain.tex`, `deliverables/figures/r19_tikz/publication/figure_T1_tohoku_kamaishi_domain.pdf`, `deliverables/figures/r19_tikz/publication/figure_T1_tohoku_kamaishi_domain.svg`, `deliverables/figures/r19_tikz/publication/figure_T1_tohoku_kamaishi_domain.png`
- T2: `deliverables/figures/r19_tikz/publication/figure_T2_longitudinal_hybrid_corridor.tex`, `deliverables/figures/r19_tikz/publication/figure_T2_longitudinal_hybrid_corridor.pdf`, `deliverables/figures/r19_tikz/publication/figure_T2_longitudinal_hybrid_corridor.svg`, `deliverables/figures/r19_tikz/publication/figure_T2_longitudinal_hybrid_corridor.png`
- T3: `deliverables/figures/r19_tikz/publication/figure_T3_computational_domains.tex`, `deliverables/figures/r19_tikz/publication/figure_T3_computational_domains.pdf`, `deliverables/figures/r19_tikz/publication/figure_T3_computational_domains.svg`, `deliverables/figures/r19_tikz/publication/figure_T3_computational_domains.png`
- T0: `deliverables/figures/r19_tikz/publication/figure_T0_domain_package_composite.tex`, `deliverables/figures/r19_tikz/publication/figure_T0_domain_package_composite.pdf`, `deliverables/figures/r19_tikz/publication/figure_T0_domain_package_composite.svg`, `deliverables/figures/r19_tikz/publication/figure_T0_domain_package_composite.png`
