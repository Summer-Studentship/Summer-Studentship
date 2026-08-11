# R17 Repository Review Register

This register records future review candidates only. R17 performed no
deletions, moves, renames, untracking, ignore-rule changes or history rewrites.

| ID | Classification | Priority | Scope | Recommendation |
| --- | --- | --- | --- | --- |
| R17-REV-001 | `REVIEW_DUPLICATE_CANDIDATE` | P2 | R16 preview/publication duplicate PNG pairs | Keep now. Later decide whether preview PNGs should be retained, regenerated or linked from publication outputs. |
| R17-REV-002 | `REVIEW_DUPLICATE_CANDIDATE` | P2 | Repeated research PDFs across research and workstream folders | Keep now. Deduplicate only after citation, bibliography and workstream-source review. |
| R17-REV-003 | `REVIEW_IGNORE_RULE_CANDIDATE` | P3 | Nine tracked `.aux.xml` GIS/render sidecars | Keep now. Consider future ignore rules only after confirming manifests and rasters preserve required provenance. |
| R17-REV-004 | `REVIEW_REQUIRED` | P2 | `docs/UI Inspiration.png` | Keep now. Review origin and decide whether it belongs in GUI inspiration, project history or archive documentation. |
| R17-REV-005 | `REVIEW_DUPLICATE_CANDIDATE` | P3 | `deliverables/figures/r16_publication/sources/python/r16_publication.py` and `tools/figures/r16_publication.py` | Keep now. Define whether figure source snapshots should duplicate live tools. |
| R17-REV-006 | `REVIEW_MOVE_CANDIDATE` | P3 | One tracked path with a newline and 24 tracked paths with non-ASCII characters | Keep now. Consider path-normalisation only in a dedicated bibliographic hygiene pass. |
| R17-REV-007 | `REVIEW_REQUIRED` | P2 | 147 tracked PDFs, 595,357,566 bytes | Keep now. Future policy may separate source library storage from repository evidence, but research inputs are protected. |
| R17-REV-008 | `REVIEW_REQUIRED` | P4 | Zero-byte local Git metadata warning from `git count-objects -vH` | No tracked change. Optional local Git maintenance only with explicit approval. |

## Candidate Details

Duplicate groups >= 1 KiB: 23 groups, 48 paths, with 22,005,955 potentially
redundant bytes if every candidate were approved. The largest visible groups
include repeated research PDFs, R16 preview/publication PNG pairs, R14
presentation variants and manifest mirrors.

Tracked `.aux.xml` files:

- `deliverables/figures/r16_publication/sources/qgis/derived/corridor_hillshade.tif.aux.xml`
- `deliverables/figures/r16_publication/sources/qgis/derived/etopo_corridor_kamaishi_utm54.tif.aux.xml`
- `deliverables/figures/r16_publication/sources/qgis/derived/etopo_japan_context_utm54.tif.aux.xml`
- `deliverables/figures/r16_publication/sources/qgis/derived/etopo_nearshore_hybrid_utm54.tif.aux.xml`
- `deliverables/figures/r16_publication/sources/qgis/derived/etopo_regional_tohoku_utm54.tif.aux.xml`
- `deliverables/figures/r16_publication/sources/qgis/derived/etopo_validation_overview_utm54.tif.aux.xml`
- `deliverables/figures/r16_publication/sources/qgis/derived/nearshore_hillshade.tif.aux.xml`
- `deliverables/figures/r16_publication/sources/qgis/derived/regional_hillshade.tif.aux.xml`
- `deliverables/figures/r17_closure/sources/blender/terrain/etopo_corridor_blender_utm54_200m.tif.aux.xml`

Path-hygiene note: one research PDF path contains an embedded newline before
`.pdf`. It is classified as a review candidate only, because renaming research
sources can break citations and provenance.
