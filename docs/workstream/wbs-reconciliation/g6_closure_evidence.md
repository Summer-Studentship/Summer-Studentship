# G6 R2 Closure Evidence

G6 implementation: `complete`.

Acceptance status: `real_kamaishi_acceptance_passed`.

Calibration remains unstarted.

## Resolved

- `G6-L3D-BC-001`: resolved. Replay schema `1.1.0` adds production `open_ocean_damped`; legacy `1.0.0` normalises to `symmetry_test`.
- `G6-L3D-BC-002`: resolved. Deterministic reflection benchmark passes: reflective control `Kr=0.8199788627`, outlet `Kr=0.0799979378`, lateral `Kr=0.1000003891`.
- `G6-L3D-WLF-001`: resolved. Production walls use `kqRWallFunction`, `omegaWallFunction`, `nutUSpaldingWallFunction`; `yPlus` evidence is generated and validated.
- `G6-L3D-TIM-001`: resolved for the G6 baseline. Production timestep policy records repository/OpenFOAM ownership, implicit-diffusion disposition and diagnostic viscous margin. Formal timestep convergence remains post-G6; exact rollback/retry supervision remains an optional post-G6 extension.

## Evidence

- Foundation 11 authority: `docs/workstream/SWE - Software/SWE-L3D/g6_openfoam_authority_record.json`
- Reflection benchmark: `docs/workstream/SWE - Software/SWE-L3D/g6_boundary_reflection/`
- Synthetic OpenFOAM smoke: `docs/workstream/SWE - Software/SWE-L3D/g6_synthetic_openfoam_smoke/`
- G5/G6 Regional2D prefix equivalence: `/home/helios/SimulationData/Summer-Studentship/g5-reference-600/evidence/g5_g6_prefix_equivalence.json`
- Kamaishi Local3D acceptance: `/home/helios/SimulationData/Summer-Studentship/g6-kamaishi/evidence/g6_openfoam_acceptance.json`

## Post-G6 Work

Formal mesh/timestep convergence, observational validation, calibration, HDF5,
HPC/GPU work, adaptive meshing, official barrier geometry and publication
visualisation remain outside G6.
