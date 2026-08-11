from __future__ import annotations

import tempfile
import unittest
import math
from datetime import UTC, datetime
from pathlib import Path

from tools.validation import r15_tohoku_validation as r15


class R15ValidationTests(unittest.TestCase):
    def test_utm54n_matches_project_epicentre_reference(self) -> None:
        point = r15.wgs84_to_utm54n(38.297, 142.373)
        self.assertAlmostEqual(point.x, 620060.6382673722, delta=0.5)
        self.assertAlmostEqual(point.y, 4239660.228986675, delta=0.5)

    def test_utm54n_round_trip(self) -> None:
        lat, lon = r15.utm54n_to_wgs84(579494.6902478148, 4340096.334024712)
        self.assertAlmostEqual(lat, 39.206500468041476, delta=1e-6)
        self.assertAlmostEqual(lon, 141.920714192236, delta=1e-6)

    def test_parse_dart_rows(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "dart.txt"
            path.write_text("     70.250000 2011  3 11  6  0  0 5660.0 5659.0    1.00000  0.000\n", encoding="utf-8")
            rows = r15.parse_dart(path)
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0].timestamp_utc, datetime(2011, 3, 11, 6, 0, 0, tzinfo=UTC))
        self.assertEqual(rows[0].residual_m, 1.0)

    def test_parse_dart_missing_sentinel_as_nan(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "dart.txt"
            path.write_text("     70.314583 2011  3 11  7 33  0 9999.0 5662.0 9999.0  0.000\n", encoding="utf-8")
            rows = r15.parse_dart(path)
        self.assertTrue(math.isnan(rows[0].raw_observation_m))
        self.assertTrue(math.isnan(rows[0].residual_m))

    def test_classify_dart_outside_corridor_as_target_only(self) -> None:
        corridor = r15.load_corridor(r15.CORRIDOR_PATH)
        classified = r15.classify_station(
            latitude=38.71,
            longitude=148.67,
            quantity="Deep ocean gauge",
            polygon=corridor["polygon_points"],
            model_outputs=["coupling_section_eta"],
        )
        self.assertFalse(classified["inside_h400_corridor"])
        self.assertEqual(classified["eligibility"], "TARGET_ONLY")
        self.assertGreater(classified["distance_to_corridor_m"], 100000)

    def test_classify_nowphas_802g_outside_corridor_as_target_only(self) -> None:
        corridor = r15.load_corridor(r15.CORRIDOR_PATH)
        classified = r15.classify_station(
            latitude=39 + 15 / 60 + 31 / 3600,
            longitude=142 + 5 / 60 + 49 / 3600,
            quantity="GPS-buoy offshore tsunami waveform",
            polygon=corridor["polygon_points"],
            model_outputs=["coupling_section_eta"],
        )
        self.assertFalse(classified["inside_h400_corridor"])
        self.assertEqual(classified["eligibility"], "TARGET_ONLY")
        self.assertAlmostEqual(classified["distance_to_corridor_m"], 12273.1, delta=1.0)


if __name__ == "__main__":
    unittest.main()
