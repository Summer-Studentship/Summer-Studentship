#!/usr/bin/env python3
"""Validate the accepted Regional2D FVM field policy against source evidence."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


class ValidationError(Exception):
    """Raised when field policy or implementation evidence is inconsistent."""


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
    require(policy.get("model_owner") == "Regional2D", "model owner must be Regional2D")
    issues = set(policy.get("wbs", {}).get("github_issues", []))
    require({178, 179, 180, 181}.issubset(issues), "WBS issues #178-#181 must be represented")
    values = {entry.get("name"): entry for entry in policy.get("supported_value_types", [])}
    require(values.get("Real", {}).get("kind") == "scalar", "Real scalar support missing")
    require(values.get("Vector3", {}).get("kind") == "vector", "Vector3 vector support missing")
    locations = set(policy.get("supported_locations", []))
    require({"cell", "face", "boundary_patch"}.issubset(locations), "cell/face/patch locations must be supported")
    require("vertex" in set(policy.get("reserved_unsupported_locations", [])), "vertex must be reserved unsupported")
    ownership = policy.get("ownership", {})
    require(ownership.get("fixed_size_after_construction") is True, "fields must be fixed-size")
    require(ownership.get("public_resize") is False, "public resize must be prohibited")
    move = policy.get("copy_move_policy", {})
    require(move.get("implicit_copy") is False, "implicit field copying must be prohibited")
    require(move.get("explicit_clone") is True, "explicit clone policy missing")
    binding = policy.get("mesh_binding", {})
    require("compatibility_signature" in binding.get("fields", []), "MeshBinding signature missing")
    require(binding.get("uses_more_than_id_and_counts") is True, "compatibility cannot rely only on mesh ID/counts")
    patch = policy.get("patch_field_layout", {})
    require(patch.get("not_one_value_per_patch") is True, "patch fields must not be one value per patch")
    require("BoundaryPatchRecord::faces" in patch.get("local_index_rule", "") or "boundary_patch" in patch.get("local_index_rule", ""), "patch-local ordering rule missing")
    metadata = " ".join(policy.get("metadata", []))
    for token in ("name", "MeshId", "FieldLocation", "unit_id"):
        require(token in metadata, f"descriptor metadata missing {token}")


def validate_sources(root: Path, policy: dict, target_policy: dict) -> None:
    field = read_text(root / "src/fvm/include/tsunami/fvm/Field.hpp")
    traits = read_text(root / "src/fvm/include/tsunami/fvm/FieldTraits.hpp")
    mesh_field = read_text(root / "src/fvm/include/tsunami/fvm/MeshField.hpp")
    patch_field = read_text(root / "src/fvm/include/tsunami/fvm/BoundaryPatchField.hpp")
    binding_header = read_text(root / "src/fvm/include/tsunami/fvm/MeshBinding.hpp")
    binding_impl = read_text(root / "src/fvm/src/MeshBinding.cpp")
    src_cmake = read_text(root / "src/CMakeLists.txt")
    tests_cmake = read_text(root / "tests/CMakeLists.txt")
    tests = read_text(root / "tests/fvm/field_tests.cpp")
    fixture = read_text(root / "tests/fvm/reference_fields.hpp")

    require("enum class FieldValueKind" in traits, "FieldValueKind missing")
    require("FieldValueTraits<tsunami::core::Real>" in traits, "Real traits missing")
    require("FieldValueTraits<Vector3>" in traits, "Vector3 traits missing")
    require("concept SupportedFieldValue" in traits, "SupportedFieldValue concept missing")
    require("std::optional<BoundaryPatchId> boundary_patch" in field, "descriptor missing optional patch id")
    for token in ("MeshId mesh_id", "FieldLocation location", "FieldValueKind value_kind", "component_count", "entity_count", "unit_id"):
        require(token in field, f"FieldDescriptor missing {token}")
    require("class MeshField final" in mesh_field, "MeshField missing")
    require("class BoundaryPatchField final" in patch_field, "BoundaryPatchField missing")
    require("std::span<Value>" in mesh_field and "std::span<const Value>" in mesh_field, "mesh field spans missing")
    require("std::span<Value>" in patch_field and "std::span<const Value>" in patch_field, "patch field spans missing")
    for blocked in ("resize(", "push_back(", "clear()", "reserve("):
        public_text = mesh_field + patch_field
        require(blocked not in public_text, f"field API exposes {blocked}")
    require("= delete" in mesh_field and "clone()" in mesh_field, "move-only/clone mesh policy missing")
    require("= delete" in patch_field and "clone()" in patch_field, "move-only/clone patch policy missing")
    require("MeshBinding" in binding_header and "compatibility_signature" in binding_header, "MeshBinding declaration missing")
    require("fnv_offset_basis" in binding_impl and "append_point" in binding_impl, "deterministic signature algorithm missing")
    for token in ("vertex.position", "face.owner", "face.neighbour", "patch.name", "patch.faces"):
        require(token in binding_impl, f"signature missing {token}")
    require("binding_ != source.binding_" in mesh_field, "copy compatibility must compare MeshBinding")
    require("descriptor_.unit_id != source.descriptor_.unit_id" in mesh_field, "copy compatibility must compare units")
    require("patch_id_ != source.patch_id_" in patch_field, "patch copy compatibility must compare patch ids")
    require("mesh.boundary_patch(patch_id).faces.size()" in patch_field, "patch count must derive from patch faces")
    require("tests/fvm/reference_fields.hpp" in str(root / "tests/fvm/reference_fields.hpp") and "sample_patch_scalar_field" in fixture, "reference field fixtures missing")
    require("multi_face_patch_input" in fixture and "south-east" in fixture, "multi-face patch fixture missing")
    require("fvm/field_tests.cpp" in tests_cmake, "field tests not registered")
    require("catch_discover_tests(tsunami_tests)" in tests_cmake, "Catch2 tests not discoverable")
    require("tsunami_fvm STATIC" in src_cmake, "tsunami_fvm must remain static")
    require("target_link_libraries(tsunami_fvm PUBLIC tsunami_core)" in src_cmake, "tsunami_fvm must link only tsunami_core")
    fvm_block = src_cmake.split("add_library(tsunami_data", 1)[0]
    for blocked in ("Qt", "CLI11", "HDF5", "Gmsh", "GDAL", "Eigen", "tsunami_data", "tsunami_r2d", "tsunami_l3d", "tsunami_coupling"):
        require(blocked not in fvm_block, f"tsunami_fvm block references prohibited dependency {blocked}")
    for header in (root / "src/fvm/include/tsunami/fvm").glob("*.hpp"):
        text = read_text(header)
        for blocked in ("Qt", "QObject", "QML"):
            require(blocked not in text, f"{header}: public FVM header references {blocked}")
    require("FVM field value traits" in tests and "FVM field copy operations" in tests, "field tests incomplete")
    target_entry = target(target_policy, "tsunami_fvm")
    require(target_entry.get("allowed_direct_project_dependencies") == ["tsunami_core"], "target policy must allow only tsunami_core")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--policy", type=Path, default=Path("architecture/regional_2d_field_policy_v0.1.json"))
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
    print("regional 2D field policy: passed")
    print("field ownership and metadata: passed")
    print("mesh binding compatibility: passed")
    print("patch field layout: passed")
    print("target dependency isolation: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
