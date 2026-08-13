# R21 Editable Figure Handoff

R21 provides clean scientific bases plus separate editable overlays for manual layout in Lucid,
Figma, Inkscape or equivalent. It does not perform new simulation, calibration, terrain editing,
corridor editing, or poster/report editing.

## Figure B

- Scientific base, terrain only: `deliverables/figures/r21_editable/publication/figure_B0_oblique_bathymetry_clean.png`
- Preferred Lucid/Figma base: `deliverables/figures/r21_editable/publication/figure_B1_oblique_bathymetry_corridor.png`
- Editable overlay: `deliverables/figures/r21_editable/editable/figure_B2_editable_overlay.svg`

B0 contains only bathymetry/topography, the z = 0 sea-level treatment, and the light background.
B1 adds only the actual Regional2D corridor footprint from the R20 scene. B2 is transparent SVG
overlay geometry with editable groups for corridor, centreline, event, interface, Kamaishi,
propagation and distance reference.

The B2 distance reference is a 25,000 m EPSG:32654 projected-ground segment parallel to the
accepted corridor centreline tangent. It is not a screen-space scale bar.

## Figure C

- Clean scientific profile: `deliverables/figures/r21_editable/editable/figure_C0_longitudinal_bathymetry_clean.svg`
- Lightly interpreted profile: `deliverables/figures/r21_editable/editable/figure_C1_longitudinal_bathymetry_interpreted.svg`
- Editable annotation suggestions: `deliverables/figures/r21_editable/editable/figure_C2_annotation_template.svg`

Figure C is regenerated from `deliverables/figures/r19_tikz/data/bathymetry_profile.csv`, not
reconstructed from pixels. The axis convention is unchanged: 0 km is the selected wet nearshore
interface and larger distance is farther offshore/sourceward.

## Mathematical Relationship

The terrain field is `b = b(x,y)`. The Blender view visualises `(x,y,b(x,y))` with 4x vertical
exaggeration for interpretation. The longitudinal profile samples the same field along the accepted
corridor centreline `x_c(s), y_c(s)`, giving `b_c(s) = b(x_c(s), y_c(s))`.

## Exact vs Conceptual

The bed profile, 2011 event reference, selected wet interface, corridor geometry and B2 25 km
reference are exact current-case geometry/data products. C1 region bands are broad conceptual
interpretation bands inherited from the R19 convention; they are not calibrated physical thresholds.
