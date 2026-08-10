# R13 Poster Scientific Handoff

Current status: A one-way 2D to 3D hybrid framework has been implemented and demonstrated at G6, and R13 generated the current h400 limited-linear replay package and Local3D case inputs. Current Local3D execution remains exploratory; R13 smoke status is `completed_not_accepted_alpha_bounds` and must not be labelled validated.

Allowed claims:
- Implemented: Regional2D NLSWE solver; terrain/source pipeline; limited-linear reconstruction; 2D to 3D replay infrastructure; Local3D URANS/VOF framework.
- Demonstrated: baseline G6 hybrid replay; current limited-linear replay package and generated Local3D cases. Current-forcing rigid-barrier smoke attempts reached their requested end times and exported VTK, but failed the repository alpha.water bounds tolerance.
- Verified: first-order Regional method; second-order limited-linear method; well-balanced/conservation regression evidence.

Prohibited claims:
- Complete historical Tohoku reconstruction.
- Mesh-converged current best-available Regional forcing.
- Calibrated or decision-grade Local3D defence-impact predictions.

Poster-safe wording:
A one-way 2D to 3D hybrid framework has been implemented and demonstrated. Regional numerical verification established a second-order limited-linear formulation; real-event refinement subsequently revealed a geospatial spatial-fidelity limitation that is now being characterised ahead of historical validation.

Figures: see `c1a_r13_figure_manifest.json`.

Recommended status timeline: verified numerical method -> event refinement -> spatial-fidelity limit -> exploratory hybrid replay -> historical validation targets.
