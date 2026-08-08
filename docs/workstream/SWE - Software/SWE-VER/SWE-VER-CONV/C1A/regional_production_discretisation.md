# C1A-R3 Regional2D Production Discretisation Decision

Status: not selected.

C1A-R3 corrected the fine-mesh construction path by separating terrain source resolution, terrain processing resolution, solver mesh target size, and actual characteristic mesh size. The previous nominal 750 m anomaly was traced to an explicit external profile with `profile.spacing_m = 750.0` and `profile.mesh_size_m = 500.0`; it was not Gmsh rounding or snapping.

The new study identity is `regional-spatial-fine-resolution-v3`. Corrected mesh-only candidates preserved the requested solver target: h900, h850, h800, h750, h700, h650, and h600. The h750 candidate produced 4008 active cells, close to the expected 4302 active cells from h1000 scaling and far from the previous 9320-cell 500 m solve.

The completed spatial ladder is h1000 reused from C1A-R1 plus new h900 and h800 full 600 s Regional2D runs. Actual characteristic mesh sizes are 638.487 m, 595.928 m, and 514.563 m. The h900 and h800 runs completed in 2080.960 s and 3078.419 s.

Spatial convergence did not qualify. For the medium-to-fine h900 to h800 pair, eta waveform NRMSE is 0.109513, qn waveform NRMSE is 0.651349, and integrated Qn waveform NRMSE is 0.184731; all exceed the 2% forcing-waveform target. The dominant residual mechanisms are bathymetric_discretisation_dominated, phase_timing_component, distributed_forcing_waveform_unresolved.

No production Regional2D mesh or timestep policy is selected. Temporal convergence remains gated. No observations were used, no physics calibration was performed, and no Local3D convergence was started.

ETOPO caveat: these mesh changes measure discretisation behaviour of the interpolated ETOPO-derived terrain representation; they do not add bathymetric information beyond the source product.

## C1A-R4 Frozen-Terrain Audit

C1A-R4 reclassifies the scientific interpretation of C1A-R3 as coupled solver-mesh plus terrain/source-discretisation sensitivity. R3 still proves the previous Regional2D family was not spatially qualified, but the h1000/h900/h800 runs cannot be reused as a pure frozen-terrain solver-mesh convergence ladder because their terrain processing resolution, terrain raster hash, and source raster hash vary by level.

The frozen authority for R4 is the accepted G6 terrain `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/case/outputs/terrain/conditioned-terrain.tif` with SHA-256 `45e5ab63a69e77ec11b293c39cbb93dd0df30a4f24d1a4f4d9515267a01f1363`. Temporal convergence remains gated until real frozen-family Regional2D runs qualify spatially.
