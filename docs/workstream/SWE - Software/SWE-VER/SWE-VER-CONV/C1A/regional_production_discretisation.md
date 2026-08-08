# C1A-R4 Regional2D Frozen-Terrain Production Discretisation Decision

Status: not selected.

C1A-R4 executed the frozen G6 terrain/source Regional2D spatial ladder `h1000, h800, h600` to 600 s under study `regional-spatial-frozen-terrain-v4`. The terrain raster, source raster, physical configuration, domain, and coupling section are invariant across all levels.

The medium-to-fine qualification pair is `h600_vs_h800`. Spatial qualification status is `not_spatially_qualified` with a 2% NRMSE threshold. Failing formal metrics: eta_waveform.nrmse, qn_waveform.nrmse, Qn_waveform.nrmse, qbar_waveform.nrmse, eta_distributed_common_support.nrmse, qn_distributed_common_support.nrmse.

Temporal convergence remains gated and was not started. No observations were used, no calibration was performed, and Local3D convergence was not started.
