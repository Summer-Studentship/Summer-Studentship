# Summer Studentship

Research and prototype software for tsunami-barrier simulation, numerical
verification, data handling and visualisation.

The active case study is the 2011 Tohoku tsunami with a Kamaishi nearshore
forcing corridor. The current modelling architecture is a regional
two-dimensional nonlinear shallow-water-equation solver driving a local
three-dimensional URANS/VOF barrier-interaction model through one-way replay
coupling. FSI, scour, structural damage, machine learning and generative
optimisation remain deferred until the hydrodynamic and validation foundation is
accepted.

## Current Numerical Status

Regional2D is the active production numerical path. It is a cell-centred
finite-volume NLSWE solver with hydrostatic reconstruction, Rusanov flux,
SSPRK(3,3), wetting/drying, Manning friction, open/radiation boundaries,
sponge relaxation and a fixed Regional-to-Local coupling section.

Verification status:

- first-order Regional2D implementation: globally first-order verified;
- `limited_linear` reconstruction: exact and smooth tests verified as
  second-order in C1A-R9;
- C1A-R10 frozen Tohoku-Kamaishi h600/h500/h400 event family:
  `APPROACHING_SPATIAL_QUALIFICATION`, not spatially qualified;
- h300 limited-linear event run: next numerical gate, no success assumed here;
- temporal convergence: gated until spatial qualification;
- observational validation and calibration: not yet performed;
- Local3D production replay: not launched from the unresolved Regional2D
  numerical gate.

## Repository Map

- `apps/` - application entry points and historical GUI prototype material.
- `src/` - C++ solver, data, geometry, FVM and case-runner libraries.
- `tests/` - C++ Catch2/CTest tests plus Python verification tests.
- `tools/` - reproducible data, verification, result-storage and plotting
  utilities.
- `docs/workstream/RES - Research/` - research decisions and methodology.
- `docs/workstream/SWE - Software/` - Software WBS, contracts and evidence.
- `deliverables/` - report/poster/figure deliverables.
- `data/` - local bulk data organisation; large data are ignored by Git.

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

## Result Storage

Scientific Regional2D outputs use a manifest-first run directory layout. The
run manifest is authoritative; directory names are navigational only.

Primary result-system documents:

- [Regional2D result layout v0.1](docs/workstream/SWE%20-%20Software/SWE-DAT/SWE-DAT-SCH/regional2d_result_layout_v0.1.md)
- [Regional2D HDF5 result schema v1.0.0](docs/workstream/SWE%20-%20Software/SWE-DAT/SWE-DAT-SCH/regional2d_hdf5_schema_v1.0.0.md)
- [Regional2D XDMF handoff v0.1](docs/workstream/SWE%20-%20Software/SWE-DAT/SWE-DAT-XDMF/regional2d_xdmf_handoff_v0.1.md)

The initial HDF5 schema is `tsunami.regional2d.result` version `1.0.0`.
Authoritative state fields are `h`, `qx` and `qy` with time-major,
cell-minor layout. Derived fields such as `eta`, velocity and `|q|` are
computed by post-processing unless a future schema makes them authoritative.

Large simulation outputs remain outside Git. Checked-in examples are tiny,
deterministic and clearly labelled synthetic fixtures.

## Visualisation And ParaView

`tools/results/regional2d_xdmf.py` generates XDMF descriptors that reference
`regional2d.h5` for ParaView-compatible inspection. `tools/results` also
provides a small `ResultDataset` API and Matplotlib proof-of-concept figures for
mesh, fields, coupling, Qn history, convergence and method comparison.

Synthetic proof-of-concept figures live under:

```text
docs/workstream/SWE - Software/SWE-DAT/examples/runs/synthetic-regional2d/synthetic-fixture/figures/
```

They are not Tohoku evidence.

## Known Limitations

- C1A-R10 did not spatially qualify the Regional2D event forcing.
- h300 is the next gate; no h250 or finer run is launched automatically.
- Temporal convergence remains gated until spatial qualification.
- No observational validation, calibration or Local3D production replay is
  authorised by the current Regional2D numerical evidence.
- The HDF5/XDMF stack is a result-storage and visualisation layer; it does not
  modify the NLSWE mathematical model, fluxes, timestep logic or solver state.
- Runtime integration of the HDF5 writer into the C++ solver is deferred until
  schema, conversion and equivalence evidence are reviewed.
