import json
import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/earthquake"))

import tohoku_usgs_finite_fault as producer


class TohokuProducerCrsTests(unittest.TestCase):
    def test_real_header_param_rows_use_segment_dimensions_and_metres(self):
        subfaults = producer.parse_usgs_basic_inversion_param(
            ROOT / "tests/fixtures/earthquake/usgs_basic_inversion_real_header.param"
        )
        self.assertEqual(len(subfaults), 2)
        self.assertAlmostEqual(subfaults[0].longitude, 143.8089)
        self.assertAlmostEqual(subfaults[0].latitude, 40.6605)
        self.assertAlmostEqual(subfaults[0].slip_m, 0.5735382)
        self.assertAlmostEqual(subfaults[0].length_km, 25.0)
        self.assertAlmostEqual(subfaults[0].width_km, 16.6)
        self.assertAlmostEqual(subfaults[1].slip_m, 6.622207)

    def test_projected_target_grid_centres_are_transformed_to_wgs84(self):
        with tempfile.TemporaryDirectory(prefix="tsunami-producer-crs-") as tmp:
            record = Path(tmp) / "terrain.json"
            record.write_text(json.dumps({
                "target_reference": {
                    "horizontal": {
                        "authority_name": "EPSG",
                        "authority_code": "32654",
                        "name": "WGS 84 / UTM zone 54N",
                    }
                },
                "grid": {
                    "width": 3,
                    "height": 2,
                    "affine": {
                        "origin_x": 400000.0,
                        "pixel_width": 500.0,
                        "row_rotation": 0.0,
                        "origin_y": 4300000.0,
                        "column_rotation": 0.0,
                        "pixel_height": -500.0,
                    },
                },
            }), encoding="utf-8")
            grid = producer._terrain_grid(record, "EPSG:32654")
            lon_axis, lat_axis, _, _, metadata = producer._working_geographic_axes(grid)
            self.assertTrue(metadata["projected_centres_transformed_to_wgs84"])
            self.assertTrue(all(135.0 < lon < 145.0 for lon in lon_axis))
            self.assertTrue(all(35.0 < lat < 45.0 for lat in lat_axis))
            self.assertLess(abs(lon_axis[0]), 180.0)
            self.assertLess(abs(lat_axis[0]), 90.0)

    def test_geographic_target_grid_remains_supported(self):
        grid = {
            "width": 2,
            "height": 2,
            "target_crs": "EPSG:4326",
            "transform": (140.0, 0.05, 0.0, 39.0, 0.0, -0.05),
        }
        lon_axis, lat_axis, _, _, metadata = producer._working_geographic_axes(grid)
        self.assertFalse(metadata["projected_centres_transformed_to_wgs84"])
        self.assertAlmostEqual(float(lon_axis[0]), 140.025)
        self.assertAlmostEqual(float(lat_axis[0]), 38.975)


if __name__ == "__main__":
    unittest.main()
