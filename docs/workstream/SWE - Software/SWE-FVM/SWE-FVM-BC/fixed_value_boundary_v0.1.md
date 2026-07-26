# Fixed-Value Boundary v0.1

`FixedValueBoundary<Value>` owns:

- a `BoundaryDescriptor`;
- a `MeshBinding`;
- a `BoundaryPatchField<Value>` containing prescribed patch-local values.

`apply` validates the internal cell-field binding and the destination patch field before mutation. It does not read internal cell values. Once validation passes, prescribed values are copied to the destination in patch-local order.

Rejected mesh, patch, layout or unit mismatches leave the destination unchanged and report `state_changed=false`.
