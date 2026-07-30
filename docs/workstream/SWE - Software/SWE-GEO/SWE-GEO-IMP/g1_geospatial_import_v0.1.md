# G1 Geospatial Import v0.1

WBS mapping: `SWE-GEO-IMP-WP1` implements the first sample geospatial import slice through Tasks `SWE-GEO-IMP-WP1-T1`, `SWE-GEO-IMP-WP1-T2` and `SWE-GEO-IMP-WP1-T3`. It consumes the accepted `DatasetManifest`, `DatasetRecord`, `DatasetAsset`, `DatasetSpatialReference`, `ContentDigest` and `CaseRevisionRef` contracts from `tsunami_data`; it does not redefine dataset provenance or case identity.

G1 introduces a method-neutral `tsunami_geo` static target and an optional `tsunami_geo_gdal` static adapter target behind `TSUNAMI_ENABLE_GEOSPATIAL`. The neutral target owns imported raster/vector value models, native spatial-reference records, datum evidence, asset resolution, validation and canonical import-record serialisation. The adapter owns GDAL calls for the selected sample formats only and does not expose GDAL, OGR, OSR, PROJ, Qt, HDF5 or solver types in public domain headers.

Accepted formats are:

| Kind | Driver | Extensions | Media types |
| --- | --- | --- | --- |
| Raster | `GTiff` | `.tif`, `.tiff` | `image/tiff`, `image/geotiff`, `application/geotiff` |
| Vector | `GPKG` | `.gpkg` | `application/geopackage+sqlite3`, `application/geopackage`, `application/octet-stream` |

Import requests supply a case root, validated manifest, dataset ID, optional asset ID, explicit datum-source evidence, resource limits, caller-supplied execution timestamp, import ID and import revision. Asset resolution accepts only manifest-managed paths beneath the canonical case root. External URIs, absolute paths, path traversal, symlink escape, unsupported media types, representation mismatch and missing files fail before GDAL publishes any imported data.

Raster import accepts exactly one scalar real-valued band and captures width, height, cell count, all six affine transform coefficients, corner-derived extent, cell registration, native data type, row-major values, nodata mask, scale and offset. Rotation terms are preserved and extent is computed from all four transformed corners.

Vector import accepts one explicit or unambiguous GeoPackage layer with 2D point, linestring or polygon features and integer, integer64, real, boolean and string attributes. It preserves source feature IDs, feature iteration order and source schema order. Multiparts, collections, curves, measured coordinates, 3D coordinates, unsupported fields and resource-limit excesses fail with deterministic diagnostics.

The importer preserves native coordinates and values. It does not transform, reproject, resample, interpolate, merge, crop, normalise units, fill nodata, smooth terrain, infer datums, download data, compute checksums or produce solver-ready fields. Digest status in G1 records is therefore `not_verified`.

Native CRS metadata is captured as a method-neutral spatial-reference record: authority name/code, CRS name, datum name, WKT2, axis names, axis directions, axis units and coordinate epoch where available. The source CRS is preserved as evidence for `SWE-GEO-CRS-WP1`; it is not replaced by the case computational CRS in this increment.

Horizontal and vertical datum evidence are separate. Accepted statuses are `authoritative_declared` and `dataset_declared`; `inferred`, `unknown` and `conflicting` evidence is rejected where the component is required. Bathymetry, topography, earthquake-displacement and prescribed-surface roles require vertical evidence with explicit units and positive direction.

The consistency gate compares manifest declarations, embedded asset metadata and supplied evidence. Authority-code, datum-name, unit, axis, coordinate-epoch, station and vertical-positive conflicts fail. No default is applied for WGS84, JGD, mean sea level, chart datum or vertical sign based on geography, provider, file type or value sign.

Canonical import records are metadata/provenance artefacts, not full data payloads. The default record path is `manifests/imports/<dataset_id>/<asset_id>.json`; writes are transactional and emit UTF-8, two-space JSON with stable field order, explicit nulls, LF endings and one final newline.

Downstream handoff: `SWE-GEO-CRS-WP1` receives native raster/vector values, source CRS metadata, axes, units, extent, resolution, nodata and datum-source provenance without re-reading or redefining source provenance. `SWE-GEO-TER-WP1` remains responsible for later terrain preparation after CRS/datum decisions are explicit.
