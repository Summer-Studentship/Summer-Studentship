# Topology and Geometry Separation v0.1

The accepted mesh model treats topology as authoritative and geometry as derived.

## Authoritative Topology

`MeshTopology` owns:

- `MeshId`;
- spatial dimension;
- `VertexRecord[]`;
- `FaceRecord[]`;
- `CellRecord[]`;
- `BoundaryPatchRecord[]`.

Raw records do not store cell centroids, cell measures, face centroids, face unit normals or face measures.

## Derived Geometry

`MeshGeometry` owns:

- `FaceGeometry { centroid, area_vector }`;
- `CellGeometry { centroid, measure }`.

Geometry arrays align with topology IDs: `FaceId::value` indexes `FaceGeometry[]`, and `CellId::value` indexes `CellGeometry[]`.

Geometry is computed only after structural validation succeeds. Expected validation failures return `Result` errors and do not publish a partially accepted mesh.
