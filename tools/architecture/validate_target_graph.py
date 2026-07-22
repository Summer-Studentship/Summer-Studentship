#!/usr/bin/env python3
"""Validate the Tsunami target dependency policy and optional CMake graph."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path


PROJECT_PREFIX = "tsunami_"
GENERATED_PROJECT_SUFFIXES = ("_tooling",)


class ValidationError(Exception):
    """Raised for policy or graph validation errors."""


def load_policy(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        policy = json.load(handle)
    if not isinstance(policy, dict):
        raise ValidationError("policy root must be a JSON object")
    if not policy.get("policy_version"):
        raise ValidationError("policy_version is required")
    if not isinstance(policy.get("targets"), list):
        raise ValidationError("targets must be a list")
    return policy


def target_map(policy: dict) -> dict[str, dict]:
    targets: dict[str, dict] = {}
    for entry in policy["targets"]:
        name = entry.get("target_name")
        if not isinstance(name, str) or not name:
            raise ValidationError("every target requires a non-empty target_name")
        if name in targets:
            raise ValidationError(f"duplicate target name: {name}")
        if not name.startswith(PROJECT_PREFIX):
            raise ValidationError(f"project target does not use {PROJECT_PREFIX} prefix: {name}")
        if not entry.get("wbs_owner"):
            raise ValidationError(f"{name}: wbs_owner is required")
        if not entry.get("state") in {"active", "planned"}:
            raise ValidationError(f"{name}: state must be active or planned")
        if not entry.get("classification"):
            raise ValidationError(f"{name}: classification is required")
        for list_key in (
            "allowed_direct_project_dependencies",
            "prohibited_direct_project_dependencies",
            "owned_external_dependencies",
            "prohibited_external_dependencies",
        ):
            if not isinstance(entry.get(list_key), list):
                raise ValidationError(f"{name}: {list_key} must be a list")
        targets[name] = entry
    return targets


def declared_edges(targets: dict[str, dict]) -> dict[str, set[str]]:
    edges: dict[str, set[str]] = {}
    for name, entry in targets.items():
        deps = set(entry["allowed_direct_project_dependencies"])
        unknown = deps - set(targets)
        if unknown:
            raise ValidationError(f"{name}: allowed dependency references unknown target(s): {sorted(unknown)}")
        unknown_prohibited = set(entry["prohibited_direct_project_dependencies"]) - set(targets)
        if unknown_prohibited:
            raise ValidationError(
                f"{name}: prohibited dependency references unknown target(s): {sorted(unknown_prohibited)}"
            )
        overlap = deps & set(entry["prohibited_direct_project_dependencies"])
        if overlap:
            raise ValidationError(f"{name}: dependency is both allowed and prohibited: {sorted(overlap)}")
        edges[name] = deps
    return edges


def assert_acyclic(edges: dict[str, set[str]], label: str) -> None:
    visiting: set[str] = set()
    visited: set[str] = set()
    stack: list[str] = []

    def visit(node: str) -> None:
        if node in visited:
            return
        if node in visiting:
            cycle_start = stack.index(node)
            cycle = stack[cycle_start:] + [node]
            raise ValidationError(f"{label}: cycle detected: {' -> '.join(cycle)}")
        visiting.add(node)
        stack.append(node)
        for dep in sorted(edges.get(node, set())):
            visit(dep)
        stack.pop()
        visiting.remove(node)
        visited.add(node)

    for node in sorted(edges):
        visit(node)


def policy_rule_checks(targets: dict[str, dict]) -> None:
    for name, entry in targets.items():
        deps = set(entry["allowed_direct_project_dependencies"])
        prohibited = set(entry["prohibited_direct_project_dependencies"])
        classification = entry["classification"]
        for dep in sorted(deps):
            dep_classification = targets[dep]["classification"]
            if dep in prohibited:
                raise ValidationError(f"{name}: allowed dependency is prohibited: {dep}")
            if dep_classification == "frontend" and classification not in {"frontend", "test"}:
                raise ValidationError(f"{name}: production target may not depend on frontend {dep}")
            if dep_classification == "test" and classification != "test":
                raise ValidationError(f"{name}: production target may not depend on test target {dep}")
            if dep_classification in {"adapter", "optional_adapter"} and classification not in {
                "adapter",
                "optional_adapter",
                "application",
                "test",
            }:
                raise ValidationError(f"{name}: domain/solver target may not depend on concrete adapter {dep}")
        if classification in {"adapter", "optional_adapter"}:
            contracts = set(entry.get("implements_contracts", []))
            if not contracts:
                raise ValidationError(f"{name}: adapter must declare implements_contracts")
            unknown_contracts = contracts - set(targets)
            if unknown_contracts:
                raise ValidationError(f"{name}: unknown implemented contract(s): {sorted(unknown_contracts)}")
            if not deps & contracts:
                raise ValidationError(f"{name}: adapter must depend on at least one implemented contract")
            for contract in contracts:
                if name in set(targets[contract]["allowed_direct_project_dependencies"]):
                    raise ValidationError(f"{contract}: contract/domain target must not depend on adapter {name}")
        for ext in entry["owned_external_dependencies"]:
            if ext in entry["prohibited_external_dependencies"]:
                raise ValidationError(f"{name}: external dependency is both owned and prohibited: {ext}")


def normalize_cmake_label(label: str) -> str:
    collapsed = label.replace("\\n", " ").strip()
    alias_match = re.match(r"^(?P<target>tsunami_[A-Za-z0-9_]+)\s+\(.+\)$", collapsed)
    if alias_match:
        return alias_match.group("target")
    return collapsed


def is_project_architecture_target(label: str) -> bool:
    return label.startswith(PROJECT_PREFIX) and not label.endswith(GENERATED_PROJECT_SUFFIXES)


def parse_cmake_graph(path: Path) -> tuple[set[str], set[tuple[str, str]], dict[str, set[str]]]:
    node_labels: dict[str, str] = {}
    edges: set[tuple[str, str]] = set()
    imported: dict[str, set[str]] = defaultdict(set)
    node_re = re.compile(r'^\s*"(?P<id>node\d+)"\s+\[\s+label\s*=\s*"(?P<label>[^"]+)"')
    edge_re = re.compile(r'^\s*"(?P<src>node\d+)"\s*->\s*"(?P<dst>node\d+)"')

    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            node_match = node_re.search(line)
            if node_match:
                label = normalize_cmake_label(node_match.group("label"))
                node_labels[node_match.group("id")] = label
                continue
            edge_match = edge_re.search(line)
            if edge_match:
                edges.add((edge_match.group("src"), edge_match.group("dst")))

    project_nodes = {
        node_id: label for node_id, label in node_labels.items() if is_project_architecture_target(label)
    }
    project_targets = set(project_nodes.values())
    project_edges: set[tuple[str, str]] = set()

    for src_id, dst_id in edges:
        src = node_labels.get(src_id)
        dst = node_labels.get(dst_id)
        if not src or not dst:
            continue
        if is_project_architecture_target(src) and is_project_architecture_target(dst):
            if src != dst:
                project_edges.add((src, dst))
        elif is_project_architecture_target(src) and not (
            dst.startswith(PROJECT_PREFIX) and dst.endswith(GENERATED_PROJECT_SUFFIXES)
        ):
            imported[src].add(dst)

    return project_targets, project_edges, dict(imported)


def external_owner_allowed(external_name: str, target_name: str, policy: dict, target: dict) -> bool:
    if external_name in target["owned_external_dependencies"]:
        return True
    owners = policy.get("rules", {}).get("external_framework_owners", {})
    allowed = set(owners.get(external_name, []))
    if target_name in allowed:
        return True
    for owned in target["owned_external_dependencies"]:
        if owned in external_name or external_name in owned:
            return True
    for key, key_owners in owners.items():
        if key in external_name and target_name in set(key_owners):
            return True
    return False


def graph_checks(
    policy: dict,
    targets: dict[str, dict],
    project_targets: set[str],
    graph_edges: set[tuple[str, str]],
    imported: dict[str, set[str]],
) -> list[str]:
    notes: list[str] = []
    unknown_project_targets = project_targets - set(targets)
    if unknown_project_targets:
        raise ValidationError(f"CMake graph contains unknown project target(s): {sorted(unknown_project_targets)}")

    active_policy_targets = {name for name, entry in targets.items() if entry["state"] == "active"}
    missing_active = active_policy_targets - project_targets
    extra_planned = {name for name in project_targets if targets[name]["state"] == "planned"}
    if missing_active:
        notes.append(f"active policy targets absent from this configuration: {', '.join(sorted(missing_active))}")
    if extra_planned:
        notes.append(f"planned policy targets present in CMake graph: {', '.join(sorted(extra_planned))}")

    project_graph: dict[str, set[str]] = {name: set() for name in project_targets}
    for src, dst in sorted(graph_edges):
        project_graph.setdefault(src, set()).add(dst)
        if dst not in targets[src]["allowed_direct_project_dependencies"]:
            raise ValidationError(f"CMake graph edge is not allowed by policy: {src} -> {dst}")
        if dst in targets[src]["prohibited_direct_project_dependencies"]:
            raise ValidationError(f"CMake graph edge is prohibited by policy: {src} -> {dst}")
        src_class = targets[src]["classification"]
        dst_class = targets[dst]["classification"]
        if dst_class == "frontend" and src_class not in {"frontend", "test"}:
            raise ValidationError(f"CMake graph has production-to-frontend edge: {src} -> {dst}")
        if dst_class == "test" and src_class != "test":
            raise ValidationError(f"CMake graph has production-to-test edge: {src} -> {dst}")
        if dst_class in {"adapter", "optional_adapter"} and src_class not in {
            "adapter",
            "optional_adapter",
            "application",
            "test",
        }:
            raise ValidationError(f"CMake graph has domain-to-adapter inversion: {src} -> {dst}")

    assert_acyclic(project_graph, "CMake project-target graph")

    for owner, externals in sorted(imported.items()):
        if owner not in targets:
            continue
        target = targets[owner]
        for external in sorted(externals):
            for prohibited in target["prohibited_external_dependencies"]:
                if prohibited in external:
                    raise ValidationError(f"{owner}: prohibited external dependency observed: {external}")
            if not external_owner_allowed(external, owner, policy, target):
                raise ValidationError(f"{owner}: external dependency has no ownership allowance: {external}")
    return notes


def print_report(
    policy: dict,
    targets: dict[str, dict],
    edges: dict[str, set[str]],
    graph_path: Path | None,
    graph_targets: set[str] | None,
    graph_edges: set[tuple[str, str]] | None,
    imported: dict[str, set[str]] | None,
    notes: list[str],
) -> None:
    active = sorted(name for name, entry in targets.items() if entry["state"] == "active")
    planned = sorted(name for name, entry in targets.items() if entry["state"] == "planned")
    print(f"policy: {policy['policy_version']}")
    print(f"targets: {len(targets)} ({len(active)} active, {len(planned)} planned)")
    print(f"policy edges: {sum(len(deps) for deps in edges.values())}")
    print("policy graph: acyclic")
    print("policy rules: passed")
    if graph_path:
        assert graph_targets is not None
        assert graph_edges is not None
        assert imported is not None
        print(f"cmake graph: {graph_path}")
        print(f"cmake project targets: {', '.join(sorted(graph_targets)) or '(none)'}")
        if graph_edges:
            rendered_edges = ", ".join(f"{src}->{dst}" for src, dst in sorted(graph_edges))
        else:
            rendered_edges = "(none)"
        print(f"cmake project edges: {rendered_edges}")
        if imported:
            rendered_imports = []
            for owner in sorted(imported):
                rendered_imports.append(f"{owner}: {', '.join(sorted(imported[owner]))}")
            print(f"imported dependencies: {'; '.join(rendered_imports)}")
        else:
            print("imported dependencies: (none)")
        print("cmake graph rules: passed")
    for note in notes:
        print(f"note: {note}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--policy", required=True, type=Path, help="target dependency policy JSON")
    parser.add_argument("--cmake-graph", type=Path, help="optional CMake Graphviz .dot file")
    args = parser.parse_args(argv)

    try:
        policy = load_policy(args.policy)
        targets = target_map(policy)
        edges = declared_edges(targets)
        assert_acyclic(edges, "policy graph")
        policy_rule_checks(targets)

        graph_targets = None
        graph_edges = None
        imported = None
        notes: list[str] = []
        if args.cmake_graph:
            graph_targets, graph_edges, imported = parse_cmake_graph(args.cmake_graph)
            notes = graph_checks(policy, targets, graph_targets, graph_edges, imported)

        print_report(policy, targets, edges, args.cmake_graph, graph_targets, graph_edges, imported, notes)
    except (OSError, json.JSONDecodeError, ValidationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
