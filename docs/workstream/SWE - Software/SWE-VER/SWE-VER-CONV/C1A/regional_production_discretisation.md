# C1A-R2 Regional2D Production Discretisation Decision

Status: not selected.

C1A-R2 diagnosed the abnormal C1A-R1 mesh scaling and completed a resource-aware Regional2D spatial attempt using h2000, h1400, and the reused h1000 fine run. The original L1 anomaly was caused by mixing a 750 m terrain request with a 500 m mesh size; the observed active-cell ratio was 3.85124, close to the 1000/500 mesh-size area ratio of 4, not the requested 1000/750 spacing ratio of 1.77778. The physical corridor/domain remained invariant.

The resource-aware ladder was h2000, h1400, and h1000. h2000 and h1400 completed 600 s solves in 172.468 s and 319.642 s, respectively; h1000 reused the prior completed L0 evidence after mesh, source, case-spec, and binary hash checks. h1800, h1600, and h1500 failed the Regional2D C++ geometry preflight with `r2d.preflight.terrain_support_missing` and were not used as convergence levels.

The fixed 245-545 s forcing-window comparison did not qualify spatial convergence. For h1400 versus h1000, eta NRMSE was 0.220214, qn NRMSE was 0.387176, and the three-support section mean normal-momentum NRMSE was 0.42437; all exceed the recorded qualification thresholds. Richardson/GCI was computable only for peak eta, while qn and section mean qn were non-monotone.

No temporal convergence was run because the spatial gate did not pass. No physical calibration was performed. No observational data were used. No Local3D convergence was started.

ETOPO caveat: these mesh changes measure discretisation behaviour of the interpolated ETOPO-derived terrain representation; they do not add bathymetric information beyond the source product.
