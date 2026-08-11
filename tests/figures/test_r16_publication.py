from __future__ import annotations

import math
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/figures"))

import r16_publication as r16


class R16PublicationTests(unittest.TestCase):
    def test_utm54n_round_trip_matches_r15_reference(self) -> None:
        point = r16.wgs84_to_utm54n(38.297, 142.373)
        self.assertAlmostEqual(point.x, 620060.6382673722, delta=0.5)
        self.assertAlmostEqual(point.y, 4239660.228986675, delta=0.5)
        lat, lon = r16.utm54n_to_wgs84(point.x, point.y)
        self.assertAlmostEqual(lat, 38.297, delta=1e-6)
        self.assertAlmostEqual(lon, 142.373, delta=1e-6)

    def test_local3d_footprint_dimensions_follow_g6_summary(self) -> None:
        corridor = r16.load_corridor()
        footprint = r16.local3d_footprint(corridor)
        self.assertEqual(footprint[0], footprint[-1])
        span = math.hypot(footprint[1].x - footprint[0].x, footprint[1].y - footprint[0].y)
        length = math.hypot(footprint[2].x - footprint[1].x, footprint[2].y - footprint[1].y)
        self.assertAlmostEqual(span, 6996.499999999534, delta=1.0e-6)
        self.assertAlmostEqual(length, 772.6171671085285, delta=1.0e-6)

    def test_centreline_sampling_uses_saved_time_cadence(self) -> None:
        section = r16.sample_eta_along_centreline(spacing_m=5000.0)
        self.assertEqual(section.eta_m.shape[0], 121)
        self.assertEqual(section.eta_m.shape[1], len(section.distance_to_shore_km))
        self.assertAlmostEqual(float(section.time_s[1] - section.time_s[0]), 5.0)
        self.assertGreater(len(section.distance_to_shore_km), 10)
        self.assertTrue((section.nearest_distance_m >= 0.0).all())

    def test_principal_times_are_saved_times(self) -> None:
        section = r16.sample_eta_along_centreline(spacing_m=8000.0)
        selected = r16.principal_times(section)
        saved = {float(value) for value in section.time_s}
        self.assertGreaterEqual(len(selected), 4)
        self.assertTrue(set(selected).issubset(saved))

    def test_qgis_status_is_terminal(self) -> None:
        env = r16.qgis_environment()
        self.assertIn(env["status"], {"AVAILABLE", "QGIS_RUNTIME_BLOCKED"})


if __name__ == "__main__":
    unittest.main()
