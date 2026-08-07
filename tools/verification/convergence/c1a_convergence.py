#!/usr/bin/env python3
"""C1A numerical convergence harness for the Kamaishi G6 hybrid baseline."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import shutil
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Sequence


REPOSITORY_BASELINE = "d01631d0d36ee89bbce13f194811de364be02de3"
G6_PHYSICAL_BASELINE = "319d347644f0fe057ca2faa66ca5e324e10b5a21"
SCHEMA = {"name": "tsunami.convergence_study", "version": "1.0.0"}
DEFAULT_RESULTS_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a")
DEFAULT_G6_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi")
DEFAULT_REPLAY_CONFIG = DEFAULT_G6_ROOT / "replay/replay_config.json"
DEFAULT_CASE_SPEC = Path("cases/kamaishi_delivery/case_spec.json")
DEFAULT_R2D_BINARY = Path("build/linux-gcc-crs-test/apps/r2d_case/tsunami_r2d_case")
DEFAULT_OPENFOAM_WRAPPER = Path("tools/openfoam/run_openfoam11.sh")
OPENFOAM_IMAGE = "docker.io/openfoam/openfoam11-paraview510:11"
STAGES = ("regional-spatial", "regional-temporal", "local-spatial", "local-temporal")


class ConvergenceError(RuntimeError):
    """Raised when a convergence record would violate the C1A contract."""


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def canonical_json(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def json_sha256(value: Any) -> str:
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_optional_json(path: Path) -> dict[str, Any] | None:
    return read_json(path) if path.is_file() else None


def command_record(command: Sequence[str], cwd: Path | None = None) -> dict[str, Any]:
    try:
        completed = subprocess.run(
            list(command),
            cwd=cwd,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        return {
            "command": list(command),
            "returncode": completed.returncode,
            "stdout": completed.stdout.strip(),
        }
    except FileNotFoundError as exc:
        return {"command": list(command), "returncode": 127, "stdout": str(exc)}


def collect_hardware() -> dict[str, Any]:
    root = repo_root()
    commands = {
        "lscpu": ["lscpu"],
        "free_h": ["free", "-h"],
        "nproc": ["nproc"],
        "nvidia_smi": ["nvidia-smi"],
        "uname": ["uname", "-a"],
        "python3": ["python3", "--version"],
        "cmake": ["cmake", "--version"],
        "g++": ["g++", "--version"],
        "clang++": ["clang++", "--version"],
        "podman": ["podman", "--version"],
        "openfoam_image": ["podman", "image", "inspect", OPENFOAM_IMAGE],
    }
    return {
        "schema": {"name": "tsunami.c1a_hardware_audit", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "commands": {name: command_record(command, cwd=root) for name, command in commands.items()},
    }


def default_study(results_root: Path) -> dict[str, Any]:
    levels = {
        "regional-spatial": [
            {"level_id": "r2d-h1000", "profile": "etopo-1000m", "nominal_spacing_m": 1000.0},
            {"level_id": "r2d-h500", "profile": "etopo-500m", "nominal_spacing_m": 500.0},
        ],
        "regional-temporal": [
            {"level_id": "r2d-cfl010", "courant_number": 0.10, "maximum_timestep_s": 0.10},
            {"level_id": "r2d-cfl005", "courant_number": 0.05, "maximum_timestep_s": 0.05},
            {"level_id": "r2d-cfl0025", "courant_number": 0.025, "maximum_timestep_s": 0.025},
        ],
        "local-spatial": [
            {"level_id": "l3d-h1", "streamwise_cells": 40, "span_cells": 60, "vertical_cells": 16},
            {"level_id": "l3d-h0p75", "streamwise_cells": 54, "span_cells": 80, "vertical_cells": 22},
            {"level_id": "l3d-h0p5", "streamwise_cells": 80, "span_cells": 120, "vertical_cells": 32},
        ],
        "local-temporal": [
            {"level_id": "l3d-co025", "target_max_co": 0.25, "target_max_alpha_co": 0.25},
            {"level_id": "l3d-co0125", "target_max_co": 0.125, "target_max_alpha_co": 0.125},
            {"level_id": "l3d-co00625", "target_max_co": 0.0625, "target_max_alpha_co": 0.0625},
        ],
    }
    return {
        "schema": SCHEMA,
        "study_id": "c1a-kamaishi-g6-numerical-convergence",
        "repository_baseline": REPOSITORY_BASELINE,
        "g6_physical_model_baseline": G6_PHYSICAL_BASELINE,
        "results_root": str(results_root),
        "artifact_policy": {
            "persistent_artifacts_root": str(results_root),
            "commit_raw_fields": False,
            "commit_small_summaries_only": True,
        },
        "qualification_thresholds": {
            "regional_pairwise_relative_change": 0.05,
            "local_pairwise_relative_change": 0.10,
            "gci_fine_percent": 5.0,
        },
        "qois": {
            "regional": ["peak_eta_m", "peak_qn_m2_per_s", "arrival_time_s", "coupling_l2_nrmse"],
            "local": ["peak_force_n", "peak_moment_nm", "peak_probe_eta_m", "yplus_max", "dt_min_s"],
        },
        "stages": {stage: {"status": "planned", "levels": levels[stage]} for stage in STAGES},
    }


def physical_payload(case_spec: dict[str, Any], replay_config: dict[str, Any] | None) -> dict[str, Any]:
    regional = dict(case_spec["regional_2d"])
    for key in ("courant_number", "maximum_timestep_s", "maximum_steps", "snapshot_interval_s", "final_time_s"):
        regional.pop(key, None)
    payload: dict[str, Any] = {
        "case_schema": case_spec.get("schema"),
        "event": case_spec.get("event"),
        "source_products": case_spec.get("source_products"),
        "computational_crs": case_spec.get("computational_crs"),
        "vertical_reference": case_spec.get("vertical_reference"),
        "corridor": case_spec.get("corridor"),
        "nearshore_interface": case_spec.get("nearshore_interface"),
        "regional_model": regional,
        "local_3d_case_spec": case_spec.get("local_3d"),
    }
    if replay_config is not None:
        local_case = dict(replay_config.get("local_case", {}))
        for key in (
            "streamwise_cells",
            "span_cells",
            "vertical_cells",
            "maximum_timestep_s",
            "maximum_courant_number",
            "maximum_alpha_courant_number",
            "initial_timestep_s",
            "write_interval_s",
            "end_time_s",
            "timestep_derivation",
        ):
            local_case.pop(key, None)
        local = dict(replay_config.get("local", {}))
        for key in ("span_cells", "vertical_cells"):
            local.pop(key, None)
        payload["replay_config"] = {
            "schema": replay_config.get("schema"),
            "section_id": replay_config.get("section_id"),
            "openfoam_patch": replay_config.get("openfoam_patch"),
            "barrier": replay_config.get("barrier"),
            "boundary_policy": replay_config.get("boundary_policy"),
            "damping_policy": replay_config.get("damping_policy"),
            "derived_dimensions": replay_config.get("derived_dimensions"),
            "local": local,
            "local_case": local_case,
            "mapping": replay_config.get("mapping"),
            "regional": replay_config.get("regional"),
            "replay_window": replay_config.get("replay_window"),
            "turbulence": replay_config.get("turbulence"),
            "wall_function_policy": replay_config.get("wall_function_policy"),
        }
    return payload


def diff_values(left: Any, right: Any, pointer: str = "") -> list[str]:
    if type(left) is not type(right):
        return [pointer or "/"]
    if isinstance(left, dict):
        paths: list[str] = []
        for key in sorted(set(left) | set(right)):
            child = f"{pointer}/{key}" if pointer else f"/{key}"
            if key not in left or key not in right:
                paths.append(child)
            else:
                paths.extend(diff_values(left[key], right[key], child))
        return paths
    if isinstance(left, list):
        paths = []
        if len(left) != len(right):
            paths.append(pointer or "/")
        for index, (l_value, r_value) in enumerate(zip(left, right)):
            paths.extend(diff_values(l_value, r_value, f"{pointer}/{index}"))
        return paths
    return [] if left == right else [pointer or "/"]


def physical_parameter_record(case_spec_path: Path, replay_config_path: Path | None) -> dict[str, Any]:
    case_spec = read_json(case_spec_path)
    replay_config = load_optional_json(replay_config_path) if replay_config_path else None
    payload = physical_payload(case_spec, replay_config)
    return {
        "schema": {"name": "tsunami.c1a_physical_parameter_invariance", "version": "1.0.0"},
        "repository_baseline": REPOSITORY_BASELINE,
        "g6_physical_model_baseline": G6_PHYSICAL_BASELINE,
        "case_spec_path": str(case_spec_path),
        "case_spec_sha256": file_sha256(case_spec_path),
        "replay_config_path": str(replay_config_path) if replay_config_path else None,
        "replay_config_sha256": file_sha256(replay_config_path) if replay_config_path and replay_config_path.is_file() else None,
        "allowed_variation_classes": [
            "regional mesh/profile spacing",
            "regional Courant and timestep controls",
            "local cell counts",
            "local Courant and timestep controls",
        ],
        "forbidden_variation_classes": [
            "earthquake source or amplitude",
            "Manning roughness",
            "bathymetry/topography source identity",
            "vertical datum/MSL convention",
            "Local3D turbulence intensity or length scale",
            "wall-function and roughness-facing policy",
            "barrier geometry",
        ],
        "physical_payload_sha256": json_sha256(payload),
        "physical_payload": payload,
    }


def assert_physical_invariance(reference: dict[str, Any], candidate: dict[str, Any]) -> None:
    reference_payload = reference["physical_payload"]
    candidate_payload = candidate["physical_payload"]
    differences = diff_values(reference_payload, candidate_payload)
    if differences:
        raise ConvergenceError("physical parameter invariance failed: " + ", ".join(differences[:12]))


def initialise_results(results_root: Path, case_spec: Path, replay_config: Path | None, write_hardware: bool = True) -> dict[str, Any]:
    results_root.mkdir(parents=True, exist_ok=True)
    manifest = default_study(results_root)
    write_json(results_root / "manifest.json", manifest)
    invariance = physical_parameter_record(case_spec, replay_config)
    write_json(results_root / "physical_parameter_invariance.json", invariance)
    if write_hardware:
        write_json(results_root / "hardware.json", collect_hardware())
    for stage in STAGES:
        (results_root / stage.replace("-", "/")).mkdir(parents=True, exist_ok=True)
    (results_root / "evidence").mkdir(parents=True, exist_ok=True)
    return manifest


def environment_preflight(python: Path, r2d_binary: Path, openfoam_wrapper: Path) -> dict[str, Any]:
    root = repo_root()
    checks = {
        "r2d_binary_executable": r2d_binary.is_file() and os.access(r2d_binary, os.X_OK),
        "terrain_source_present": (root / "data/source/terrain/ETOPO_2022_v1_15s_N45E135_surface.tif").is_file(),
        "earthquake_source_present": (root / "data/source/earthquake/usgs_usp000hvnu_1539808472261_basic_inversion.param").is_file(),
        "g6_replay_config_present": DEFAULT_REPLAY_CONFIG.is_file(),
        "openfoam_wrapper_executable": openfoam_wrapper.is_file() and os.access(openfoam_wrapper, os.X_OK),
        "python_executable_present": python.is_file() and os.access(python, os.X_OK),
    }
    python_imports = command_record([str(python), "-c", "import numpy, rasterio, pyproj; print('ok')"], cwd=root) if checks["python_executable_present"] else {
        "command": [str(python), "-c", "import numpy, rasterio, pyproj"],
        "returncode": 127,
        "stdout": "python executable is missing",
    }
    checks["python_has_preprocessing_imports"] = python_imports["returncode"] == 0
    return {
        "schema": {"name": "tsunami.c1a_environment_preflight", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "checks": checks,
        "python_import_probe": python_imports,
    }


def write_stage_record(results_root: Path, stage: str, level_id: str, payload: dict[str, Any]) -> Path:
    if stage not in STAGES:
        raise ConvergenceError(f"unsupported stage: {stage}")
    path = results_root / stage.replace("-", "/") / level_id / "record.json"
    write_json(path, payload)
    return path


def infeasible_record(stage: str, level: dict[str, Any], reason: str, preflight: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema": {"name": "tsunami.c1a_convergence_level", "version": "1.0.0"},
        "stage": stage,
        "level": level,
        "status": "infeasible",
        "recorded_at_utc": utc_now(),
        "repository_baseline": REPOSITORY_BASELINE,
        "g6_physical_model_baseline": G6_PHYSICAL_BASELINE,
        "reason": reason,
        "preflight": preflight,
        "fabricated_results": False,
    }


def nrmse(candidate: Sequence[float], reference: Sequence[float]) -> float:
    if len(candidate) != len(reference) or not candidate:
        raise ConvergenceError("NRMSE requires non-empty equal-length series")
    mse = sum((c - r) ** 2 for c, r in zip(candidate, reference)) / len(candidate)
    scale = max(reference) - min(reference)
    if abs(scale) < 1.0e-300:
        scale = max(max(abs(value) for value in reference), 1.0)
    return math.sqrt(mse) / scale


def relative_change(coarse: float, fine: float) -> float:
    return abs(fine - coarse) / max(abs(fine), abs(coarse), 1.0e-300)


def richardson_gci(values: Sequence[float], spacings: Sequence[float], safety_factor: float = 1.25) -> dict[str, Any]:
    if len(values) != 3 or len(spacings) != 3:
        raise ConvergenceError("Richardson/GCI requires exactly three levels")
    h1, h2, h3 = [float(item) for item in spacings]
    f1, f2, f3 = [float(item) for item in values]
    if not (h1 < h2 < h3):
        raise ConvergenceError("spacings must be ordered fine-to-coarse")
    r21 = h2 / h1
    r32 = h3 / h2
    eps21 = f2 - f1
    eps32 = f3 - f2
    if abs(eps21) < 1.0e-300 or abs(eps32) < 1.0e-300 or eps21 * eps32 <= 0.0:
        return {"status": "not_monotone", "r21": r21, "r32": r32, "observed_order": None, "gci21_percent": None}
    if abs(r21 - r32) <= 1.0e-12:
        p = abs(math.log(abs(eps32 / eps21)) / math.log(r21))
    else:
        p = abs(math.log(abs(eps32 / eps21)) / math.log(math.sqrt(r21 * r32)))
    extrapolated = f1 + (f1 - f2) / (r21**p - 1.0)
    gci21 = safety_factor * abs((f1 - f2) / max(abs(f1), 1.0e-300)) / (r21**p - 1.0) * 100.0
    return {
        "status": "computed",
        "r21": r21,
        "r32": r32,
        "observed_order": p,
        "extrapolated_value": extrapolated,
        "gci21_percent": gci21,
    }


def local_characteristic_h(volume_m3: float, cell_count: int) -> float:
    if volume_m3 <= 0.0 or cell_count <= 0:
        raise ConvergenceError("Local3D characteristic h requires positive volume and cell count")
    return (volume_m3 / cell_count) ** (1.0 / 3.0)


def read_metric_rows(results_root: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in results_root.glob("*/*/*/metrics.csv"):
        with path.open(encoding="utf-8", newline="") as handle:
            for row in csv.DictReader(handle):
                row.setdefault("metrics_path", str(path))
                rows.append(row)
    return rows


def write_summary(results_root: Path) -> dict[str, Any]:
    rows = read_metric_rows(results_root)
    by_stage_qoi: dict[tuple[str, str], list[dict[str, str]]] = {}
    for row in rows:
        by_stage_qoi.setdefault((row["stage"], row["qoi"]), []).append(row)
    comparisons: list[dict[str, Any]] = []
    for (stage, qoi), group in sorted(by_stage_qoi.items()):
        ordered = sorted(group, key=lambda item: float(item["h_or_dt"]), reverse=True)
        for coarse, fine in zip(ordered, ordered[1:]):
            comparisons.append(
                {
                    "stage": stage,
                    "qoi": qoi,
                    "coarse_level": coarse["level_id"],
                    "fine_level": fine["level_id"],
                    "relative_change": relative_change(float(coarse["value"]), float(fine["value"])),
                }
            )
    summary = {
        "schema": {"name": "tsunami.c1a_convergence_summary", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "source_metric_rows": len(rows),
        "pairwise_comparisons": comparisons,
        "qualification_status": "insufficient_real_results" if not rows else "review_required",
        "fabricated_results": False,
    }
    write_json(results_root / "summary.json", summary)
    return summary


def plot_summary(results_root: Path, figure_root: Path) -> dict[str, Any]:
    rows = read_metric_rows(results_root)
    figure_root.mkdir(parents=True, exist_ok=True)
    if not rows:
        record = {
            "schema": {"name": "tsunami.c1a_convergence_figure_provenance", "version": "1.0.0"},
            "status": "not_generated",
            "reason": "no real metric rows were available",
            "generated_at_utc": utc_now(),
        }
        write_json(figure_root / "c1a_convergence_not_generated.json", record)
        return record
    import matplotlib.pyplot as plt  # Imported only for the plotting command.

    by_qoi: dict[str, list[dict[str, str]]] = {}
    for row in rows:
        by_qoi.setdefault(f"{row['stage']}:{row['qoi']}", []).append(row)
    outputs: list[dict[str, Any]] = []
    for name, group in sorted(by_qoi.items()):
        group = sorted(group, key=lambda item: float(item["h_or_dt"]))
        x = [float(item["h_or_dt"]) for item in group]
        y = [float(item["value"]) for item in group]
        slug = name.replace(":", "_").replace("/", "_")
        fig, ax = plt.subplots(figsize=(6.0, 4.0))
        ax.plot(x, y, marker="o", color="#2d62a0")
        ax.set_xlabel("h or dt")
        ax.set_ylabel(name)
        ax.grid(True, alpha=0.3)
        fig.tight_layout()
        png = figure_root / f"{slug}.png"
        fig.savefig(png, dpi=180)
        plt.close(fig)
        provenance = {
            "schema": {"name": "tsunami.c1a_convergence_figure_provenance", "version": "1.0.0"},
            "figure": str(png),
            "generated_at_utc": utc_now(),
            "source_rows": group,
            "matplotlib": True,
            "seaborn": False,
        }
        write_json(png.with_suffix(".json"), provenance)
        outputs.append({"figure": str(png), "provenance": str(png.with_suffix(".json"))})
    record = {"schema": {"name": "tsunami.c1a_convergence_plot_set", "version": "1.0.0"}, "outputs": outputs}
    write_json(figure_root / "plot_manifest.json", record)
    return record


def stage_levels(manifest: dict[str, Any], stage: str) -> list[dict[str, Any]]:
    return list(manifest["stages"][stage]["levels"])


def record_stage(args: argparse.Namespace, stage: str) -> int:
    results_root = args.results_root
    manifest = read_json(results_root / "manifest.json") if (results_root / "manifest.json").is_file() else initialise_results(
        results_root, args.case_spec, args.replay_config, write_hardware=False
    )
    preflight = environment_preflight(args.python, args.r2d_binary, args.openfoam_wrapper)
    if args.execute:
        reason = "execution is intentionally guarded in this harness revision; run records must be attached by stage-specific execution adapters"
    else:
        reason = args.reason or "stage not executed in this invocation"
    if stage.startswith("regional") and not preflight["checks"]["python_has_preprocessing_imports"]:
        reason = "fresh Regional2D preprocessing dependency probe failed"
    if stage.startswith("local") and not preflight["checks"]["openfoam_wrapper_executable"]:
        reason = "OpenFOAM wrapper is not executable"
    for level in stage_levels(manifest, stage):
        write_stage_record(results_root, stage, str(level["level_id"]), infeasible_record(stage, level, reason, preflight))
    return 0


def command_init(args: argparse.Namespace) -> int:
    initialise_results(args.results_root, args.case_spec, args.replay_config, write_hardware=not args.no_hardware)
    return 0


def command_hardware(args: argparse.Namespace) -> int:
    write_json(args.results_root / "hardware.json", collect_hardware())
    return 0


def command_summarise(args: argparse.Namespace) -> int:
    print(json.dumps(write_summary(args.results_root), indent=2, sort_keys=True))
    return 0


def command_plot(args: argparse.Namespace) -> int:
    print(json.dumps(plot_summary(args.results_root, args.figure_root), indent=2, sort_keys=True))
    return 0


def command_validate_invariance(args: argparse.Namespace) -> int:
    reference = read_json(args.reference)
    candidate = physical_parameter_record(args.case_spec, args.replay_config)
    assert_physical_invariance(reference, candidate)
    write_json(args.output, {"schema": {"name": "tsunami.c1a_invariance_validation", "version": "1.0.0"}, "status": "passed", "validated_at_utc": utc_now()})
    return 0


def add_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--results-root", type=Path, default=DEFAULT_RESULTS_ROOT)
    parser.add_argument("--case-spec", type=Path, default=repo_root() / DEFAULT_CASE_SPEC)
    parser.add_argument("--replay-config", type=Path, default=DEFAULT_REPLAY_CONFIG)
    parser.add_argument("--python", type=Path, default=Path("/tmp/tsunami-g3-producer-venv/bin/python"))
    parser.add_argument("--r2d-binary", type=Path, default=repo_root() / DEFAULT_R2D_BINARY)
    parser.add_argument("--openfoam-wrapper", type=Path, default=repo_root() / DEFAULT_OPENFOAM_WRAPPER)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    init = sub.add_parser("init", help="Initialise the persistent C1A convergence artifact tree.")
    add_common(init)
    init.add_argument("--no-hardware", action="store_true")
    init.set_defaults(func=command_init)
    hardware = sub.add_parser("hardware", help="Write hardware audit evidence.")
    add_common(hardware)
    hardware.set_defaults(func=command_hardware)
    for stage in STAGES:
        stage_parser = sub.add_parser(stage, help=f"Record or execute {stage} convergence levels.")
        add_common(stage_parser)
        stage_parser.add_argument("--execute", action="store_true")
        stage_parser.add_argument("--reason")
        stage_parser.set_defaults(func=lambda args, selected=stage: record_stage(args, selected))
    summarise = sub.add_parser("summarise", help="Summarise committed metric rows.")
    add_common(summarise)
    summarise.set_defaults(func=command_summarise)
    plot = sub.add_parser("plot", help="Create single-panel convergence figures from metric rows.")
    add_common(plot)
    plot.add_argument("--figure-root", type=Path, default=repo_root() / "deliverables/figures/convergence")
    plot.set_defaults(func=command_plot)
    validate = sub.add_parser("validate-invariance", help="Validate a candidate case/replay pair against a frozen physical baseline.")
    add_common(validate)
    validate.add_argument("--reference", type=Path, required=True)
    validate.add_argument("--output", type=Path, required=True)
    validate.set_defaults(func=command_validate_invariance)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except ConvergenceError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
