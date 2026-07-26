#!/usr/bin/env python3
"""Validate the accepted Regional2D FVM boundary-condition policy."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


class ValidationError(Exception):
    """Raised when boundary policy or implementation evidence is inconsistent."""


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValidationError(f"{path}: root must be a JSON object")
    return value


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def target(policy: dict, name: str) -> dict:
    for entry in policy.get("targets", []):
        if entry.get("target_name") == name:
            return entry
    raise ValidationError(f"target policy missing {name}")


def validate_policy(policy: dict) -> None:
    require(policy.get("policy_version") == "0.1", "policy version must be 0.1")
    require(policy.get("wbs", {}).get("work_package") == "SWE-FVM-BC-WP1", "boundary work package missing")
    issues = set(policy.get("wbs", {}).get("github_issues", []))
    require({182, 183, 184, 185}.issubset(issues), "WBS issues #182-#185 must be represented")
    require(policy.get("scope", {}).get("model_owner") == "Regional2D", "model owner must be Regional2D")
    require(policy.get("scope", {}).get("allowed_project_dependencies") == ["tsunami_core"], "boundary target must depend only on tsunami_core")
    kinds = set(policy.get("identity", {}).get("kinds", []))
    require({"fixed_value", "zero_gradient", "named_reference"}.issubset(kinds), "boundary kinds missing")
    descriptor = set(policy.get("identity", {}).get("descriptor", []))
    for token in ("id", "name", "mesh_id", "patch_id", "patch_name", "kind", "value_kind", "component_count", "entity_count", "unit_id", "executable"):
        require(token in descriptor, f"BoundaryDescriptor missing {token}")
    mapping = policy.get("patch_mapping", {})
    require("exactly match" in mapping.get("resolution_rule", ""), "exact patch-tag rule missing")
    require("BoundaryPatchId order" in mapping.get("ordering_rule", ""), "deterministic ordering rule missing")
    require("one boundary condition per mesh boundary patch" in mapping.get("coverage_rule", ""), "complete coverage rule missing")
    operations = policy.get("operations", {})
    require(operations.get("fixed_value", {}).get("executable") is True, "fixed-value must be executable")
    require(operations.get("zero_gradient", {}).get("executable") is True, "zero-gradient must be executable")
    require(operations.get("named_reference", {}).get("executable") is False, "named references must be non-executable")
    require("target[j] = internal[mesh.face(patch.faces[j]).owner]" in operations.get("zero_gradient", {}).get("mapping", ""), "zero-gradient owner mapping missing")
    require(policy.get("transactionality", {}).get("failed_apply_preserves_destination") is True, "transactionality rule missing")
    codes = set(policy.get("diagnostic_codes", []))
    for code in (
        "fvm.boundary.id_required",
        "fvm.boundary.id_duplicate",
        "fvm.boundary.name_required",
        "fvm.boundary.patch_tag_required",
        "fvm.boundary.unit_required",
        "fvm.boundary.named_type_required",
        "fvm.boundary.patch_unknown",
        "fvm.boundary.patch_duplicate",
        "fvm.boundary.patch_missing",
        "fvm.boundary.patch_entity_count_mismatch",
        "fvm.boundary.mesh_incompatible",
        "fvm.boundary.internal_field_incompatible",
        "fvm.boundary.target_field_incompatible",
        "fvm.boundary.patch_incompatible",
        "fvm.boundary.value_kind_incompatible",
        "fvm.boundary.unit_incompatible",
        "fvm.boundary.named_condition_not_executable",
        "fvm.boundary.set_incomplete",
        "fvm.boundary.descriptor_inconsistent",
    ):
        require(code in codes, f"diagnostic code missing {code}")


def validate_sources(root: Path, policy: dict, target_policy: dict) -> None:
    boundary = read_text(root / "src/fvm/include/tsunami/fvm/Boundary.hpp")
    specs = read_text(root / "src/fvm/include/tsunami/fvm/BoundarySpecification.hpp")
    fixed = read_text(root / "src/fvm/include/tsunami/fvm/FixedValueBoundary.hpp")
    zero = read_text(root / "src/fvm/include/tsunami/fvm/ZeroGradientBoundary.hpp")
    named = read_text(root / "src/fvm/include/tsunami/fvm/NamedBoundaryReference.hpp")
    wrapper = read_text(root / "src/fvm/include/tsunami/fvm/BoundaryCondition.hpp")
    condition_set = read_text(root / "src/fvm/include/tsunami/fvm/BoundaryConditionSet.hpp")
    tests_cmake = read_text(root / "tests/CMakeLists.txt")
    tests = read_text(root / "tests/fvm/boundary_tests.cpp")
    fixture = read_text(root / "tests/fvm/reference_boundaries.hpp")
    src_cmake = read_text(root / "src/CMakeLists.txt")

    require("struct BoundaryConditionId" in boundary, "BoundaryConditionId missing")
    require("enum class BoundaryConditionKind" in boundary, "BoundaryConditionKind missing")
    require("fixed_value" in boundary and "zero_gradient" in boundary and "named_reference" in boundary, "kind conversions missing")
    for token in ("MeshId mesh_id", "BoundaryPatchId patch_id", "patch_name", "FieldValueKind value_kind", "component_count", "entity_count", "unit_id", "bool executable"):
        require(token in boundary, f"BoundaryDescriptor missing {token}")
    require("class IBoundaryConditionView" in boundary, "descriptor-only view missing")
    require("FixedValueSpecification" in specs and "ZeroGradientSpecification" in specs and "NamedBoundarySpecification" in specs, "boundary specifications missing")
    require("std::variant" in specs and "BoundaryOperationSpecification" in specs, "operation variant missing")
    require("prescribed_values_" in fixed and "copy_values_from" in fixed, "fixed-value copy path missing")
    require("internal.binding() != binding_" in fixed, "fixed-value internal binding validation missing")
    require("mesh.face(face_id).owner" in zero and "target.at(index) = values[index]" in zero, "zero-gradient owner mapping missing")
    require("std::vector<Value> values" in zero and "values.push_back" in zero, "zero-gradient prevalidation buffer missing")
    require("requested_type_" in named and "named_condition_not_executable" in named, "named placeholder failure missing")
    require("std::variant" in wrapper and "FixedValueBoundary" in wrapper and "ZeroGradientBoundary" in wrapper and "NamedBoundaryReference" in wrapper, "BoundaryCondition variant missing")
    require("class BoundaryConditionSet" in condition_set, "BoundaryConditionSet missing")
    require("std::span<const BoundaryCondition<Value>>" in condition_set, "const span inspection missing")
    for blocked in ("auto resize", "auto push_back", "auto clear"):
        public_slice = condition_set.split("private:", 1)[0]
        require(blocked not in public_slice, f"BoundaryConditionSet public API exposes {blocked}")
    require("patches_by_name.find(specification.patch_tag)" in condition_set, "exact patch lookup missing")
    require("seen_patches" in condition_set and "patch_missing" in condition_set, "complete patch coverage validation missing")
    require("std::ranges::sort" in condition_set, "deterministic patch ordering missing")
    for code in policy.get("diagnostic_codes", []):
        if code != "fvm.boundary.value_kind_incompatible":
            require(code in (boundary + fixed + zero + named + wrapper + condition_set + tests), f"source evidence missing {code}")
    require("scalar_boundary_specs" in fixture and "vector_boundary_specs" in fixture, "standard scalar/vector boundary fixtures missing")
    require("multi_face_scalar_boundary_specs" in fixture and "south-east" in fixture, "multi-face boundary fixture missing")
    require("fvm/boundary_tests.cpp" in tests_cmake, "boundary tests not registered")
    for token in ("patch_unknown", "patch_duplicate", "patch_missing", "Fixed-value", "Zero-gradient", "Named boundary", "mesh_incompatible"):
        require(token in tests, f"boundary tests missing {token}")
    require("tsunami_fvm STATIC" in src_cmake, "tsunami_fvm must remain static")
    require("target_link_libraries(tsunami_fvm PUBLIC tsunami_core)" in src_cmake, "tsunami_fvm must link only tsunami_core")
    fvm_block = src_cmake.split("add_library(tsunami_data", 1)[0]
    for blocked in ("Qt", "CLI11", "HDF5", "Gmsh", "GDAL", "Eigen", "tsunami_data", "tsunami_r2d", "tsunami_l3d", "tsunami_coupling"):
        require(blocked not in fvm_block, f"tsunami_fvm block references prohibited dependency {blocked}")
    for header in (root / "src/fvm/include/tsunami/fvm").glob("*.hpp"):
        text = read_text(header)
        for blocked in ("Qt", "QObject", "QML", "tsunami/r2d", "tsunami/l3d", "tsunami/coupling"):
            require(blocked not in text, f"{header}: public FVM header references {blocked}")
    target_entry = target(target_policy, "tsunami_fvm")
    require(target_entry.get("allowed_direct_project_dependencies") == ["tsunami_core"], "target policy must allow only tsunami_core")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--policy", type=Path, default=Path("architecture/regional_2d_boundary_policy_v0.1.json"))
    parser.add_argument("--targets", type=Path, default=Path("architecture/target_dependency_policy_v0.1.json"))
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        policy = load_json(root / args.policy)
        target_policy = load_json(root / args.targets)
        validate_policy(policy)
        validate_sources(root, policy, target_policy)
    except (OSError, json.JSONDecodeError, ValidationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print("regional 2D boundary policy: passed")
    print("boundary identity and patch mapping: passed")
    print("boundary operation transactionality: passed")
    print("named placeholder contract: passed")
    print("target dependency isolation: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
