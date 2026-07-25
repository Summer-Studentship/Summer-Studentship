# Owner Neighbour Orientation v0.1

Faces use OpenFOAM-like owner/neighbour addressing adapted to static planar triangular Regional2D meshes.

## Convention

- Boundary face: `owner` is set, `neighbour` is empty, `boundary_patch` is set.
- Internal face: `owner` and `neighbour` are set, `boundary_patch` is empty.

## Area Vectors

For a two-point edge, the raw area-vector candidate is `(dy, -dx, 0)`.

- Internal faces are oriented so `S_f dot (x_N - x_P) > 0`.
- Boundary faces are oriented so `S_f dot (x_f - x_P) > 0`.
- Ambiguous orientation within tolerance is rejected.

`x_P` is the owner cell centroid, `x_N` is the neighbour cell centroid and `x_f` is the face centroid.

## Closure

For each cell, owner-side face vectors contribute `+S_f`; neighbour-side face vectors contribute `-S_f`. The signed sum must close to approximately zero with a scale-aware tolerance.
