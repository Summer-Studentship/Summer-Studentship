# R16 Publication Figure Handoff

R16B completes the QGIS publication cartography that was blocked in the original R16 run. The original QGIS-runtime blocker remains recorded in provenance history; the current package uses QGIS 4.2 headless exports.

| Figure | Priority | Status | QC class | Recommended use | Caption | Caveat |
|---|---|---|---|---|---|---|
| A | HERO | COMPLETE | POSTER_READY_HERO | Poster opening map | Tohoku event and accepted Kamaishi corridor geography. | Use with R10 h400 uncertainty caveat. |
| B | HERO | COMPLETE | POSTER_READY_HERO | Poster real-case domain/bathymetry | Real bathymetry/topography context for the accepted corridor. | Context only; not spatial qualification. |
| D1 | PRIMARY | COMPLETE | POSTER_READY_PRIMARY | Poster or report wave-evolution panel | Frozen R10 h400 eta evolution sampled along the corridor toward Kamaishi. | Best available numerically uncertain, uncalibrated, not historically validated. |
| D2 | SECONDARY | COMPLETE | REPORT_READY_COMPANION | Report page 2 companion to D1 | Selected eta profiles show waveform evolution toward the nearshore interface. | Nearest-cell centreline sampling; no smoothing or rerun. |
| E | PRIMARY | COMPLETE | POSTER_READY_PRIMARY | Framework figure | Geographic Regional2D to conceptual Local3D one-way forcing relationship. | Local3D current-generation remains unresolved. |
| F | PRIMARY | COMPLETE | POSTER_READY_PRIMARY | Validation status figure | R15 validation targets relative to the current corridor. | 0 DIRECT, 1 PROXY, 28 TARGET_ONLY preserved. |
| C | SECONDARY | COMPLETE | REPORT_READY_SECONDARY | Report or optional poster visual | Oblique real-terrain view of the corridor. | Visualisation with vertical exaggeration; not a solver result. |
| S1 | REPORT_ONLY | COMPLETE | REPORT_ONLY_SUPPORTING | Two-page report bathymetry context | Centreline bed profile used by D1/D2. | Wet-conditioned mesh profile, not independent coastal topography. |

## Report Page 2 Recommendations

- Best corridor/domain figure: Figure B.
- Best bathymetry figure: Figure B, with S1 as report-only longitudinal support.
- Best hybrid-domain schematic: Figure E.
- Best wave-evolution figure: Figure D1, with D2 as the companion explanatory panel.
- Best validation figure: Figure F.

## Allowed Claims

The package can claim reproducible QGIS cartography, editable project/layouts and completed frozen-result propagation figures. It must not claim full historical validation, physical calibration, h250/h300/h400 spatial qualification, or Local3D current-generation closure.
