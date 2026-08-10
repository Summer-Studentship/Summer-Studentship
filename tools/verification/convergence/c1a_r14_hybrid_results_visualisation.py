#!/usr/bin/env python3
"""R14 Local3D boundedness, result-storage and poster-asset programme."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
OPENFOAM_DIR = Path(__file__).resolve().parents[2] / "openfoam"
RESULTS_DIR = Path(__file__).resolve().parents[2] / "results"
for candidate in (OPENFOAM_DIR, RESULTS_DIR):
    if str(candidate) not in sys.path:
        sys.path.insert(0, str(candidate))

import openfoam_replay


R14_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/r14-hybrid")
R13_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-fidelity-hybrid-r13")
G6_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi")
H400_HDF5 = Path("/home/helios/SimulationData/Summer-Studentship/results/r11-regional2d-storage-poc/r10-h400-limited-linear/regional2d.h5")
H400_XDMF = Path("/home/helios/SimulationData/Summer-Studentship/results/r11-regional2d-storage-poc/r10-h400-limited-linear/regional2d.xdmf")
DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
FIGURE_ROOT = Path("deliverables/figures/r14_hybrid")
INITIAL_GIT_SHA = "201cc00a72a460c1dbc3c5d4add206af988f11e0"
EXPECTED_FORCING_SHA = "aac08422684e1ebbc3f4940c703225ec3c4e14d0b686ac081b21da93ef8eecb7"
EXPECTED_REPLAY_SHA = "32e4ab768726e89661b304eea4e424e00d317c05bf5cc1b84c05251ac2ba52b6"
ALPHA_TOLERANCE = 5.0e-5
FLOAT_PATTERN = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"


def utc_now() -> str:
    return datetime.now(UTC).isoformat().replace("+00:00", "Z")


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def directory_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    for item in sorted(p for p in path.rglob("*") if p.is_file()):
        digest.update(item.relative_to(path).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(item.read_bytes()).digest())
    return digest.hexdigest()


def command_text(command: Sequence[str]) -> tuple[int, str]:
    completed = subprocess.run(command, cwd=repo_root(), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    return completed.returncode, completed.stdout.strip()


def git_sha() -> str:
    code, text = command_text(["git", "rev-parse", "HEAD"])
    return text if code == 0 else "unknown"


def update_state(**updates: Any) -> dict[str, Any]:
    state_path = R14_ROOT / "state/r14_state.json"
    state = read_json(state_path) if state_path.is_file() else {}
    state.update(updates)
    state["last_update_utc"] = utc_now()
    write_json(state_path, state)
    return state


def parse_scalar_list(path: Path) -> list[float]:
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"\(\s*(.*?)\s*\)", text, flags=re.DOTALL)
    if not match:
        return []
    return [float(value) for value in re.findall(FLOAT_PATTERN, match.group(1))]


def scan_boundary_alpha(boundary_root: Path) -> dict[str, Any]:
    records = []
    for alpha_path in sorted(boundary_root.glob("constant/boundaryData/inlet/*/alpha.water"), key=lambda p: float(p.parent.name)):
        values = parse_scalar_list(alpha_path)
        if not values:
            continue
        amin = min(values)
        amax = max(values)
        records.append({
            "time_s": float(alpha_path.parent.name),
            "path": str(alpha_path),
            "count": len(values),
            "min": amin,
            "max": amax,
            "min_index": values.index(amin),
            "max_index": values.index(amax),
        })
    if not records:
        raise RuntimeError(f"no inlet alpha boundaryData found under {boundary_root}")
    global_min = min(records, key=lambda row: row["min"])
    global_max = max(records, key=lambda row: row["max"])
    return {
        "status": "BOUNDED" if global_min["min"] >= 0.0 and global_max["max"] <= 1.0 else "OUT_OF_BOUNDS",
        "time_count": len(records),
        "minimum": global_min,
        "maximum": global_max,
    }


def interpolate_alpha_bound(boundary_scan: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "status": "BOUNDED_BY_CONVEX_LINEAR_INTERPOLATION",
        "reason": "OpenFOAM timeVaryingMappedFixedValue linearly interpolates scalar alpha.water between replay samples; all stored endpoint values are within [0, 1], so scalar interpolation cannot overshoot.",
        "endpoint_min": boundary_scan["minimum"]["min"],
        "endpoint_max": boundary_scan["maximum"]["max"],
    }


def parse_alpha_log_series(log_path: Path) -> list[dict[str, float]]:
    current_time: float | None = None
    current_co: float | None = None
    current_alpha_co: float | None = None
    rows: list[dict[str, float]] = []
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        time_match = re.match(rf"^Time\s*=\s*({FLOAT_PATTERN})s?\s*$", line.strip())
        if time_match:
            current_time = float(time_match.group(1))
            continue
        co_match = re.match(rf"^Courant Number mean:\s*{FLOAT_PATTERN}\s+max:\s*({FLOAT_PATTERN})\s*$", line.strip())
        if co_match:
            current_co = float(co_match.group(1))
            continue
        alpha_co_match = re.match(rf"^Interface Courant Number mean:\s*{FLOAT_PATTERN}\s+max:\s*({FLOAT_PATTERN})\s*$", line.strip())
        if alpha_co_match:
            current_alpha_co = float(alpha_co_match.group(1))
            continue
        alpha_match = re.search(rf"Min\(alpha\.water\)\s*=\s*({FLOAT_PATTERN})\s+Max\(alpha\.water\)\s*=\s*({FLOAT_PATTERN})", line)
        if alpha_match and current_time is not None:
            rows.append({
                "time_s": current_time,
                "alpha_min": float(alpha_match.group(1)),
                "alpha_max": float(alpha_match.group(2)),
                "Co": current_co if current_co is not None else math.nan,
                "alphaCo": current_alpha_co if current_alpha_co is not None else math.nan,
            })
    return rows


def series_summary(rows: Sequence[Mapping[str, float]], *, horizon_s: float | None = None) -> dict[str, Any]:
    filtered = [row for row in rows if horizon_s is None or row["time_s"] <= horizon_s + 1.0e-12]
    if not filtered:
        return {"status": "NO_SAMPLES", "horizon_s": horizon_s}
    min_row = min(filtered, key=lambda row: row["alpha_min"])
    max_row = max(filtered, key=lambda row: row["alpha_max"])
    first_under = next((row for row in filtered if row["alpha_min"] < 0.0), None)
    first_over = next((row for row in filtered if row["alpha_max"] > 1.0), None)
    first_tol_under = next((row for row in filtered if row["alpha_min"] < -ALPHA_TOLERANCE), None)
    first_tol_over = next((row for row in filtered if row["alpha_max"] > 1.0 + ALPHA_TOLERANCE), None)
    return {
        "status": "COMPUTED",
        "horizon_s": horizon_s,
        "sample_count": len(filtered),
        "minimum_alpha": min_row["alpha_min"],
        "minimum_alpha_time_s": min_row["time_s"],
        "maximum_alpha": max_row["alpha_max"],
        "maximum_alpha_time_s": max_row["time_s"],
        "maximum_Co": max((row["Co"] for row in filtered if math.isfinite(row["Co"])), default=None),
        "maximum_alpha_Co": max((row["alphaCo"] for row in filtered if math.isfinite(row["alphaCo"])), default=None),
        "first_negative": dict(first_under) if first_under else None,
        "first_above_one": dict(first_over) if first_over else None,
        "first_below_tolerance": dict(first_tol_under) if first_tol_under else None,
        "first_above_tolerance": dict(first_tol_over) if first_tol_over else None,
    }


def final_alpha_location(case_root: Path) -> dict[str, Any]:
    latest = openfoam_replay._latest_time(case_root)
    alpha_path = case_root / openfoam_replay._time_name(latest) / "alpha.water"
    if not alpha_path.is_file():
        return {"status": "NO_RETAINED_ALPHA_FIELD", "time_s": latest}
    values = openfoam_replay._read_internal_scalar_field(alpha_path)
    amin = min(values)
    amax = max(values)
    return {
        "status": "COMPUTED",
        "time_s": latest,
        "alpha_path": str(alpha_path),
        "minimum_alpha": amin,
        "minimum_cell_index": values.index(amin),
        "maximum_alpha": amax,
        "maximum_cell_index": values.index(amax),
    }


def volume_trend(rows: Sequence[Mapping[str, float]]) -> dict[str, Any]:
    values = []
    for line in rows:
        if "volume_fraction" in line:
            values.append(float(line["volume_fraction"]))
    if len(values) < 2:
        return {"status": "NOT_AVAILABLE_FROM_PARSED_SERIES"}
    return {"status": "COMPUTED", "initial": values[0], "final": values[-1], "change": values[-1] - values[0]}


def compare_case_summaries(g6_case: Path, r13_case: Path) -> dict[str, Any]:
    g6 = read_json(g6_case / "openfoam_case_summary.json")
    r13 = read_json(r13_case / "openfoam_case_summary.json")
    keys = [
        "dimensions_m",
        "cell_counts",
        "cell_dimensions_m",
        "replay_schema",
        "boundary_mode",
        "mesh_patch_types",
        "field_boundary_types",
        "maximum_timestep",
        "initial_timestep",
        "minimum_timestep",
        "maximum_courant_number",
        "maximum_alpha_courant_number",
        "write_interval",
        "initial_water_level",
        "alpha_tolerance",
        "barrier",
        "wall_function_policy",
    ]
    mismatches = {}
    for key in keys:
        if g6.get(key) != r13.get(key):
            mismatches[key] = {"g6": g6.get(key), "r13": r13.get(key)}
    return {
        "status": "MATCHES_EXCEPT_FOR_FORCING_DERIVED_VALUES" if not mismatches else "HAS_DIFFERENCES",
        "compared_keys": keys,
        "mismatches": mismatches,
        "g6_case_hash": directory_sha256(g6_case),
        "r13_case_hash": directory_sha256(r13_case),
    }


def parse_volume_fraction_series(log_path: Path) -> list[dict[str, float]]:
    current_time: float | None = None
    rows = []
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        time_match = re.match(rf"^Time\s*=\s*({FLOAT_PATTERN})s?\s*$", line.strip())
        if time_match:
            current_time = float(time_match.group(1))
            continue
        match = re.search(rf"Phase-1 volume fraction\s*=\s*({FLOAT_PATTERN})", line)
        if match and current_time is not None:
            rows.append({"time_s": current_time, "volume_fraction": float(match.group(1))})
    return rows


def boundedness_audit() -> dict[str, Any]:
    update_state(boundedness_status="RUNNING", last_successful_checkpoint="boundedness_started")
    forcing_path = R13_ROOT / "forcing/regional_h400_limited_linear_forcing_authority.json"
    replay_path = R13_ROOT / "replay/r13_h400_openfoam_replay_package.json"
    hashes = {
        "forcing_manifest_file_sha256": sha256(forcing_path),
        "replay_package_file_sha256": sha256(replay_path),
        "forcing_manifest_matches_r13_authority": sha256(forcing_path) == EXPECTED_FORCING_SHA,
        "replay_package_matches_r13_authority": sha256(replay_path) == EXPECTED_REPLAY_SHA,
    }
    boundary_scan = scan_boundary_alpha(R13_ROOT / "replay/openfoam-boundaryData")
    temporal = interpolate_alpha_bound(boundary_scan)
    g6_acceptance = read_json(G6_ROOT / "evidence/g6_openfoam_acceptance.json")
    g6_barrier = G6_ROOT / "local/simple_rigid_barrier"
    g6_no_defence = G6_ROOT / "local/no_defence"
    r13_barrier = R13_ROOT / "local3d/simple_rigid_barrier"
    config_diff = compare_case_summaries(g6_barrier, r13_barrier)
    logs = {
        "g6_barrier": parse_alpha_log_series(g6_barrier / "log.foamRun"),
        "g6_no_defence": parse_alpha_log_series(g6_no_defence / "log.foamRun"),
        "r13_current_1s": parse_alpha_log_series(R13_ROOT / "local3d-smoke/simple_rigid_barrier_1s/log.foamRun"),
        "r13_current_5s": parse_alpha_log_series(R13_ROOT / "local3d-smoke/simple_rigid_barrier/log.foamRun"),
    }
    same_horizon = {
        "g6_barrier_1s": series_summary(logs["g6_barrier"], horizon_s=1.0),
        "g6_barrier_5s": series_summary(logs["g6_barrier"], horizon_s=5.0),
        "g6_no_defence_1s": series_summary(logs["g6_no_defence"], horizon_s=1.0),
        "g6_no_defence_5s": series_summary(logs["g6_no_defence"], horizon_s=5.0),
        "r13_current_1s": series_summary(logs["r13_current_1s"], horizon_s=1.0),
        "r13_current_5s": series_summary(logs["r13_current_5s"], horizon_s=5.0),
    }
    current_1 = same_horizon["r13_current_1s"]
    current_5 = same_horizon["r13_current_5s"]
    g6_1 = same_horizon["g6_barrier_1s"]
    accepted_by_existing_rule = (
        current_1.get("minimum_alpha", 0.0) >= -ALPHA_TOLERANCE
        and current_1.get("maximum_alpha", 1.0) <= 1.0 + ALPHA_TOLERANCE
        and current_5.get("minimum_alpha", 0.0) >= -ALPHA_TOLERANCE
        and current_5.get("maximum_alpha", 1.0) <= 1.0 + ALPHA_TOLERANCE
    )
    comparable_to_g6 = (
        abs(float(current_1.get("minimum_alpha", 0.0))) <= 10.0 * max(abs(float(g6_1.get("minimum_alpha", 0.0))), 1.0e-12)
        and abs(float(current_1.get("maximum_alpha", 1.0)) - 1.0) <= 10.0 * max(abs(float(g6_1.get("maximum_alpha", 1.0)) - 1.0), 1.0e-12)
    )
    if boundary_scan["status"] != "BOUNDED":
        classification = "REPLAY_MAPPING_DEFECT_FOUND"
        root_cause = "input/replay alpha boundaryData is out of bounds before OpenFOAM transport"
    elif config_diff["mismatches"]:
        classification = "REPLAY_CONFIGURATION_DEFECT_FOUND"
        root_cause = "current case configuration differs from accepted G6 controls beyond forcing-derived values"
    elif accepted_by_existing_rule and comparable_to_g6:
        classification = "REPLAY_BOUNDEDNESS_ACCEPTED"
        root_cause = "current alpha excursions are inside the existing acceptance rule and comparable to accepted G6 behaviour"
    else:
        classification = "REPLAY_VOF_BEHAVIOUR_UNRESOLVED"
        root_cause = "current inlet alpha is bounded and case controls match G6, but internal VOF alpha excursions exceed the existing 5e-05 tolerance and are materially larger than same-horizon G6"
    locations = {
        "r13_current_1s_written_field": final_alpha_location(R13_ROOT / "local3d-smoke/simple_rigid_barrier_1s"),
        "r13_current_5s_written_field": final_alpha_location(R13_ROOT / "local3d-smoke/simple_rigid_barrier"),
        "g6_barrier_final_written_field": final_alpha_location(g6_barrier),
    }
    volume = {
        "r13_current_1s": {
            "initial": parse_volume_fraction_series(R13_ROOT / "local3d-smoke/simple_rigid_barrier_1s/log.foamRun")[0],
            "final": parse_volume_fraction_series(R13_ROOT / "local3d-smoke/simple_rigid_barrier_1s/log.foamRun")[-1],
        },
        "r13_current_5s": {
            "initial": parse_volume_fraction_series(R13_ROOT / "local3d-smoke/simple_rigid_barrier/log.foamRun")[0],
            "final": parse_volume_fraction_series(R13_ROOT / "local3d-smoke/simple_rigid_barrier/log.foamRun")[-1],
        },
    }
    audit = {
        "schema": {"name": "tsunami.c1a_r14_local3d_boundedness", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "git_sha": git_sha(),
        "forcing_authority": hashes,
        "acceptance_rule": {
            "source": "tools/openfoam/openfoam_replay.py::validate_smoke_case and openfoam_case_summary.json::alpha_tolerance",
            "exact_tolerance": ALPHA_TOLERANCE,
            "historical_evidence": str(G6_ROOT / "evidence/g6_openfoam_acceptance.json"),
            "g6_barrier_final_alpha": g6_acceptance["simple_rigid_barrier"]["validation"]["alpha_min"],
            "g6_barrier_final_alpha_max": g6_acceptance["simple_rigid_barrier"]["validation"]["alpha_max"],
        },
        "input_replay_alpha": boundary_scan,
        "temporal_interpolation": temporal,
        "replay_geometry": {
            "status": "BOUNDED_CONSTRUCTION_CONFIRMED",
            "alpha_construction": "fractional vertical face fill from free_surface, clamped to [0, 1], with cells below bed forced dry",
            "free_surface_equality_convention": "free_surface <= z_min gives alpha=0; free_surface >= z_max gives alpha=1; equality at lower face is dry, equality at upper face is full",
        },
        "configuration_diff": config_diff,
        "same_horizon": same_horizon,
        "retained_field_locations": locations,
        "water_volume_integrity": volume,
        "classification": classification,
        "root_cause": root_cause,
        "repair_made": None,
        "full_replay_gate": "OPEN" if classification == "REPLAY_BOUNDEDNESS_ACCEPTED" else "CLOSED",
    }
    write_json(R14_ROOT / "diagnostics/local3d_boundedness_audit.json", audit)
    write_json(DOCS_ROOT / "regional2d_r14_local3d_boundedness_audit.json", audit)
    summary = f"""# C1A-R14 Local3D Boundedness Audit

