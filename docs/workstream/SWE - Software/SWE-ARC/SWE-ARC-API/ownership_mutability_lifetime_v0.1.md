# Ownership, Mutability and Lifetime v0.1

- **Work Package:** `SWE-ARC-API-WP1`

## Ownership

Value objects own their data. Factories and ports use `std::unique_ptr` when transferring exclusive implementation ownership. `std::shared_ptr` is permitted only for explicitly shared immutable lifetime, such as cancellation tokens. Raw pointers never express ownership.

Concrete adapter ownership belongs to application composition and later service work, not numerical domains. Interfaces have virtual destructors.

## Borrowed Lifetime

References, `std::span` and `std::string_view` are borrowed. Public interfaces must not retain borrowed arguments unless a later contract states that explicitly. Callback payloads passed to observers are valid for the callback duration.

Returned references remain valid only while the owner remains alive and unmodified.

## Mutability

Immutable value inputs are the default. Read-only views cross target boundaries. Mutable computational storage remains inside its owning implementation target. Completed run records and accepted artefacts are immutable. Writes use explicit transaction and commit semantics.

No public contract relies on `const_cast`.

## Thread Safety and ABI

Interfaces are not thread-safe by default unless marked by a later implementation contract. Observers must tolerate callbacks outside the GUI thread. Concurrent calls require explicit support.

G0 guarantees source-level contract review, not long-term binary ABI stability. Public contracts avoid concrete external-library types, and changes require interface policy review.
