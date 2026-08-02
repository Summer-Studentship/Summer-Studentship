# OpenFOAM Replay Pipeline v0.1

## Scope

This note documents the G4 replay path from a G3 Regional2D coupling export into
small OpenFOAM 11 `incompressibleVoF` smoke cases. The implementation is a
repo-owned conversion and case-generation harness, not a replacement for the
Regional2D solver and not a calibrated coastal-engineering model.

The supported source contract is the G3 coupling section output:

- `metadata.json`
- `samples.csv`
- `history.csv`

The generated OpenFOAM inputs are:

- `constant/boundaryData/inlet/points`
- time directories containing `U`
- time directories containing `alpha.water`
- synthetic `no_defence` and `simple_rigid_barrier` case dictionaries

The replay converter records source paths, SHA-256 values, section metadata,
sample ordering, local support widths, mapping options, boundary face count,
time range, turbulence inputs and maximum mapped speed in
`replay_conversion.json`. Discharge reconstruction residuals are written to
`replay_diagnostics.csv`.

## OpenFOAM Authority

The pinned container image is:

`docker.io/openfoam/openfoam11-paraview510:11`

The image reports `OpenFOAM-11` and provides the tutorials under
`/opt/openfoam11/tutorials`. Dictionary syntax and runtime choices were checked
against these OpenFOAM 11 tutorial locations:

- `/opt/openfoam11/tutorials/incompressibleVoF/damBreakWithObstacle`
- `/opt/openfoam11/tutorials/incompressibleVoF/damBreakPorousBaffle`
- `/opt/openfoam11/tutorials/incompressibleVoF/waterChannel`
- `/opt/openfoam11/tutorials/incompressibleFluid/pitzDailySteadyExperimentalInlet`
- `/opt/openfoam11/tutorials/incompressibleVoF/DTCHull`
- `/opt/openfoam11/tutorials/incompressibleVoF/sloshingTank3D`

The repo-owned wrapper is `tools/openfoam/run_openfoam11.sh`. It uses Podman,
disables networking with `--network none`, overrides the image entrypoint, mounts
the case directory at `/case`, sources `/opt/openfoam11/etc/bashrc`, and then
executes the requested OpenFOAM command.

## Mapping Contract

The replay configuration declares the Regional2D frame, the OpenFOAM local
frame, interpolation choices and turbulence inputs. The current implementation
requires:

- `spatial_interpolation: piecewise_linear_along_section`
- `outside_span: clamp`
- `velocity_profile: depth_uniform`
- `preserve_discrete_discharge: true`

The inlet point ordering is span-major then vertical. Regional sample support
widths are inferred from midpoint Voronoi intervals along the coupling-section
tangent, then mapped across the configured OpenFOAM inlet span. Water volume
fraction is reconstructed from bed elevation, free-surface elevation and vertical
cell bounds. Momentum is projected onto the Regional2D inward normal and tangent
axes, then mapped onto the configured OpenFOAM inlet and span axes.

## Synthetic Smoke Cases

The smoke workflow is:

```bash
tools/openfoam/run_synthetic_replay_smoke.sh /tmp/g4-openfoam-replay-smoke
```

The workflow performs:

1. Convert the fixture coupling export to OpenFOAM `boundaryData`.
2. Generate `no_defence`.
3. Run `blockMesh`, `checkMesh`, `setFields`, `foamRun -solver incompressibleVoF`
   and `foamToVTK`.
4. Validate final time, `alpha.water` bounds, non-empty probe files and VTK
   output.
5. Generate `simple_rigid_barrier`.
6. Run the same OpenFOAM commands and additionally validate non-empty force
   output from the `barrier` patch.

The generated case dimensions are intentionally small: 2.0 m streamwise length,
0.6 m span and 0.6 m height. The barrier case adds a rigid wall obstacle between
`x = 0.90 m` and `x = 0.96 m`, with height `0.24 m`.

## Prompt C Handoff

This replay path is a deterministic smoke-test bridge. It does not yet cover:

- full-scale bathymetry-driven OpenFOAM meshing;
- calibrated turbulence, wall functions or roughness;
- bidirectional coupling back to Regional2D;
- moving, deformable or porous barrier mechanics;
- validation against laboratory or field measurements;
- automated ParaView rendering beyond writing VTK files.

Prompt C should treat this output as a verified producer-to-consumer skeleton:
the file contracts, case dictionaries, solver invocation and generated force,
probe and VTK artifacts are present and runnable, while physical fidelity remains
a downstream modelling task.
