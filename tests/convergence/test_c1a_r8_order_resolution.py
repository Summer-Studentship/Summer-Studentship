import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/verification/convergence"))

import c1a_r8_order_resolution as r8


class C1AR8OrderResolutionTests(unittest.TestCase):
    def test_geometry_identities_are_floating_point_clean(self):
        result = r8.geometry_identities([4])
        self.assertFalse(result["geometry_defect_exists"])
        row = result["levels"][0]
        self.assertLess(row["closure"]["linf"], 1.0e-14)
        self.assertLess(row["first_moment"]["linf"], 1.0e-14)
        self.assertLess(row["linear_x_divergence"]["linf"], 1.0e-14)

    def test_mms_source_matches_manufactured_balance_shape(self):
        value = r8.mms_source(0.25, 0.35, 0.01)
        self.assertEqual(len(value), 3)
        self.assertTrue(all(math.isfinite(component) for component in value))

    def test_global_classifier_accepts_first_order_fine_pair(self):
        rows = [
            {"actual_h": 0.25, "mass_l1": 0.2, "mass_l2": 0.3, "qx_l1": 0.4, "qx_l2": 0.5, "qy_l1": 0.6, "qy_l2": 0.7},
            {"actual_h": 0.125, "mass_l1": 0.1, "mass_l2": 0.15, "qx_l1": 0.2, "qx_l2": 0.25, "qy_l1": 0.3, "qy_l2": 0.35},
        ]
        for row in rows:
            row.update({"mass_linf": row["mass_l2"], "qx_linf": row["qx_l2"], "qy_linf": row["qy_l2"]})
        r8.add_orders(rows)
        temporal = {"temporal_contamination_negligible": True}
        self.assertEqual(r8.classify_global(rows, temporal), "GLOBAL_FIRST_ORDER_VERIFIED")


if __name__ == "__main__":
    unittest.main()
