import math
import tempfile
import unittest
from pathlib import Path
import shutil

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/verification/convergence"))

import c1a_r7_exact_spatial_benchmark as r7


class C1AR7ExactSpatialBenchmarkTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tsunami-r7-unit-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_manufactured_state_is_fully_wet(self):
        samples = [r7.state(i / 10.0, j / 10.0)[0] for i in range(11) for j in range(11)]
        self.assertGreater(min(samples), 1.8)

    def test_mesh_family_is_nested_and_deterministic(self):
        coarse = r7.make_mesh(4)
        fine = r7.make_mesh(8)
        self.assertEqual(len(coarse.triangles), 32)
        self.assertEqual(len(fine.triangles), 128)
        self.assertEqual(r7.mesh_hash(coarse), r7.mesh_hash(r7.make_mesh(4)))

    def test_reference_quadrature_is_below_discretisation_scale(self):
        rows = [r7.run_level(level, 10, 0.2) for level in (4, 8, 16)]
        r7.add_orders(rows)
        quadrature = r7.quadrature_verification(16, 10, 16, 0.2)
        gate = r7.classify_first_order(rows, quadrature)
        for component, ratio in gate["quadrature_error_to_finest_l2_error"].items():
            with self.subTest(component=component):
                self.assertTrue(math.isfinite(ratio))
                self.assertLess(ratio, 1.0e-3)

    def test_payload_records_closed_second_order_gate_when_baseline_fails(self):
        rows = [r7.run_level(level, 10, 0.2) for level in (4, 8, 16)]
        r7.add_orders(rows)
        quadrature = r7.quadrature_verification(16, 10, 16, 0.2)
        payload = r7.build_payload(rows, quadrature)
        self.assertFalse(payload["second_order_gate_opened"])
        self.assertEqual(payload["final_r7_classification"], "BASELINE_ORDER_UNRESOLVED")


if __name__ == "__main__":
    unittest.main()
