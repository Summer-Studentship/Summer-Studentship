# Regional2D File-Driven Earthquake Coupling v0.1

This slice binds generated earthquake displacement artifacts into file-driven Regional2D runs and exports a coupling section from the Regional2D snapshot stream.

## Earthquake Artifact

The supported artifact is a single-band GeoTIFF plus JSON metadata record:

- GeoTIFF band description: `vertical_seabed_displacement`
- GeoTIFF band unit: `m`
- JSON contract version: `1`
- Required metadata: `event_id`, `model_id`, `source_format`, `coordinate_reference`, `subfault_count`, `vertical_unit`

The GeoTIFF grid must match the accepted conditioned-terrain grid. The dataset manifest must bind the artifact as a generated raster dataset with role `earthquake_displacement`, a primary managed-path GeoTIFF asset, and a metadata managed-path JSON asset.

The locked Tohoku finite-fault source is:

```text
https://earthquake.usgs.gov/archive/product/finite-fault/usp000hvnu/us/1539808472261/basic_inversion.param
```

It is documented under `data/source/earthquake/README.md`; no build, test, or runtime path downloads it.

## Runner Behavior

When earthquake physics is disabled, the runner still requires a strict final lake-at-rest state. When earthquake physics is enabled, the runner reads and transfers the displacement artifact, invokes the existing Regional2D earthquake initialisation path, and validates that the final dynamic state remains finite and physically admissible.

Prescribed free-surface transfer remains unsupported by the file-driven runner.

## Coupling Export

`tsunami_r2d_case --coupling-section <boundary.patch>` exports:

```text
runs/<run-id>/outputs/regional2d/coupling/<section-id>/metadata.json
runs/<run-id>/outputs/regional2d/coupling/<section-id>/samples.csv
runs/<run-id>/outputs/regional2d/coupling/<section-id>/history.csv
```

The export samples the requested mesh boundary patch from the existing Regional2D snapshots. `samples.csv` contains per-sample depth, momentum, bed elevation, and free-surface elevation. `history.csv` contains compact per-snapshot section summaries.
