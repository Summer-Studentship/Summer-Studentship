# Regional2D XDMF Handoff v0.1

Status: draft implementation contract for ParaView-compatible inspection.

Owner: `SWE-DAT-XDMF`

The Regional2D XDMF descriptor is a lightweight XML view over
`regional2d.h5`. It must reference HDF5 datasets and must not duplicate field
arrays.

## Scope

The v0.1 generator supports:

- triangular unstructured topology;
- projected XY geometry;
- temporal collections;
- cell-centred `h`, `qx` and `qy` fields;
- HDF5-backed data items;
- structural validation of referenced paths and shapes.

## Required HDF5 Inputs

The generator consumes `tsunami.regional2d.result` schema v1.x files and uses:

- `/mesh/points`
- `/mesh/cells/connectivity`
- `/time/values`
- `/fields/cell/h`
- `/fields/cell/qx`
- `/fields/cell/qy`

## Validation

`tools.results.regional2d_xdmf.validate_xdmf` verifies:

- XML well-formedness;
- root element and temporal grid count;
- triangular topology;
- XY geometry;
- HDF5 dataset references exist;
- topology dimensions match the HDF5 mesh;
- field shapes are compatible with `(time, cell)`;
- `h`, `qx` and `qy` are present as cell-centred attributes.

## ParaView

When ParaView or `pvpython` is installed, the generated XDMF file should be
opened through a command-line smoke check. If no ParaView runtime is available,
the programme records `PARAVIEW_RUNTIME_NOT_AVAILABLE` after structural XDMF
validation passes.
