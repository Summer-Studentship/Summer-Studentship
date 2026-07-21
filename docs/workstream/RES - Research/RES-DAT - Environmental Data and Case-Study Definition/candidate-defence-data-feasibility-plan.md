# Candidate-Defence Data-Feasibility Plan

Date: 2026-06-30

Status: Research in progress

This checklist controls selection of the initial documented defence structure.
It supports G1 data feasibility and must be completed before ML training is
considered.

## Minimum Evidence Checklist

| Requirement | Required evidence | Status |
| --- | --- | --- |
| Location | Coordinates, site name, reference datum and source provenance. | Open |
| Orientation | Defence alignment relative to shoreline and incoming local flow. | Open |
| Pre-event geometry | Height, crest width, base width, thickness, slope or face angle, openings or porosity where applicable. | Open |
| Current or reconstructed geometry | Current survey, reconstruction drawing, post-event report or defensible reconstruction evidence. | Open |
| Material information | Concrete, armour, rubble, steel, composite or inferred material assumptions with uncertainty. | Open |
| Foundation information | Embedment, soil, bedrock, toe, scour protection or explicit assumption state. | Open |
| Observed damage or survival | Failure, damage, survival, repair or reconstruction record tied to the selected event. | Open |
| Local tsunami exposure | Local inundation depth, velocity, direction, run-up, water level, time history or defensible proxy. | Open |
| Bathymetric/topographic compatibility | Bathymetry and topography adequate for event reconstruction and local impact modelling. | Open |
| Source provenance | Official reports, peer-reviewed studies, survey data, drawings, photographs or georeferenced imagery. | Open |
| Known uncertainty | Missing fields, inferred values, datum uncertainty, temporal mismatch and evidence quality. | Open |
| Modelling feasibility | Geometry can be represented in regional, local fluid, structural and ML feature spaces. | Open |

## Gate Rule

A candidate defence passes G1 only when the evidence is sufficient to define a
traceable baseline model and a defensible current or reconstructed comparison.
Weak or contextual historical labels may support interpretation, but controlled
validated simulations remain the principal causal source of response and
adequacy labels.

## Rejection or Deferral Conditions

Defer a candidate if its location, pre-event geometry, event exposure, material
or foundation assumptions, or damage/survival evidence cannot be reconstructed
with enough provenance for numerical modelling.

Reject a candidate for the initial project if the geometry cannot be meshed or
represented in the selected simulation workflow, or if the evidence would force
the surrogate to learn from before/after geometry without validated simulation
targets.
