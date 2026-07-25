# Mesh Binding and Compatibility v0.1

Mesh ID and entity counts are not enough to prove field compatibility. Two meshes can share the same ID and counts while changing coordinates, face ordering, owner/neighbour addressing or patch layout.

## Binding

`MeshBinding` records:

- mesh ID;
- spatial dimension;
- vertex, face, cell and patch counts;
- deterministic compatibility signature.

## Signature

The signature is fixed-width FNV-1a over canonical mesh data:

- mesh ID and counts;
- vertex IDs and coordinates;
- face IDs, ordered face vertex IDs, owner, optional neighbour and optional patch;
- cell IDs and ordered face IDs;
- patch IDs, names and ordered face IDs.

The signature is a runtime compatibility guard. It is not a security hash, persistence schema ID or substitute for mesh validation.

Independently reconstructed identical meshes produce the same binding. Same-ID/count meshes with altered topology or coordinates produce a different binding.
