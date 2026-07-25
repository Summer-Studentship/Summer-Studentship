# Field Ownership and Lifetime v0.1

Each concrete field owns exactly one contiguous `std::vector<Value>`.

## Rules

- Size is fixed after construction.
- Public APIs expose spans and bounds-checked `at()`, not the owning vector.
- Public APIs do not expose resize, reserve, push, erase or clear operations.
- Fields are move-only.
- Implicit deep copying is disabled.
- `clone()` performs an explicit deep copy.
- `copy_values_from()` copies into preallocated compatible storage.
- Failed copy operations leave destination values unchanged.
- Fields store a `MeshBinding`, not an owning mesh pointer.

This policy keeps whole-field duplication visible in numerical paths and allows operators to reuse storage without redesigning ownership.
