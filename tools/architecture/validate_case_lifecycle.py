#!/usr/bin/env python3
"""Validate the simulation-case lifecycle architecture policy."""

from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict, deque
from pathlib import Path


MUTABILITY_CLASSES = {"mutable", "versioned", "append_only", "immutable"}
RESTART_CLASSES = {"exact_resume", "compatible_resume", "migration_required", "replay_only", "incompatible"}
PROVENANCE_CATEGORIES = {
    "source_identity",
    "build_identity",
    "execution_identity",
    "scientific_input_identity",
    "numerical_identity",
    "output_identity",
}
RUN_TERMINAL_GUARDS = {"completed", "failed", "cancelled"}


class ValidationError(Exception):
    """Raised when the case lifecycle policy violates architectural rules."""


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValidationError(f"{path}: root must be a JSON object")
    return value


def index_by_name(items: list[dict], label: str) -> dict[str, dict]:
    result: dict[str, dict] = {}
    if not isinstance(items, list):
        raise ValidationError(f"{label}: must be a list")
    for item in items:
        name = item.get("name")
        if not isinstance(name, str) or not name:
            raise ValidationError(f"{label}: every entry requires a non-empty name")
        if name in result:
            raise ValidationError(f"{label}: duplicate state name: {name}")
        result[name] = item
    return result


def reachable_from(initial: str, transitions: list[dict]) -> set[str]:
    edges: dict[str, set[str]] = defaultdict(set)
    for transition in transitions:
        edges[transition["from"]].add(transition["to"])
    seen = {initial}
    queue: deque[str] = deque([initial])
    while queue:
        state = queue.popleft()
        for destination in edges[state]:
            if destination not in seen:
                seen.add(destination)
                queue.append(destination)
    return seen


def can_reach(source: str, destination: str, transitions: list[dict]) -> bool:
    return destination in reachable_from(source, transitions)


def validate_references(policy: dict, targets: dict, layers: dict) -> None:
    refs = policy.get("references", {})
    target_ref = refs.get("target_policy", {})
    layer_ref = refs.get("layer_policy", {})
    if target_ref.get("policy_version") != targets.get("policy_version"):
        raise ValidationError("case policy target-policy version reference does not match target policy")
    if layer_ref.get("policy_version") != layers.get("policy_version"):
        raise ValidationError("case policy layer-policy version reference does not match layer policy")


def validate_state_machine(machine: dict, label: str) -> tuple[dict[str, dict], list[dict]]:
    if not isinstance(machine, dict):
        raise ValidationError(f"{label}: state machine must be an object")
    states = index_by_name(machine.get("states", []), f"{label} states")
    initial = machine.get("initial_state")
    if initial not in states:
        raise ValidationError(f"{label}: initial state is unknown: {initial}")
    terminal_states = set(machine.get("terminal_states", []))
    unknown_terminal = terminal_states - set(states)
    if unknown_terminal:
        raise ValidationError(f"{label}: terminal state is unknown: {sorted(unknown_terminal)}")

    transitions = machine.get("transitions", [])
    if not isinstance(transitions, list):
        raise ValidationError(f"{label}: transitions must be a list")
    for transition in transitions:
        source = transition.get("from")
        destination = transition.get("to")
        event = transition.get("event")
        guard = transition.get("guard")
        if source not in states:
            raise ValidationError(f"{label}: transition source is unknown: {source}")
        if destination not in states:
            raise ValidationError(f"{label}: transition destination is unknown: {destination}")
        if not isinstance(event, str) or not event:
            raise ValidationError(f"{label}: transition {source}->{destination} requires an event")
        if not isinstance(guard, str) or not guard:
            raise ValidationError(f"{label}: transition {source}->{destination} requires a guard")

    reachable = reachable_from(initial, transitions)
    unreachable = set(states) - reachable
    if unreachable:
        raise ValidationError(f"{label}: unreachable state(s): {sorted(unreachable)}")
    unreachable_terminal = terminal_states - reachable
    if unreachable_terminal:
        raise ValidationError(f"{label}: terminal state(s) not reachable: {sorted(unreachable_terminal)}")
    return states, transitions


def validate_case_machine(machine: dict, states: dict[str, dict], transitions: list[dict]) -> None:
    if "archived" not in states:
        raise ValidationError("case states: archived state is required")
    for transition in transitions:
        if transition["from"] == "archived":
            raise ValidationError("case states: archived must not have outgoing transitions")


def validate_run_machine(machine: dict, states: dict[str, dict], transitions: list[dict]) -> None:
    for guarded in RUN_TERMINAL_GUARDS:
        if guarded not in states:
            raise ValidationError(f"run states: required state missing: {guarded}")
        if can_reach(guarded, "running", transitions):
            raise ValidationError(f"run states: {guarded} must not return to running")
    restart_rule = machine.get("restart_rule", {})
    if restart_rule.get("creates_new_run") is not True:
        raise ValidationError("run restart: restart must create a new run")
    if restart_rule.get("reopens_parent_run") is not False:
        raise ValidationError("run restart: restart must not reopen the parent run")
    if not restart_rule.get("source_reference_required"):
        raise ValidationError("run restart: source run reference is required")
    if "<new-run-id>" not in str(restart_rule.get("new_run_identity_pattern", "")):
        raise ValidationError("run restart: new run identity pattern must include <new-run-id>")
    restart_states = set(machine.get("restart_eligible_states", []))
    unknown_restart_states = restart_states - set(states)
    if unknown_restart_states:
        raise ValidationError(f"run restart: unknown restart-eligible states: {sorted(unknown_restart_states)}")


