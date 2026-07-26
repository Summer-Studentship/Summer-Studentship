# Boundary Identity and Mapping v0.1

Boundary conditions are authored as `BoundarySpecification<Value>` records with id, display name, exact patch tag, unit id and operation.

Factory resolution is exact:

- `patch_tag` must match `BoundaryPatchRecord::name`.
- Substrings do not match.
- Case differences do not match.
- Unknown, duplicate and missing patches are rejected.

The completed `BoundaryConditionSet<Value>` stores conditions in deterministic `BoundaryPatchId` order, independent of input specification order. The set has no public resize or replacement API.

Patch-local values follow the accepted field rule: `values[local_index]` corresponds to `mesh.boundary_patch(patch_id).faces[local_index]`.
