# Regional2D Result Layout v0.1

Status: draft implementation contract for scientific simulation results.

Owner: `SWE-DAT-SCH`

This document defines the canonical on-disk layout for a completed or
in-progress simulation run. The directory tree is a transport and organisation
convention; the run manifest remains authoritative for scientific identity,
provenance, schema versions and input/output relationships.

## Run Identity

A run is identified by:

- `case_id`
- `run_id`
- result schema name and version
- solver and reconstruction policy
- input artefact identities and checksums
- executable and Git provenance

Directory names may repeat `case_id` and `run_id` for human navigation, but no
essential scientific metadata may exist only in path names.

## Canonical Tree

Generated run directories use this logical structure:

```text
runs/
└── <case_id>/
    └── <run_id>/
        ├── manifest.json
        ├── inputs/
        │   ├── case/
        │   ├── terrain/
        │   ├── source/
        │   └── geometry/
        ├── results/
        │   ├── regional2d.h5
        │   ├── regional2d.xdmf
        │   └── coupling/
        ├── diagnostics/
        │   ├── runtime/
        │   ├── verification/
        │   └── convergence/
        ├── figures/
        │   ├── mesh/
        │   ├── fields/
        │   ├── coupling/
        │   ├── convergence/
        │   ├── comparisons/
        │   ├── validation/
        │   ├── diagnostics/
        │   └── publication/
        ├── tables/
        ├── animations/
        ├── logs/
        └── provenance/
            ├── execution.json
            ├── environment.json
            └── checksums.sha256
```

The implementation should create directories only when there is content to
place in them or when a helper is explicitly preparing a run for imminent
writing. Empty committed directories are not part of the contract.

## Manifest

`manifest.json` is the run authority. It must record:

- schema name and version for the manifest itself;
- `case_id`, `run_id`, solver name and solver version if known;
- result files and their schema names/versions;
- scientific input references and checksums;
- executable path, executable checksum and Git SHA;
- data classification: `REAL`, `LEGACY_CONVERTED` or `SYNTHETIC`;
- coordinate reference and unit policy;
- generation timestamps in UTC;
- provenance files and checksum catalogue location.

## Figures

Figures are first-class run outputs under `figures/`. Generated analysis
figures belong to one of:

- `mesh`
- `fields`
- `coupling`
- `convergence`
- `comparisons`
- `validation`
- `diagnostics`

`figures/publication/` is reserved for manually promoted publication-ready
exports. Automatic generation must not write directly there.

Every scientific figure must have either a `<figure>.provenance.json` sidecar
or an entry in `figures/index.json`. Lightweight workflows should provide both.
Minimum provenance fields are:

- figure path;
- figure type;
- source run/result identifier;
- input files and hashes;
- data classification;
- generating script;
- Git SHA;
- generation timestamp;
- fields used;
- time/range used;
- units.

Synthetic proof-of-concept figures must be unambiguously labelled as
`SYNTHETIC` in provenance and should use filenames beginning with
`synthetic_`.

## Large Data Policy

Large simulation data remain outside Git. Repository commits may contain:

- schema documents;
- helper code;
- deterministic tiny fixtures;
- generated SVG proof-of-concept figures when they are small and clearly
  labelled;
- validation records and issue-reconciliation documents.

Production Regional2D scientific results are stored as `regional2d.h5` plus a
referencing `regional2d.xdmf` descriptor. Legacy CSV output remains supported
until numerical equivalence and downstream compatibility have been established.
