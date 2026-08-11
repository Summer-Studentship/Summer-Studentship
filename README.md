# Summer Studentship

Research software, evidence and publication assets for a tsunami modelling
studentship centred on the 2011 Tohoku event and a Kamaishi nearshore forcing
corridor.

The project demonstrates a one-way hybrid modelling framework: a Regional2D
nonlinear shallow-water solver supplies replay forcing for a Local3D
OpenFOAM-based URANS/VOF barrier-interaction model. The repository is now in a
closure state for the studentship: the strongest verified claims are preserved,
the unresolved claims are labelled, and the final figure and audit package are
recorded under `docs/project/`.

## Current Status

| Area | Closure status |
| --- | --- |
| Mathematical model to implementation | `MODEL_CONSISTENT_WITH_DOCUMENTATION_FIXES`; no accepted core model-code mismatch found. |
| Regional2D formal order | `GLOBAL_FIRST_ORDER_VERIFIED` for the baseline and `SECOND_ORDER_VERIFIED` for `limited_linear` verification cases. |
| Real-event Regional2D result | R10 h400 `limited_linear`, `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`. |
| Real-event spatial qualification | Not spatially qualified; h400 to h300 behaviour exposed a terrain/projection fidelity limit. |
| Regional fidelity diagnosis | `TERRAIN_SOURCE_FIDELITY_DOMINANT`, confidence `MODERATE`, with `PROJECTION_FIDELITY_CEILING`. |
| Local3D hybrid framework | G6 one-way replay framework implemented and demonstrated; current R10-h400 replay remains `REPLAY_VOF_BEHAVIOUR_UNRESOLVED`. |
| Historical validation | Not claimed. R15 found 29 observations: 0 `DIRECT`, 1 `PROXY`, 28 `TARGET_ONLY`. |
| Publication figures | R16 QGIS figure package complete; R17 adds a 6200 px Blender 3D bathymetry/topography render. |

The real-event numerical result is suitable as best-available evidence with
explicit uncertainty labels. It is not a calibrated, historically validated or
spatially converged prediction.

## Key Findings

- Regional2D implements the accepted NLSWE finite-volume model with
  hydrostatic reconstruction, Rusanov flux, SSPRK time integration,
  wetting/drying, Manning friction, open/radiation boundaries, sponge
  relaxation and a fixed Regional-to-Local coupling section.
- The second-order `limited_linear` reconstruction is verified on exact and
  smooth tests, but the Tohoku-Kamaishi real-event mesh family is limited by
  bathymetry/source/projection fidelity rather than formal solver order.
- Kamaishi remains the correct target for the studentship because it is the
  selected nearshore/corridor case. The available R15 observations do not give a
  direct comparison inside the frozen corridor: NOWPHAS 802G is about 12.3 km
  outside it, and DART 21418 is about 545 km outside it.
- The Local3D framework is real and demonstrated on the accepted G6 baseline.
  No claim is made that the current R10 h400 forcing has produced an accepted
  current-generation 300 s Local3D replay.

## Primary Closure Documents

- [R17 studentship status](docs/project/r17_studentship_status.md)
- [R17 repository audit](docs/project/r17_repository_audit.md)
- [R17 repository map](docs/project/r17_repository_map.md)
- [R17 repository review register](docs/project/r17_repository_review_register.md)
- [R17 proposed repository structure](docs/project/r17_proposed_repository_structure.md)
- [R17 deliverables register](docs/project/r17_deliverables_register.md)

## Repository Guide

- `src/` - C++ solver, FVM, geometry, coupling, data and case-runner libraries.
- `apps/` - command-line, benchmark, case-runner and GUI entry points.
- `tests/` - C++ Catch2/CTest tests plus Python verification, validation and
  result-storage tests.
- `tools/` - reproducible verification, validation, GIS, OpenFOAM, result and
  figure-generation utilities.
- `docs/workstream/RES - Research/` - research sources, model context and
  scientific workstream evidence.
- `docs/workstream/SWE - Software/` - software WBS, architecture, verification,
  result schema and method evidence.
- `docs/project/` - R17 closure entry points, repository audit and future
  rationalisation recommendations.
