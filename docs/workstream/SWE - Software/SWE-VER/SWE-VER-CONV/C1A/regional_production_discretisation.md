# C1A-R4 Regional2D Frozen-Terrain Production Discretisation Decision

Status: not selected.

C1A-R4 executed the frozen G6 terrain/source Regional2D spatial ladder `h1000, h800, h600` to 600 s under study `regional-spatial-frozen-terrain-v4`. The terrain raster, source raster, physical configuration, domain, and coupling section are invariant across all levels.

The medium-to-fine qualification pair is `h600_vs_h800`. Spatial qualification status is `not_spatially_qualified` with a 2% NRMSE threshold. Failing formal metrics: eta_waveform.nrmse, qn_waveform.nrmse, Qn_waveform.nrmse, qbar_waveform.nrmse, eta_distributed_common_support.nrmse, qn_distributed_common_support.nrmse.

Temporal convergence remains gated and was not started. No observations were used, no calibration was performed, and Local3D convergence was not started.

## R10/R11/R12 status

R10 established the completed limited-linear event ladder through `h400`, but
did not qualify the Regional2D event solution for temporal convergence.

R11 added the full `h300` frozen-terrain run and independently combined it
with the R10 `h500`/`h400` evidence. The decisive `h300_vs_h400` pair remained
non-qualified, with qn waveform NRMSE `1.225976541239782`, Qn waveform NRMSE
`0.4784379614303039`, eta distributed NRMSE `0.14317153704071578`, and qn
distributed NRMSE `0.2496688889318955`.

R12 performed a forensic diagnosis only. It rejected a post-processing or
configuration defect and classified the h400-to-h300 divergence as a
physical/numerical spatial-fidelity limitation led by fixed 1000 m
terrain/source support and mesh-dependent bed/source projection. No new
production simulations, h250/h200 runs, temporal convergence, Local3D work,
calibration, or scheme tuning were performed.
