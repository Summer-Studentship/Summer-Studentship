# Layer Ownership Validation v0.1

- **Work Package:** `SWE-ARC-LAY-WP1`
- **Issues:** `#126`, `#127`, `#128`, `#129`
- **Layer policy:** `architecture/layer_ownership_policy_v0.1.json`
- **Target policy:** `architecture/target_dependency_policy_v0.1.json`
- **Policy versions:** layer `0.1`, target `0.1`
- **Document date:** 2026-07-22

## Environment

| Tool | Version |
|---|---|
| Python | `3.14.5` |

## Validator Commands

```sh
python3 -m json.tool architecture/layer_ownership_policy_v0.1.json
python3 -m json.tool architecture/target_dependency_policy_v0.1.json
python3 -m py_compile tools/architecture/validate_target_graph.py
python3 -m py_compile tools/architecture/validate_layer_ownership.py
python3 tools/architecture/validate_target_graph.py \
  --policy architecture/target_dependency_policy_v0.1.json
python3 tools/architecture/validate_layer_ownership.py \
  --layers architecture/layer_ownership_policy_v0.1.json \
  --targets architecture/target_dependency_policy_v0.1.json
```

Layer-validator result:

```text
layer policy: 0.1
target policy: 0.1
layers: 12 (9 runtime)
mapped WBS domains: 13
mapped targets: 16
domain ownership: passed
target-layer mapping: passed
layer direction: passed
adapter inversion: passed
frontend leaf: passed
solver separation: passed
external framework isolation: passed
deferred baseline isolation: passed
```

## Validation Results

| Check | Result |
|---|---|
| Mapped WBS-domain count | 13 of 13 |
| Mapped target count | 16 of 16 target-policy targets |
| Layer-direction result | Passed |
| Adapter-inversion result | Passed |
| Frontend-leaf result | Passed |
| Solver-separation result | Passed |
| External-framework isolation result | Passed |
| Deferred baseline isolation result | Passed |
| Documentation-link result | Software README links resolve locally |

## Invalid Fixture Rejection

Temporary fixtures were created under `/tmp` and were not committed.

| Fixture | Injected fault | Result |
|---|---|---|
| `/tmp/invalid-layer-policy-duplicate-domain.json` | Duplicate `SWE-GUI` governing owner | `ERROR: domain has multiple governing owners: SWE-GUI` |
| `/tmp/invalid-target-policy-gui-core.json` | `tsunami_core -> tsunami_gui` | `ERROR: tsunami_core: dependency tsunami_gui crosses from L1 to disallowed L9` |
| `/tmp/invalid-target-policy-domain-adapter.json` | `tsunami_geo -> tsunami_geo_gdal` | `ERROR: tsunami_geo: dependency tsunami_geo_gdal crosses from L4 to disallowed L8` |

## Unresolved Decisions

- Concrete C++ interface names, header topology and abstract contracts remain
  with `SWE-ARC-API-WP1`.
- Simulation-case states, directories and lifecycle rules remain with
  `SWE-ARC-CASE-WP1`.
- Windows-host layer validation is not required because the layer validator
  checks policies rather than generated platform CMake graphs.

## Handoff

`SWE-ARC-CASE-WP1` can use L7 Application and Orchestration as the owner for
case lifecycle work. `SWE-ARC-API-WP1` can use the layer interface categories
and extension-point records as the placement guide for concrete C++ contracts.
