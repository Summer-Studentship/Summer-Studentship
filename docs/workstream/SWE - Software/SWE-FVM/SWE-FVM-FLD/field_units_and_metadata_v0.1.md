# Field Units and Metadata v0.1

`FieldDescriptor` is the GUI-neutral and I/O-neutral inspection record.

It contains:

- `FieldId`;
- name;
- `MeshId`;
- `FieldLocation`;
- `FieldValueKind`;
- component count;
- entity count;
- unit ID;
- optional boundary patch ID.

Unit IDs are opaque stable identifiers such as `m`, `m/s`, `m2/s`, `kg/m3` or `Pa`. This work package does not parse, convert or algebraically combine units.

Cell and face descriptors do not carry a patch ID. Boundary-patch descriptors must carry one.
