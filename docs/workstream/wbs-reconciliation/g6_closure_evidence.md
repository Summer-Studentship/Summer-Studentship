# G6 R2 Closure Evidence

Implementation status: `implementation_complete`.

Acceptance status: `real_kamaishi_acceptance_pending`.

Calibration remains unstarted.

## Implemented

- `G6-L3D-BC-001`: replay schema `1.1.0` adds production `open_ocean_damped`; legacy `1.0.0` normalises to `symmetry_test`.
- `G6-L3D-BC-002`: deterministic reflection benchmark passes: reflective control `Kr=0.8199788627`, outlet `Kr=0.0799979378`, lateral `Kr=0.1000003891`.
- `G6-L3D-WLF-001`: production walls use `kqRWallFunction`, `omegaWallFunction`, `nutUSpaldingWallFunction`; `yPlus` evidence is generated and validated.
- `G6-L3D-TIM-001`: production timestep policy records repository/OpenFOAM ownership, implicit-diffusion disposition, diagnostic viscous margin, and the missing exact rollback/retry supervisor.

## Evidence

- Foundation 11 authority: `docs/workstream/SWE - Software/SWE-L3D/g6_openfoam_authority_record.json`
- Reflection benchmark: `docs/workstream/SWE - Software/SWE-L3D/g6_boundary_reflection/`
- Synthetic OpenFOAM smoke: `docs/workstream/SWE - Software/SWE-L3D/g6_synthetic_openfoam_smoke/`
- Kamaishi rerun attempt: `docs/workstream/SWE - Software/SWE-L3D/g6_kamaishi_rerun_attempt.json`

## Remaining Blocker

The accepted Regional2D artifacts were not present in this checkout. A fallback
`etopo-1000m` regeneration was started with a temporary `/tmp` Python
environment containing `rasterio`, `pyproj` and `clawpack==5.14.0`. After about
fourteen minutes the Regional2D run had reached only `198.1 s` of the required
`1800 s`, so the temporary run was interrupted before Local3D generation. The
required real no-defence and rigid-barrier `300 s` Local3D acceptance therefore
remains unproven locally for this branch.