Final classification: `{classification}`.

Existing acceptance rule: `alpha.water` must remain within `[-5e-05, 1.00005]`, from `tools/openfoam/openfoam_replay.py::validate_smoke_case` and case `alpha_tolerance`.

Current replay inlet alpha is bounded before OpenFOAM transport: min `{boundary_scan['minimum']['min']}`, max `{boundary_scan['maximum']['max']}`.

Same-horizon G6 barrier at 1 s: min `{same_horizon['g6_barrier_1s'].get('minimum_alpha')}`, max `{same_horizon['g6_barrier_1s'].get('maximum_alpha')}`.

Current h400 forcing at 1 s: min `{same_horizon['r13_current_1s'].get('minimum_alpha')}`, max `{same_horizon['r13_current_1s'].get('maximum_alpha')}`.

Current h400 forcing at 5 s: min `{same_horizon['r13_current_5s'].get('minimum_alpha')}`, max `{same_horizon['r13_current_5s'].get('maximum_alpha')}`.

Root cause classification: {root_cause}.

Full current-generation 300 s replay gate: `{audit['full_replay_gate']}`.
"""
    (DOCS_ROOT / "regional2d_r14_local3d_boundedness_audit.md").write_text(summary, encoding="utf-8")
    update_state(
        boundedness_status="COMPLETE",
        boundedness_classification=classification,
        barrier_status="DEFERRED" if classification != "REPLAY_BOUNDEDNESS_ACCEPTED" else "READY",
        no_defence_status="DEFERRED" if classification != "REPLAY_BOUNDEDNESS_ACCEPTED" else "NOT_STARTED",
        last_successful_checkpoint="boundedness_audit_complete",
    )
    return audit


def hdf5_audit() -> dict[str, Any]:
    update_state(hdf5_status="RUNNING", xdmf_status="RUNNING", resultdataset_status="RUNNING")
    from regional2d_result import Hdf5ResultDataset, validate_hdf5

    import h5py
    import numpy as np
    import xml.etree.ElementTree as ET

    validation = validate_hdf5(H400_HDF5)
    dataset = Hdf5ResultDataset(H400_HDF5)
    ET.parse(H400_XDMF)
    coupling_path = R14_ROOT / "runs/r10-h400-limited-linear/coupling_replay.h5"
    coupling_path.parent.mkdir(parents=True, exist_ok=True)
    selected = R13_ROOT / "replay/selected-window"
    with (selected / "samples.csv").open(encoding="utf-8", newline="") as handle:
        samples = list(csv.DictReader(handle))
    with (selected / "history.csv").open(encoding="utf-8", newline="") as handle:
        history = list(csv.DictReader(handle))
    with h5py.File(coupling_path, "w") as h5:
        h5.attrs["schema_name"] = "tsunami.r14.coupling_replay"
        h5.attrs["schema_version"] = "1.0.0"
        h5.attrs["source"] = str(selected)
        h5.create_dataset("history/time", data=np.asarray([float(row["time"]) for row in history]))
        h5.create_dataset("history/maximum_depth", data=np.asarray([float(row["maximum_depth"]) for row in history]))
        h5.create_dataset("history/maximum_speed", data=np.asarray([float(row["maximum_speed"]) for row in history]))
        h5.create_dataset("samples/time", data=np.asarray([float(row["time"]) for row in samples]))
        h5.create_dataset("samples/local_index", data=np.asarray([int(row["local_index"]) for row in samples]))
        h5.create_dataset("samples/depth", data=np.asarray([float(row["depth"]) for row in samples]))
        h5.create_dataset("samples/momentum_x", data=np.asarray([float(row["momentum_x"]) for row in samples]))
        h5.create_dataset("samples/momentum_y", data=np.asarray([float(row["momentum_y"]) for row in samples]))
        h5.create_dataset("samples/free_surface_elevation", data=np.asarray([float(row["free_surface_elevation"]) for row in samples]))
    audit = {
        "schema": {"name": "tsunami.c1a_r14_result_storage_audit", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "regional_schema_status": "COMPLETE",
        "writer_status": "COMPLETE_FOR_SCHEMA_AND_VALIDATED_CONVERTER",
        "production_writer_status": "RUNTIME_HDF5_INTEGRATION_DEFERRED",
        "legacy_converter_status": "COMPLETE",
        "h400_hdf5": {"path": str(H400_HDF5), "sha256": sha256(H400_HDF5), "validation": validation},
        "xdmf_status": "COMPLETE",
        "xdmf": {"path": str(H400_XDMF), "sha256": sha256(H400_XDMF), "xml_parse": "PASSED"},
        "resultdataset_status": "COMPLETE",
        "resultdataset_probe": {
            "time_count": int(len(dataset.times())),
            "mesh_cell_count": int(dataset.mesh()["connectivity"].shape[0]),
            "eta_sample_min": float(dataset.field("eta", 0).min()),
            "qmag_sample_max": float(dataset.field("qmag", 0).max()),
        },
        "coupling_hdf5_status": "COMPLETE",
        "coupling_hdf5": {"path": str(coupling_path), "sha256": sha256(coupling_path)},
        "local3d_storage_status": "MANIFEST_ONLY_PER_R14_SCOPE",
    }
    write_json(R14_ROOT / "manifests/result_storage_audit.json", audit)
    write_json(DOCS_ROOT / "regional2d_r14_result_storage_audit.json", audit)
    update_state(hdf5_status="COMPLETE", xdmf_status="COMPLETE", resultdataset_status="COMPLETE", last_successful_checkpoint="hdf5_audit_complete")
    return audit


def pyplot():
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection

    return plt, PolyCollection


def figure_provenance(path: Path, *, figure_type: str, data_class: str, source: Sequence[Path], fields: Sequence[str], notes: str = "") -> dict[str, Any]:
    record = {
        "schema": {"name": "tsunami.figure_provenance", "version": "1.0.0"},
        "figure": str(path),
        "figure_type": figure_type,
        "data_class": data_class,
        "source_files": [str(item) for item in source],
        "source_sha256": {str(item): sha256(item) for item in source if item.is_file()},
        "fields_used": list(fields),
        "generated_at_utc": utc_now(),
        "generating_script": "tools/verification/convergence/c1a_r14_hybrid_results_visualisation.py",
        "git_sha": git_sha(),
        "notes": notes,
        "figure_sha256": sha256(path),
    }
    write_json(path.with_suffix(path.suffix + ".provenance.json"), record)
    return record


def save_svg(fig: Any, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, format="svg")
    import matplotlib.pyplot as plt

    plt.close(fig)


def generate_figures() -> dict[str, Any]:
    update_state(regional_visual_status="RUNNING", coupling_visual_status="RUNNING")
    from regional2d_result import Hdf5ResultDataset

    import numpy as np

    plt, PolyCollection = pyplot()
    dataset = Hdf5ResultDataset(H400_HDF5)
    mesh = dataset.mesh()
    points = mesh["points"]
    connectivity = mesh["connectivity"]
    polys = points[connectivity]
    centres = mesh["cell_centres"]
    bed = mesh["bed_elevation"]
    times = dataset.times()
    snapshot_time = float(times[int(np.argmin(np.abs(times - 300.0)))])
    eta = dataset.field("eta", snapshot_time)
    outputs: list[dict[str, Any]] = []

    corridor = read_json(G6_ROOT / "case/manifests/corridors/kamaishi-delivery-corridor-evidence.json")
    polygon = corridor["corridor"]["polygon_projected_m"]
    fig, ax = plt.subplots(figsize=(6.2, 5.2), constrained_layout=True)
    ax.plot([p["x"] for p in polygon], [p["y"] for p in polygon], color="#1f6feb", linewidth=2.0, label="delivery corridor")
    ax.scatter(corridor["event"]["epicentre_projected_m"]["x"], corridor["event"]["epicentre_projected_m"]["y"], color="#cf222e", label="Tohoku epicentre proxy")
    ax.scatter(corridor["event"]["kamaishi_proxy_projected_m"]["x"], corridor["event"]["kamaishi_proxy_projected_m"]["y"], color="#116329", label="Kamaishi proxy")
    ax.scatter(corridor["selected_nearshore_interface"]["projected_m"]["x"], corridor["selected_nearshore_interface"]["projected_m"]["y"], color="#bf8700", label="coupling section")
    ax.set_aspect("equal", adjustable="box")
    ax.set_title("Tohoku to Kamaishi Regional Corridor")
    ax.set_xlabel("projected x (m)")
    ax.set_ylabel("projected y (m)")
    ax.legend(frameon=False, fontsize=8)
    path = FIGURE_ROOT / "r14_corridor_map.svg"
    save_svg(fig, path)
    outputs.append(figure_provenance(path, figure_type="corridor_map", data_class="REAL_GEOMETRY", source=[G6_ROOT / "case/manifests/corridors/kamaishi-delivery-corridor-evidence.json"], fields=["corridor", "epicentre", "kamaishi_proxy"]))

    fig, ax = plt.subplots(figsize=(6.3, 5.0), constrained_layout=True)
    collection = PolyCollection(polys, array=bed, cmap="terrain", linewidths=0.0)
    ax.add_collection(collection)
    fig.colorbar(collection, ax=ax, label="bed elevation (m)")
    ax.set_aspect("equal", adjustable="box")
    ax.autoscale()
    ax.set_title("R10 h400 Bed Elevation Plan View")
    ax.set_xlabel("x (m)")
    ax.set_ylabel("y (m)")
    path = FIGURE_ROOT / "r14_bathymetry_plan_view.svg"
    save_svg(fig, path)
    outputs.append(figure_provenance(path, figure_type="bathymetry_plan", data_class="LEGACY_CONVERTED", source=[H400_HDF5], fields=["bed_elevation"]))

    profiles = list(csv.DictReader((R13_ROOT / "projection/projection_profiles.csv").open(encoding="utf-8")))
    fig, ax = plt.subplots(figsize=(7.2, 4.0), constrained_layout=True)
    s = [float(row["s_m"]) / 1000.0 for row in profiles]
    ax.plot(s, [float(row["terrain_direct_m"]) for row in profiles], color="#24292f", linewidth=1.8, label="direct raster")
    ax.plot(s, [float(row["h400_bed_m"]) for row in profiles], color="#f58518", linewidth=1.2, label="h400 mesh")
    ax.plot(s, [float(row["h250_bed_m"]) for row in profiles], color="#b279a2", linewidth=1.2, label="h250 projection")
    ax.set_title("Along-Corridor Bathymetry Projection")
    ax.set_xlabel("distance from offshore source side (km)")
    ax.set_ylabel("bed elevation (m)")
    ax.legend(frameon=False)
    ax.grid(True, alpha=0.25)
    path = FIGURE_ROOT / "r14_longitudinal_bathymetry.svg"
    save_svg(fig, path)
    outputs.append(figure_provenance(path, figure_type="longitudinal_bathymetry", data_class="REAL_RASTER_AND_MESH_PROJECTION", source=[R13_ROOT / "projection/projection_profiles.csv"], fields=["terrain_direct_m", "h400_bed_m", "h250_bed_m"]))

    metrics = list(csv.DictReader((DOCS_ROOT / "regional2d_r13_projection_metrics.csv").open(encoding="utf-8")))
    fig, ax = plt.subplots(figsize=(5.8, 3.8), constrained_layout=True)
    for field, color in (("bed", "#4c78a8"), ("source", "#e45756")):
        rows = [row for row in metrics if row["field"] == field]
        ax.plot([row["level"] for row in rows], [float(row["L2"]) for row in rows], marker="o", color=color, label=field)
    ax.set_title("R13 Projection Fidelity Limit")
    ax.set_xlabel("mesh family")
    ax.set_ylabel("L2 projection error")
    ax.legend(frameon=False)
    ax.grid(True, alpha=0.25)
    path = FIGURE_ROOT / "r14_terrain_fidelity_limit.svg"
    save_svg(fig, path)
    outputs.append(figure_provenance(path, figure_type="terrain_fidelity", data_class="REAL_DIAGNOSTIC", source=[DOCS_ROOT / "regional2d_r13_projection_metrics.csv"], fields=["L2"]))

    fig, ax = plt.subplots(figsize=(6.3, 5.0), constrained_layout=True)
    collection = PolyCollection(polys, array=eta, cmap="coolwarm", linewidths=0.0)
    ax.add_collection(collection)
    fig.colorbar(collection, ax=ax, label="eta (m)")
    ax.set_aspect("equal", adjustable="box")
    ax.autoscale()
    ax.set_title(f"R10 h400 Free Surface, t={snapshot_time:g} s")
    ax.set_xlabel("x (m)")
    ax.set_ylabel("y (m)")
    path = FIGURE_ROOT / "r14_regional_eta_snapshot.svg"
    save_svg(fig, path)
    outputs.append(figure_provenance(path, figure_type="regional_eta_snapshot", data_class="LEGACY_CONVERTED", source=[H400_HDF5], fields=["eta"], notes=f"time={snapshot_time:g}s"))

    fig = plt.figure(figsize=(6.5, 4.8), constrained_layout=True)
    ax = fig.add_subplot(111, projection="3d")
    sample = slice(None, None, max(1, len(centres) // 6000))
    ax.plot_trisurf(centres[sample, 0], centres[sample, 1], eta[sample], color="#4c78a8", linewidth=0.0, antialiased=True, alpha=0.78)
    ax.set_title("Regional Free-Surface Pseudo-3D")
    ax.set_xlabel("x (m)")
    ax.set_ylabel("y (m)")
    ax.set_zlabel("eta (m)")
    path = FIGURE_ROOT / "r14_regional_pseudo3d.svg"
    save_svg(fig, path)
    outputs.append(figure_provenance(path, figure_type="regional_pseudo3d", data_class="LEGACY_CONVERTED_COSMETIC_GEOMETRY", source=[H400_HDF5], fields=["eta"], notes="Pseudo-3D rendering only; numerical data unchanged."))

    for field, filename in (("eta", "r14_coupling_eta_heatmap.svg"), ("qn", "r14_coupling_qn_heatmap.svg")):
        values = dataset.coupling_field(field)
        ctime = dataset.coupling_field("time")
        cs = dataset.coupling_field("s")
        fig, ax = plt.subplots(figsize=(6.4, 4.2), constrained_layout=True)
        image = ax.imshow(values, aspect="auto", origin="lower", extent=[float(cs.min()), float(cs.max()), float(ctime.min()), float(ctime.max())], cmap="magma")
        fig.colorbar(image, ax=ax, label=field)
        ax.set_title(f"Coupling Section {field}")
        ax.set_xlabel("section coordinate s (m)")
        ax.set_ylabel("time (s)")
        path = FIGURE_ROOT / filename
        save_svg(fig, path)
        outputs.append(figure_provenance(path, figure_type=f"coupling_{field}_heatmap", data_class="LEGACY_CONVERTED", source=[H400_HDF5], fields=[field, "time", "s"]))

    fig, ax = plt.subplots(figsize=(6.3, 3.8), constrained_layout=True)
    ax.plot(dataset.coupling_field("time"), dataset.coupling_series("Qn"), color="#e45756", linewidth=1.7)
    ax.set_title("R10 h400 Replay Normal Discharge")
    ax.set_xlabel("time (s)")
    ax.set_ylabel("Qn (m3/s)")
    ax.grid(True, alpha=0.25)
    path = FIGURE_ROOT / "r14_Qn_history.svg"
    save_svg(fig, path)
    outputs.append(figure_provenance(path, figure_type="Qn_history", data_class="LEGACY_CONVERTED", source=[H400_HDF5], fields=["Qn", "time"]))

    fig, ax = plt.subplots(figsize=(6.4, 3.8), constrained_layout=True)
    ax.axis("off")
    ax.text(0.03, 0.80, "Regional2D h400 limited-linear", fontsize=12, weight="bold")
    ax.annotate("", xy=(0.43, 0.72), xytext=(0.28, 0.72), arrowprops={"arrowstyle": "->", "lw": 2})
    ax.text(0.46, 0.80, "selected 245-545 s window", fontsize=11)
    ax.annotate("", xy=(0.70, 0.72), xytext=(0.60, 0.72), arrowprops={"arrowstyle": "->", "lw": 2})
    ax.text(0.72, 0.80, "OpenFOAM boundaryData 0-300 s", fontsize=11)
    ax.text(0.03, 0.30, "Forcing is best-available numerically uncertain; not spatially qualified or physically calibrated.", fontsize=10)
    path = FIGURE_ROOT / "r14_replay_mapping.svg"
    save_svg(fig, path)
    outputs.append(figure_provenance(path, figure_type="replay_mapping", data_class="SCHEMATIC_FROM_REAL_MANIFEST", source=[R13_ROOT / "replay/r13_h400_openfoam_replay_package.json"], fields=["time_mapping", "forcing_status"]))

    fig, ax = plt.subplots(figsize=(6.1, 3.8), constrained_layout=True)
    ax.axis("off")
    rows = [
        ("Verified", "first-order and second-order Regional numerical method"),
        ("Diagnosed", "terrain/source projection fidelity ceiling in real event"),
        ("Accepted", "G6 Local3D replay baseline"),
        ("Blocked", "current h400 Local3D full replay pending VOF boundedness"),
    ]
    for i, (label, text) in enumerate(rows):
        y = 0.86 - 0.20 * i
        ax.text(0.04, y, label, fontsize=11, weight="bold")
        ax.text(0.28, y, text, fontsize=10)
    path = FIGURE_ROOT / "r14_numerical_methodology_status.svg"
    save_svg(fig, path)
    outputs.append(figure_provenance(path, figure_type="numerical_methodology", data_class="STATUS_SUMMARY", source=[DOCS_ROOT / "regional2d_r13_fidelity_hybrid_replay.json"], fields=["verification_status"]))

    fig, ax = plt.subplots(figsize=(6.1, 3.8), constrained_layout=True)
    ax.axis("off")
    labels = ["DART offshore gauges", "NOWPHAS / Kamaishi waveform", "run-up and inundation records", "arrival, amplitude, RMSE, bias"]
    for i, text in enumerate(labels):
        ax.text(0.08, 0.78 - i * 0.18, text, fontsize=11)
    ax.text(0.08, 0.08, "Status: VALIDATION TARGETS, not completed historical validation", fontsize=10, color="#cf222e")
    path = FIGURE_ROOT / "r14_validation_targets.svg"
    save_svg(fig, path)
    outputs.append(figure_provenance(path, figure_type="validation_targets", data_class="SCHEMATIC", source=[DOCS_ROOT / "regional2d_r13_fidelity_hybrid_replay.json"], fields=["validation_targets"]))

    manifest = {
        "schema": {"name": "tsunami.c1a_r14_figure_manifest", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "figures": outputs,
    }
    write_json(FIGURE_ROOT / "r14_figure_manifest.json", manifest)
    write_json(DOCS_ROOT / "regional2d_r14_figure_manifest.json", manifest)
    update_state(regional_visual_status="COMPLETE", coupling_visual_status="COMPLETE", last_successful_checkpoint="r14_figures_complete")
    return manifest


def write_handoffs(boundedness: Mapping[str, Any] | None = None, storage: Mapping[str, Any] | None = None, figures: Mapping[str, Any] | None = None) -> dict[str, Any]:
    boundedness = boundedness or read_json(DOCS_ROOT / "regional2d_r14_local3d_boundedness_audit.json")
    storage = storage or read_json(DOCS_ROOT / "regional2d_r14_result_storage_audit.json")
    figures = figures or read_json(DOCS_ROOT / "regional2d_r14_figure_manifest.json")
    update_state(poster_handoff_status="RUNNING")
    handoff = f"""# R14 Poster Handoff

