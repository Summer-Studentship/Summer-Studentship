# Mesh Records Audit v0.1

## Scope

This audit records how commit `c073670 feat(fvm): define mesh topology records [SWE-FVM-MSH-WP1-T1]` was accepted as the manual starting point for `SWE-FVM-MSH-WP1`.

The first draft correctly introduced strong mesh entity identifiers and basic OpenFOAM-like addressing, but it combined authoritative topology and derived geometry too closely. `SWE-FVM-MSH-WP1` separates those concerns for the accepted Regional2D baseline.

## Classification

| Member | Classification | Decision |
| --- | --- | --- |
| `VertexId::value` | authoritative topology | Retain as contiguous index. |
| `FaceId::value` | authoritative topology | Retain as contiguous index. |
| `CellId::value` | authoritative topology | Retain as contiguous index. |
| `BoundaryPatchId::value` | authoritative topology | Retain as contiguous index. |
| `Point3` | authoritative coordinates | Retain; accepted meshes require `z=0` within tolerance. |
| `Vector3` | derived geometry support | Retain for derived area vectors. |
| `VertexRecord::id` | authoritative topology | Retain. |
| `VertexRecord::position` | authoritative coordinates | Retain. |
| `FaceRecord::id` | authoritative topology | Retain. |
| `FaceRecord::vertices` | authoritative topology | Retain; exactly two vertices for Regional2D. |
| `FaceRecord::owner` | authoritative topology | Retain; one owner per face. |
| `FaceRecord::neighbour` | authoritative topology | Retain; zero or one neighbour. |
| `FaceRecord::centroid` | derived geometry | Removed from raw topology; now `FaceGeometry::centroid`. |
| `FaceRecord::measure` | derived geometry | Removed; face edge length is `|FaceGeometry::area_vector|`. |
| `FaceRecord::unit_normal` | derived geometry | Removed; use oriented `FaceGeometry::area_vector`. |
| `FaceRecord::boundary_patch` | authoritative topology | Retain; required for boundary faces, prohibited for internal faces. |
| `FaceRecord::is_boundary()` | convenience operation | Retain as read-only classification. |
| `FaceRecord::is_internal()` | convenience operation | Retain as read-only classification. |
| `CellRecord::id` | authoritative topology | Retain. |
| `CellRecord::vertices` | duplicated connectivity | Removed; cell vertices are recovered deterministically from faces. |
| `CellRecord::faces` | authoritative topology | Retain; exactly three faces for Regional2D. |
| `CellRecord::centroid` | derived geometry | Removed from raw topology; now `CellGeometry::centroid`. |
| `CellRecord::measure` | derived geometry | Removed from raw topology; now `CellGeometry::measure`. |
| `BoundaryPatchRecord::id` | authoritative topology | Retain. |
| `BoundaryPatchRecord::name` | authoritative boundary metadata | Retain. |
| `BoundaryPatchRecord::faces` | authoritative boundary grouping | Retain. |

## Outcome

Accepted raw records now contain only topology and coordinates. `MeshGeometry` owns derived face and cell geometry after validation succeeds.
