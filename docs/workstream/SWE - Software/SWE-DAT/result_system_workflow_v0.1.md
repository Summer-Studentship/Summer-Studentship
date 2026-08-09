# Regional Result-System Workflow v0.1

Status: draft repository-facing workflow.

This note connects the run layout, HDF5 schema, XDMF descriptor and
visualisation proof of concept.

## Run Layout

Use the canonical manifest-first run tree in
[`SWE-DAT-SCH/regional2d_result_layout_v0.1.md`](SWE-DAT-SCH/regional2d_result_layout_v0.1.md).
The manifest is authoritative for scientific identity, schema versions, input
hashes, executable identity, data classification and provenance files.

## HDF5

The Regional2D scientific result schema is
[`SWE-DAT-SCH/regional2d_hdf5_schema_v1.0.0.md`](SWE-DAT-SCH/regional2d_hdf5_schema_v1.0.0.md).
The implementation is:

```text
tools/results/regional2d_result.py
```

Commands:

```sh
python -m tools.results.regional2d_result write-synthetic /tmp/regional2d.h5
python -m tools.results.regional2d_result validate /tmp/regional2d.h5
python -m tools.results.regional2d_result convert-legacy \
  --output-dir /path/to/legacy/outputs/regional2d \
  --mesh /path/to/r4-h400.msh \
  --hdf5 /tmp/regional2d.h5
```

Converted legacy files must keep `data_class = LEGACY_CONVERTED` and record
source hashes. Synthetic fixtures must keep `data_class = SYNTHETIC`.

## XDMF And ParaView

XDMF handoff is documented in
[`SWE-DAT-XDMF/regional2d_xdmf_handoff_v0.1.md`](SWE-DAT-XDMF/regional2d_xdmf_handoff_v0.1.md).

Commands:

```sh
python -m tools.results.regional2d_xdmf generate /tmp/regional2d.h5
python -m tools.results.regional2d_xdmf validate /tmp/regional2d.xdmf \
  --hdf5 /tmp/regional2d.h5
```

If ParaView is installed, open the XDMF file through ParaView or `pvpython`.
If it is unavailable, record `PARAVIEW_RUNTIME_NOT_AVAILABLE` after structural
XDMF validation passes.

## Visualisation

The plotting layer consumes the `ResultDataset` interface rather than direct
`h5py` calls. The proof-of-concept implementation is:

```text
tools/results/regional2d_visualisation.py
```

Generate the synthetic example hierarchy with:

```sh
python -m tools.results.regional2d_visualisation generate-synthetic-poc \
  docs/workstream/SWE\ -\ Software/SWE-DAT/examples/runs/synthetic-regional2d/synthetic-fixture
```

Generated figures use:

- `figures/mesh/`
- `figures/fields/`
- `figures/coupling/`
- `figures/convergence/`
- `figures/comparisons/`

`figures/publication/` is reserved for explicit manual promotion.

## Provenance Rules

Every scientific figure has a `<figure>.provenance.json` sidecar and an entry
in `figures/index.json`. Minimum provenance includes source run, data
classification, generating script, Git SHA, generation time, input fields,
units and output hash.

No synthetic figure or converted legacy result may be presented as physical
Tohoku evidence.
