#!/usr/bin/env python3
"""Validate layer ownership policy against the target dependency policy."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


EXPECTED_DOMAINS = {
    "SWE-ENV", "SWE-ARC", "SWE-FVM", "SWE-DAT", "SWE-GEO", "SWE-R2D", "SWE-L3D",
    "SWE-CPL", "SWE-GUI", "SWE-VER", "SWE-HPC", "SWE-STR", "SWE-REL",
}
FRAMEWORK_OWNERS = {
    "Qt": {"tsunami_gui"},
    "Qt6::": {"tsunami_gui"},
    "CLI11": {"tsunami_cli"},
    "Catch2": {"tsunami_tests"},
    "Matplot": {"tsunami_diagnostics_matplot"},
}


class ValidationError(Exception):
    """Raised when a layer or target policy violates ownership rules."""


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValidationError(f"{path}: root must be a JSON object")
    return value


def index_by(items: list[dict], key: str, label: str) -> dict[str, dict]:
    result: dict[str, dict] = {}
    for item in items:
        name = item.get(key)
        if not isinstance(name, str) or not name:
            raise ValidationError(f"{label}: missing {key}")
        if name in result:
            raise ValidationError(f"{label}: duplicate {key}: {name}")
        result[name] = item
    return result


def validate_layer_policy_shape(layers_policy: dict, targets_policy: dict) -> tuple[dict[str, dict], dict[str, dict]]:
    if not layers_policy.get("policy_version"):
        raise ValidationError("layer policy requires policy_version")
    target_ref = layers_policy.get("target_policy", {})
    if target_ref.get("policy_version") != targets_policy.get("policy_version"):
        raise ValidationError("layer policy target-policy version reference does not match target policy")

    layers = index_by(layers_policy.get("layers", []), "layer_id", "layers")
    seen_orders: set[int] = set()
    for layer_id, layer in layers.items():
        order = layer.get("layer_order")
        if not isinstance(order, int):
            raise ValidationError(f"{layer_id}: layer_order must be an integer")
        if order in seen_orders:
            raise ValidationError(f"duplicate layer_order: {order}")
        seen_orders.add(order)
        for required in ("name", "classification", "owning_wbs_domains", "associated_targets", "allowed_inward_layers", "status"):
            if required not in layer:
                raise ValidationError(f"{layer_id}: missing {required}")
        for dep_layer in layer["allowed_inward_layers"]:
            if dep_layer not in layers:
                raise ValidationError(f"{layer_id}: unknown allowed inward layer {dep_layer}")

    targets = index_by(targets_policy.get("targets", []), "target_name", "targets")
    return layers, targets


def validate_domain_map(layers_policy: dict, layers: dict[str, dict]) -> None:
    domain_map = layers_policy.get("domain_map", [])
    if not isinstance(domain_map, list):
        raise ValidationError("domain_map must be a list")
    domains: dict[str, str] = {}
    for entry in domain_map:
        domain = entry.get("domain")
        layer = entry.get("governing_layer")
        if domain in domains:
            raise ValidationError(f"domain has multiple governing owners: {domain}")
        if layer not in layers:
            raise ValidationError(f"{domain}: governing layer is unknown: {layer}")
        domains[domain] = layer
    missing = EXPECTED_DOMAINS - set(domains)
    extra = set(domains) - EXPECTED_DOMAINS
    if missing or extra:
        raise ValidationError(f"domain map mismatch: missing={sorted(missing)} extra={sorted(extra)}")


def validate_target_layer_map(layers_policy: dict, layers: dict[str, dict], targets: dict[str, dict]) -> dict[str, str]:
    mapping = layers_policy.get("target_layer_map", {})
    if not isinstance(mapping, dict):
        raise ValidationError("target_layer_map must be an object")
    missing = set(targets) - set(mapping)
    extra = set(mapping) - set(targets)
    if missing or extra:
        raise ValidationError(f"target-layer map mismatch: missing={sorted(missing)} extra={sorted(extra)}")
    for target, layer in mapping.items():
        if layer not in layers:
            raise ValidationError(f"{target}: maps to unknown layer {layer}")
    for target, entry in targets.items():
        if entry.get("state") == "active" and target not in mapping:
            raise ValidationError(f"active target lacks layer mapping: {target}")
    return mapping


def validate_layer_direction(layers: dict[str, dict], targets: dict[str, dict], mapping: dict[str, str]) -> None:
    for target, entry in targets.items():
        source_layer = mapping[target]
        source_class = entry.get("classification")
        allowed_layers = set(layers[source_layer]["allowed_inward_layers"]) | {source_layer}
        for dep in entry.get("allowed_direct_project_dependencies", []):
            dep_layer = mapping[dep]
            dep_class = targets[dep].get("classification")
            if dep_layer not in allowed_layers:
                raise ValidationError(f"{target}: dependency {dep} crosses from {source_layer} to disallowed {dep_layer}")
            if dep_class == "frontend" and source_class not in {"frontend", "test"}:
                raise ValidationError(f"{target}: production target depends on frontend {dep}")
            if dep_class == "test" and source_class != "test":
                raise ValidationError(f"{target}: production target depends on verification target {dep}")
            if dep_class in {"adapter", "optional_adapter"} and source_class not in {"adapter", "optional_adapter", "application", "test"}:
                raise ValidationError(f"{target}: domain target depends on concrete adapter {dep}")
        if source_class == "frontend":
            for other, other_entry in targets.items():
                if other == target:
                    continue
                if target in other_entry.get("allowed_direct_project_dependencies", []) and other_entry.get("classification") not in {"frontend", "test"}:
                    raise ValidationError(f"frontend target is not a leaf: {other} -> {target}")


def validate_solver_and_coupling_rules(targets: dict[str, dict]) -> None:
    r2d_deps = set(targets["tsunami_r2d"].get("allowed_direct_project_dependencies", []))
    l3d_deps = set(targets["tsunami_l3d"].get("allowed_direct_project_dependencies", []))
    if "tsunami_l3d" in r2d_deps:
        raise ValidationError("tsunami_r2d must not depend on tsunami_l3d")
    if "tsunami_r2d" in l3d_deps:
        raise ValidationError("tsunami_l3d must not depend on tsunami_r2d")
    if "tsunami_coupling" in r2d_deps or "tsunami_coupling" in l3d_deps:
        raise ValidationError("solver targets must not depend on tsunami_coupling")
    coupling_deps = set(targets["tsunami_coupling"].get("allowed_direct_project_dependencies", []))
    if not {"tsunami_r2d", "tsunami_l3d"}.issubset(coupling_deps):
        raise ValidationError("tsunami_coupling must own cross-solver relationships")


def validate_external_isolation(targets: dict[str, dict]) -> None:
    for target, entry in targets.items():
        owned = entry.get("owned_external_dependencies", [])
        prohibited = entry.get("prohibited_external_dependencies", [])
        for external in owned:
            for key, owners in FRAMEWORK_OWNERS.items():
                if key in external and target not in owners:
                    raise ValidationError(f"{target}: {external} is isolated to {sorted(owners)}")
            for blocked in prohibited:
                if blocked in external:
                    raise ValidationError(f"{target}: owns prohibited external dependency {external}")


def validate_deferred_layers(layers: dict[str, dict], targets: dict[str, dict], mapping: dict[str, str]) -> None:
    deferred = {layer_id for layer_id, layer in layers.items() if layer.get("classification") == "deferred"}
    for target, entry in targets.items():
        if entry.get("state") == "active" and mapping[target] in deferred:
            raise ValidationError(f"active target maps to deferred layer: {target}")
        if entry.get("state") == "active":
            for dep in entry.get("allowed_direct_project_dependencies", []):
                if mapping[dep] in deferred:
                    raise ValidationError(f"active target depends on deferred layer: {target} -> {dep}")


def print_report(layers_policy: dict, layers: dict[str, dict], targets: dict[str, dict], mapping: dict[str, str]) -> None:
    runtime_layers = sorted(layer_id for layer_id, layer in layers.items() if layer["classification"] == "runtime")
    print(f"layer policy: {layers_policy['policy_version']}")
    print(f"target policy: {layers_policy['target_policy']['policy_version']}")
    print(f"layers: {len(layers)} ({len(runtime_layers)} runtime)")
    print(f"mapped WBS domains: {len(EXPECTED_DOMAINS)}")
    print(f"mapped targets: {len(mapping)}")
    print("domain ownership: passed")
    print("target-layer mapping: passed")
    print("layer direction: passed")
    print("adapter inversion: passed")
    print("frontend leaf: passed")
    print("solver separation: passed")
    print("external framework isolation: passed")
    print("deferred baseline isolation: passed")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layers", required=True, type=Path, help="layer ownership policy JSON")
    parser.add_argument("--targets", required=True, type=Path, help="target dependency policy JSON")
    args = parser.parse_args(argv)
    try:
        layers_policy = load_json(args.layers)
        targets_policy = load_json(args.targets)
        layers, targets = validate_layer_policy_shape(layers_policy, targets_policy)
        validate_domain_map(layers_policy, layers)
        mapping = validate_target_layer_map(layers_policy, layers, targets)
        validate_layer_direction(layers, targets, mapping)
        validate_solver_and_coupling_rules(targets)
        validate_external_isolation(targets)
        validate_deferred_layers(layers, targets, mapping)
        print_report(layers_policy, layers, targets, mapping)
    except (OSError, json.JSONDecodeError, ValidationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
