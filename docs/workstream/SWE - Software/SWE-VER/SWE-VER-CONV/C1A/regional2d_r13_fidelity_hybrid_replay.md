# C1A-R13 Regional Fidelity and Hybrid Replay Evidence

Projection classification: `PROJECTION_FIDELITY_CEILING`.

h250 projection-only mesh SHA: `ff99323e599049782fd78a59a45bf1124adb7c3915442ec1d71dce23d4f06186`.

Final Regional mechanism: `TERRAIN_SOURCE_FIDELITY_DOMINANT` with `MODERATE` confidence.

Another full Regional production run before Wednesday: `NO`.

Best available Regional forcing: `R10 h400 limited_linear`, status `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`; not spatially qualified and allowed only for exploratory hybrid replay.

R13 generated the 245-545 s to 0-300 s h400 limited-linear OpenFOAM replay package and generated no-defence and rigid-barrier Local3D case directories. Local3D smoke result: `completed_not_accepted_alpha_bounds`. Current-forcing rigid-barrier smoke attempts reached their requested end times and exported VTK, but are not accepted under the repository alpha.water bounds tolerance; no 300 s no-defence or rigid-barrier replay was executed.