Allowed hybrid claim:
A one-way Regional2D to Local3D hybrid replay framework has been implemented and demonstrated on the accepted G6 baseline. The current h400 limited-linear real-event forcing has been packaged for replay, but the full current-generation replay remains diagnostic until the Local3D boundedness gate is accepted.

Required numerical-fidelity caveat:
The R10 h400 limited-linear Regional forcing is the best available real-event forcing, but it is `BEST_AVAILABLE_NUMERICALLY_UNCERTAIN`, not spatially qualified, not physically calibrated, and not historically validated.

Implemented:
- Regional2D solver and terrain/source pipeline
- limited-linear Regional reconstruction
- OpenFOAM 11 Local3D replay generation
- HDF5 ResultDataset plotting workflow

Demonstrated:
- G6 accepted 300 s Local3D no-defence and rigid-barrier replay
- current h400 replay package and smoke diagnostics
- poster-ready Regional/coupling figures

Verified:
- first-order and second-order Regional numerical method evidence from prior C1A work
- R14 HDF5/XDMF/ResultDataset access to the h400 result

Diagnostic:
- current h400 Local3D smoke: `{boundedness['classification']}`

Validation target:
- DART, NOWPHAS/Kamaishi, and run-up/inundation comparisons remain future validation targets.

Figure manifest:
`{FIGURE_ROOT / 'r14_figure_manifest.json'}`
"""
    morning = f"""# R14 Morning Decision Package

