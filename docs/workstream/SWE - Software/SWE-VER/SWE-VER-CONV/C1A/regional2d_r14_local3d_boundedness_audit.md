# C1A-R14 Local3D Boundedness Audit

Final classification: `REPLAY_VOF_BEHAVIOUR_UNRESOLVED`.

Existing acceptance rule: `alpha.water` must remain within `[-5e-05, 1.00005]`, from `tools/openfoam/openfoam_replay.py::validate_smoke_case` and case `alpha_tolerance`.

Current replay inlet alpha is bounded before OpenFOAM transport: min `0.0`, max `1.0`.

Same-horizon G6 barrier at 1 s: min `0.0`, max `1.000000911`.

Current h400 forcing at 1 s: min `-8.737632442e-05`, max `1.000087482`.

Current h400 forcing at 5 s: min `-0.0003782604591`, max `1.000465925`.

Root cause classification: current inlet alpha is bounded and case controls match G6, but internal VOF alpha excursions exceed the existing 5e-05 tolerance and are materially larger than same-horizon G6.

Full current-generation 300 s replay gate: `CLOSED`.
