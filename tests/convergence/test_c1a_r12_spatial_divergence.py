import csv
import math
import shutil
import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/verification/convergence"))

import c1a_r12_spatial_divergence_diagnosis as r12


class R12SpatialDivergenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tsunami-r12-unit-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def write_station(self, level: str, rows: list[dict[str, object]]) -> None:
        path = self.tmp / "common-grid" / f"{level}_corridor_station_histories.csv"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(
                handle,
                fieldnames=[
                    "station_id",
                    "xi",
                    "distance_from_offshore_m",
                    "x_m",
                    "y_m",
                    "cell",
                    "distance_m",
                    "time_s",
                    "eta_m",
                    "qn_m2_per_s",
                    "bed_elevation_m",
                    "eta_raw_m",
                    "qn_raw_m2_per_s",
                ],
            )
            writer.writeheader()
            writer.writerows(rows)

    def test_metric_values_reports_reference_normalised_error(self):
        result = r12.metric_values([1.0, 2.0, 4.0], [1.0, 2.0, 3.0])
        self.assertAlmostEqual(result["rmse"], math.sqrt(1.0 / 3.0))
        self.assertGreater(result["nrmse"], 0.0)
        self.assertEqual(result["sample_count"], 3)

    def test_nearest_cells_preserves_station_metadata(self):
        points = [{"station_id": "xi_0.50", "xi": 0.5, "x_m": 0.2, "y_m": 0.1}]
        cells = [{"cell": 7, "centroid_x_m": 0.0, "centroid_y_m": 0.0}, {"cell": 8, "centroid_x_m": 10.0, "centroid_y_m": 0.0}]
        match = r12.nearest_cells(points, cells)[0]
        self.assertEqual(match["cell"], 7)
        self.assertEqual(match["station_id"], "xi_0.50")
        self.assertLess(match["distance_m"], 0.3)

    def test_divergence_onset_requires_three_consecutive_samples(self):
        base_rows = []
        fine_rows = []
        for time_s in range(0, 40, 5):
            base_rows.append(
                {
                    "station_id": "xi_1.00",
                    "xi": 1.0,
                    "distance_from_offshore_m": 1.0,
                    "x_m": 0.0,
                    "y_m": 0.0,
                    "cell": 1,
                    "distance_m": 0.0,
                    "time_s": float(time_s),
                    "eta_m": 1.0,
                    "qn_m2_per_s": 2.0,
                    "bed_elevation_m": -5.0,
                    "eta_raw_m": 1.0,
                    "qn_raw_m2_per_s": 2.0,
                }
            )
            fine = dict(base_rows[-1])
            if time_s >= 20:
                fine["eta_m"] = 1.2
                fine["qn_m2_per_s"] = 2.3
            fine_rows.append(fine)
        self.write_station("h500", base_rows)
        self.write_station("h400", fine_rows)
        self.write_station("h300", fine_rows)
        onset = r12.divergence_onset(self.tmp)
        self.assertEqual(onset["h400_vs_h500"]["eta"]["time_s"], 20.0)
        self.assertEqual(onset["h400_vs_h500"]["qn"]["time_s"], 20.0)


if __name__ == "__main__":
    unittest.main()