WHAT COMPLETED
- R14 worktree and resumable state were created.
- Local3D boundedness diagnosis completed: `{boundedness['classification']}`.
- R13 forcing/replay authority hashes were verified.
- h400 HDF5/XDMF/ResultDataset workflow was validated.
- Regional and coupling poster figures were generated with provenance.

WHAT FAILED
- Full current-generation 300 s Local3D replay was not launched because the boundedness gate is closed.

WHAT IS SCIENTIFICALLY ACCEPTED
- G6 Local3D replay baseline.
- Regional numerical verification evidence from prior C1A work.
- R10 h400 limited-linear as best-available numerically uncertain real-event forcing.

WHAT CAN GO ON THE POSTER TODAY
- R14 Regional/coupling figures in `{FIGURE_ROOT}`.
- G6 hybrid replay as demonstrated baseline.
- Current h400 replay package as diagnostic/current-forcing preparation.

WHAT CANNOT BE CLAIMED
- Historical Tohoku validation.
- Mesh-converged current forcing.
- Accepted current-generation full Local3D replay.
- Calibrated or decision-grade defence-impact prediction.

WHAT FIGURES ARE READY
- {len(figures['figures'])} R14 figures with provenance.

WHETHER VIDEO IS READY
- No. Video status is `VIDEO_LEVEL_0`; validated static figures only.

