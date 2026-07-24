# Interface Contract Validation v0.1

- **Work Package:** `SWE-ARC-API-WP1`
- **Policy:** `architecture/interface_contract_policy_v0.1.json`
- **Validator:** `tools/architecture/validate_interface_contracts.py`
- **Document date:** 2026-07-24

## Environment

| Tool | Version |
|---|---|
| Python | `3.14.5` |
| CMake | `4.3.3` |
| GCC | `g++ (GCC) 16.1.1 20260430` |
| Clang | `clang version 22.1.6` |

## Counts

| Item | Count |
|---|---:|
| Public headers | 14 |
| Contract families | 7 |
| Activated contract targets | 5 |

Activated targets: `tsunami_fvm`, `tsunami_data`, `tsunami_r2d`, `tsunami_l3d`, `tsunami_coupling`.

## Validator Commands

```sh
python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json
python3 tools/architecture/validate_layer_ownership.py --layers architecture/layer_ownership_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json
python3 tools/architecture/validate_case_lifecycle.py --policy architecture/case_lifecycle_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json
python3 tools/architecture/validate_interface_contracts.py --interfaces architecture/interface_contract_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json --cases architecture/case_lifecycle_policy_v0.1.json
CXX=clang++ python3 tools/architecture/validate_interface_contracts.py --interfaces architecture/interface_contract_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json --cases architecture/case_lifecycle_policy_v0.1.json
```

All validator commands passed. The interface validator reports 14 public headers, 7 contract families, forbidden-token scan passed, solver sibling independence passed and independent header compilation passed.

## Build Evidence

The accepted preset workflows were attempted but blocked before project compilation because `VCPKG_ROOT` is unset and the cached presets reference missing `/tmp/tsunami-g0-vcpkg/scripts/buildsystems/vcpkg.cmake`.

| Requested command | Result |
|---|---|
| `cmake --workflow --preset linux-gcc-test-workflow` | blocked at configure: missing `/tmp/tsunami-g0-vcpkg/scripts/buildsystems/vcpkg.cmake` |
| `cmake --workflow --preset linux-clang-test-workflow` | blocked at configure: missing `/tmp/tsunami-g0-vcpkg/scripts/buildsystems/vcpkg.cmake` |
| `cmake --preset linux-gcc-gui-dev` | blocked at configure: missing `/tmp/tsunami-g0-vcpkg/scripts/buildsystems/vcpkg.cmake`; Qt also reported missing optional Vulkan headers |

Substitute local validation without vcpkg:

```sh
cmake -S . -B /tmp/summer-api-contract-build -G Ninja -DTSUNAMI_BUILD_CLI=OFF -DTSUNAMI_BUILD_GUI=OFF -DBUILD_TESTING=OFF
cmake --build /tmp/summer-api-contract-build
g++ -std=c++20 -fsyntax-only -Isrc/core/include -Isrc/fvm/include -Isrc/data/include -Isrc/r2d/include -Isrc/l3d/include -Isrc/coupling/include tests/contracts/contract_dummy_consumers.cpp
clang++ -std=c++20 -fsyntax-only -Isrc/core/include -Isrc/fvm/include -Isrc/data/include -Isrc/r2d/include -Isrc/l3d/include -Isrc/coupling/include tests/contracts/contract_dummy_consumers.cpp
```

The substitute configure/build and both dummy-consumer syntax checks passed.

## CMake Graph

The no-vcpkg graph contains:

```text
tsunami_coupling -> tsunami_core
tsunami_coupling -> tsunami_data
tsunami_coupling -> tsunami_l3d
tsunami_coupling -> tsunami_r2d
tsunami_data -> tsunami_core
tsunami_fvm -> tsunami_core
tsunami_l3d -> tsunami_core
tsunami_l3d -> tsunami_data
tsunami_r2d -> tsunami_core
tsunami_r2d -> tsunami_data
```

`validate_target_graph.py --cmake-graph /tmp/summer-api-contract-build/graph.dot` passed. `tsunami_cli`, `tsunami_gui` and `tsunami_tests` were absent from this substitute configuration by design.

## Invalid Fixture Rejection

| Fixture | Injected fault | Result |
|---|---|---|
| `/tmp/interface-contract-invalid/qstring-fvm/policy.json` | `QString` in FVM header | rejected |
| `/tmp/interface-contract-invalid/hdf5-data/policy.json` | `H5::DataSet` in data contract | rejected |
| `/tmp/interface-contract-invalid/direct-r2d-l3d/policy.json` | R2D references L3D | rejected |
| `/tmp/interface-contract-invalid/missing-destructor/policy.json` | abstract interface lacks virtual destructor | rejected |
| `/tmp/interface-contract-invalid/raw-owning-pointer/policy.json` | raw pointer return from interface | rejected |
| `/tmp/interface-contract-invalid/missing-header/policy.json` | missing public header | rejected |
| `/tmp/interface-contract-invalid/unknown-consumer/policy.json` | unknown consumer target | rejected |

## Handoffs

Solver algorithms, FVM storage, data schemas, parser behaviour, concrete adapters, application services, GUI integration, diagnostic taxonomy and structured logging remain downstream work. `SWE-ARC-SVC-WP1` is ready to consume case/preparation/run/artefact references plus cancellation and observer contracts.
