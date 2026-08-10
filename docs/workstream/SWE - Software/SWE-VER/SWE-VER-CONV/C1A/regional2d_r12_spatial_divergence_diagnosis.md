# C1A-R12 Regional2D h400 to h300 Spatial Divergence Diagnosis

Status: `PHYSICAL_NUMERICAL_SPATIAL_FIDELITY_LIMITATION_NOT_POSTPROCESSING`.

Independent raw recomputation reproduced the R11 decisive h300-vs-h400 metrics over the exact `245-545 s` window. The decisive NRMSE values are eta waveform `0.504309`, qn waveform `1.22598`, Qn waveform `0.478438`, eta distributed `0.143172`, and qn distributed `0.249669`.

Post-processing/configuration defect classification: `rejected`. Kernel integrity, binary equality, time arrays, coupling geometry, and Qn/qbar reconstruction all pass.

Dominant diagnosis: fixed 1000 m terrain/source support and mesh-dependent source/bed projection form the leading explanation for the h400-to-h300 divergence; no h300 mesh topology spike, CFL/timestep failure, or boundary/sponge configuration change was found.

Recommended next experiment: Run a cheap diagnostic-only source/terrain projection study on h500/h400/h300/h250 without time integration, then one short 60 s h300/h400 replay with limiter activation counters before any new 600 s production mesh.

No new production simulations, h250/h200 runs, temporal convergence, Local3D work, calibration, or scheme tuning were performed.