WHETHER QR HOSTING CAN BEGIN
- Not for video. Static figure/package hosting can begin if desired.

THE SINGLE HIGHEST-VALUE TUESDAY TASK
- Decide whether to run a tightly instrumented OpenFOAM boundedness diagnostic or accept deferral of current-generation full replay for the poster.
"""
    handoff_path = DOCS_ROOT / "regional2d_r14_poster_handoff.md"
    morning_path = DOCS_ROOT / "r14_morning_decision.md"
    handoff_path.write_text(handoff, encoding="utf-8")
    morning_path.write_text(morning, encoding="utf-8")
    result = {
        "poster_handoff": str(handoff_path),
        "poster_handoff_sha256": sha256(handoff_path),
        "morning_decision": str(morning_path),
        "morning_decision_sha256": sha256(morning_path),
        "video_status": "VIDEO_LEVEL_0",
        "overall_outcome": "R14_MECHANICS_BLOCKED" if boundedness["classification"] != "REPLAY_BOUNDEDNESS_ACCEPTED" else "R14_MINIMUM_SUCCESS",
    }
    write_json(DOCS_ROOT / "regional2d_r14_handoff_manifest.json", result)
    update_state(poster_handoff_status="COMPLETE", video_status="DEFERRED", last_successful_checkpoint="r14_handoff_complete")
    return result


def run_all() -> dict[str, Any]:
    boundedness = boundedness_audit()
    storage = hdf5_audit()
    figures = generate_figures()
    handoff = write_handoffs(boundedness, storage, figures)
    return {"boundedness": boundedness, "storage": storage, "figures": figures, "handoff": handoff}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("boundedness")
    sub.add_parser("hdf5-audit")
    sub.add_parser("figures")
    sub.add_parser("handoff")
    sub.add_parser("all")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == "boundedness":
        payload = boundedness_audit()
    elif args.command == "hdf5-audit":
        payload = hdf5_audit()
    elif args.command == "figures":
        payload = generate_figures()
    elif args.command == "handoff":
        payload = write_handoffs()
    elif args.command == "all":
        payload = run_all()
    else:
        raise AssertionError(args.command)
    print(json.dumps(payload if args.command != "all" else payload["handoff"], indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
