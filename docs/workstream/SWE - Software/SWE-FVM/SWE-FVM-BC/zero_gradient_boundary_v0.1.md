# Zero-Gradient Boundary v0.1

`ZeroGradientBoundary<Value>` is the generic owner-cell copy operation for Regional2D boundary patches.

For each local patch face:

```text
target[j] = internal[mesh.face(patch.faces[j]).owner]
```

The implementation validates mesh binding, internal field binding, destination patch, entity count and unit compatibility before assigning destination values. Computed values are staged in temporary storage so failed validation preserves the destination field.

This package does not define a numerical flux or equation discretisation; it only provides the boundary data transfer primitive.
