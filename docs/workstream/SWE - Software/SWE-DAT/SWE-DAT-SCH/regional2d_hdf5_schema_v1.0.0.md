# Regional2D HDF5 Result Schema v1.0.0

Status: draft implemented schema for Regional2D scientific simulation results.

Schema name: `tsunami.regional2d.result`

Schema version: `1.0.0`

This schema stores scientific Regional2D results for post-processing,
visualisation and ParaView handoff. It is not a restart or checkpoint format.
Restart state remains a separate `SWE-DAT-CHK` concern.

## Compatibility

Readers must reject unknown major versions. Minor/patch additions may be read
only when required datasets remain present and the reader declares support for
ignoring unknown extensions.

## Root Metadata

The HDF5 root stores:

- `schema_name = tsunami.regional2d.result`
- `schema_version = 1.0.0`
- `schema_major = 1`

`/metadata` attributes include:

- producer
- created UTC timestamp
- `case_id`
- `run_id`
- solver
- Git SHA
- binary SHA-256
- coordinate reference, preferably EPSG and/or WKT
- physical unit registry
- `data_class`: `REAL`, `LEGACY_CONVERTED` or `SYNTHETIC`

## Hierarchy

```text
/
├── metadata/
├── mesh/
│   ├── points
│   ├── cells/
│   │   ├── connectivity
│   │   └── type
│   ├── cell_centres
│   ├── bed_elevation
│   ├── region_tags
│   └── boundary_tags
├── time/
│   └── values
├── fields/
│   └── cell/
│       ├── h
│       ├── qx
│       └── qy
├── coupling/
│   ├── s
│   ├── time
│   ├── eta
│   ├── qn
│   ├── Qn
│   └── qbar
├── diagnostics/
│   ├── time
│   ├── dt
│   ├── water_volume
│   └── cfl
└── provenance/
```

## Mesh

`/mesh/points` stores projected coordinates in metres. For triangular
Regional2D meshes, `/mesh/cells/connectivity` stores zero-based point indices
with shape `(cell, 3)`, and `/mesh/cells/type` stores `triangle` for each cell.
`/mesh/cell_centres`, `/mesh/bed_elevation`, `/mesh/region_tags` and
`/mesh/boundary_tags` provide the minimum post-processing state required by
the current Regional2D workflow. Face topology is intentionally not mandatory
in v1.0.0 because the accepted coupling quantities are stored separately.

## Time And Fields

`/time/values` is in seconds. Authoritative conserved fields are:

- `/fields/cell/h`, units `m`
- `/fields/cell/qx`, units `m^2/s`
- `/fields/cell/qy`, units `m^2/s`

The layout is time-major, cell-minor: `(time, cell)`. Datasets are appendable
along the time dimension and chunked by time slab. Derived fields such as
`eta`, `u`, `v` and `|q|` should be computed by post-processing unless a
future schema version identifies them as authoritative outputs.

## Coupling

The coupling group preserves the existing Regional-to-Local section quantities:

- `/coupling/s`, metres along section
- `/coupling/time`, seconds
- `/coupling/eta`, metres
- `/coupling/qn`, `m^2/s`
- `/coupling/Qn`, `m^3/s`
- `/coupling/qbar`, `m^2/s`

Schema v1.0.0 does not redefine those quantities.

## Diagnostics

Diagnostics are lightweight numerical summaries, not complete logs. Text logs
remain under the run directory `logs/`. Required v1.0.0 diagnostic datasets are
`time`, `dt` and `water_volume`; `cfl` is supported when available.

## Provenance

`/provenance` stores a JSON provenance attribute with at least:

- mesh SHA-256
- terrain SHA-256
- source SHA-256
- Git SHA
- binary SHA-256
- reconstruction policy
- Manning coefficient
- gravity
- boundary policy
- coupling definition
- time interval
- source format for converted legacy results

## Compression

Compression is configurable. The default writer uses uncompressed datasets so
schema validation and numerical equivalence are transparent. Lightweight
compression may be enabled by caller policy after measurement.
