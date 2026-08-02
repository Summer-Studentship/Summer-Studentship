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
        shutil.copytree(FIXTURE / "coupling/boundary.offshore", coupling)
        for name in ("metadata.json", "samples.csv", "history.csv"):
            path = coupling / name
            path.write_text(path.read_text().replace("boundary.offshore", kamaishi.SECTION_ID), encoding="utf-8")
        metadata = json.loads((coupling / "metadata.json").read_text())
        metadata["section_id"] = kamaishi.SECTION_ID
        metadata["boundary_patch_name"] = "boundary.inland"
        (coupling / "metadata.json").write_text(json.dumps(metadata), encoding="utf-8")
        return coupling

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
        self.assertEqual(evidence["selected_time_count"], 4)
        with (selected / "samples.csv").open() as handle:
            samples = list(csv.DictReader(handle))
        self.assertEqual(float(samples[0]["time"]), 0.0)
        self.assertTrue((selected / "window_selection.json").is_file())
        config = json.loads((FIXTURE / "replay_config.json").read_text())
        config["section_id"] = kamaishi.SECTION_ID
        coupling = replay.load_coupling_export(selected, config)
        self.assertEqual(coupling.times[0], 0.0)

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
        replay.load_replay_config(config_path)

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
