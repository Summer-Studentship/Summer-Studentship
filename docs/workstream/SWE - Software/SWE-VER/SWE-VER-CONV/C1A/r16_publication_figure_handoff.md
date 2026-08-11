# R16 Publication Figure Handoff

R16 supplies publication-figure assets only and does not edit the active poster.

QGIS status: **QGIS_RUNTIME_BLOCKED**. Final cartographic Figures A, B, E and F require a QGIS-capable runtime; R16 has staged editable GIS layers and PyQGIS scripts rather than substituting non-QGIS final maps.

| Figure | Priority | Status | Recommended use | Caption | Caveat |
|---|---|---|---|---|---|
| A | HERO | BLOCKED_BY_QGIS_RUNTIME | Poster opening map after QGIS export | Tohoku event and accepted Kamaishi corridor geography. | Blocked until QGIS export. |
| B | HERO | BLOCKED_BY_QGIS_RUNTIME | Poster real-case domain/bathymetry after QGIS export | Real bathymetry/topography context for the accepted corridor. | Blocked until QGIS export; conditioned corridor terrain is wet-only. |
| D1 | PRIMARY | COMPLETE | Poster or report wave-evolution panel | Frozen R10 h400 eta evolution sampled along the corridor toward Kamaishi. | Best available numerically uncertain, uncalibrated, not historically validated. |
| D2 | PRIMARY | COMPLETE | Report page 2 companion to D1 | Selected eta profiles show waveform evolution toward the nearshore interface. | Nearest-cell centreline sampling; no smoothing or rerun. |
| E | PRIMARY | BLOCKED_BY_QGIS_RUNTIME | Framework figure after QGIS export | Geographic Regional2D to conceptual Local3D one-way forcing relationship. | Local3D footprint is conceptual, not final production closure. |
| F | PRIMARY | BLOCKED_BY_QGIS_RUNTIME | Validation status figure after QGIS export | R15 validation targets relative to the current corridor. | R15 classifications preserved exactly: 0 DIRECT, 1 PROXY, 28 TARGET_ONLY. |
| C | SECONDARY | BLOCKED_BY_QGIS_RUNTIME | Poster visual if QGIS 3D/PyVista becomes available | Oblique real terrain view of the corridor. | Blocked by missing QGIS/PyVista runtime. |
| S1 | REPORT_ONLY | COMPLETE | Two-page report bathymetry context | Centreline bed profile used by D1/D2. | Wet-conditioned mesh profile, not a full coastal topography map. |

## Report Page 2 Recommendations

- Best corridor/domain figure: Figure B after QGIS export; use S1 as fallback context if QGIS remains unavailable.
- Best bathymetry figure: Figure B after QGIS export; S1 for report-only longitudinal context.
- Best hybrid-domain schematic: Figure E after QGIS export.
- Best wave-evolution figure: Figure D1, with D2 as the companion explanatory panel.
- Best validation figure: Figure F after QGIS export; until then, use R15 validation_station_domain_map only with its existing caveats.

## Allowed Claims

R16 can claim reproducible GIS layer preparation and completed frozen-result propagation figures. It must not claim full historical validation, physical calibration, h250/h300/h400 spatial qualification, or Local3D current-generation closure.