def validate_paths(policy: dict) -> None:
    paths = policy.get("canonical_paths", [])
    if not isinstance(paths, list) or not paths:
        raise ValidationError("canonical_paths must be a non-empty list")
    seen: set[str] = set()
    for entry in paths:
        path = entry.get("path")
        owner = entry.get("owner")
        mutability = entry.get("mutability")
        if not isinstance(path, str) or not path:
            raise ValidationError("canonical_paths: every entry requires a path")
        if path in seen:
            raise ValidationError(f"canonical_paths: duplicate path: {path}")
        seen.add(path)
        if not isinstance(owner, str) or not owner:
            raise ValidationError(f"canonical_paths: {path} lacks owner")
        if mutability not in MUTABILITY_CLASSES:
            raise ValidationError(f"canonical_paths: {path} has unknown mutability {mutability}")
    classes = set(policy.get("artefact_mutability_classes", {}))
    missing_classes = MUTABILITY_CLASSES - classes
    if missing_classes:
        raise ValidationError(f"artefact mutability classes missing: {sorted(missing_classes)}")


def validate_schema_rules(policy: dict) -> None:
    schema_rules = policy.get("schema_version_rules", {})
    classes = schema_rules.get("schema_classes", [])
    if not isinstance(classes, list) or not classes:
        raise ValidationError("schema rules: schema_classes must be a non-empty list")
    for entry in classes:
        name = entry.get("name")
        behavior = entry.get("version_behavior", {})
        if not isinstance(name, str) or not name:
            raise ValidationError("schema rules: every class requires a name")
        for key in ("major", "minor", "patch", "unknown_major"):
            if not behavior.get(key):
                raise ValidationError(f"schema rules: {name} missing {key} version behaviour")
        if behavior.get("unknown_major") != "reject" or "reject" not in behavior.get("major", ""):
            raise ValidationError(f"schema rules: {name} must reject unknown major versions")


def validate_provenance(policy: dict) -> None:
    provenance = policy.get("provenance", {})
    mandatory = set(provenance.get("mandatory_categories", []))
    categories = index_by_name(provenance.get("categories", []), "provenance categories")
    missing_mandatory = PROVENANCE_CATEGORIES - mandatory
    missing_records = PROVENANCE_CATEGORIES - set(categories)
    if missing_mandatory or missing_records:
        raise ValidationError(
            f"provenance categories incomplete: mandatory_missing={sorted(missing_mandatory)} "
            f"records_missing={sorted(missing_records)}"
        )
    for name, entry in categories.items():
        if not entry.get("owner"):
            raise ValidationError(f"provenance category lacks owner: {name}")
        if not entry.get("fields"):
            raise ValidationError(f"provenance category lacks fields: {name}")


def validate_restart_compatibility(policy: dict) -> None:
    entries = index_by_name(policy.get("restart_compatibility", {}).get("classes", []), "restart compatibility classes")
    missing = RESTART_CLASSES - set(entries)
    if missing:
        raise ValidationError(f"restart compatibility classes missing: {sorted(missing)}")
    for name, entry in entries.items():
        if "allows_restart" not in entry or "requires_migration" not in entry:
            raise ValidationError(f"restart compatibility class incomplete: {name}")


def validate_machine_separation(policy: dict) -> None:
    if policy.get("case_state_machine") is policy.get("run_state_machine"):
        raise ValidationError("case and run state machines must be separate objects")
    if not isinstance(policy.get("case_state_machine"), dict) or not isinstance(policy.get("run_state_machine"), dict):
        raise ValidationError("case and run state machines must both be defined")


def print_report(policy: dict, case_states: dict[str, dict], run_states: dict[str, dict]) -> None:
    print(f"case lifecycle policy: {policy['policy_version']}")
    print(f"target policy: {policy['references']['target_policy']['policy_version']}")
    print(f"layer policy: {policy['references']['layer_policy']['policy_version']}")
    print(f"case states: {len(case_states)}")
    print(f"run states: {len(run_states)}")
    print(f"canonical paths: {len(policy['canonical_paths'])}")
    print("case state machine: passed")
    print("run state machine: passed")
    print("artefact ownership: passed")
    print("schema compatibility: passed")
    print("provenance categories: passed")
    print("restart compatibility: passed")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", required=True, type=Path, help="case lifecycle policy JSON")
    parser.add_argument("--targets", required=True, type=Path, help="target dependency policy JSON")
    parser.add_argument("--layers", required=True, type=Path, help="layer ownership policy JSON")
    args = parser.parse_args(argv)
    try:
        policy = load_json(args.policy)
        targets = load_json(args.targets)
        layers = load_json(args.layers)
        if not policy.get("policy_version"):
            raise ValidationError("case lifecycle policy requires policy_version")
        validate_references(policy, targets, layers)
        validate_machine_separation(policy)
        case_states, case_transitions = validate_state_machine(policy["case_state_machine"], "case")
        run_states, run_transitions = validate_state_machine(policy["run_state_machine"], "run")
        validate_case_machine(policy["case_state_machine"], case_states, case_transitions)
        validate_run_machine(policy["run_state_machine"], run_states, run_transitions)
        validate_paths(policy)
        validate_schema_rules(policy)
        validate_provenance(policy)
        validate_restart_compatibility(policy)
        print_report(policy, case_states, run_states)
    except (OSError, json.JSONDecodeError, ValidationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
