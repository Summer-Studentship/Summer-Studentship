import json
import shutil
import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/openfoam"))

import boundary_reflection_benchmark as benchmark


FIXTURE = ROOT / "tests/fixtures/openfoam/boundary_reflection/benchmark_config.json"


class BoundaryReflectionBenchmarkTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tsunami-boundary-reflection-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_incident_and_reflected_windows_are_analytical(self):
        config = benchmark.load_config(FIXTURE)
        case = config["cases"][1]
        windows = benchmark.derive_windows(case, config["common"])
        c0 = benchmark.wave_speed(config["common"]["depth_m"])
        self.assertAlmostEqual(windows["incident_centre_s"], case["gauge_position_m"] / c0)
        self.assertAlmostEqual(windows["reflected_centre_s"], (2.0 * case["target_boundary_position_m"] - case["gauge_position_m"]) / c0)
        self.assertLess(windows["incident"][1], windows["reflected"][0])

    def test_reflective_control_and_production_thresholds_pass(self):
        summary = benchmark.run_benchmark(FIXTURE, self.tmp, overwrite=True)
        control = summary["cases"]["reflective_control"]["metrics"]
        outlet = summary["cases"]["production_outlet"]["metrics"]
        lateral = summary["cases"]["production_lateral"]["metrics"]
        self.assertGreaterEqual(control["Kr"], 0.70)
        self.assertLessEqual(outlet["Kr"], 0.15)
        self.assertLessEqual(outlet["RE"], 0.05)
        self.assertLessEqual(lateral["Kr"], 0.15)
        self.assertLessEqual(lateral["RE"], 0.05)
        self.assertTrue((self.tmp / "outlet_boundary_reflection.png").is_file())
        self.assertTrue((self.tmp / "lateral_boundary_reflection.png").is_file())

    def test_threshold_failure_is_not_suppressed(self):
        config = json.loads(FIXTURE.read_text(encoding="utf-8"))
        for case in config["cases"]:
            if case["case_id"] == "production_outlet":
                case["imposed_reflection_coefficient"] = 0.3
        path = self.tmp / "bad.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        with self.assertRaisesRegex(benchmark.BenchmarkError, "threshold failed"):
            benchmark.run_benchmark(path, self.tmp / "bad-output")


if __name__ == "__main__":
    unittest.main()
