
# R20 Publication Figure Handoff

R20 creates a small figure-only refinement package under `deliverables/figures/r20_publication/`.
The package reuses accepted R16--R19/G6 geometry and terrain sources, and performs no new Regional2D,
Local3D, calibration, replay, mesh, source or solver work.

## Final figures

| Figure | Final outputs | Intended use | Notes |
|---|---|---|---|
| R20-A geographic corridor context | `publication/figure_R20_A_geographic_corridor_context.{pdf,svg,png}` | Poster or report context figure | Strongest for showing Japan/Tohoku/Kamaishi and the accepted corridor in one clean map. |
| R20-B oblique bathymetry/topography | `publication/figure_R20_B_oblique_bathymetry_topography.{pdf,png}` | Poster-primary terrain figure | Blender terrain render from real ETOPO data; no final SVG because the core evidence is raster terrain. Editable `.blend` retained in `sources/blender/`. |
| R20-C hybrid longitudinal corridor | `publication/figure_R20_C_hybrid_longitudinal_2d3d_corridor.{pdf,svg,png}` | Report bridge figure and poster footer candidate | Combines the real corridor bed profile with nearshore transition / candidate Local3D context. |

## Caveats

These figures are explanatory and cartographic. They do not imply completed historical validation,
accepted Local3D replay behaviour, new production simulations, calibration, or new geometry authority.
The Local3D zone in R20-C is a framework/candidate context derived from accepted G6 dimensions.

## Provenance

Per-figure provenance JSON files and `r20_publication_figure_manifest.json` are in
`deliverables/figures/r20_publication/provenance/`.
