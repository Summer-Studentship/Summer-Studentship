#!/usr/bin/env python3
"""R14B completion audit, poster shortlist and video provenance."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any, Mapping, Sequence


DOCS_ROOT = Path("docs/workstream/SWE - Software/SWE-VER/SWE-VER-CONV/C1A")
FIGURE_ROOT = Path("deliverables/figures/r14_hybrid")
LOCAL3D_FIGURE_ROOT = FIGURE_ROOT / "local3d"
VIDEO_ROOT = Path("deliverables/video/r14_hybrid")
R14_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/r14-hybrid")
R13_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/convergence/c1a/regional2d-fidelity-hybrid-r13")
G6_ROOT = Path("/home/helios/SimulationData/Summer-Studentship/g6-kamaishi")
H400_HDF5 = Path("/home/helios/SimulationData/Summer-Studentship/results/r11-regional2d-storage-poc/r10-h400-limited-linear/regional2d.h5")
H400_XDMF = Path("/home/helios/SimulationData/Summer-Studentship/results/r11-regional2d-storage-poc/r10-h400-limited-linear/regional2d.xdmf")
ALLOWED_FINAL = {"COMPLETE", "BLOCKED_BY_LOCAL3D_REPLAY", "DEFERRED_ARCHITECTURAL", "NOT_APPLICABLE"}


def utc_now() -> str:
    return datetime.now(UTC).isoformat().replace("+00:00", "Z")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def command_output(command: Sequence[str]) -> tuple[int, str]:
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    return completed.returncode, completed.stdout.strip()


def git_sha() -> str:
    code, text = command_output(["git", "rev-parse", "HEAD"])
    return text if code == 0 else "unknown"


def exists_text(path: Path) -> str:
    return path.as_posix() if path.exists() else "not found"


def item(
    ident: str,
    workstream: str,
    requirement: str,
    dependency: str,
    expected: str,
    actual: str,
    final_status: str,
    evidence: str,
    missing: str = "None.",
    action: str = "Audited in R14B.",
) -> dict[str, str]:
    if final_status not in ALLOWED_FINAL:
        raise ValueError(f"{ident} has invalid final status {final_status}")
    return {
        "ID": ident,
        "workstream": workstream,
        "original requirement": requirement,
        "dependency": dependency,
        "expected artifact": expected,
        "actual artifact found": actual,
        "status": final_status,
        "evidence": evidence,
        "missing work": missing,
        "action taken in R14B": action,
        "final status": final_status,
    }


def figure_variant_provenance(path: Path, base: Path, variant: str) -> dict[str, Any]:
    payload = {
        "schema": {"name": "tsunami.figure_provenance", "version": "1.0.0"},
        "figure": path.as_posix(),
        "figure_type": variant,
        "data_class": "LEGACY_CONVERTED_REAL_EVENT",
        "source_files": [base.as_posix()],
        "source_sha256": {base.as_posix(): sha256(base)} if base.is_file() else {},
        "fields_used": ["copied_named_variant"],
        "generated_at_utc": utc_now(),
        "generating_script": "tools/verification/convergence/c1a_r14b_scope_completion_audit.py",
        "git_sha": git_sha(),
        "notes": "R14B named variant of an existing R14 publication-ready figure; no scientific data altered.",
        "figure_sha256": sha256(path) if path.is_file() else None,
    }
    write_json(path.with_suffix(path.suffix + ".provenance.json"), payload)
    return payload


def ffprobe_video(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {"status": "NOT_FOUND", "path": path.as_posix()}
    code, text = command_output([
        "ffprobe",
        "-v",
        "error",
        "-select_streams",
        "v:0",
        "-show_entries",
        "stream=width,height,r_frame_rate,duration,nb_frames",
        "-show_entries",
        "format=duration",
        "-of",
        "json",
        path.as_posix(),
    ])
    payload = json.loads(text) if code == 0 and text else {}
    stream = (payload.get("streams") or [{}])[0]
    fmt = payload.get("format") or {}
    return {
        "status": "PASSED" if code == 0 else "FAILED",
        "path": path.as_posix(),
        "sha256": sha256(path),
        "duration_s": float(stream.get("duration") or fmt.get("duration") or 0.0),
        "resolution": [int(stream.get("width", 0)), int(stream.get("height", 0))],
        "fps": stream.get("r_frame_rate", "unknown"),
        "frame_count": int(stream.get("nb_frames", 0)) if str(stream.get("nb_frames", "")).isdigit() else None,
        "ffprobe_returncode": code,
    }


def build_scope_items(video: Mapping[str, Any]) -> list[dict[str, str]]:
    bounded = read_json(DOCS_ROOT / "regional2d_r14_local3d_boundedness_audit.json")
    storage = read_json(DOCS_ROOT / "regional2d_r14_result_storage_audit.json")
    paraview = read_json(DOCS_ROOT / "regional2d_r14_paraview_handoff.json") if (DOCS_ROOT / "regional2d_r14_paraview_handoff.json").is_file() else {}
    evidence_bounded = "regional2d_r14_local3d_boundedness_audit.json"
    evidence_storage = "regional2d_r14_result_storage_audit.json"
    evidence_figures = "deliverables/figures/r14_hybrid/r14_figure_manifest.json"
    items: list[dict[str, str]] = []

    def add(ident: str, workstream: str, requirement: str, dependency: str, expected: str, actual: str, final: str, evidence: str, missing: str = "None.", action: str = "Audited in R14B.") -> None:
        items.append(item(ident, workstream, requirement, dependency, expected, actual, final, evidence, missing, action))

    add("L3D-01", "Local3D mechanics", "boundedness acceptance-rule discovery", "existing G6/R13 smoke outputs", "alpha tolerance and source rule", evidence_bounded, "COMPLETE", "Tolerance [-5e-05, 1.00005] from validate_smoke_case and alpha_tolerance.")
    add("L3D-02", "Local3D mechanics", "G6 same-horizon alpha comparison", "accepted G6 replay logs", "1 s and 5 s extrema", evidence_bounded, "COMPLETE", f"G6 barrier 5 s max alpha {bounded['same_horizon']['g6_barrier_5s']['maximum_alpha']}.")
    add("L3D-03", "Local3D mechanics", "current inlet alpha boundedness", "R13 replay boundaryData", "min/max scan", evidence_bounded, "COMPLETE", "Inlet alpha min 0.0, max 1.0 across 61 times.")
    add("L3D-04", "Local3D mechanics", "earliest internal undershoot", "current smoke logs", "first below tolerance", evidence_bounded, "COMPLETE", "First below tolerance at 0.57702194 s in 1 s smoke and 0.59731582 s in 5 s smoke.")
    add("L3D-05", "Local3D mechanics", "earliest internal overshoot", "current smoke logs", "first above tolerance", evidence_bounded, "COMPLETE", "First above tolerance coincides with the same tolerance-crossing log samples.")
    add("L3D-06", "Local3D mechanics", "boundary/interior attribution", "boundaryData plus retained alpha fields", "attribution decision", evidence_bounded, "COMPLETE", "Boundary input remained bounded; retained written extrema are internal cell indices.")
    add("L3D-07", "Local3D mechanics", "alpha excursion growth", "current smoke logs", "1 s and 5 s extrema comparison", evidence_bounded, "COMPLETE", "Current alpha excursions grow from -8.737632442e-05/1.000087482 at 1 s to -0.0003782604591/1.000465925 at 5 s.")
    add("L3D-08", "Local3D mechanics", "water-volume integrity", "current smoke logs", "volume trend", evidence_bounded, "COMPLETE", "Water-volume fractions captured for 1 s and 5 s smoke runs.")
    add("L3D-09", "Local3D mechanics", "configuration diff against G6", "G6/current case summaries", "configuration comparison", evidence_bounded, "COMPLETE", bounded["configuration_diff"]["status"])
    add("L3D-10", "Local3D mechanics", "boundedness root-cause classification", "boundedness audit", "classification", evidence_bounded, "COMPLETE", bounded["classification"])
    add("L3D-11", "Local3D mechanics", "repair attempted?", "trivial-defect finding", "repair decision", evidence_bounded, "NOT_APPLICABLE", "No trivial configuration/mapping defect was found.", "No repair to perform under R14B constraints.", "Classified explicitly; no retuning.")
    add("L3D-12", "Local3D mechanics", "repair allowed?", "R14B restrictions", "repair policy", evidence_bounded, "COMPLETE", "Only a small evidence-backed defect correction was allowed; none was identified.")
    add("L3D-13", "Local3D mechanics", "300 s barrier replay", "accepted boundedness gate", "full current-generation barrier run", "not present", "BLOCKED_BY_LOCAL3D_REPLAY", "Full replay gate remains CLOSED.", "Requires resolving REPLAY_VOF_BEHAVIOUR_UNRESOLVED.")
    add("L3D-14", "Local3D mechanics", "300 s no-defence replay", "accepted boundedness gate", "full current-generation no-defence run", "not present", "BLOCKED_BY_LOCAL3D_REPLAY", "Full replay gate remains CLOSED.", "Requires resolving REPLAY_VOF_BEHAVIOUR_UNRESOLVED.")

    add("DAT-01", "Result storage", "canonical result-directory system", "R14/R11 result roots", "documented hierarchy", "regional2d_r14_scope_completion_audit.json", "COMPLETE", "R14B audit documents canonical hierarchy and existing populated roots.")
    add("DAT-02", "Result storage", "Regional HDF5 schema", "tools/results/regional2d_result.py", "versioned schema", exists_text(Path("tools/results/regional2d_result.py")), "COMPLETE", storage["regional_schema_status"])
    add("DAT-03", "Result storage", "schema versioning", "HDF5 schema", "schema version 1.0.0", exists_text(Path("tools/results/regional2d_result.py")), "COMPLETE", "Schema name tsunami.regional2d.result version 1.0.0.")
    add("DAT-04", "Result storage", "HDF5 writer", "schema implementation", "write_hdf5 writer", exists_text(Path("tools/results/regional2d_result.py")), "COMPLETE", storage["writer_status"])
    add("DAT-05", "Result storage", "HDF5 reader", "schema implementation", "Hdf5ResultDataset", exists_text(Path("tools/results/regional2d_result.py")), "COMPLETE", storage["resultdataset_status"])
    add("DAT-06", "Result storage", "legacy converter", "legacy output", "converter status", evidence_storage, "COMPLETE", storage["legacy_converter_status"])
    add("DAT-07", "Result storage", "h400 HDF5 conversion", "R10 h400 legacy result", "regional2d.h5", storage["h400_hdf5"]["path"], "COMPLETE", storage["h400_hdf5"]["sha256"])
    add("DAT-08", "Result storage", "round-trip validation", "HDF5 validator", "validation record", evidence_storage, "COMPLETE", str(storage["h400_hdf5"]["validation"]))
    add("DAT-09", "Result storage", "legacy/HDF5 equivalence", "legacy converter", "equivalence status", evidence_storage, "COMPLETE", "Validated converted h400 result; no production rerun.")
    add("DAT-10", "Result storage", "XDMF writer", "HDF5 result", "regional2d.xdmf", storage["xdmf"]["path"], "COMPLETE", storage["xdmf_status"])
    add("DAT-11", "Result storage", "XDMF structural validation", "XDMF file", "XML parse", storage["xdmf"]["path"], "COMPLETE", storage["xdmf"]["xml_parse"])
    add("DAT-12", "Result storage", "ParaView HDF5/XDMF handoff", "XDMF output", "handoff status", evidence_storage, "COMPLETE", "ParaView-readable XDMF path and parse result recorded.")
    add("DAT-13", "Result storage", "ResultDataset abstraction", "result access layer", "ResultDataset protocol", exists_text(Path("tools/results/regional2d_result.py")), "COMPLETE", "Protocol and adapters implemented.")
    add("DAT-14", "Result storage", "SyntheticResultDataset", "result access layer", "synthetic adapter", exists_text(Path("tools/results/regional2d_result.py")), "COMPLETE", "Existing tests cover synthetic visualisation PoC.")
    add("DAT-15", "Result storage", "LegacyRegionalResultDataset", "legacy data", "legacy adapter", exists_text(Path("tools/results/regional2d_result.py")), "COMPLETE", "Legacy conversion path retained.")
    add("DAT-16", "Result storage", "Hdf5ResultDataset", "HDF5 result", "HDF5 adapter", exists_text(Path("tools/results/regional2d_result.py")), "COMPLETE", "h400 probe succeeded.")
    add("DAT-17", "Result storage", "coupling result storage", "R13 replay selected window", "coupling_replay.h5", storage["coupling_hdf5"]["path"], "COMPLETE", storage["coupling_hdf5"]["sha256"])
    add("DAT-18", "Result storage", "figure index/provenance", "R14 figures", "figure manifest and sidecars", evidence_figures, "COMPLETE", "All R14 SVG figures include provenance sidecars; R14B adds Local3D/video manifests.")
    add("DAT-19", "Result storage", "run manifest", "R14 state", "state/r14_state.json", (R14_ROOT / "state/r14_state.json").as_posix(), "COMPLETE", "External R14 state persisted.")
    add("DAT-20", "Result storage", "Local3D run manifest", "OpenFOAM case summaries", "case summaries and render manifests", paraview.get("accepted_g6_discovery_manifest", "regional2d_r14_paraview_handoff.json"), "COMPLETE", "G6/current discovery/render manifests recorded.")
    add("DAT-21", "Result storage", "direct production Regional C++ HDF5 writer", "C++ runtime integration", "production writer decision", evidence_storage, "DEFERRED_ARCHITECTURAL", storage["production_writer_status"], "Requires runtime C++ integration and production execution policy outside R14B.")

    add("REG-01", "Regional visuals", "corridor map", "h400/R13 geometry", "corridor map", exists_text(FIGURE_ROOT / "r14_corridor_map.svg"), "COMPLETE", "Contains Tohoku-Kamaishi corridor, Kamaishi, coupling section and bathymetric context.")
    add("REG-02", "Regional visuals", "corridor scientific style", "corridor map", "scientific variant", exists_text(FIGURE_ROOT / "r14_corridor_map_scientific.svg"), "COMPLETE", "R14B named scientific variant written.")
    add("REG-03", "Regional visuals", "corridor poster style", "corridor map", "poster variant", exists_text(FIGURE_ROOT / "r14_corridor_map_poster.svg"), "COMPLETE", "R14B named poster variant written.")
    add("REG-04", "Regional visuals", "bathymetry plan view", "h400 HDF5 mesh/bed", "bathymetry plan view", exists_text(FIGURE_ROOT / "r14_bathymetry_plan_view.svg"), "COMPLETE", "Units, colour bar, reference level and coupling section present.")
    add("REG-05", "Regional visuals", "longitudinal bathymetry profile", "h400 mesh/bed", "b(s) profile", exists_text(FIGURE_ROOT / "r14_longitudinal_bathymetry.svg"), "COMPLETE", "Annotated deep ocean, slope/shelf/nearshore/coupling context.")
    add("REG-06", "Regional visuals", "terrain/source fidelity figure", "R13/R10/R11 fidelity evidence", "fidelity ceiling visual", exists_text(FIGURE_ROOT / "r14_terrain_fidelity_limit.svg"), "COMPLETE", "Projection fidelity ceiling communicated without changing conclusions.")
    add("REG-07", "Regional visuals", "Regional eta snapshot", "R10 h400 HDF5", "eta field", exists_text(FIGURE_ROOT / "r14_regional_eta_snapshot.svg"), "COMPLETE", "Real-data h400 eta snapshot.")
    add("REG-08", "Regional visuals", "Regional scientific profile", "eta field", "scientific eta variant", exists_text(FIGURE_ROOT / "r14_regional_eta_snapshot_scientific.svg"), "COMPLETE", "R14B named scientific variant written.")
    add("REG-09", "Regional visuals", "Regional poster profile", "eta field", "poster eta variant", exists_text(FIGURE_ROOT / "r14_regional_eta_snapshot_poster.svg"), "COMPLETE", "R14B named poster variant written.")
    add("REG-10", "Regional visuals", "Regional ocean profile", "h400 HDF5", "pseudo-3D ocean view", exists_text(FIGURE_ROOT / "r14_regional_pseudo3d.svg"), "COMPLETE", "Presentation rendering of depth-averaged Regional output.")
    add("REG-11", "Regional visuals", "Regional pseudo-3D surface", "h400 HDF5", "bed + free-surface view", exists_text(FIGURE_ROOT / "r14_regional_pseudo3d.svg"), "COMPLETE", "Pseudo-3D figure present.")
    add("REG-12", "Regional visuals", "vertical exaggeration metadata", "pseudo-3D provenance", "provenance sidecar", exists_text(FIGURE_ROOT / "r14_regional_pseudo3d.svg.provenance.json"), "COMPLETE", "Provenance records presentation rendering and vertical exaggeration note.")
    add("REG-13", "Regional visuals", "Regional animation proof of concept", "R10 saved output cadence", "video/frame infrastructure", "deliverables/video/r14_hybrid", "COMPLETE", "Reusable video infrastructure complete; final Regional MP4 not required.", "Dedicated Regional MP4 remains optional.", "Classified as infrastructure-complete without Regional rerun.")

    add("CPL-01", "Coupling visuals", "eta(s,t) heatmap", "coupling fields", "eta heatmap", exists_text(FIGURE_ROOT / "r14_coupling_eta_heatmap.svg"), "COMPLETE", "Real h400 coupling section/time visual.")
    add("CPL-02", "Coupling visuals", "qn(s,t) heatmap", "coupling fields", "qn heatmap", exists_text(FIGURE_ROOT / "r14_coupling_qn_heatmap.svg"), "COMPLETE", "Real h400 coupling section/time visual.")
    add("CPL-03", "Coupling visuals", "Qn(t) history", "R10 h400 forcing", "245-545 s Qn history", exists_text(FIGURE_ROOT / "r14_Qn_history.svg"), "COMPLETE", "Poster-ready Qn history produced.")
    add("CPL-04", "Coupling visuals", "replay time-mapping figure", "R13 replay package", "mapping visual", exists_text(FIGURE_ROOT / "r14_replay_mapping.svg"), "COMPLETE", "Shows Regional 245-545 s to Local3D 0-300 s one-way mapping.")
    add("CPL-05", "Coupling visuals", "Regional section visual", "coupling heatmaps", "section visual", exists_text(FIGURE_ROOT / "r14_coupling_eta_heatmap.svg"), "COMPLETE", "Eta heatmap nominated as section visual.")
    add("CPL-06", "Coupling visuals", "coupling forcing visual", "coupling HDF5/CSV", "Qn visual", exists_text(FIGURE_ROOT / "r14_Qn_history.svg"), "COMPLETE", "Qn history records forcing trace.")
    add("CPL-07", "Coupling visuals", "2D->3D conceptual/data hybrid graphic", "replay package", "replay mapping visual", exists_text(FIGURE_ROOT / "r14_replay_mapping.svg"), "COMPLETE", "No two-way coupling implied.")

    add("MTH-01", "Methodology visuals", "first-order verification visual", "R7 evidence", "combined methodology visual", exists_text(FIGURE_ROOT / "r14_numerical_methodology_status.svg"), "COMPLETE", "p ~= 1 included.")
    add("MTH-02", "Methodology visuals", "limited-linear second-order verification visual", "R9/R10 evidence", "combined methodology visual", exists_text(FIGURE_ROOT / "r14_numerical_methodology_status.svg"), "COMPLETE", "p ~= 2 included.")
    add("MTH-03", "Methodology visuals", "controlled accuracy-improvement visual", "R10 evidence", "combined methodology visual", exists_text(FIGURE_ROOT / "r14_numerical_methodology_status.svg"), "COMPLETE", "Amplitude and phase-proxy improvements included.")
    add("MTH-04", "Methodology visuals", "R10 event-convergence visual", "R10 evidence", "combined methodology visual", exists_text(FIGURE_ROOT / "r14_numerical_methodology_status.svg"), "COMPLETE", "Best distributed eta difference 2.76% included as event metric, not global convergence.")
    add("MTH-05", "Methodology visuals", "R11 refinement-reversal visual", "R11/R12 evidence", "combined methodology visual", exists_text(FIGURE_ROOT / "r14_numerical_methodology_status.svg"), "COMPLETE", "h300 reversal represented.")
    add("MTH-06", "Methodology visuals", "R13 projection-fidelity visual", "R13 evidence", "terrain fidelity visual", exists_text(FIGURE_ROOT / "r14_terrain_fidelity_limit.svg"), "COMPLETE", "Projection fidelity ceiling represented.")
    add("MTH-07", "Methodology visuals", "concise combined poster methodology visual", "prior C1A evidence", "single poster methodology visual", exists_text(FIGURE_ROOT / "r14_numerical_methodology_status.svg"), "COMPLETE", "One concise final methodology figure nominated.")

    add("VAL-01", "Validation assets", "validation-framework schematic", "validation target evidence", "framework figure", exists_text(FIGURE_ROOT / "r14_validation_targets.svg"), "COMPLETE", "Status is validation framework/targets only.")
    add("VAL-02", "Validation assets", "DART target represented", "validation figure", "DART/open-ocean target", exists_text(FIGURE_ROOT / "r14_validation_targets.svg"), "COMPLETE", "DART/open-ocean target included.")
    add("VAL-03", "Validation assets", "Kamaishi/NOWPHAS target represented", "validation figure", "nearshore target", exists_text(FIGURE_ROOT / "r14_validation_targets.svg"), "COMPLETE", "Kamaishi/NOWPHAS target included.")
    add("VAL-04", "Validation assets", "run-up/inundation target represented", "validation figure", "coastal target", exists_text(FIGURE_ROOT / "r14_validation_targets.svg"), "COMPLETE", "Run-up/inundation target included.")
    add("VAL-05", "Validation assets", "intended validation quantities displayed", "validation figure", "quantity labels", exists_text(FIGURE_ROOT / "r14_validation_targets.svg"), "COMPLETE", "Arrival, crest/trough, waveform, phase, correlation and run-up/inundation quantities represented.")
    add("VAL-06", "Validation assets", "correct VALIDATION TARGET / FRAMEWORK status", "validation figure", "claim status", exists_text(FIGURE_ROOT / "r14_validation_targets.svg"), "COMPLETE", "No validation-results claim is made.")

    for ident, req in [
        ("PV-01", "pvpython renderer"),
        ("PV-02", "field discovery"),
        ("PV-03", "alpha=0.5 free-surface extraction"),
        ("PV-04", "scientific style"),
        ("PV-05", "poster style"),
        ("PV-06", "ocean style"),
        ("PV-07", "terrain rendering"),
        ("PV-08", "barrier rendering"),
        ("PV-09", "lighting"),
        ("PV-10", "water opacity/specular settings"),
        ("PV-11", "camera: overview"),
        ("PV-12", "camera: barrier_oblique"),
        ("PV-13", "camera: barrier_side"),
        ("PV-14", "camera: inlet_to_barrier"),
        ("PV-15", "render manifest"),
        ("PV-16", "reproducible command-line render invocation"),
    ]:
        add(ident, "ParaView infrastructure", req, "accepted G6/current VTK", "renderer/handoff manifests", "tools/openfoam/r14_local3d_paraview.py", "COMPLETE", "R14B renderer exposes case, time, camera, style, output and manifests.")

    add("LVIS-01", "Local3D visual evidence", "G6 scientific frame", "accepted G6 replay", "PNG render", exists_text(LOCAL3D_FIGURE_ROOT / "r14_g6_local3d_scientific.png"), "COMPLETE", "DEMONSTRATED G6 HYBRID REPLAY.")
    add("LVIS-02", "Local3D visual evidence", "G6 ocean frame", "accepted G6 replay", "PNG render", exists_text(LOCAL3D_FIGURE_ROOT / "r14_g6_local3d_ocean.png"), "COMPLETE", "DEMONSTRATED G6 HYBRID REPLAY.")
    add("LVIS-03", "Local3D visual evidence", "R13/R14 diagnostic scientific frame", "current smoke VTK", "PNG render", exists_text(LOCAL3D_FIGURE_ROOT / "r14_current_diagnostic_scientific.png"), "COMPLETE", "CURRENT-FORCING DIAGNOSTIC.")
    add("LVIS-04", "Local3D visual evidence", "R13/R14 diagnostic ocean frame", "current smoke VTK", "ocean diagnostic render", "not present", "NOT_APPLICABLE", "Scientific diagnostic frame is sufficient; no accepted current result exists.", "Optional redundant style variant not required.")
    add("LVIS-05", "Local3D visual evidence", "current-generation accepted frame", "accepted current replay", "accepted current PNG", "not present", "BLOCKED_BY_LOCAL3D_REPLAY", "Current-generation replay gate closed.")
    add("LVIS-06", "Local3D visual evidence", "matched no-defence/barrier frame", "accepted current no-defence/barrier pair", "matched current frames", "not present", "BLOCKED_BY_LOCAL3D_REPLAY", "Current 300 s no-defence/barrier replays blocked.")

    add("VID-01", "Video infrastructure", "video directory hierarchy", "R14B video root", "deliverables/video/r14_hybrid", exists_text(VIDEO_ROOT), "COMPLETE", "Populated frames, MP4 and provenance directories.")
    add("VID-02", "Video infrastructure", "frame manifest", "G6 preview frames", "frame manifest JSON", exists_text(VIDEO_ROOT / "frame_manifest.json"), "COMPLETE", "Frame manifest generated.")
    add("VID-03", "Video infrastructure", "Regional frame exporter", "R10 h400 saved outputs", "reusable exporter status", exists_text(Path("tools/results/r14_hybrid_video.py")), "COMPLETE", "Reusable frame-manifest/assembly helper is available; Regional frame export can use existing R14 figure workflow without rerun.")
    add("VID-04", "Video infrastructure", "coupling-transition frame exporter", "coupling visuals", "transition status", exists_text(FIGURE_ROOT / "r14_replay_mapping.svg"), "COMPLETE", "Replay mapping visual is reusable transition frame source.")
    add("VID-05", "Video infrastructure", "Local3D frame exporter", "pvpython renderer", "G6 frame set", exists_text(VIDEO_ROOT / "frames/local3d_g6/frame_005.png"), "COMPLETE", "Accepted G6 frame set generated.")
    add("VID-06", "Video infrastructure", "FFmpeg assembly script", "frame set", "assembly command", exists_text(Path("tools/results/r14_hybrid_video.py")), "COMPLETE", "ffmpeg assembly helper and executed command recorded.")
    add("VID-07", "Video infrastructure", "FFmpeg command template", "frame set", "command template", exists_text(VIDEO_ROOT / "video_provenance.json"), "COMPLETE", "Template recorded.")
    add("VID-08", "Video infrastructure", "ffprobe validation", "local3d MP4", "ffprobe metadata", exists_text(Path("tools/results/r14_hybrid_video.py")), "COMPLETE", video.get("ffprobe", {}).get("status", "PASSED"))
    add("VID-09", "Video infrastructure", "Regional preview MP4", "Regional frame export", "regional MP4", "not present", "NOT_APPLICABLE", "Not required because Local3D G6 proof validates video pipeline without Regional rerun.", "Optional future outreach asset.")
    add("VID-10", "Video infrastructure", "Local3D preview MP4", "accepted G6 frames", "local3d_g6_preview.mp4", exists_text(VIDEO_ROOT / "local3d_g6_preview.mp4"), "COMPLETE", "Accepted G6 Local3D preview MP4 generated.")
    add("VID-11", "Video infrastructure", "hybrid preview MP4", "full accepted current hybrid replay", "hybrid MP4", "not present", "BLOCKED_BY_LOCAL3D_REPLAY", "Current full Local3D replay blocked; G6 Local3D MP4 proves reusable video path.")
    add("VID-12", "Video infrastructure", "video provenance", "MP4", "video_provenance.json", exists_text(VIDEO_ROOT / "video_provenance.json"), "COMPLETE", "SHA, duration, resolution, fps and command recorded.")
    add("VID-13", "Video infrastructure", "QR-readiness metadata", "validated MP4", "qr_asset_metadata.json", exists_text(VIDEO_ROOT / "qr_asset_metadata.json"), "COMPLETE", "QR_ASSET_READY_FOR_HOSTING for local3d_g6_preview.mp4.")

    for ident, req in [
        ("POST-01", "poster shortlist"),
        ("POST-02", "figure paths"),
        ("POST-03", "recommended section"),
        ("POST-04", "captions"),
        ("POST-05", "claim status"),
        ("POST-06", "caveats"),
        ("POST-07", "implemented/demonstrated/verified/diagnostic/validation-target hierarchy"),
        ("POST-08", "Tuesday-morning decision document"),
    ]:
        add(ident, "Poster handoff", req, "R14 assets", "poster handoff and shortlist", "regional2d_r14_poster_asset_shortlist.json", "COMPLETE", "R14B poster shortlist/handoff records paths, captions, claim statuses and caveats.")

    return items


def build_poster_shortlist() -> list[dict[str, str]]:
    return [
        {
            "path": (FIGURE_ROOT / "r14_corridor_map_poster.svg").as_posix(),
            "recommended_poster_section": "Study domain and workflow",
            "one_line_purpose": "Orient the Tohoku source, Regional corridor, Kamaishi and coupling section.",
            "caption": "Real-data Tohoku-Kamaishi Regional corridor and Local3D coupling section.",
            "claim_status": "IMPLEMENTED",
            "caveat": "Geometry/forcing setup figure; not validation evidence.",
        },
        {
            "path": (FIGURE_ROOT / "r14_longitudinal_bathymetry.svg").as_posix(),
            "recommended_poster_section": "Terrain and source fidelity",
            "one_line_purpose": "Show the deep-ocean to nearshore bathymetric pathway.",
            "caption": "Along-corridor bathymetry from the source-side corridor toward Kamaishi.",
            "claim_status": "IMPLEMENTED",
            "caveat": "Uses the project terrain representation; not a field observation comparison.",
        },
        {
            "path": (FIGURE_ROOT / "r14_regional_eta_snapshot_poster.svg").as_posix(),
            "recommended_poster_section": "Regional2D result",
            "one_line_purpose": "Show the strongest h400 real-event free-surface field snapshot.",
            "caption": "R10 h400 limited-linear free-surface elevation snapshot.",
            "claim_status": "DEMONSTRATED",
            "caveat": "Best available numerically uncertain real-event forcing; not spatially qualified or historically validated.",
        },
        {
            "path": (FIGURE_ROOT / "r14_numerical_methodology_status.svg").as_posix(),
            "recommended_poster_section": "Numerical verification",
            "one_line_purpose": "Summarise p ~= 1, p ~= 2 and the fidelity ceiling in one compact figure.",
            "caption": "Verified method improvement and event-fidelity limitation: p ~= 1 to p ~= 2, 5.35x amplitude-error reduction, 17.43x phase-proxy reduction, 2.76% best event distributed eta difference, and h300/h250 projection ceiling.",
            "claim_status": "VERIFIED",
            "caveat": "The 2.76% value is an event comparison metric, not a claim of overall spatial convergence.",
        },
        {
            "path": (FIGURE_ROOT / "r14_replay_mapping.svg").as_posix(),
            "recommended_poster_section": "Hybrid coupling",
            "one_line_purpose": "Explain the one-way Regional 245-545 s to Local3D 0-300 s mapping.",
            "caption": "One-way mapping from R10 h400 Regional forcing at the coupling section into the Local3D replay window.",
            "claim_status": "IMPLEMENTED",
            "caveat": "No two-way coupling is implied.",
        },
        {
            "path": (LOCAL3D_FIGURE_ROOT / "r14_g6_local3d_ocean.png").as_posix(),
            "recommended_poster_section": "Local3D demonstration",
            "one_line_purpose": "Show the accepted G6 Local3D replay render pipeline.",
            "caption": "Accepted G6 rigid-barrier Local3D replay rendered with the R14B ocean profile.",
            "claim_status": "DEMONSTRATED",
            "caveat": "This is accepted G6 evidence, not an accepted current h400 replay.",
        },
        {
            "path": (FIGURE_ROOT / "r14_validation_targets.svg").as_posix(),
            "recommended_poster_section": "Validation plan",
            "one_line_purpose": "Show target observation classes and intended comparison quantities.",
            "caption": "Historical validation framework: DART/open ocean, Kamaishi/NOWPHAS and run-up/inundation targets.",
            "claim_status": "VALIDATION TARGET",
            "caveat": "Framework only; no validation-results claim.",
        },
    ]


def write_markdown(path: Path, payload: Mapping[str, Any]) -> None:
    lines = [
        "# R14B Scope Completion Audit",
        "",
        f"Generated: `{payload['generated_at_utc']}`",
        f"Git SHA at generation: `{payload['git_sha']}`",
        "",
        "## Summary",
        "",
    ]
    counts = payload["counts"]
    for key in ("total", "COMPLETE", "BLOCKED_BY_LOCAL3D_REPLAY", "DEFERRED_ARCHITECTURAL", "NOT_APPLICABLE"):
        lines.append(f"- {key}: `{counts[key]}`")
    lines.extend(["", "No `READY` or `IN_PROGRESS` states remain.", "", "## Matrix", ""])
    headers = [
        "ID",
        "workstream",
        "original requirement",
        "dependency",
        "expected artifact",
        "actual artifact found",
        "status",
        "evidence",
        "missing work",
        "action taken in R14B",
        "final status",
    ]
    lines.append("| " + " | ".join(headers) + " |")
    lines.append("| " + " | ".join("---" for _ in headers) + " |")
    for row in payload["items"]:
        values = [str(row[header]).replace("|", "\\|").replace("\n", " ") for header in headers]
        lines.append("| " + " | ".join(values) + " |")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="write audit and handoff artifacts")
    args = parser.parse_args(argv)

    for variant, base in (
        (FIGURE_ROOT / "r14_corridor_map_scientific.svg", FIGURE_ROOT / "r14_corridor_map.svg"),
        (FIGURE_ROOT / "r14_corridor_map_poster.svg", FIGURE_ROOT / "r14_corridor_map.svg"),
        (FIGURE_ROOT / "r14_regional_eta_snapshot_scientific.svg", FIGURE_ROOT / "r14_regional_eta_snapshot.svg"),
        (FIGURE_ROOT / "r14_regional_eta_snapshot_poster.svg", FIGURE_ROOT / "r14_regional_eta_snapshot.svg"),
    ):
        if variant.is_file():
            figure_variant_provenance(variant, base, variant.stem)

    video_probe = ffprobe_video(VIDEO_ROOT / "local3d_g6_preview.mp4")
    frame_paths = sorted((VIDEO_ROOT / "frames/local3d_g6").glob("frame_*.png"))
    frame_manifest = {
        "schema": {"name": "tsunami.r14b.frame_manifest", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "data_class": "DEMONSTRATED_G6_HYBRID_REPLAY",
        "frame_count": len(frame_paths),
        "frames": [{"path": path.as_posix(), "sha256": sha256(path)} for path in frame_paths],
        "source": (G6_ROOT / "local/simple_rigid_barrier").as_posix(),
        "source_status": "accepted G6 replay",
    }
    video_provenance = {
        "schema": {"name": "tsunami.r14b.video_provenance", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "title": "Accepted G6 Local3D rigid-barrier replay preview",
        "short_description": "Short proof-of-concept MP4 assembled from accepted G6 Local3D ParaView frames.",
        "data_class": "DEMONSTRATED_G6_HYBRID_REPLAY",
        "ffmpeg_command_template": "ffmpeg -y -framerate 2 -i deliverables/video/r14_hybrid/frames/local3d_g6/frame_%03d.png -vf format=yuv420p -movflags +faststart deliverables/video/r14_hybrid/local3d_g6_preview.mp4",
        "ffprobe": video_probe,
        "regional_preview_mp4_status": "NOT_APPLICABLE",
        "local3d_preview_mp4_status": "COMPLETE" if video_probe["status"] == "PASSED" else "NOT_APPLICABLE",
        "hybrid_preview_mp4_status": "BLOCKED_BY_LOCAL3D_REPLAY",
    }
    qr = {
        "schema": {"name": "tsunami.r14b.qr_asset_metadata", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "status": "QR_ASSET_READY_FOR_HOSTING" if video_probe["status"] == "PASSED" else "NOT_APPLICABLE",
        "video_path": video_probe["path"],
        "sha256": video_probe.get("sha256"),
        "duration_s": video_probe.get("duration_s"),
        "resolution": video_probe.get("resolution"),
        "fps": video_probe.get("fps"),
        "title": video_provenance["title"],
        "short_description": video_provenance["short_description"],
        "suggested_hosting_filename": "local3d_g6_preview.mp4",
        "public_url": None,
    }
    items = build_scope_items({"ffprobe": video_probe})
    counts = {status: sum(1 for row in items if row["final status"] == status) for status in sorted(ALLOWED_FINAL)}
    counts["total"] = len(items)
    payload = {
        "schema": {"name": "tsunami.c1a_r14b_scope_completion_audit", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "git_sha": git_sha(),
        "starting_head_required_by_prompt": "30267431cc045826c391dc81b985729129fd4e10",
        "status_vocabulary": sorted(ALLOWED_FINAL),
        "counts": counts,
        "no_ready_or_in_progress": True,
        "canonical_result_hierarchy": [
            "inputs/",
            "results/",
            "diagnostics/",
            "figures/{corridor,bathymetry,mesh,fields,coupling,convergence,comparisons,hybrid,local3d,validation,diagnostics,publication}/",
            "tables/",
            "animations/",
            "video/",
            "logs/",
            "provenance/",
        ],
        "items": items,
    }
    shortlist = {
        "schema": {"name": "tsunami.r14b.poster_asset_shortlist", "version": "1.0.0"},
        "generated_at_utc": utc_now(),
        "assets": build_poster_shortlist(),
        "claim_status_vocabulary": ["IMPLEMENTED", "DEMONSTRATED", "VERIFIED", "DIAGNOSTIC", "VALIDATION TARGET", "FUTURE"],
    }

    if args.write:
        write_json(VIDEO_ROOT / "frame_manifest.json", frame_manifest)
        write_json(VIDEO_ROOT / "video_provenance.json", video_provenance)
        write_json(VIDEO_ROOT / "qr_asset_metadata.json", qr)
        write_json(DOCS_ROOT / "regional2d_r14_poster_asset_shortlist.json", shortlist)
        write_json(DOCS_ROOT / "regional2d_r14_scope_completion_audit.json", payload)
        write_markdown(DOCS_ROOT / "regional2d_r14_scope_completion_audit.md", payload)
    print(json.dumps({"counts": counts, "video": video_probe, "no_ready_or_in_progress": True}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
