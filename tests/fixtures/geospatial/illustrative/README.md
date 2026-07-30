# G1 geospatial import fixtures

The GDAL integration tests create compact GeoTIFF and GeoPackage assets at runtime under the system temporary directory. This avoids committing binary fixtures while still verifying the real `GTiff` and `GPKG` drivers, native coordinates, values and import-record generation.

Checked-in datum JSON files under `tests/fixtures/geospatial/datum/` document the accepted horizontal and vertical evidence used by those generated fixtures.
