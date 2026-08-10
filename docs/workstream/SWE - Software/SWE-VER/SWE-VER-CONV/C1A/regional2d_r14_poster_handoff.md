# R14 Poster Handoff

Allowed hybrid claim:
A one-way Regional2D to Local3D hybrid replay framework has been implemented and demonstrated on the accepted G6 baseline. The current h400 limited-linear real-event forcing has been packaged for replay, but the full current-generation replay remains diagnostic until the Local3D boundedness gate is accepted.

Required numerical-fidelity caveat:
The R10 h400 limited-linear Regional forcing is the best available real-event forcing, but it is `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`, not spatially qualified, not physically calibrated, and not historically validated.

Implemented:
- Regional2D solver and terrain/source pipeline
- limited-linear Regional reconstruction
- OpenFOAM 11 Local3D replay generation
- HDF5 ResultDataset plotting workflow

Demonstrated:
- G6 accepted 300 s Local3D no-defence and rigid-barrier replay
- current h400 replay package and smoke diagnostics
- poster-ready Regional/coupling figures

Verified:
- first-order and second-order Regional numerical method evidence from prior C1A work
- R14 HDF5/XDMF/ResultDataset access to the h400 result

Diagnostic:
- current h400 Local3D smoke: `REPLAY_VOF_BEHAVIOUR_UNRESOLVED`

Validation target:
- DART, NOWPHAS/Kamaishi, and run-up/inundation comparisons remain future validation targets.

Figure manifest:
`deliverables/figures/r14_hybrid/r14_figure_manifest.json`
