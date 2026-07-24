#!/usr/bin/env python3
"""Validate public interface-contract policy and headers."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ACTIVE_CONTRACT_TARGETS = {"tsunami_fvm", "tsunami_data", "tsunami_r2d", "tsunami_l3d", "tsunami_coupling"}
FORBIDDEN_LIBRARY_TOKENS = {
    "QObject", "QString", "QVariant", "#include <Q", "CLI::", "Catch::", "#include <catch2",
    "H5::", "hid_t", "GDAL", "OGR", "Gmsh", "matplot", "Eigen::",
}
FALLIBLE_NAMES = (
    "inspect", "read", "write", "begin_transaction", "run", "validate", "replay", "reconstruct"
)


class ValidationError(Exception):
    """Raised when interface policy or public headers violate API rules."""


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


def validate_references(policy: dict, targets: dict, layers: dict, cases: dict) -> None:
    refs = policy.get("references", {})
    if refs.get("target_policy", {}).get("policy_version") != targets.get("policy_version"):
        raise ValidationError("interface policy target-policy version reference does not match")
    if refs.get("layer_policy", {}).get("policy_version") != layers.get("policy_version"):
        raise ValidationError("interface policy layer-policy version reference does not match")
    if refs.get("case_policy", {}).get("policy_version") != cases.get("policy_version"):
        raise ValidationError("interface policy case-policy version reference does not match")


def read_headers(families: dict[str, dict]) -> dict[Path, str]:
    headers: dict[Path, str] = {}
    for family_name, family in families.items():
        paths = family.get("public_headers", [])
        if not isinstance(paths, list) or not paths:
            raise ValidationError(f"{family_name}: public_headers must be a non-empty list")
        for raw_path in paths:
            path = Path(raw_path)
            if not path.exists():
                raise ValidationError(f"{family_name}: public header does not exist: {path}")
            headers[path] = path.read_text(encoding="utf-8")
    return headers


def validate_targets(policy: dict, families: dict[str, dict], targets: dict[str, dict]) -> None:
    known = set(targets)
    for family_name, family in families.items():
        owner = family.get("owning_target")
        if owner not in targets:
            raise ValidationError(f"{family_name}: unknown owning target: {owner}")
        consumers = set(family.get("permitted_consumer_targets", []))
        dependencies = set(family.get("permitted_dependency_targets", []))
        unknown = (consumers | dependencies | set(family.get("prohibited_dependency_targets", []))) - known
        if unknown:
            raise ValidationError(f"{family_name}: unknown target reference(s): {sorted(unknown)}")
        if family.get("state") == "active" and owner in ACTIVE_CONTRACT_TARGETS:
            target = targets[owner]
            if target.get("state") != "active":
                raise ValidationError(f"{owner}: active contract target is not active in target policy")
            if target.get("type") != "interface_library":
                raise ValidationError(f"{owner}: active contract target must be interface_library")
            if "contract scaffold" not in target.get("status", ""):
                raise ValidationError(f"{owner}: target status must record contract scaffold")
            target_deps = set(target.get("allowed_direct_project_dependencies", []))
            if not dependencies.issubset(target_deps):
                raise ValidationError(f"{family_name}: target policy lacks permitted dependencies {sorted(dependencies - target_deps)}")

    active_missing = ACTIVE_CONTRACT_TARGETS - {family["owning_target"] for family in families.values()}
    if active_missing:
        raise ValidationError(f"active contract targets lack contract families: {sorted(active_missing)}")


def validate_classifications(policy: dict, families: dict[str, dict]) -> None:
    classifications = policy.get("classifications", {})
    for key in ("ownership", "mutability", "borrowed_lifetime", "error_model", "thread_safety"):
        if not classifications.get(key):
            raise ValidationError(f"classifications missing: {key}")
    for family_name, family in families.items():
        if family.get("ownership_model") not in classifications["ownership"]:
            raise ValidationError(f"{family_name}: unknown ownership model")
        if family.get("mutability_model") not in classifications["mutability"]:
            raise ValidationError(f"{family_name}: unknown mutability model")
        if family.get("borrowed_lifetime_model") not in classifications["borrowed_lifetime"]:
            raise ValidationError(f"{family_name}: unknown borrowed lifetime model")
        if family.get("error_model") not in classifications["error_model"]:
            raise ValidationError(f"{family_name}: unknown error model")
        if family.get("thread_safety") not in classifications["thread_safety"]:
            raise ValidationError(f"{family_name}: unknown thread-safety model")


def validate_forbidden_tokens(families: dict[str, dict], headers: dict[Path, str]) -> None:
    header_to_family: dict[Path, dict] = {}
    for family in families.values():
        for header in family["public_headers"]:
            header_to_family[Path(header)] = family
    for path, text in headers.items():
        family = header_to_family[path]
        tokens = set(family.get("prohibited_external_type_tokens", [])) | FORBIDDEN_LIBRARY_TOKENS
        for token in sorted(tokens):
            if token in text:
                raise ValidationError(f"{path}: prohibited public-header token: {token}")
        if "src/fvm/include" in str(path) and "Eigen" in text:
            raise ValidationError(f"{path}: Eigen must not enter G0 FVM public inspection contracts")
        if "src/data/include" in str(path) and ("H5::" in text or "hid_t" in text):
            raise ValidationError(f"{path}: HDF5 types must not enter data contracts")


def validate_interfaces(headers: dict[Path, str]) -> None:
    for path, text in headers.items():
        for class_name in re.findall(r"class\s+(I[A-Z][A-Za-z0-9_]+)\s*(?:final\s*)?(?::|\{)", text):
            destructor = re.compile(rf"virtual\s+~{re.escape(class_name)}\s*\(\s*\)\s*=\s*default\s*;")
            if not destructor.search(text):
                raise ValidationError(f"{path}: abstract interface lacks virtual destructor: {class_name}")
        pending_virtual = ""
        for line in text.splitlines():
            stripped = line.strip()
            if "virtual auto" in stripped:
                pending_virtual = stripped
            elif pending_virtual:
                pending_virtual = f"{pending_virtual} {stripped}"
            declaration = pending_virtual if pending_virtual and (";" in pending_virtual or "= 0" in pending_virtual) else stripped
            if any(name in declaration for name in FALLIBLE_NAMES) and "virtual auto" in declaration and "->" in declaration and "Result<" not in declaration:
                raise ValidationError(f"{path}: fallible interface operation must return Result: {declaration}")
            if re.search(r"virtual\s+auto\s+\w+.*->\s*[^;]*\*", declaration):
                raise ValidationError(f"{path}: raw pointer return in public interface: {declaration}")
            if pending_virtual and ";" in pending_virtual:
                pending_virtual = ""


def validate_solver_independence(headers: dict[Path, str]) -> None:
    for path, text in headers.items():
        raw = str(path)
        if "src/r2d/include" in raw and ("tsunami/l3d" in text or "tsunami::l3d" in text):
            raise ValidationError(f"{path}: R2D public header references L3D")
        if "src/l3d/include" in raw and ("tsunami/r2d" in text or "tsunami::r2d" in text):
            raise ValidationError(f"{path}: L3D public header references R2D")
    coupling_text = "\n".join(text for path, text in headers.items() if "src/coupling/include" in str(path))
    if "tsunami/r2d" not in coupling_text or "tsunami/l3d" not in coupling_text:
        raise ValidationError("coupling headers must remain the cross-solver owner")


def compile_headers(policy: dict, headers: dict[Path, str]) -> None:
    compiler = os.environ.get("CXX", "c++")
    include_args: list[str] = []
    for root in policy.get("public_header_roots", []):
        include_args.extend(["-I", root])
    with tempfile.TemporaryDirectory(prefix="interface-header-") as tmp:
        tmpdir = Path(tmp)
        for header in headers:
            marker = "/include/"
            relative = header.as_posix().split(marker, 1)[1] if marker in header.as_posix() else header.as_posix()
            source = tmpdir / (header.name.replace(".", "_") + ".cpp")
            source.write_text(f"#include <{relative}>\nint main() {{ return 0; }}\n", encoding="utf-8")
            command = [compiler, "-std=c++20", "-fsyntax-only", *include_args, str(source)]
            completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
            if completed.returncode != 0:
                raise ValidationError(f"{header}: independent header compile failed:\n{completed.stderr.strip()}")


def print_report(policy: dict, families: dict[str, dict], headers: dict[Path, str]) -> None:
    print(f"interface policy: {policy['policy_version']}")
    print(f"target policy: {policy['references']['target_policy']['policy_version']}")
    print(f"layer policy: {policy['references']['layer_policy']['policy_version']}")
    print(f"case policy: {policy['references']['case_policy']['policy_version']}")
    print(f"contract families: {len(families)}")
    print(f"public headers: {len(headers)}")
    print(f"active contract targets: {', '.join(sorted(ACTIVE_CONTRACT_TARGETS))}")
    print("policy references: passed")
    print("target ownership: passed")
    print("forbidden token scan: passed")
    print("abstract interface destructors: passed")
    print("result-based fallible interfaces: passed")
    print("solver sibling independence: passed")
    print("independent header compilation: passed")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--interfaces", required=True, type=Path, help="interface contract policy JSON")
    parser.add_argument("--targets", required=True, type=Path, help="target dependency policy JSON")
    parser.add_argument("--layers", required=True, type=Path, help="layer ownership policy JSON")
    parser.add_argument("--cases", required=True, type=Path, help="case lifecycle policy JSON")
    args = parser.parse_args(argv)
    try:
        policy = load_json(args.interfaces)
        targets_policy = load_json(args.targets)
        layers_policy = load_json(args.layers)
        cases_policy = load_json(args.cases)
        if not policy.get("policy_version"):
            raise ValidationError("interface policy requires policy_version")
        validate_references(policy, targets_policy, layers_policy, cases_policy)
        targets = index_by(targets_policy.get("targets", []), "target_name", "targets")
        families = index_by(policy.get("contract_families", []), "name", "contract families")
        validate_targets(policy, families, targets)
        validate_classifications(policy, families)
        headers = read_headers(families)
        validate_forbidden_tokens(families, headers)
        validate_interfaces(headers)
        validate_solver_independence(headers)
        compile_headers(policy, headers)
        print_report(policy, families, headers)
    except (OSError, json.JSONDecodeError, subprocess.SubprocessError, ValidationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
