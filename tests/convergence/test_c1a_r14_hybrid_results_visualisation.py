import shutil
import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/verification/convergence"))

import c1a_r14_hybrid_results_visualisation as r14


class R14HybridResultsVisualisationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tsunami-r14-unit-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_parse_scalar_boundary_list(self):
        path = self.tmp / "alpha.water"
        path.write_text("// Data on points\n3\n(\n0\n0.5\n1\n)\n", encoding="utf-8")
        self.assertEqual(r14.parse_scalar_list(path), [0.0, 0.5, 1.0])

    def test_scan_boundary_alpha_reports_global_bounds(self):
        root = self.tmp / "case"
        for time, values in {"0": [0.0, 1.0], "1": [0.25, 0.75]}.items():
            folder = root / f"constant/boundaryData/inlet/{time}"
            folder.mkdir(parents=True)
            (folder / "alpha.water").write_text("// Data on points\n2\n(\n" + "\n".join(str(v) for v in values) + "\n)\n", encoding="utf-8")
        scan = r14.scan_boundary_alpha(root)
        self.assertEqual(scan["status"], "BOUNDED")
        self.assertEqual(scan["time_count"], 2)
        self.assertEqual(scan["minimum"]["min"], 0.0)
        self.assertEqual(scan["maximum"]["max"], 1.0)

    def test_parse_alpha_log_series_and_tolerance_summary(self):
        log = self.tmp / "log.foamRun"
        log.write_text(
            "\n".join(
                [
                    "Courant Number mean: 0.0 max: 0.004",
                    "Interface Courant Number mean: 0.0 max: 0.002",
                    "Time = 1s",
                    "Phase-1 volume fraction = 0.1  Min(alpha.water) = -8.7e-05  Max(alpha.water) = 1.000087",
                    "Time = 2s",
                    "Phase-1 volume fraction = 0.1  Min(alpha.water) = -0.0002  Max(alpha.water) = 1.0003",
                ]
            ),
            encoding="utf-8",
        )
        rows = r14.parse_alpha_log_series(log)
        self.assertEqual(len(rows), 2)
        summary = r14.series_summary(rows, horizon_s=1.0)
        self.assertAlmostEqual(summary["minimum_alpha"], -8.7e-05)
        self.assertIsNotNone(summary["first_below_tolerance"])
        self.assertIsNotNone(summary["first_above_tolerance"])

    def test_temporal_alpha_interpolation_is_convex_when_endpoints_bounded(self):
        scan = {"minimum": {"min": 0.0}, "maximum": {"max": 1.0}}
        result = r14.interpolate_alpha_bound(scan)
        self.assertEqual(result["status"], "BOUNDED_BY_CONVEX_LINEAR_INTERPOLATION")


if __name__ == "__main__":
    unittest.main()
