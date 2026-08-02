# File-Driven Regional2D Runner Verification v0.1

WBS: SWE-VER / SWE-VER-REG / SWE-VER-REG-WP1-T3

## Purpose

The file-driven Regional2D runner verifies the first production trust boundary from accepted case artefacts to deterministic Regional2D CSV outputs. The runner composes existing domain and adapter contracts; it does not introduce new schemas, HDF5/XDMF persistence, OpenFOAM, Local3D, coupling or GUI ownership.

## Canonical Input Layout

The runner starts from an accepted case root containing `case.json`. The case configuration names the dataset manifest path. The runner then consumes a corridor construction record, a terrain conditioning record, the strict three-file conditioned-terrain GeoTIFF bundle, and a Gmsh MSH 4.1 mesh file. Caller-provided override paths are case-relative.

## Producer Path

The deterministic success fixture uses transformed epicentre and target point evidence, `construct_corridor`, `write_corridor_construction_record`, the accepted terrain-conditioning producer, the strict terrain GeoTIFF bundle writer, and the accepted Gmsh importer. Injected-invalid records are reserved for negative tests that intentionally target rejected persisted contracts.

## Containment Model

All inputs are resolved beneath the canonical case root. The runner rejects absolute paths, `..`, missing files, non-regular files, symlink escapes and filesystem-query failures before opening the input. Terrain bundle paths are derived from the accepted terrain record and each final artefact is physically validated beneath the case root before GDAL read-back.

The output path is `runs/<run-id>/outputs/regional2d`. The `RunId` is the core strong ID and must also satisfy the accepted logical-ID syntax and length bound. Existing output-path components are walked before writer construction; symlinks are accepted only when their resolved location remains inside the case root.

## Provenance Binding

The loaded manifest must own every source provenance reference used by corridor and terrain evidence. Corridor reference-point transformations must match the case revision and loaded manifest identity, and their source dataset and asset must exist in the manifest. Terrain bathymetry and topography import, transformation and resampling identities must agree with the same manifest, datasets and assets. Generated-output checksum registration remains deferred under parent #230.

## Parameters And Physics

The runner requires explicit preparation and transfer policies: pre-event free surface, dry-depth and depth/normal/zero-momentum tolerances, transfer area tolerances and maximum terrain contributors. Uniform Manning and constant Coriolis are supported. Earthquake artefacts, prescribed-surface mode, dataset Manning and dataset Coriolis are rejected before output creation.

## Output Ownership

CSV outputs are owned by the run directory. Existing nonempty outputs are preserved unless overwrite is explicitly enabled. Writer preparation checks all filesystem operations; CSV writes flush and report stable failures. Once directory or file state changes, subsequent runner failures report `state_changed=true` and retain partial outputs for diagnosis.

## Deterministic Evidence

The lake-at-rest fixture exercises absolute and relative case roots, default and explicit corridor record paths, producer-derived corridor and terrain evidence, Gmsh import, path containment, overwrite replacement, output failure propagation and byte-identical scientific CSV outputs across run IDs. Final evidence checks final time, maximum steps, mesh binding, water volume and zero integrated momentum within accepted policies.

## CLI Usage

`tsunami_r2d_case` accepts `--case-root`, `--terrain-record`, `--mesh`, optional `--corridor-record`, `--run-id`, explicit preparation and transfer policy options, and `--overwrite`. Success prints case, manifest, run, corridor, terrain, mesh, step, final-time and output-directory summaries. Failure prints error code, message, cause code when present, and deterministic context entries in key order.

## Limitations And Handoffs

This PR intentionally does not register generated terrain or Regional2D outputs as complete manifest entries with generated-output checksums. That remains parent #230 work. Earthquake displacement artefact persistence, HDF5/XDMF outputs, Local3D, coupling and GUI integration are also deferred to their owning work packages.
