# Boundary Patch Field Layout v0.1

Patch fields store one value per boundary face in one selected patch.

```text
patch local index
    -> BoundaryPatchRecord::faces[local index]
    -> global FaceId
```

A patch field does not store one value for the whole patch. It also does not duplicate the patch face list. The mesh remains authoritative for patch-to-global-face mapping.

This layout lets `SWE-FVM-BC-WP1` consume patch fields directly when boundary-condition behaviour is added.
