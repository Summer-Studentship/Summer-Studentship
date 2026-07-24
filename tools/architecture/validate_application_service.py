#!/usr/bin/env python3
"""Validate shared application-service policy, headers and target boundaries."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


REQUIRED_OPERATIONS = {"validate_case", "prepare_run", "launch_run", "request_cancellation", "discover_results"}
FORBIDDEN_HEADER_TOKENS = {"QObject", "QString", "QVariant", "#include <Q", "CLI::", "#include <CLI"}
FORBIDDEN_FRONTEND_DEPS = {
    "tsunami_fvm", "tsunami_r2d", "tsunami_l3d", "tsunami_coupling",
    "tsunami_io_hdf5", "tsunami_io_xdmf", "tsunami_geo", "tsunami_geo_gdal", "tsunami_mesh_gmsh",
}


class ValidationError(Exception):
    """Raised when the application-service policy violates architecture rules."""


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValidationError(f"{path}: root must be a JSON object")
    return value


def index_by(items: list[dict], key: str, label: str) -> dict[str, dict]:
    result: dict[str, dict] = {}
    if not isinstance(items, list):
        raise ValidationError(f"{label}: must be a list")
    for item in items:
        name = item.get(key)
        if not isinstance(name, str) or not name:
            raise ValidationError(f"{label}: every entry requires {key}")
        if name in result:
            raise ValidationError(f"{label}: duplicate {key}: {name}")
        result[name] = item
    return result


def validate_references(policy: dict, interfaces: dict, cases: dict, targets: dict, layers: dict) -> None:
    refs = policy.get("references", {})
    expected = {
        "interface_policy": interfaces,
        "case_policy": cases,
        "target_policy": targets,
        "layer_policy": layers,
    }
    for ref_name, referenced in expected.items():
        if refs.get(ref_name, {}).get("policy_version") != referenced.get("policy_version"):
            raise ValidationError(f"service policy {ref_name} version reference does not match")


def validate_operations(policy: dict, cases: dict) -> dict[str, dict]:
    operations = index_by(policy.get("operations", []), "name", "operations")
    missing = REQUIRED_OPERATIONS - set(operations)
    if missing:
        raise ValidationError(f"missing required operation(s): {sorted(missing)}")
    case_states = {entry["name"] for entry in cases.get("case_state_machine", {}).get("states", [])}
    run_states = {entry["name"] for entry in cases.get("run_state_machine", {}).get("states", [])}
    for name, operation in operations.items():
        if not operation.get("request_type") or not operation.get("response_type"):
            raise ValidationError(f"{name}: request_type and response_type are required")
        unknown_case = set(operation.get("permitted_case_states", [])) - case_states
        unknown_run = set(operation.get("permitted_run_states", [])) - run_states
        if unknown_case or unknown_run:
            raise ValidationError(f"{name}: unknown lifecycle states case={sorted(unknown_case)} run={sorted(unknown_run)}")
        if name == "launch_run":
            if operation.get("permitted_case_states") != ["prepared"]:
                raise ValidationError("launch_run must require prepared case state")
            if operation.get("restart_rule") != "creates_new_run":
                raise ValidationError("launch_run restart must create a new run")
        if name == "discover_results" and operation.get("side_effect") != "read_only":
            raise ValidationError("discover_results must be read-only")
        if operation.get("side_effect") == "state_changing" and operation.get("unsupported_state_changed") is not False:
            raise ValidationError(f"{name}: unsupported state-changing operation must not claim state mutation")
    return operations


def validate_headers(policy: dict) -> None:
    for raw_path in policy.get("public_headers", []):
        path = Path(raw_path)
        if not path.exists():
            raise ValidationError(f"public service header does not exist: {path}")
        text = path.read_text(encoding="utf-8")
        for token in sorted(FORBIDDEN_HEADER_TOKENS):
            if token in text:
                raise ValidationError(f"{path}: prohibited service-header token: {token}")
    factory = policy.get("factory", "")
    if "make_no_solver_application_service" not in factory:
        raise ValidationError("no-solver factory must be recorded")


def validate_targets(policy: dict, targets_policy: dict, layers_policy: dict) -> None:
    targets = index_by(targets_policy.get("targets", []), "target_name", "targets")
    target = targets.get(policy.get("application_target"))
    if not target:
        raise ValidationError("application target is missing from target policy")
    if target.get("state") != "active" or target.get("type") != "static_library":
        raise ValidationError("application target must be an active static library")
    deps = set(target.get("allowed_direct_project_dependencies", []))
    if deps != {"tsunami_core", "tsunami_data"}:
        raise ValidationError(f"application target must depend only on core and data, got {sorted(deps)}")
    if set(target.get("owned_external_dependencies", [])) & {"Qt", "CLI11", "Catch2"}:
        raise ValidationError("application target must not own Qt, CLI11 or Catch2")
    mapping = layers_policy.get("target_layer_map", {})
    if mapping.get(policy.get("application_target")) != policy.get("application_layer"):
        raise ValidationError("application target does not map to application layer")
    for frontend in ("tsunami_cli", "tsunami_gui"):
        frontend_deps = set(targets[frontend].get("allowed_direct_project_dependencies", []))
        if "tsunami_application" not in frontend_deps:
            raise ValidationError(f"{frontend}: missing tsunami_application dependency")
        blocked = frontend_deps & FORBIDDEN_FRONTEND_DEPS
        if blocked:
            raise ValidationError(f"{frontend}: forbidden direct frontend dependency {sorted(blocked)}")
        if "tsunami_core" in frontend_deps:
            raise ValidationError(f"{frontend}: transitional direct core dependency must be removed")


def validate_no_solver(policy: dict) -> None:
    behaviour = policy.get("no_solver_behaviour", {})
    if behaviour.get("implementation_id") != "no-solver":
        raise ValidationError("no-solver implementation id must be no-solver")
    if behaviour.get("status_available") is not True:
        raise ValidationError("no-solver service must expose service status")
    if behaviour.get("solver_available") is not False or behaviour.get("launch_available") is not False:
        raise ValidationError("no-solver must not claim solver or launch capability")
    if behaviour.get("state_changing_unsupported_operations_succeed") is not False:
        raise ValidationError("unsupported state-changing operations must not claim success")
    if behaviour.get("launch_fabricates_run") is not False:
        raise ValidationError("no-solver launch must not fabricate a run")
    if behaviour.get("creates_files") is not False:
        raise ValidationError("no-solver must not create files")


def print_report(policy: dict, operations: dict[str, dict]) -> None:
    print(f"application service policy: {policy['policy_version']}")
    print(f"target policy: {policy['references']['target_policy']['policy_version']}")
    print(f"layer policy: {policy['references']['layer_policy']['policy_version']}")
    print(f"case policy: {policy['references']['case_policy']['policy_version']}")
    print(f"interface policy: {policy['references']['interface_policy']['policy_version']}")
    print(f"operations: {len(operations)}")
    print("operation coverage: passed")
    print("lifecycle preconditions: passed")
    print("public service headers: passed")
    print("frontend dependency boundary: passed")
    print("no-solver behaviour: passed")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--services", required=True, type=Path, help="application service policy JSON")
    parser.add_argument("--interfaces", required=True, type=Path, help="interface contract policy JSON")
    parser.add_argument("--cases", required=True, type=Path, help="case lifecycle policy JSON")
    parser.add_argument("--targets", required=True, type=Path, help="target dependency policy JSON")
    parser.add_argument("--layers", required=True, type=Path, help="layer ownership policy JSON")
    args = parser.parse_args(argv)
    try:
        policy = load_json(args.services)
        interfaces = load_json(args.interfaces)
        cases = load_json(args.cases)
        targets = load_json(args.targets)
        layers = load_json(args.layers)
        validate_references(policy, interfaces, cases, targets, layers)
        operations = validate_operations(policy, cases)
        validate_headers(policy)
        validate_targets(policy, targets, layers)
        validate_no_solver(policy)
        print_report(policy, operations)
    except (OSError, json.JSONDecodeError, ValidationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
