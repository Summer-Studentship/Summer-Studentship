# C1A-R1 Regional2D Production Discretisation Decision

Status: not selected.

C1A-R1 recovered the geospatial runtime, passed the real preprocessing smoke, rebuilt/proved the release Regional2D executable, and completed one real Regional2D spatial level (`L0`, 1000 m / 1000 m mesh, 600 s).

The next spatial candidate (`L1`, 750 m / 500 m mesh) was stopped as resource-limited after reaching 3.88489347290889 s of the requested 600 s horizon. Its observed mean timestep was 0.026978426895200622 s, projecting to hours for one spatial member. This left fewer than three valid spatial levels, so Richardson/GCI, pairwise convergence metrics, temporal convergence, and production-discretisation selection are not scientifically justified.

No physical calibration was performed. No observational data were used. No Local3D convergence was started.

ETOPO caveat: any later mesh refinement measures discretisation convergence of the interpolated ETOPO-derived terrain representation; it does not create additional bathymetric information beyond the source product.
