# Application Service Validation v0.1

- **Work Package:** `SWE-ARC-SVC-WP1`
- **Policy:** `architecture/application_service_policy_v0.1.json`
- **Validator:** `tools/architecture/validate_application_service.py`
- **Document date:** 2026-07-24

## Environment

| Tool | Version |
|---|---|
| Python | `3.14.5` |
| CMake | `4.3.3` |
| GCC | `g++ (GCC) 16.1.1 20260430` |
| Clang | `clang version 22.1.6` |
| vcpkg | `/tmp/tsunami-svc-vcpkg` at `d015e31e90838a4c9dfa3eed45979bc70d9357fc` |

## Validator Results

All architecture validators passed:

```sh
python3 tools/architecture/validate_target_graph.py --policy architecture/target_dependency_policy_v0.1.json
python3 tools/architecture/validate_layer_ownership.py --layers architecture/layer_ownership_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json
python3 tools/architecture/validate_case_lifecycle.py --policy architecture/case_lifecycle_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json
python3 tools/architecture/validate_interface_contracts.py --interfaces architecture/interface_contract_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json --cases architecture/case_lifecycle_policy_v0.1.json
python3 tools/architecture/validate_application_service.py --services architecture/application_service_policy_v0.1.json --interfaces architecture/interface_contract_policy_v0.1.json --cases architecture/case_lifecycle_policy_v0.1.json --targets architecture/target_dependency_policy_v0.1.json --layers architecture/layer_ownership_policy_v0.1.json
```

## Build and Smoke Results

| Command | Result |
|---|---|
| `VCPKG_ROOT=/tmp/tsunami-svc-vcpkg cmake --workflow --preset linux-gcc-test-workflow` | passed, 8/8 tests |
| `VCPKG_ROOT=/tmp/tsunami-svc-vcpkg cmake --workflow --preset linux-clang-test-workflow` | passed, 8/8 tests |
| `VCPKG_ROOT=/tmp/tsunami-svc-vcpkg cmake --preset linux-gcc-gui-dev` | passed; Qt optional Vulkan headers not found, non-fatal |
| `VCPKG_ROOT=/tmp/tsunami-svc-vcpkg cmake --build --preset linux-gcc-gui-dev-build` | passed |
| `build/linux-gcc-gui-dev/apps/tsunami_cli/tsunami_cli --service-status` | passed |
| `QT_QPA_PLATFORM=offscreen build/linux-gcc-gui-dev/apps/tsunami_gui/tsunami_gui --smoke-startup` | passed |

CLI smoke output:

```text
service_backend=no-solver
service_boundary=available
solver_available=false
validation_available=false
preparation_available=false
launch_available=false
cancellation_available=false
result_discovery_available=false
```

## Graph Results

`linux-gcc-dev`, `linux-gcc-test` and `linux-gcc-gui-dev` Graphviz outputs validate against `target_dependency_policy_v0.1.json`. `tsunami_application` appears in all three. CLI and GUI link to `tsunami_application`; the application target links only to `tsunami_core` and `tsunami_data`.

## Invalid Fixture Rejection

| Fixture | Injected fault | Result |
|---|---|---|
| `/tmp/application-service-invalid/qt-header/service.json` | `QString` in application header | rejected |
| `/tmp/application-service-invalid/launch-draft/service.json` | launch allowed from `draft` | rejected |
| `/tmp/application-service-invalid/restart-reuses-parent/service.json` | restart reuses parent run | rejected |
| `/tmp/application-service-invalid/cli-missing-app/targets.json` | CLI missing application dependency | rejected |
| `/tmp/application-service-invalid/gui-direct-r2d/targets.json` | GUI directly depends on `tsunami_r2d` | rejected |
| `/tmp/application-service-invalid/no-solver-launch-capability/service.json` | no-solver claims launch capability | rejected |
| `/tmp/application-service-invalid/discovery-mutates/service.json` | discovery marked state-mutating | rejected |

## Research Stash

Unrelated Research HTML deletions were preserved before this branch in:

```text
stash@{0}: On main: wip: preserve unrelated research HTML deletions before SWE-ARC-SVC-WP1
```

They are not part of this branch diff and were not applied.
