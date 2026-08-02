#!/usr/bin/env python3
"""Validate the accepted Regional2D FVM mesh policy against source evidence."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


class ValidationError(Exception):
    """Raised when mesh policy or source evidence is inconsistent."""


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


def extract_target(policy: dict, name: str) -> dict:
    for target in policy.get("targets", []):
        if target.get("target_name") == name:
            return target
    raise ValidationError(f"target policy missing {name}")


def source_has_registered_test(tests_cmake: str, filename: str) -> bool:
    return filename in tests_cmake and "catch_discover_tests(tsunami_tests)" in tests_cmake


def validate_policy(policy: dict) -> None:
    require(policy.get("policy_version") == "0.1", "mesh policy version must be 0.1")
    wbs = policy.get("wbs", {})
    require(wbs.get("work_package") == "SWE-FVM-MSH-WP1", "mesh policy must reference SWE-FVM-MSH-WP1")
    require({174, 175, 176, 177}.issubset(set(wbs.get("github_issues", []))), "mesh policy must map issues 174-177")
    scope = policy.get("scope", {})
    require(scope.get("spatial_dimension") == 2, "Regional2D mesh dimension must be 2")
    require("triangle" in scope.get("accepted_cells", []), "triangular cells must be accepted")
    require("two_point_edge" in scope.get("accepted_faces", []), "two-point edges must be accepted")
    topology = policy.get("topology", {})
    prohibited = set(topology.get("raw_topology_prohibited_fields", []))
    require({"centroid", "measure", "unit_normal", "area_vector"}.issubset(prohibited), "raw topology must prohibit derived fields")
    records = topology.get("records", {})
    require(records.get("CellRecord") == ["id", "faces"], "CellRecord must be face-addressed only")
    geometry = policy.get("geometry", {}).get("records", {})
    require(geometry.get("FaceGeometry") == ["centroid", "area_vector"], "FaceGeometry fields are incorrect")
    require(geometry.get("CellGeometry") == ["centroid", "measure"], "CellGeometry fields are incorrect")
    orientation = policy.get("owner_neighbour", {})
    require("owner" in orientation.get("internal_face", ""), "internal orientation must mention owner")
    require("neighbour" in orientation.get("internal_face", ""), "internal orientation must mention neighbour")
    require("outward" in orientation.get("boundary_face", ""), "boundary orientation must be outward")
    require("closure" in orientation, "closure invariant must be documented")
    unsupported = set(policy.get("unsupported", []))
    require({"dimension_not_2", "arbitrary_polygons", "quads", "3d_polyhedra"}.issubset(unsupported), "unsupported topology list is incomplete")


def validate_sources(root: Path, policy: dict, target_policy: dict) -> None:
    records = read_text(root / "src/fvm/include/tsunami/fvm/MeshRecords.hpp")
    geometry = read_text(root / "src/fvm/include/tsunami/fvm/MeshGeometry.hpp")
    finite_volume = read_text(root / "src/fvm/include/tsunami/fvm/FiniteVolumeMesh.hpp")
    implementation = read_text(root / "src/fvm/src/FiniteVolumeMesh.cpp")
    src_cmake = read_text(root / "src/CMakeLists.txt")
    tests_cmake = read_text(root / "tests/CMakeLists.txt")
    fixture_path = root / policy["fixture"]["path"]
    fixture = read_text(fixture_path)

    face_record = re.search(r"struct FaceRecord\s*\{(?P<body>.*?)\n\s*\};", records, re.S)
    cell_record = re.search(r"struct CellRecord\s*\{(?P<body>.*?)\n\s*\};", records, re.S)
    require(face_record is not None, "FaceRecord not found")
    require(cell_record is not None, "CellRecord not found")
    raw_record_text = face_record.group("body") + cell_record.group("body")
    for token in ("centroid", "measure", "unit_normal", "area_vector"):
        require(token not in raw_record_text, f"raw topology record contains derived field {token}")
    require("std::vector<FaceId> faces" in cell_record.group("body"), "CellRecord must store FaceId faces")
    require("std::vector<VertexId>" not in cell_record.group("body"), "CellRecord must not store authoritative vertices")

    require("struct FaceGeometry" in geometry and "area_vector" in geometry, "FaceGeometry area vector missing")
    require("struct CellGeometry" in geometry and "measure" in geometry, "CellGeometry measure missing")
    require("make_finite_volume_mesh" in finite_volume, "factory declaration missing")
    require("Result<FiniteVolumeMesh>" in finite_volume, "factory must return Result<FiniteVolumeMesh>")
    for code in (
        "fvm.mesh.invalid_dimension",
        "fvm.mesh.face_vertex_count_unsupported",
        "fvm.mesh.cell_face_count_unsupported",
        "fvm.mesh.face_orientation_ambiguous",
        "fvm.mesh.cell_closure_failed",
    ):
        require(code in implementation, f"implementation missing diagnostic code {code}")
    require("DiagnosticCategory::numerical" in implementation, "mesh failures must use numerical diagnostics")
    require("state_changed" in implementation and "false" in implementation, "failure context must report state_changed=false")

    require("add_library(tsunami_fvm STATIC" in src_cmake, "tsunami_fvm must be a static library")
    require("target_link_libraries(tsunami_fvm PUBLIC tsunami_core)" in src_cmake, "tsunami_fvm must link only tsunami_core")
    fvm_block = src_cmake.split("add_library(tsunami_mesh_gmsh", 1)[0]
    for blocked in ("Qt", "Qt6::", "CLI11", "HDF5", "GDAL", "Gmsh", "Eigen3::Eigen"):
        require(blocked not in fvm_block, f"tsunami_fvm CMake block references {blocked}")

    for header in (root / "src/fvm/include/tsunami/fvm").glob("*.hpp"):
        text = read_text(header)
        for blocked in ("Qt", "QObject", "QML"):
            require(blocked not in text, f"{header}: public FVM header references {blocked}")

    require(fixture_path.exists(), "reference mesh fixture is missing")
    for token in ("south", "east", "north", "west", "CellRecord{CellId{0}", "CellRecord{CellId{1}"):
        require(token in fixture, f"fixture missing {token}")
    require(source_has_registered_test(tests_cmake, "fvm/mesh_topology_tests.cpp"), "mesh topology tests are not registered")
    require("fvm/mesh_records_tests.cpp" in tests_cmake, "mesh record tests are not registered")

    target = extract_target(target_policy, "tsunami_fvm")
    require(target.get("type") == "static_library", "target policy must classify tsunami_fvm as static_library")
    require(target.get("allowed_direct_project_dependencies") == ["tsunami_core"], "target policy must allow only tsunami_core")
    prohibited = set(target.get("prohibited_external_dependencies", []))
    require({"Qt", "CLI11", "Catch2", "Eigen", "GDAL", "HDF5"}.issubset(prohibited), "target policy missing prohibited FVM externals")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."), help="repository root")
    parser.add_argument("--policy", type=Path, default=Path("architecture/regional_2d_mesh_policy_v0.1.json"))
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
    print("regional 2D mesh policy: passed")
    print("topology/geometry separation: passed")
    print("target dependency isolation: passed")
    print("fixture and tests: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