- `deliverables/figures/r16_publication/` - QGIS publication GIS package.
- `deliverables/figures/r17_closure/` - final Blender 3D corridor
  bathymetry/topography figure package.
- `data/` - local data organisation notes; large runtime data remain outside
  Git unless explicitly checked in as small fixtures.

## Result Architecture

Scientific Regional2D outputs use a manifest-first run directory layout. The
run manifest is authoritative; directory names are navigational.

Primary result-system documents:

- [Regional2D result layout v0.1](docs/workstream/SWE%20-%20Software/SWE-DAT/SWE-DAT-SCH/regional2d_result_layout_v0.1.md)
- [Regional2D HDF5 result schema v1.0.0](docs/workstream/SWE%20-%20Software/SWE-DAT/SWE-DAT-SCH/regional2d_hdf5_schema_v1.0.0.md)
- [Regional2D XDMF handoff v0.1](docs/workstream/SWE%20-%20Software/SWE-DAT/SWE-DAT-XDMF/regional2d_xdmf_handoff_v0.1.md)

The HDF5 schema is `tsunami.regional2d.result` version `1.0.0`.
Authoritative state fields are `h`, `qx` and `qy` with time-major,
cell-minor layout. Derived fields such as `eta`, velocity and `|q|` are
computed by post-processing unless a future schema makes them authoritative.

## Build And Test

The project uses CMake, Ninja, C++20 and vcpkg. The recovered Linux GCC/vcpkg
workflow is the current verified production route.

```sh
cmake --build build/linux-gcc-crs-test --target tsunami_tests tsunami_r2d_case
build/linux-gcc-crs-test/tests/tsunami_tests "[r2d-file-runner]"
build/linux-gcc-crs-test/tests/tsunami_tests "[r9]"
```

The release Regional2D case runner is:

```text
build/linux-gcc-crs-release/apps/r2d_case/tsunami_r2d_case
```

It supports:

```text
--reconstruction TEXT:{first_order,limited_linear}
```

Python result-storage and visualisation tests require `h5py`, `numpy` and
Matplotlib:

```sh
python -m unittest tests.results.test_regional2d_result \
  tests.results.test_regional2d_xdmf \
  tests.results.test_regional2d_visualisation
```

## Visual Toolchain

R16 provides the publication GIS package. R17 adds a Blender render of the same
authoritative ETOPO 2022 WGS84 + EGM2008 terrain lineage:

- final PNG: `deliverables/figures/r17_closure/publication/figure_C_corridor_bathymetry_3d.png`
- final PDF: `deliverables/figures/r17_closure/publication/figure_C_corridor_bathymetry_3d.pdf`
- selected vertical exaggeration: 4x, chosen after inspecting 2x, 4x and 6x
  candidates
- caveat: this is terrain visualisation only, not a 3D fluid simulation

Regeneration commands are recorded in
`deliverables/figures/r17_closure/provenance/figure_C_corridor_bathymetry_3d.provenance.json`.

## External Data

Large simulation and validation data live outside Git under local data roots
recorded in manifests. The checked-in repository preserves small fixtures,
scripts, evidence summaries, hashes and figure outputs. No new DEM was
downloaded for R17; the 3D render derives from the R16 ETOPO 2022 terrain
lineage.

## Limitations

- No real-event mesh convergence, temporal convergence or calibration is
  claimed.
- No historical validation is claimed.
- The R10 h400 `limited_linear` result remains
  `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`.
- R10-h400 Local3D current-generation replay remains
  `REPLAY_VOF_BEHAVIOUR_UNRESOLVED`.
- FSI, scour, structural damage, ML optimisation, dispersive regional physics,
  two-way coupling and full impact/load qualification remain future work.

## Future Work

The next scientific work should acquire direct Kamaishi/NOWPHAS or harbour
waveform evidence, resolve the Local3D boundedness gate for current forcing,
and only then reconsider calibration, temporal convergence or production
barrier-load studies. Repository rationalisation should follow
`docs/project/r17_repository_review_register.md` and must remain
non-destructive until each candidate is reviewed.
