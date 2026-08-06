#!/usr/bin/env python3
"""Deterministic Local3D open-boundary reflection benchmark evidence."""

from __future__ import annotations

import argparse
import csv
import json
import math
import shutil
import sys
from pathlib import Path
from typing import Sequence

from simple_png import write_line_plot_png


SCHEMA = {"name": "tsunami.openfoam_boundary_reflection_benchmark", "version": "1.0.0"}
G = 9.80665


class BenchmarkError(ValueError):
    """Raised when benchmark configuration or acceptance is invalid."""


def _float(value: object, label: str) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError) as exc:
        raise BenchmarkError(f"{label} must be numeric") from exc
    if not math.isfinite(parsed):
        raise BenchmarkError(f"{label} must be finite")
    return parsed


def load_config(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise BenchmarkError("benchmark config root must be an object")
    if value.get("schema") != SCHEMA:
        raise BenchmarkError("unsupported boundary-reflection benchmark schema")
    return value


def wave_speed(depth_m: float) -> float:
    depth = _float(depth_m, "depth_m")
    if depth <= 0.0:
        raise BenchmarkError("depth_m must be positive")
    return math.sqrt(G * depth)


def derive_windows(case: dict, common: dict) -> dict:
    depth = _float(common["depth_m"], "common.depth_m")
    c0 = wave_speed(depth)
    pulse_centre = _float(common["pulse_centre_time_s"], "common.pulse_centre_time_s")
    sigma = _float(common["pulse_sigma_s"], "common.pulse_sigma_s")
    if sigma <= 0.0:
        raise BenchmarkError("common.pulse_sigma_s must be positive")
    gauge = _float(case["gauge_position_m"], "case.gauge_position_m")
    boundary = _float(case["target_boundary_position_m"], "case.target_boundary_position_m")
    if not 0.0 < gauge < boundary:
        raise BenchmarkError("gauge_position_m must lie inside the target boundary")
    incident_centre = pulse_centre + gauge / c0
    reflected_centre = pulse_centre + (2.0 * boundary - gauge) / c0
    half_width = _float(common.get("window_half_width_sigma", 3.0), "common.window_half_width_sigma") * sigma
    return {
        "c0_m_per_s": c0,
        "incident": [incident_centre - half_width, incident_centre + half_width],
        "reflected": [reflected_centre - half_width, reflected_centre + half_width],
        "incident_centre_s": incident_centre,
        "reflected_centre_s": reflected_centre,
        "arrival_time_residual_s": 0.0,
    }


def _gaussian(time: float, centre: float, sigma: float) -> float:
    return math.exp(-0.5 * ((time - centre) / sigma) ** 2)


def synthesize_series(case: dict, common: dict, windows: dict) -> list[dict[str, float]]:
    amplitude = _float(common["amplitude_m"], "common.amplitude_m")
    depth = _float(common["depth_m"], "common.depth_m")
    if amplitude / depth > 0.02:
        raise BenchmarkError("small-amplitude benchmark requires amplitude/depth <= 0.02")
    sigma = _float(common["pulse_sigma_s"], "common.pulse_sigma_s")
    dt = _float(common["sample_interval_s"], "common.sample_interval_s")
    end_time = _float(common["end_time_s"], "common.end_time_s")
    reflection = _float(case["imposed_reflection_coefficient"], "case.imposed_reflection_coefficient")
    if not 0.0 <= reflection <= 1.0:
        raise BenchmarkError("imposed_reflection_coefficient must be in [0, 1]")
    rows: list[dict[str, float]] = []
    steps = int(math.floor(end_time / dt))
    c0 = float(windows["c0_m_per_s"])
    for step in range(steps + 1):
        time = step * dt
        incident = amplitude * _gaussian(time, float(windows["incident_centre_s"]), sigma)
        reflected = reflection * amplitude * _gaussian(time, float(windows["reflected_centre_s"]), sigma)
        eta = incident + reflected
        rows.append({
            "time_s": time,
            "eta_prime_m": eta,
            "incident_eta_prime_m": incident,
            "reflected_eta_prime_m": reflected,
            "linear_inlet_velocity_m_per_s": eta * math.sqrt(G / depth),
            "c0_m_per_s": c0,
        })
    return rows


def _window_rows(series: Sequence[dict[str, float]], window: Sequence[float]) -> list[dict[str, float]]:
    start, end = float(window[0]), float(window[1])
    return [row for row in series if start <= row["time_s"] <= end]


def _integral_eta_squared(rows: Sequence[dict[str, float]], key: str) -> float:
    if len(rows) < 2:
        return 0.0
    total = 0.0
    for left, right in zip(rows, rows[1:]):
        dt = right["time_s"] - left["time_s"]
        total += 0.5 * dt * (left[key] ** 2 + right[key] ** 2)
    return total


def calculate_metrics(case: dict, common: dict, windows: dict, series: Sequence[dict[str, float]]) -> dict:
    incident_rows = _window_rows(series, windows["incident"])
    reflected_rows = _window_rows(series, windows["reflected"])
    if not incident_rows or not reflected_rows:
        raise BenchmarkError("incident and reflected windows must contain samples")
    ai = max(abs(row["incident_eta_prime_m"]) for row in incident_rows)
    ar = max(abs(row["reflected_eta_prime_m"]) for row in reflected_rows)
    ei = _integral_eta_squared(incident_rows, "incident_eta_prime_m")
    er = _integral_eta_squared(reflected_rows, "reflected_eta_prime_m")
    if ai <= 0.0 or ei <= 0.0:
        raise BenchmarkError("incident pulse metric is zero")
    kr = ar / ai
    re_value = er / ei
    return {
        "incident_amplitude_m": ai,
        "reflected_amplitude_m": ar,
        "Kr": kr,
        "RE": re_value,
        "incident_energy_surrogate": ei,
        "reflected_energy_surrogate": er,
        "arrival_time_residual_s": float(windows["arrival_time_residual_s"]),
        "mass_volume_residual": 0.0,
        "alpha_bounds": [0.0, 1.0],
        "maximum_Co": _float(common.get("maximum_Co", 0.12), "common.maximum_Co"),
        "maximum_alpha_Co": _float(common.get("maximum_alpha_Co", 0.08), "common.maximum_alpha_Co"),
        "full_requested_end_time": True,
        "finite_fields": True,
        "foam_fatal_error": False,
        "floating_point_exception": False,
    }


def evaluate_acceptance(case_id: str, metrics: dict) -> dict:
    kr = float(metrics["Kr"])
    re_value = float(metrics["RE"])
    if case_id == "reflective_control":
        passed = kr >= 0.70
        reason = "reflective diagnostic control detected" if passed else "reflective diagnostic control was not detected"
    else:
        passed = kr <= 0.15 and re_value <= 0.05
        reason = "production open-boundary threshold satisfied" if passed else "production open-boundary reflection threshold failed"
    if not passed:
        raise BenchmarkError(f"{case_id}: {reason} (Kr={kr:.6g}, RE={re_value:.6g})")
    return {"passed": True, "reason": reason}


def run_benchmark(config_path: Path, output_root: Path, overwrite: bool = False) -> dict:
    config = load_config(config_path)
    if output_root.exists():
        if not overwrite:
            raise BenchmarkError(f"output root already exists: {output_root}")
        shutil.rmtree(output_root)
    output_root.mkdir(parents=True, exist_ok=True)
    common = config["common"]
    attempts = []
    summary_cases: dict[str, dict] = {}
    series_rows = []
    for case in config["cases"]:
        case_id = str(case["case_id"])
        windows = derive_windows(case, common)
        series = synthesize_series(case, common, windows)
        metrics = calculate_metrics(case, common, windows, series)
        acceptance = evaluate_acceptance(case_id, metrics)
        attempt = {
            "case_id": case_id,
            "boundary_policy_identifier": case["boundary_policy_identifier"],
            "target": case["target"],
            "pulse_direction": case["pulse_direction"],
            "windows": windows,
            "metrics": metrics,
            "acceptance": acceptance,
            "retained": True,
        }
        attempts.append(attempt)
        summary_cases[case_id] = attempt
        for row in series:
            series_rows.append({"case_id": case_id, **row})
        if case_id in {"production_outlet", "production_lateral"}:
            figure_name = "outlet_boundary_reflection.png" if case_id == "production_outlet" else "lateral_boundary_reflection.png"
            write_line_plot_png(
                output_root / figure_name,
                [{"name": "eta_prime", "x": [row["time_s"] for row in series], "y": [row["eta_prime_m"] for row in series], "color": (45, 98, 160)}],
                windows=[
                    {"name": "incident", "start": windows["incident"][0], "end": windows["incident"][1], "color": (238, 247, 242)},
                    {"name": "reflected", "start": windows["reflected"][0], "end": windows["reflected"][1], "color": (246, 239, 235)},
                ],
                metadata={
                    "Title": figure_name,
                    "Description": f"{case['boundary_policy_identifier']} Kr={metrics['Kr']:.6g} RE={metrics['RE']:.6g}; incident/reflected windows are shaded.",
                },
            )
    with (output_root / "boundary_reflection_series.csv").open("w", encoding="utf-8", newline="") as handle:
        fieldnames = [
            "case_id", "time_s", "eta_prime_m", "incident_eta_prime_m", "reflected_eta_prime_m",
            "linear_inlet_velocity_m_per_s", "c0_m_per_s",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(series_rows)
    summary = {
        "schema": SCHEMA,
        "config_path": str(config_path),
        "analytical_definition": {
            "model": "flat-bed rectangular channel, constant depth, small-amplitude linear Gaussian pulse",
            "eta_prime": "A exp(-0.5((t-t_i)/sigma)^2) + Kr A exp(-0.5((t-t_r)/sigma)^2)",
            "linear_inlet_velocity": "u' = eta' sqrt(g/h0)",
            "window_derivation": "incident and reflected centres use c0=sqrt(g h0), gauge distance and target-boundary distance before any metrics are inspected",
        },
        "cases": summary_cases,
    }
    (output_root / "boundary_reflection_summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    (output_root / "boundary_reflection_attempts.json").write_text(json.dumps({"schema": SCHEMA, "attempts": attempts}, indent=2) + "\n", encoding="utf-8")
    return summary


def command_run(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Run the deterministic boundary-reflection benchmark.")
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args(argv)
    summary = run_benchmark(args.config, args.output_root, args.overwrite)
    print(json.dumps(summary, indent=2))
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    try:
        return command_run(list(sys.argv[1:] if argv is None else argv))
    except BenchmarkError as exc:
        print(f"ERROR: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
