# Datum Source Register v0.1

This register records the accepted evidence model for the G1 geospatial import slice. It classifies source authorities by scope so imports can preserve provenance without silently choosing a computational CRS or vertical datum.

Horizontal and vertical references are distinct. A geodetic datum or CRS identifies horizontal coordinates, axis order and units. A vertical reference may be ellipsoidal height, orthometric height, a national height datum, mean sea level, a hydrographic chart or survey datum, or a tide-gauge reference plane. These are not interchangeable even when they share units.

| Component | Accepted status | Required evidence | Rejected status |
| --- | --- | --- | --- |
| Horizontal CRS/datum | `authoritative_declared`, `dataset_declared` | datum name, unit, source document title/URI, access timestamp; authority name/code when authoritative | `inferred`, `unknown`, `conflicting` |
| Vertical datum | `authoritative_declared`, `dataset_declared` | datum name, unit, positive direction, source document title/URI, access timestamp | `inferred`, `unknown`, `conflicting` |

Authority hierarchy:

| Authority class | Scope | G1 use |
| --- | --- | --- |
| Geospatial Information Authority of Japan (GSI) | Japanese geodetic datum realisations, national coordinate systems, benchmark elevations, geoid-based heights and datum-transition publications | Primary source for Japanese terrestrial horizontal and national-height evidence |
| EPSG Geodetic Parameter Dataset | Machine-readable CRS identifiers, axis order, units, coordinate operation identifiers and areas of use | Registry for codes and names; not proof that a specific asset used a CRS |
| Hydrographic and Oceanographic Department, Japan Coast Guard (JHOD) | Hydrographic survey, chart and sounding reference surfaces, marine datum metadata and product-specific publications | Primary source for Japanese bathymetry and chart/survey datum evidence |
| JMA, JHOD and station operators | Tide, tsunami and water-level station reference planes, station IDs, effective periods and time standards | Primary source for observation-layer vertical evidence |
| GEBCO release documentation | Release-specific global-grid horizontal and vertical assumptions and caveats | Fallback model-assumption evidence only when product-level metadata is no more specific |

The exact product or station metadata remains authoritative for the asset. The organisation-level register does not replace product-level evidence.

Source-document requirements: each datum evidence item records datum name, unit, document title, absolute credential-free URI, access timestamp, and effective period when relevant. Authoritative evidence records authority name/code. Tide-gauge references require station ID and station-local reference-plane metadata. Vertical evidence requires positive direction. Coordinate epoch and pre-event/post-event status are preserved where declared, because the study reconstructs the 11 March 2011 event.

Conflict policy: manifest declarations, embedded asset metadata and supplied source evidence must agree on authority code, datum name, unit, axis convention, vertical-positive convention, coordinate epoch, station and effective period. The importer rejects conflicts instead of resolving them by precedence.

No-default rule: do not default to WGS84, JGD2011, JGD2024, Tokyo Peil, mean sea level, chart datum, positive up or positive down because an asset is Japanese, GDAL opened it, the file is a GeoTIFF, values are negative offshore, a provider is hydrographic or the dataset resembles GEBCO.

GEBCO caveat handling: GEBCO-style global grid assumptions are represented as `reference_kind=model_assumption` and `status=dataset_declared` unless more specific cell/source metadata is available. The usual global-grid horizontal WGS84 and vertical mean-sea-level assumptions do not become authoritative coastal datum declarations.

Historical-event rule: preserve native datum, datum realisation, coordinate epoch, effective date and pre-event/post-event status where declared. This G1 import work does not decide whether JGD2000, JGD2011 or JGD2024 becomes the computational datum; that decision belongs to `SWE-GEO-CRS-WP1`.

The G1 fixtures use EPSG:4326/WGS 84 horizontal evidence and project-declared mean-sea-level vertical evidence for accepted imports, plus synthetic rejected examples for inferred, unknown and conflicting cases. Embedded GDAL CRS authority metadata must agree with the dataset manifest and supplied evidence. Vertical unit and positive direction must also agree with the manifest before an import record can be emitted.
