# R17 Studentship Status

Status date: 2026-08-11.

This document is the scientific closure summary for the studentship repository.
It preserves the strongest accepted claims and labels unresolved work without
expanding scope.

## Executive Position

The repository contains a verified Regional2D numerical implementation, an
implemented one-way Regional2D to Local3D replay framework, a real Tohoku
Kamaishi h400 `limited_linear` result, and publication-quality figure assets.
The real-event result is not spatially qualified, calibrated or historically
validated. It remains best-available evidence with mandatory uncertainty
labels.

## Capability Matrix

| Capability | Evidence | Closure classification |
| --- | --- | --- |
| Model to implementation traceability | `docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A/regional2d_model_implementation_traceability.md` | `MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES` |
| Regional2D first-order baseline | R7/R8 verification scripts and figures | `GLOBAL_FIRST_ORDER_VERIFIED` |
| Regional2D `limited_linear` reconstruction | R9 exact/smooth verification and R10 option path | `SECOND_ORDER_VERIFIED` |
| Real Tohoku-Kamaishi Regional2D forcing | R10 h400 `limited_linear` | `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN` |
| h400 to h300 event-family behaviour | R12/R13 fidelity diagnosis | Not spatially qualified |
| Regional fidelity mechanism | R13 projection and terrain analysis | `TERRAIN_SOURCE_FIDELITY_DOMINANT`, confidence `MODERATE`, `PROJECTION_FIDELITY_CEILING` |
| One-way hybrid framework | G6 replay and R14/R14B evidence | Implemented and demonstrated |
| Current-forcing Local3D replay | R13/R14 current h400 replay package and diagnostics | `REPLAY_VOF_BEHAVIOUR_UNRESOLVED` |
| Historical validation evidence | R15 observation framework | 29 observations, 0 `DIRECT`, 1 `PROXY`, 28 `TARGET_ONLY`; no validation claim |
| Publication GIS package | R16/R16B QGIS package | Complete |
| Publication 3D bathymetry visual | R17 Blender package | Complete; terrain visualisation only |

## Ancestry Matrix

| Programme stage | Role in closure | Present claim |
| --- | --- | --- |
| Pre-WBS and G5-era material | Provenance and historical context | Retained, not used as current Local3D authority unless superseded evidence cites it explicitly |
| G6 | Accepted theoretical-model and one-way replay baseline | Framework authority preserved |
| R7/R8 | First-order model/numerical verification | `GLOBAL_FIRST_ORDER_VERIFIED` |
| R9 | Limited-linear verification | `SECOND_ORDER_VERIFIED` on verification cases |
| R10 | Best available real-event h400 `limited_linear` Regional result | `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN` |
| R11/R12 | h300 and divergence diagnosis | Did not establish real-event spatial qualification |
| R13 | Regional fidelity and hybrid replay packaging | `TERRAIN_SOURCE_FIDELITY_DOMINANT`; current forcing allowed only with caveats |
| R14/R14B | Local3D replay acceptance audit and poster evidence | G6 replay accepted/demonstrated; current-forcing replay unresolved |
| R15 | Historical validation framework | No direct historical validation claim |
| R16/R16B | QGIS/GIS publication package | Complete GIS visual package |
| R17 | Closure figure, README and non-destructive repository audit | Complete closure package; no clean-up action performed |

## Kamaishi And Validation Rationale

Kamaishi remains the selected nearshore target and the right narrative focus
for the studentship. The limitation is not the absence of all relevant
observations. The limitation is comparison eligibility for the frozen corridor
output: NOWPHAS 802G is about 12.3 km outside the corridor, and DART 21418 is
about 545 km outside it. R15 therefore provides a validation framework and real
observation context, but no direct validation.

## Corridor Design Principles

- The corridor is a controlled nearshore forcing slice, not a basin-wide
  observational domain.
- Geometry, projection, source lineage, terrain lineage and coupling-section
  definitions are provenance-bearing scientific inputs.
- Corridor changes would invalidate direct comparison with existing frozen
  results and must be handled as a new study.
- Sponge, open-boundary and coupling-section choices belong to the numerical
  evidence chain, not to visual styling.

## Final Scientific Limitations

- No real-event spatial convergence is claimed.
- No temporal convergence is claimed.
- No calibration is claimed.
- No direct historical validation is claimed.
- No accepted current-generation R10-h400 Local3D production replay is claimed.
- No FSI, damage, scour, ML, two-way coupling or dispersive regional extension
  is part of the accepted studentship result.
