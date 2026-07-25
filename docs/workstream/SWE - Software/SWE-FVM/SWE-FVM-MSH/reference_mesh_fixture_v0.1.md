# Reference Mesh Fixture v0.1

The reusable fixture is [reference_mesh.hpp](../../../../../tests/fvm/reference_mesh.hpp).

## Mesh

Unit square split into two triangles:

- vertices: `(0,0,0)`, `(1,0,0)`, `(1,1,0)`, `(0,1,0)`;
- cells: cell 0 uses triangle `(0,1,2)`, cell 1 uses triangle `(0,2,3)`;
- faces: 5 total;
- internal faces: 1;
- boundary faces: 4;
- patches: `south`, `east`, `north`, `west`.

## Expected Geometry

- cell areas: `0.5`, `0.5`;
- total area: `1.0`;
- cell 0 centroid: `(2/3, 1/3, 0)`;
- cell 1 centroid: `(1/3, 2/3, 0)`;
- internal face area vector points from cell 0 to cell 1;
- boundary area vectors point outward from the owner.
