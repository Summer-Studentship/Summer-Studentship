import csv
import json
import shutil
import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/cases"))
sys.path.insert(0, str(ROOT / "tools/openfoam"))

import kamaishi_delivery as kamaishi
import openfoam_replay as replay


FIXTURE = ROOT / "tests/fixtures/openfoam/synthetic_replay"


class KamaishiDeliveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tsunami-kamaishi-unit-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _kamaishi_coupling_fixture(self) -> Path:
        coupling = self.tmp / "coupling"
        self._write_coupling(coupling, [float(value) for value in range(0, 1801, 5)])
        return coupling

    def _write_coupling(
        self,
        coupling: Path,
        times: list[float],
        *,
        arrival_time: float = 300.0,
        peak_time: float = 600.0,
        persistent_after: float = 0.0,
    ) -> None:
        coupling.mkdir(parents=True, exist_ok=True)
        samples = [
            {"local_index": index, "cell": 10 + index, "face": 20 + index, "x_m": 1000.0, "y_m": 500.0 + index * 250.0}
            for index in range(4)
        ]
        metadata = {
            "contract_version": 1,
            "section_id": kamaishi.SECTION_ID,
            "boundary_patch_name": "boundary.inland",
            "mesh_id": "unit-mesh",
            "sample_count": len(samples),
            "samples": samples,
        }
        (coupling / "metadata.json").write_text(json.dumps(metadata), encoding="utf-8")
        with (coupling / "samples.csv").open("w", encoding="utf-8", newline="") as handle:
            fieldnames = [
                "step", "time", "section_id", "local_index", "cell", "face", "x_m", "y_m",
                "depth", "momentum_x", "momentum_y", "bed_elevation", "free_surface_elevation",
            ]
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            for step, time in enumerate(times):
                amplitude = persistent_after
                if arrival_time <= time <= peak_time:
                    amplitude = max(amplitude, (time - arrival_time) / max(peak_time - arrival_time, 1.0))
                elif peak_time < time <= peak_time + 300.0:
                    amplitude = max(amplitude, max(0.0, 1.0 - (time - peak_time) / 300.0))
                for sample in samples:
                    depth = 20.0 + sample["local_index"]
                    bed = -depth
                    eta = amplitude
                    qx = 0.2 * amplitude * depth
                    writer.writerow({
                        "step": step,
                        "time": f"{time:.17g}",
                        "section_id": kamaishi.SECTION_ID,
                        "local_index": sample["local_index"],
                        "cell": sample["cell"],
                        "face": sample["face"],
                        "x_m": sample["x_m"],
                        "y_m": sample["y_m"],
                        "depth": depth + eta,
                        "momentum_x": qx,
                        "momentum_y": 0.0,
                        "bed_elevation": bed,
                        "free_surface_elevation": eta,
                    })
        with (coupling / "history.csv").open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=["step", "time", "section_id", "sample_count", "maximum_depth", "maximum_speed"])
            writer.writeheader()
            for step, time in enumerate(times):
                writer.writerow({"step": step, "time": f"{time:.17g}", "section_id": kamaishi.SECTION_ID, "sample_count": len(samples), "maximum_depth": 24.0, "maximum_speed": 0.2})

    def test_case_spec_defaults_are_delivery_acceptance_values(self):
        spec = json.loads((ROOT / "cases/kamaishi_delivery/case_spec.json").read_text())
        self.assertGreaterEqual(float(spec["regional_2d"]["final_time_s"]), 1800.0)
        self.assertEqual(float(spec["corridor"]["inland_extent_m"]), 0.0)
        self.assertLessEqual(float(spec["regional_2d"]["maximum_timestep_s"]), 2.0)
        self.assertEqual(spec["nearshore_interface"]["fallback_depth_band_m"], [5.0, 50.0])

    def test_full_cross_section_wetness_is_required(self):
        preferred = (15.0, 30.0)
        fallback = (5.0, 50.0)
        dry = kamaishi.evaluate_cross_section_beds([-20.0, 1.0, -20.0], -20.0, preferred, fallback)
        self.assertTrue(dry.valid)
        self.assertFalse(dry.fully_wet)
        fallback_wet = kamaishi.evaluate_cross_section_beds([-6.0, -8.0, -7.0], -7.0, preferred, fallback)
        self.assertTrue(fallback_wet.fully_wet)
        self.assertTrue(fallback_wet.fallback_used)
        preferred_wet = kamaishi.evaluate_cross_section_beds([-20.0, -22.0, -21.0], -20.0, preferred, fallback)
        self.assertTrue(preferred_wet.fully_wet)
        self.assertFalse(preferred_wet.fallback_used)

    def test_boundary_alignment_residual_is_bounded(self):
        rows = [
            {"time": "0", "x_m": "1000", "y_m": "0", "depth": "10", "bed_elevation": "-10"},
            {"time": "0", "x_m": "1000", "y_m": "500", "depth": "12", "bed_elevation": "-12"},
        ]
        evidence = kamaishi.boundary_alignment_and_wetness(rows, kamaishi.Point(1000.0, 250.0), (1.0, 0.0), 500.0)
        self.assertLessEqual(evidence["mesh_spacing_normalised_alignment_residual"], 1.0)
        rows[1]["x_m"] = "1601"
        with self.assertRaises(kamaishi.DeliveryError):
            kamaishi.boundary_alignment_and_wetness(rows, kamaishi.Point(1000.0, 250.0), (1.0, 0.0), 500.0)

    def test_signal_metrics_use_perturbation_not_absolute_eta(self):
        rows = [
            {"time": "0", "local_index": "0", "free_surface_elevation": "115", "momentum_x": "0", "momentum_y": "0"},
            {"time": "5", "local_index": "0", "free_surface_elevation": "115", "momentum_x": "0", "momentum_y": "0"},
        ]
        with self.assertRaises(kamaishi.DeliveryError):
            kamaishi.coupling_signal_metrics(rows, (1.0, 0.0))
        rows[1]["free_surface_elevation"] = "115.1"
        eta = kamaishi.coupling_signal_metrics(rows, (1.0, 0.0))
        self.assertAlmostEqual(eta["maximum_absolute_free_surface_perturbation_m"], 0.1)
        rows[1]["free_surface_elevation"] = "115"
        rows[1]["momentum_x"] = "0.2"
        qn = kamaishi.coupling_signal_metrics(rows, (1.0, 0.0))
        self.assertAlmostEqual(qn["maximum_absolute_normal_momentum_change_m2_per_s"], 0.2)
        self.assertGreater(qn["peak_perturbation_time_s"], 0.0)

    def test_projected_kamaishi_trajectory_sanity(self):
        epicentre = kamaishi.transform_wgs84(142.373, 38.297)
        proxy = kamaishi.transform_wgs84(141.8858, 39.2757)
        unit, distance = kamaishi._unit_vector(epicentre, proxy)
        bearing = kamaishi._bearing(unit)
        self.assertGreater(distance, 110_000.0)
        self.assertLess(distance, 125_000.0)
        self.assertGreater(bearing, 330.0)
        self.assertLess(bearing, 345.0)

    def test_replay_window_preserves_contract_and_shifts_time(self):
        selected = self.tmp / "selected"
        evidence = kamaishi.select_replay_window(self._kamaishi_coupling_fixture(), selected, (1.0, 0.0))
        self.assertGreaterEqual(evidence["selected_time_count"], 4)
        self.assertGreaterEqual(evidence["shifted_duration_s"], 180.0)
        self.assertLessEqual(evidence["shifted_duration_s"], 300.0)
        self.assertLessEqual(evidence["selected_source_start_s"], evidence["peak_source_time_s"])
        self.assertGreaterEqual(evidence["selected_source_end_s"], evidence["peak_source_time_s"])
        self.assertAlmostEqual(evidence["threshold"], 0.02 * evidence["peak_metric"])
        self.assertGreaterEqual(evidence["window_anchor_source_time_s"], evidence["first_crossing_source_time_s"])
        with (selected / "samples.csv").open() as handle:
            samples = list(csv.DictReader(handle))
        self.assertEqual(float(samples[0]["time"]), 0.0)
        self.assertTrue((selected / "window_selection.json").is_file())
        config = json.loads((FIXTURE / "replay_config.json").read_text())
        config["section_id"] = kamaishi.SECTION_ID
        coupling = replay.load_coupling_export(selected, config)
        self.assertEqual(coupling.times[0], 0.0)

    def test_replay_window_rejects_short_and_insufficient_history(self):
        short = self.tmp / "short"
        self._write_coupling(short, [0.0, 5.0, 10.0, 15.0], persistent_after=1.0)
        with self.assertRaises(kamaishi.DeliveryError):
            kamaishi.select_replay_window(short, self.tmp / "short-selected", (1.0, 0.0))
        insufficient = self.tmp / "insufficient"
        self._write_coupling(insufficient, [float(value) for value in range(0, 1801, 5)], arrival_time=1700.0, peak_time=1800.0)
        with self.assertRaises(kamaishi.DeliveryError):
            kamaishi.select_replay_window(insufficient, self.tmp / "insufficient-selected", (1.0, 0.0))

    def test_replay_config_is_data_derived_and_valid(self):
        coupling = self._kamaishi_coupling_fixture()
        selected = self.tmp / "selected"
        kamaishi.select_replay_window(coupling, selected, (1.0, 0.0))
        trajectory = kamaishi.Trajectory(
            epicentre_wgs84=(0.0, 0.0),
            proxy_wgs84=(0.0, 0.0),
            epicentre=kamaishi.Point(0.0, 0.0),
            proxy=kamaishi.Point(0.0, 0.0),
            selected=kamaishi.Point(1.0, 0.0),
            selected_wgs84=(0.0, 0.0),
            unit=kamaishi.Point(1.0, 0.0),
            left=kamaishi.Point(0.0, 1.0),
            distance_m=1.0,
            proxy_distance_to_interface_m=0.0,
            bearing_degrees=90.0,
            selected_bed_elevation_m=-1.0,
            selected_depth_m=1.0,
            selection_fallback=False,
            selection_reason="unit fixture",
        )
        config_path = self.tmp / "replay_config.json"
        config = kamaishi.replay_config_from_window(selected, trajectory, config_path)
        self.assertEqual(config["section_id"], kamaishi.SECTION_ID)
        self.assertIn("local_case", config)
        self.assertIn("span_fraction", config["barrier"])
        self.assertGreater(config["local_case"]["maximum_timestep_s"], 0.005)
        self.assertIn("timestep_derivation", config["local_case"])
        replay.load_replay_config(config_path)

    def test_openfoam_timestep_derivation_responds_to_speed_and_mesh(self):
        rows = [{"depth": "10", "momentum_x": "10", "momentum_y": "0"}]
        slow = kamaishi.derive_openfoam_timestep(rows, (1.0, 0.0), (0.0, 1.0), streamwise_length_m=500.0, streamwise_cells=20, span_length_m=100.0, span_cells=10, vertical_height_m=20.0, vertical_cells=10)
        fast_rows = [{"depth": "10", "momentum_x": "200", "momentum_y": "0"}]
        fast = kamaishi.derive_openfoam_timestep(fast_rows, (1.0, 0.0), (0.0, 1.0), streamwise_length_m=500.0, streamwise_cells=20, span_length_m=100.0, span_cells=10, vertical_height_m=20.0, vertical_cells=10)
        coarse = kamaishi.derive_openfoam_timestep(rows, (1.0, 0.0), (0.0, 1.0), streamwise_length_m=500.0, streamwise_cells=10, span_length_m=100.0, span_cells=5, vertical_height_m=20.0, vertical_cells=5)
        self.assertGreater(slow["selected_maximum_timestep_s"], 0.0)
        self.assertLess(fast["calculated_timestep_limit_s"], slow["calculated_timestep_limit_s"])
        self.assertGreater(coarse["calculated_timestep_limit_s"], slow["calculated_timestep_limit_s"])
        self.assertLessEqual(slow["selected_maximum_timestep_s"], slow["calculated_timestep_limit_s"])

    def test_openfoam_generator_honours_full_scale_controls(self):
        replay_root = self.tmp / "replay"
        conversion = replay.convert_boundary_data(FIXTURE / "coupling/boundary.offshore", FIXTURE / "replay_config.json", replay_root)
        self.assertGreater(conversion["local_inlet_face_count"], 0)
        config = json.loads((FIXTURE / "replay_config.json").read_text())
        config["local_case"] = {
            "streamwise_length_m": 320.0,
            "streamwise_cells": 42,
            "span_cells": 8,
            "vertical_cells": 14,
            "end_time_s": 2.0,
            "maximum_timestep_s": 0.02,
            "write_interval_s": 0.5,
            "initial_water_level_m": 1.2,
        }
        config["barrier"] = {
            "streamwise_position_m": 192.0,
            "thickness_m": 15.0,
            "height_m": 0.6,
            "span_fraction": 1.0,
        }
        config_path = self.tmp / "full-scale-config.json"
        config_path.write_text(json.dumps(config), encoding="utf-8")
        case_dir = self.tmp / "barrier"
        summary = replay.generate_case(replay_root, config_path, case_dir, "simple_rigid_barrier")
        self.assertEqual(summary["dimensions_m"]["length"], 320.0)
        self.assertEqual(summary["cell_counts"]["streamwise"], 42)
        self.assertEqual(summary["barrier"]["streamwise_position_m"], 192.0)
        self.assertEqual(summary["barrier"]["span_fraction"], 1.0)
        self.assertIn("199.5", (case_dir / "system/controlDict").read_text())


if __name__ == "__main__":
    unittest.main()
