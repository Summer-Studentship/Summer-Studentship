#!/usr/bin/env python3
"""Validate diagnostic and expected-failure propagation policy."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


REQUIRED_SEVERITIES = {"trace", "debug", "info", "warning", "error", "critical"}
REQUIRED_CATEGORIES = {
    "validation", "configuration", "input_data", "preparation", "execution", "persistence",
    "geospatial", "numerical", "coupling", "cancellation", "resource", "unsupported", "internal",
}
REQUIRED_STATES = {"idle", "accepted", "running", "succeeded", "failed", "cancelled"}
REQUIRED_CONTEXT = {"operation", "rule_id", "case_location", "case_revision", "state_changed"}
CLI_FIELDS = {
    "operation", "state", "severity", "category", "code", "message", "context.operation",
    "context.rule_id", "context.case_location", "context.case_revision", "context.state_changed",
    "context_preserved",
}
GUI_FIELDS = {
    "active", "operation", "state", "severity", "category", "code", "message", "ruleId",
    "caseLocation", "caseRevision", "stateChanged", "contextPreserved",
}
FORBIDDEN_PUBLIC_TOKENS = {
    "QObject", "QString", "QVariant", "#include <Q", "CLI::", "Catch::", "#include <catch2",
    "spdlog", "fmt::", "H5::", "hid_t", "GDAL", "OGR", "Gmsh", "matplot",
}


class ValidationError(Exception):
    """Raised when diagnostic policy validation fails."""


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValidationError(f"{path}: root must be a JSON object")
    return value


def unique_names(items: list, label: str) -> set[str]:
    names: list[str] = []
    for item in items:
        name = item.get("name") if isinstance(item, dict) else item
        if not isinstance(name, str) or not name:
            raise ValidationError(f"{label}: every entry requires a name")
        names.append(name)
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise ValidationError(f"{label}: duplicate names {duplicates}")
    return set(names)


def index_by(items: list[dict], key: str, label: str) -> dict[str, dict]:
    indexed: dict[str, dict] = {}
    if not isinstance(items, list):
        raise ValidationError(f"{label}: must be a list")
    for item in items:
        name = item.get(key)
        if not isinstance(name, str) or not name:
            raise ValidationError(f"{label}: every entry requires {key}")
        if name in indexed:
            raise ValidationError(f"{label}: duplicate {key}: {name}")
        indexed[name] = item
    return indexed


def validate_references(policy: dict, services: dict, interfaces: dict, cases: dict, targets: dict, layers: dict) -> None:
    refs = policy.get("references", {})
    expected = {
        "service_policy": services,
        "interface_policy": interfaces,
        "case_policy": cases,
        "target_policy": targets,
        "layer_policy": layers,
    }
    for name, referenced in expected.items():
        if refs.get(name, {}).get("policy_version") != referenced.get("policy_version"):
            raise ValidationError(f"{name} version reference does not match")


def validate_policy_model(policy: dict) -> None:
    severities = unique_names(policy.get("severity_levels", []), "severity_levels")
    categories = unique_names(policy.get("diagnostic_categories", []), "diagnostic_categories")
    states = unique_names(policy.get("operation_states", []), "operation_states")
    if not REQUIRED_SEVERITIES.issubset(severities):
        raise ValidationError(f"missing severities {sorted(REQUIRED_SEVERITIES - severities)}")
    if not REQUIRED_CATEGORIES.issubset(categories):
        raise ValidationError(f"missing categories {sorted(REQUIRED_CATEGORIES - categories)}")
    if not REQUIRED_STATES.issubset(states):
        raise ValidationError(f"missing operation states {sorted(REQUIRED_STATES - states)}")
    pattern = policy.get("error_code_format", {}).get("pattern")
    if not isinstance(pattern, str) or not pattern:
        raise ValidationError("error-code format pattern is required")
    failure = policy.get("representative_validation_failure", {})
    if not re.fullmatch(pattern, failure.get("code", "")):
        raise ValidationError("representative failure code does not match error-code format")
    if failure.get("category") not in categories:
        raise ValidationError("representative failure category is unknown")
    if failure.get("severity") not in severities:
        raise ValidationError("representative failure severity is unknown")
    if set(failure.get("required_context", {})) != REQUIRED_CONTEXT:
        raise ValidationError("representative failure required context keys do not match")
    if failure.get("required_context", {}).get("state_changed") != "false":
        raise ValidationError("representative failure must not claim state mutation")
    if failure.get("filesystem_access") is not False or failure.get("schema_parsing") is not False:
        raise ValidationError("representative validation failure must not access files or parse schemas")
    if not set(policy.get("required_diagnostic_fields", [])).issuperset({"code", "message", "category", "severity", "context"}):
        raise ValidationError("required diagnostic fields are incomplete")
    if policy.get("logging_rules", {}).get("sink_lifecycle_owner") is not False:
        raise ValidationError("logging sinks must not own lifecycle state")
    if policy.get("logging_rules", {}).get("diagnostic_creation_logs_automatically") is not False:
        raise ValidationError("diagnostic creation must not imply automatic logging")
    observer_preserves = set(policy.get("propagation_rules", {}).get("observer_preserves", []))
    if not observer_preserves.issuperset({"code", "message", "category", "severity", "context"}):
        raise ValidationError("observer preservation rules must include complete diagnostic context")
    cancellation = policy.get("cancellation_rules", {})
    if cancellation.get("category") != "cancellation" or "numerical" in cancellation.get("normal_severity", []):
        raise ValidationError("cancellation must not be classified as numerical failure")
    if not set(cancellation.get("required_context", [])).issuperset({"operation", "state_changed", "cancellation_stage"}):
        raise ValidationError("cancellation context requirements are incomplete")


def validate_headers(policy: dict) -> None:
    for raw_path in policy.get("public_headers", []):
        path = Path(raw_path)
        if not path.exists():
            raise ValidationError(f"public diagnostic header missing: {path}")
        text = path.read_text(encoding="utf-8")
        for token in sorted(set(policy.get("prohibited_public_tokens", [])) | FORBIDDEN_PUBLIC_TOKENS):
            if token in text:
                raise ValidationError(f"{path}: prohibited public token {token}")
    error_text = Path("src/core/include/tsunami/core/Error.hpp").read_text(encoding="utf-8")
    for token in ("code()", "message()", "category()", "severity()", "context()", "context_value"):
        if token not in error_text:
            raise ValidationError(f"Error does not expose {token}")
    observer_text = Path("src/core/include/tsunami/core/Observer.hpp").read_text(encoding="utf-8")
    if "using Observation = OperationStatus" not in observer_text:
        raise ValidationError("observer payload must use canonical OperationStatus model")
    if "virtual ~IObserver() = default" not in observer_text:
        raise ValidationError("IObserver must retain a virtual destructor")


def validate_service_source(policy: dict) -> None:
    source = Path("src/application/src/NoSolverApplicationService.cpp").read_text(encoding="utf-8")
    failure = policy["representative_validation_failure"]
    for token in (
        failure["code"],
        failure["message"],
        "DiagnosticCategory::validation",
        "Severity::error",
        "rule_id",
        "case.location.required",
        "state_changed",
        "false",
        "failed_status",
    ):
        if token not in source:
            raise ValidationError(f"application validation path missing {token}")
    if "create_directories" in source or "ifstream" in source or "nlohmann" in source or "yaml" in source.lower():
        raise ValidationError("representative validation path must not parse schema or create files")
    if "DiagnosticCategory::cancellation" not in source:
        raise ValidationError("application cancellation diagnostic category missing")
    if "DiagnosticCategory::unsupported" not in source:
        raise ValidationError("application unsupported diagnostic category missing")


def validate_frontends(policy: dict, targets_policy: dict) -> None:
    cli_fields = set(policy.get("cli_mapping", {}).get("fields", []))
    gui_fields = set(policy.get("gui_mapping", {}).get("fields", []))
    if not CLI_FIELDS.issubset(cli_fields):
        raise ValidationError(f"CLI diagnostic mapping missing fields {sorted(CLI_FIELDS - cli_fields)}")
    if not GUI_FIELDS.issubset(gui_fields):
        raise ValidationError(f"GUI diagnostic mapping missing fields {sorted(GUI_FIELDS - gui_fields)}")
    cli_text = Path("apps/tsunami_cli/main.cpp").read_text(encoding="utf-8")
    gui_text = Path("apps/tsunami_gui/main.cpp").read_text(encoding="utf-8")
    qml_text = "\n".join(
        path.read_text(encoding="utf-8")
        for path in Path("apps/tsunami_gui/qml").glob("**/*.qml")
    )
    for token in ("--diagnostic-smoke", "diagnostic_severity", "diagnostic_category", "diagnostic_code", "diagnostic_context_preserved"):
        if token not in cli_text:
            raise ValidationError(f"CLI diagnostic smoke missing {token}")
    for token in ("--diagnostic-smoke", "ShellController", "diagnosticCode", "diagnosticSeverity", "diagnosticCategory", "diagnosticContextPreserved"):
        if token not in gui_text:
            raise ValidationError(f"GUI diagnostic smoke missing {token}")
    for token in ("diagnosticCode", "diagnosticMessage", "diagnosticRuleId", "Diagnostic:"):
        if token not in qml_text:
            raise ValidationError(f"QML diagnostic presentation missing {token}")
    targets = index_by(targets_policy.get("targets", []), "target_name", "targets")
    for frontend in ("tsunami_cli", "tsunami_gui"):
        if "tsunami_application" not in targets[frontend].get("allowed_direct_project_dependencies", []):
            raise ValidationError(f"{frontend} must depend on tsunami_application")
    app_deps = set(targets["tsunami_application"].get("allowed_direct_project_dependencies", []))
    if app_deps != {"tsunami_core", "tsunami_data"}:
        raise ValidationError("application target dependency boundary changed unexpectedly")


def validate_service_policy(services: dict) -> None:
    failure = services.get("representative_validation_failure", {})
    if failure.get("code") != "application.validation.case_location_required":
        raise ValidationError("service policy missing representative validation code")
    if set(failure.get("required_context", {})) != REQUIRED_CONTEXT:
        raise ValidationError("service policy representative context is incomplete")
    if services.get("no_solver_behaviour", {}).get("observer_preserves_error_context") is not True:
        raise ValidationError("service policy must require observer/error context preservation")


def print_report(policy: dict) -> None:
    print(f"diagnostic policy: {policy['policy_version']}")
    print(f"service policy: {policy['references']['service_policy']['policy_version']}")
    print(f"interface policy: {policy['references']['interface_policy']['policy_version']}")
    print(f"severity levels: {len(policy['severity_levels'])}")
    print(f"diagnostic categories: {len(policy['diagnostic_categories'])}")
    print(f"operation states: {len(policy['operation_states'])}")
    print(f"representative code: {policy['representative_validation_failure']['code']}")
    print("diagnostic headers: passed")
    print("observer payload: passed")
    print("application propagation: passed")
    print("frontend mappings: passed")
    print("cancellation and logging rules: passed")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--diagnostics", required=True, type=Path, help="diagnostic failure policy JSON")
    parser.add_argument("--services", required=True, type=Path, help="application service policy JSON")
    parser.add_argument("--interfaces", required=True, type=Path, help="interface contract policy JSON")
    parser.add_argument("--cases", required=True, type=Path, help="case lifecycle policy JSON")
    parser.add_argument("--targets", required=True, type=Path, help="target dependency policy JSON")
    parser.add_argument("--layers", required=True, type=Path, help="layer ownership policy JSON")
    args = parser.parse_args(argv)
    try:
        policy = load_json(args.diagnostics)
        services = load_json(args.services)
        interfaces = load_json(args.interfaces)
        cases = load_json(args.cases)
        targets = load_json(args.targets)
        layers = load_json(args.layers)
        validate_references(policy, services, interfaces, cases, targets, layers)
        validate_policy_model(policy)
        validate_headers(policy)
        validate_service_source(policy)
        validate_frontends(policy, targets)
        validate_service_policy(services)
        print_report(policy)
    except (OSError, json.JSONDecodeError, ValidationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
