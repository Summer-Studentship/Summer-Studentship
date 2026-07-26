# Boundary Transactionality v0.1

All boundary application failures are non-mutating.

The diagnostic context includes:

- `rule_id=SWE-FVM-BC-WP1`
- `state_changed=false`
- `operation`
- boundary identity and patch metadata when available

Validation precedes mutation for fixed-value and zero-gradient conditions. Named references never mutate because they are not executable.

This contract is intentionally conservative so later solver code can compose boundary operations without defensive rollback logic.
