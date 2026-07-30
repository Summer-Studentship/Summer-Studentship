# G1 Case Schema v1.0.0

- **Deliverable:** `SWE-DAT-CFG`
- **Work Package:** `SWE-DAT-CFG-WP1`
- **Schema:** `schemas/case/1.0.0/case.schema.json`
- **Authoritative file:** `case.json`
- **Schema name:** `tsunami.case_configuration`
- **Policy version:** `0.1`
- **Document date:** 2026-07-30

## Scope

This note records the G1 JSON-only case-configuration contract. The contract
captures the minimum simulation case structure needed to identify a case,
bind dataset identifiers, describe the Regional2D corridor request, select
physics and numerical controls, and configure output cadence.

The work deliberately excludes YAML parsing, dataset-manifest content,
geospatial ingestion, HDF5/XDMF persistence, GUI editing and Python-side
validators. Dataset identifiers are validated as bindings only; the dataset
manifest workstream resolves their provenance and file locations later.

## Public API

`tsunami_data` now exposes immutable, value-style C++ records through:

```text
src/data/include/tsunami/data/CaseConfiguration.hpp
src/data/include/tsunami/data/CaseConfigurationParsing.hpp
src/data/include/tsunami/data/CaseConfigurationValidation.hpp
src/data/include/tsunami/data/CaseConfigurationSerialisation.hpp
```

The public API depends only on `tsunami_core`. JSON remains a private
implementation detail behind parsing and serialisation.

## Compatibility

The supported schema version is `1.0.0`.

| Input version | Classification | Behaviour |
|---|---|---|
| `1.0.0` | `exact` | accepted |
| `1.0.x` | `patch_equivalent` | accepted |
| `1.y.z`, `y > 0` | `forward_compatible_minor` | accepted with extension preservation |
| `0.x.y` | `migration_required` | rejected with migration diagnostic |
| `>1.x.y` | `unsupported_major` | rejected |

Unknown core fields are rejected. Unknown forward-minor material belongs in the
top-level `extensions` object and is preserved as canonical JSON.

## Semantic Rules

Validation checks deterministic identifiers, positive revisions, UTC creation
timestamps, safe relative manifest paths under `manifests/`, dataset-binding
consistency, corridor extents, sponge and narrowing rules, source configuration,
earthquake/prescribed-surface consistency, timestep bounds, boundary kinds,
relaxation coupling and output cadence.

Failures return `tsunami::core::Error` through `Result` with stable diagnostic
codes, JSON pointers, rule identifiers and `state_changed=false`.

## Canonical Serialisation

`serialise_case_configuration` writes deterministic two-space JSON with one
trailing line feed. `write_case_configuration` writes via a sibling temporary
file before committing the target path. The authoritative case file remains
`case.json`.
