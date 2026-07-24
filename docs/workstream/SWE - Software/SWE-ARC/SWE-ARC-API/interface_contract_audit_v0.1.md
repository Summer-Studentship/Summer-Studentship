# Interface Contract Audit v0.1

- **Work Package:** `SWE-ARC-API-WP1`
- **Issues:** `#134`, `#135`, `#136`, `#137`
- **Policy:** `architecture/interface_contract_policy_v0.1.json`
- **Document date:** 2026-07-24

## Audit Result

| Path | Current purpose | Completeness | Owning target | WBS owner | Compatibility | Decision |
|---|---|---|---|---|---|---|
| `src/core/Constants.hpp` | Historical constants header | Partial | `tsunami_core` | `SWE-ARC-API` | Qt-free but old include path | Retain as forwarding header to public include tree |
| `src/core/Types.hpp` | Historical numeric aliases | Partial | `tsunami_core` | `SWE-ARC-API` | Qt-free but old include path | Retain as forwarding header |
| `src/core/Error.hpp` | Empty placeholder | Incomplete | `tsunami_core` | `SWE-ARC-API` | No accepted contract | Replace with forwarding header to `tsunami/core/Error.hpp` |
| `src/core/Results.hpp` | Empty placeholder | Incomplete | `tsunami_core` | `SWE-ARC-API` | No accepted contract | Replace with forwarding header to `tsunami/core/Result.hpp` |
| `tests/Reworked Prior Model/**` | Historical solver prototype | Historical only | none | none | Not target/layer compliant | Defer; not accepted as public contract |
| `docs/workstream/SWE - Software/SWE-ARC/SWE-ARC-TGT/**` | Target architecture evidence | Complete baseline | architecture | `SWE-ARC-TGT-WP1` | Authoritative | Retain as source policy |
| `docs/workstream/SWE - Software/SWE-ARC/SWE-ARC-LAY/**` | Layer architecture evidence | Complete baseline | architecture | `SWE-ARC-LAY-WP1` | Authoritative | Retain as source policy |
| `docs/workstream/SWE - Software/SWE-ARC/SWE-ARC-CASE/**` | Case lifecycle evidence | Complete baseline | architecture | `SWE-ARC-CASE-WP1` | Authoritative | Retain as source policy |

No existing mesh, field, reader, writer, solver, observer, cancellation, checkpoint, replay, `CaseId` or `RunId` C++ declaration was complete enough to accept as the stable API contract without replacement.
