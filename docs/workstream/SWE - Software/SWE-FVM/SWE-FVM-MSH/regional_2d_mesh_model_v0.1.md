# Regional2D Mesh Model v0.1

`SWE-FVM-MSH-WP1` defines the first accepted finite-volume mesh model for the standalone Regional2D solver.

## Accepted

- Static planar mesh.
- Spatial dimension exactly `2`.
- All accepted vertex `z` coordinates are zero within geometry tolerance.
- Straight two-point faces.
- Triangular cells with exactly three faces.
- One owner cell per face.
- Zero or one neighbour cell per face.
- Named boundary patches.
- Dense, contiguous, deterministic IDs for vertices, faces, cells and patches.

## Rejected

- Dimension other than `2`.
- Non-planar points.
- Faces with anything other than two vertices.
- Cells with anything other than three faces.
- Recovered cell vertex count other than three.
- Degenerate edges or triangles.
- Arbitrary polygons, quads, 3D polyhedra, moving mesh, MPI, GPU, registries and persistence adapters.

## Containers

- `MeshTopologyInput`: mutable input passed to validation.
- `MeshTopology`: accepted immutable topology and coordinates.
- `MeshGeometry`: derived immutable face and cell geometry.
- `FiniteVolumeMesh`: immutable aggregate implementing `IMeshView`.

The factory is `make_finite_volume_mesh(MeshTopologyInput)` and returns `tsunami::core::Result<FiniteVolumeMesh>`.
