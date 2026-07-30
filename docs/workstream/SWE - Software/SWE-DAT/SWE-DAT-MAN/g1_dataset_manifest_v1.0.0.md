# G1 Dataset Manifest v1.0.0

## WBS Mapping

`SWE-DAT-MAN-WP1` implements the dataset and provenance manifest under deliverable `SWE-DAT-MAN`. The authorised tasks are `SWE-DAT-MAN-WP1-T1` for typed records, `SWE-DAT-MAN-WP1-T2` for validation and lineage, and `SWE-DAT-MAN-WP1-T3` for fixtures.

## Scope

The manifest records dataset identity and provenance after `case.json` selects logical dataset bindings. It owns provider records, licence records, source and generated dataset records, physical asset metadata, SHA-256 digest metadata, spatial references, resolution, uncertainty, processing records, generated-artifact lineage and case-to-manifest binding validation.

It does not own case physics, numerics, corridor geometry, downloading, hashing, geospatial import, CRS transformation, vertical-datum transformation, terrain conditioning, HDF5 persistence, solver preparation or GUI inspection.

## Format

The authoritative default path is `manifests/datasets.json`. The media type is `application/json`. The schema identity is `tsunami.dataset_manifest`, version `1.0.0`, policy version `0.1`, using JSON Schema draft 2020-12.

Runtime schema compatibility is:

- `1.0.0`: exact.
- `1.0.x`: patch equivalent.
- `1.y.z`: forward-compatible minor, provided unknown content is confined to declared `extensions` objects.
- `0.x.y`: migration required and not published.
- `2.x.y` and later: unsupported major and not published.

## Manifest Identity

`DatasetManifestIdentity` binds one immutable manifest revision to one immutable `CaseRevisionRef`. `manifest_id` follows the ASCII logical-ID pattern `[a-z0-9]+(?:[._-][a-z0-9]+)*`, `manifest_revision` and `case_revision` are positive, `created_at_utc` is `YYYY-MM-DDTHH:MM:SSZ`, and `created_by` is nonempty.

## Records

Providers identify provenance sources with `provider_id`, display name, optional organisation, optional credential-free absolute homepage URI and extensions. Provider records describe provenance only and do not imply endorsement.

Licences define `licence_id`, name, nonempty expression, optional credential-free absolute URI, optional attribution and extensions. The implementation does not parse complete SPDX expressions or decide legal suitability.

Datasets carry one or more roles: `bathymetry`, `topography`, `earthquake_displacement`, `prescribed_surface`, `manning`, `coriolis`, `observation` and `auxiliary`. Roles are unique per dataset and serialised by stable string order. Representation metadata is descriptive only: `raster`, `vector`, `point_series`, `table`, `multidimensional` or `other`.

Source datasets require source acquisition metadata and no producer. Generated datasets require `generated_by_process_id`, no source acquisition record, project-computed digests, managed asset paths and exactly one declared processing producer. Generated records do not inherit provider or licence metadata.

## Assets

Each dataset has one primary asset and may have metadata or auxiliary assets. Asset IDs are logical IDs. Media types must resemble `type/subtype`, byte sizes are positive when present, and each asset has SHA-256 digest metadata.

Managed paths are relative, lexically normal, use `/` in canonical JSON, contain no `..`, root name or root directory, and remain under `inputs/data/`. External URIs are absolute, credential-free and not dereferenced. The manifest validates digest syntax only: SHA-256 values are exactly 64 lower-case hexadecimal characters.

## Spatial Metadata

Spatial applicability is either `spatial` or `not_applicable`. Spatial datasets require horizontal CRS, horizontal unit and axis order. Bathymetry, topography, earthquake displacement and prescribed-surface roles also require vertical datum, vertical unit and vertical-positive convention. Native source conventions are preserved; no EPSG, GDAL or PROJ lookup is performed.

Spatial resolution supports `grid_spacing`, `nominal`, `irregular`, `not_reported` and `not_applicable`. Temporal resolution supports `static_dataset`, `interval`, `irregular`, `not_reported` and `not_applicable`. The manifest records status explicitly and does not judge scientific sufficiency.

Uncertainty is `reported`, `estimated`, `not_reported` or `not_applicable`. Reported and estimated uncertainty require at least one measure or a description. Not-reported and not-applicable statuses carry no numerical measures.

## Processing And Lineage

Processing records contain a logical process ID, operation identifier, UTC execution time, software name/version, optional repository URI, optional commit SHA, canonical JSON object parameters, input dataset IDs, output dataset IDs and extensions. Inputs and outputs are sorted canonically and must be disjoint.

Generated lineage is a bipartite graph from dataset to process inputs and process outputs back to generated datasets. Validation requires source datasets to have zero producers, generated datasets to have exactly one producer, producer declarations to agree with process outputs, no multiple producers, no direct or multi-step cycles and every generated dataset to be recursively source-rooted. The implementation uses deterministic colour-marked traversal bounded by the dataset count.

## Case Binding

`validate_dataset_manifest_for_case` compares the manifest case ID and revision with an already parsed `CaseConfiguration`, then verifies every active logical binding resolves to a manifest dataset carrying the required role. It does not reparse files, load assets, transform CRS or mutate either input.

## Serialisation And Diagnostics

Canonical serialisation revalidates the manifest, builds ordered JSON, sorts set-like records and opaque extensions deterministically, emits nullable fields explicitly as `null`, uses two-space indentation, LF line endings and exactly one terminal newline. Transactional writing serialises in memory, writes a sibling temporary file, flushes, closes, atomically replaces where supported and preserves the existing target on failure.

Diagnostics use `tsunami::core::Error` and stable codes under `data.dataset_manifest.*`. Expected failures use validation or input-data categories, severity `error`, deterministic context and `state_changed=false`.

## Fixtures

The illustrative Tohoku/Kamaishi corridor manifest and case pair is illustrative and non-authoritative. It is not suitable for scientific validation of the 2011 Tohoku event. It exists to demonstrate raw source records, generated corridor bathymetry/topography lineage, earthquake-displacement binding and observation binding without inventing authoritative provider claims.

## Handoffs

Downstream geospatial import adapters can consume validated dataset IDs, media types, source CRS/datum metadata, managed/external asset locations and digest metadata. Downstream HDF5 schema work can bind persistent artefacts to dataset IDs, case revisions, asset digests and generated-processing lineage. Solver preparation and GUI inspection remain separate work packages.
