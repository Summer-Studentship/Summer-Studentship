# Code Placement Rules v0.1

- **Work Package:** `SWE-ARC-LAY-WP1`
- **Issues:** `#126`, `#127`, `#128`
- **Document date:** 2026-07-22

## Planned Production Layout

```text
src/
|-- core/
|-- fvm/
|-- data/
|-- geo/
|-- r2d/
|-- l3d/
|-- coupling/
|-- application/
`-- adapters/
    |-- io/
    |   |-- hdf5/
    |   `-- xdmf/
    |-- geo/
    |   `-- gdal/
    |-- mesh/
    |   `-- gmsh/
    `-- diagnostics/
        `-- matplot/

apps/
|-- tsunami_cli/
`-- tsunami_gui/

tests/
|-- unit/
|-- acceptance/
|-- benchmarks/
|-- regression/
|-- convergence/
`-- validation/
```

This is a planned placement map. Missing production directories are not created
by this work package.

## Rules

- Public headers live with the owning target and expose only dependencies that
  are valid for that target's public contract.
- Private implementation files stay inside the owning target's source tree.
- Namespace roots follow layer ownership, for example `tsunami::fvm` in
  `src/fvm/` and `tsunami::adapters::hdf5` in `src/adapters/io/hdf5/`.
- Tests mirror owning production components under the appropriate test
  category instead of becoming production dependencies.
- Adapters remain outside domain directories.
- Application orchestration remains separate from GUI and CLI.
- Do not create generic dumping-ground `utils/` directories.
- Do not create cross-domain `common/` directories without explicit target and
  WBS ownership.
- Do not put solver-specific files in `src/core/`.
- Do not place external-library wrappers in domain-model directories.
- Historical prototype directories do not establish production placement.
