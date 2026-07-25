# Reference Field Fixtures v0.1

Reusable field fixtures live in [reference_fields.hpp](../../../../../tests/fvm/reference_fields.hpp).

## Fields

- Cell scalar: `cell-scalar`, unit `m`, values `[1.0, 2.0]`.
- Face scalar: `face-scalar`, unit `m2/s`, values `[0.0, 1.0, 2.0, 3.0, 4.0]`.
- Cell vector: centroid-based deterministic vectors.
- Face vector: accepted face-area vectors.
- Patch scalar: one value per ordered boundary face in patch 0.
- Patch vector: face-area vectors for ordered boundary faces in patch 0.

## Multi-Face Patch Mesh

The fixture also provides a valid mesh whose patch 0 is `south-east` with ordered faces `[0, 1]`. This verifies that patch field local indexing follows `BoundaryPatchRecord::faces` order.
