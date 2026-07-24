#!/usr/bin/env python3
"""Validate the Qt Quick GUI shell policy, resources and target boundary."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


EXPECTED_NAVIGATION = ["data", "domain", "mesh", "physics", "solver", "run", "post_processing"]
REQUIRED_COMPONENTS = {
    "ApplicationHeader.qml", "NavigationRail.qml", "NavigationDelegate.qml",
    "ServiceStatusBar.qml", "DiagnosticPanel.qml", "PlaceholderPage.qml",
}
REQUIRED_SHELL_SMOKE = {"shellReady", "navigationCount", "activeNavigationIndex", "activeSectionId", "activeSectionTitle"}
FORBIDDEN_PUBLIC_QT = {"QObject", "QString", "QVariant", "#include <Q"}
FORBIDDEN_QML_TOKENS = {"HDF5", "GDAL", "Gmsh", "IRegionalSolver", "ILocalSolver", "tsunami::r2d", "tsunami::l3d"}


class ValidationError(Exception):
    """Raised when the GUI shell violates policy."""


def load_json(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValidationError(f"{path}: root must be object")
    return value


def index_by(items: list[dict], key: str, label: str) -> dict[str, dict]:
    out: dict[str, dict] = {}
    for item in items:
        name = item.get(key)
        if not isinstance(name, str) or not name:
            raise ValidationError(f"{label}: missing {key}")
        if name in out:
            raise ValidationError(f"{label}: duplicate {key}: {name}")
        out[name] = item
    return out


def validate_references(gui: dict, diagnostics: dict, services: dict, interfaces: dict, targets: dict, layers: dict) -> None:
    refs = gui.get("references", {})
    expected = {
        "diagnostic_policy": diagnostics,
        "service_policy": services,
        "interface_policy": interfaces,
        "target_policy": targets,
        "layer_policy": layers,
    }
    for name, policy in expected.items():
        if refs.get(name, {}).get("policy_version") != policy.get("policy_version"):
            raise ValidationError(f"{name} version reference does not match")


def validate_cmake(gui: dict, targets_policy: dict) -> None:
    cmake = Path("apps/tsunami_gui/CMakeLists.txt").read_text(encoding="utf-8")
    if f"URI {gui['qml_module_uri']}" not in cmake:
        raise ValidationError("QML module URI mismatch")
    for module in gui.get("required_qt_modules", []):
        if module.split("::")[-1] not in cmake and module not in cmake:
            raise ValidationError(f"required Qt module missing from CMake: {module}")
    for path in [gui["root_qml"], *gui.get("components", []), *gui.get("pages", [])]:
        cmake_path = path.removeprefix("apps/tsunami_gui/")
        if cmake_path not in cmake:
            raise ValidationError(f"QML resource not declared in qt_add_qml_module: {path}")
    targets = index_by(targets_policy.get("targets", []), "target_name", "targets")
    gui_target = targets.get(gui.get("gui_target"))
    if not gui_target:
        raise ValidationError("GUI target missing from target policy")
    direct_deps = set(gui_target.get("allowed_direct_project_dependencies", []))
    if "tsunami_application" not in direct_deps:
        raise ValidationError("GUI target must link tsunami_application")
    forbidden = direct_deps & set(gui.get("prohibited_direct_target_dependencies", []))
    if forbidden:
        raise ValidationError(f"GUI target has prohibited direct dependency: {sorted(forbidden)}")


def validate_resources(gui: dict) -> None:
    root = Path(gui["root_qml"])
    if not root.exists():
        raise ValidationError("root QML missing")
    for label, paths in (("components", gui.get("components", [])), ("pages", gui.get("pages", []))):
        for raw in paths:
            path = Path(raw)
            if not path.exists():
                raise ValidationError(f"{label}: missing {path}")
            if not re.fullmatch(r"[A-Z][A-Za-z0-9]+\.qml", path.name):
                raise ValidationError(f"{path}: QML names must be PascalCase")
            if any(part.lower() != part for part in path.parts if part in {"components", "pages"}):
                raise ValidationError(f"{path}: resource directory must be lowercase")
    component_names = {Path(path).name for path in gui.get("components", [])}
    missing_components = REQUIRED_COMPONENTS - component_names
    if missing_components:
        raise ValidationError(f"required GUI component missing from policy: {sorted(missing_components)}")
    root_text = root.read_text(encoding="utf-8")
    if "ApplicationWindow" not in root_text:
        raise ValidationError("root QML must use ApplicationWindow")
    if "import QtQuick.Controls" not in root_text:
        raise ValidationError("Qt Quick Controls import missing")
    for token in ("NavigationRail", "StackLayout", "ServiceStatusBar", "DiagnosticPanel", "shellReady", "navigationCount"):
        if token not in root_text:
            raise ValidationError(f"root QML missing {token}")
    all_qml = "\n".join(Path(path).read_text(encoding="utf-8") for path in [gui["root_qml"], *gui.get("components", []), *gui.get("pages", [])])
    for token in FORBIDDEN_QML_TOKENS | set(gui.get("prohibited_backend_tokens", [])):
        if token in all_qml:
            raise ValidationError(f"QML contains prohibited backend token: {token}")
    if 'diagnosticSeverity: "error"' in root_text or "solverAvailable: false" in root_text:
        raise ValidationError("root QML must not hard-code service or diagnostic semantics")
    navigation_delegate = Path("apps/tsunami_gui/qml/components/NavigationDelegate.qml").read_text(encoding="utf-8")
    if "Accessible.name" not in navigation_delegate:
        raise ValidationError("navigation delegates require Accessible.name")


def validate_navigation(gui: dict) -> None:
    navigation = gui.get("navigation", [])
    ids = [entry.get("id") for entry in navigation]
    if ids != EXPECTED_NAVIGATION:
        raise ValidationError(f"navigation order mismatch: {ids}")
    if len(set(ids)) != len(ids):
        raise ValidationError("duplicate navigation ID")
    for entry in navigation:
        if not entry.get("downstream_owners"):
            raise ValidationError(f"{entry.get('id')}: downstream owner required")
    smoke_fields = set(gui.get("shell_smoke_fields", []))
    if not REQUIRED_SHELL_SMOKE.issubset(smoke_fields):
        raise ValidationError(f"shell smoke fields missing {sorted(REQUIRED_SHELL_SMOKE - smoke_fields)}")


def validate_controller(gui: dict) -> None:
    header = Path("apps/tsunami_gui/ShellController.hpp")
    source = Path("apps/tsunami_gui/ShellController.cpp")
    if not header.exists() or not source.exists():
        raise ValidationError("ShellController files are missing")
    text = header.read_text(encoding="utf-8") + source.read_text(encoding="utf-8")
    main_text = Path("apps/tsunami_gui/main.cpp").read_text(encoding="utf-8")
    if "make_no_solver_application_service" not in main_text:
        raise ValidationError("GUI bootstrap must create the accepted no-solver service factory product")
    for token in ("QObject", "IApplicationService", "runDiagnosticSmoke", "refreshServiceStatus", "clearDiagnostic"):
        if token not in text:
            raise ValidationError(f"ShellController missing {token}")
    for prop in gui.get("service_status_properties", []) + gui.get("diagnostic_properties", []):
        if prop not in text:
            raise ValidationError(f"ShellController missing property {prop}")


def validate_public_headers(gui: dict) -> None:
    for raw in [
        *Path("src/core/include").glob("**/*.hpp"),
        *Path("src/application/include").glob("**/*.hpp"),
    ]:
        text = raw.read_text(encoding="utf-8")
        for token in FORBIDDEN_PUBLIC_QT:
            if token in text:
                raise ValidationError(f"{raw}: Qt token leaked into public inward header")


def print_report(gui: dict) -> None:
    print(f"gui shell policy: {gui['policy_version']}")
    print(f"target: {gui['gui_target']}")
    print(f"qml module: {gui['qml_module_uri']}")
    print(f"components: {len(gui['components'])}")
    print(f"pages: {len(gui['pages'])}")
    print(f"navigation sections: {len(gui['navigation'])}")
    print("resource structure: passed")
    print("target boundary: passed")
    print("controller binding: passed")
    print("navigation and smoke fields: passed")
    print("accessibility markers: passed")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gui", required=True, type=Path)
    parser.add_argument("--diagnostics", required=True, type=Path)
    parser.add_argument("--services", required=True, type=Path)
    parser.add_argument("--interfaces", required=True, type=Path)
    parser.add_argument("--targets", required=True, type=Path)
    parser.add_argument("--layers", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        gui = load_json(args.gui)
        diagnostics = load_json(args.diagnostics)
        services = load_json(args.services)
        interfaces = load_json(args.interfaces)
        targets = load_json(args.targets)
        layers = load_json(args.layers)
        validate_references(gui, diagnostics, services, interfaces, targets, layers)
        validate_cmake(gui, targets)
        validate_resources(gui)
        validate_navigation(gui)
        validate_controller(gui)
        validate_public_headers(gui)
        print_report(gui)
    except (OSError, json.JSONDecodeError, ValidationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
