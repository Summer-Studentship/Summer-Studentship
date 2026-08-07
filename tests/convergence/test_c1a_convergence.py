import json
import shutil
import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/verification/convergence"))

import c1a_convergence as c1a


class C1AConvergenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tsunami-c1a-unit-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_default_study_uses_c1a_schema_and_frozen_baselines(self):
        study = c1a.default_study(self.tmp)
        self.assertEqual(study["schema"], {"name": "tsunami.convergence_study", "version": "1.0.0"})
        self.assertEqual(study["repository_baseline"], c1a.REPOSITORY_BASELINE)
        self.assertEqual(study["g6_physical_model_baseline"], c1a.G6_PHYSICAL_BASELINE)
        self.assertEqual(set(study["stages"]), set(c1a.STAGES))
        self.assertFalse(study["artifact_policy"]["commit_raw_fields"])

    def test_physical_invariance_allows_discretisation_changes(self):
        case_spec = json.loads((ROOT / "cases/kamaishi_delivery/case_spec.json").read_text(encoding="utf-8"))
        replay = json.loads((ROOT / "tests/fixtures/openfoam/synthetic_replay/replay_config_production.json").read_text(encoding="utf-8"))
        reference = {"physical_payload": c1a.physical_payload(case_spec, replay)}
        changed = json.loads(json.dumps(replay))
        changed["local_case"]["streamwise_cells"] = int(changed["local_case"]["streamwise_cells"]) * 2
        changed["local_case"]["maximum_timestep_s"] = float(changed["local_case"]["maximum_timestep_s"]) * 0.5
        candidate = {"physical_payload": c1a.physical_payload(case_spec, changed)}
        c1a.assert_physical_invariance(reference, candidate)

    def test_physical_invariance_rejects_manning_and_barrier_changes(self):
        case_spec = json.loads((ROOT / "cases/kamaishi_delivery/case_spec.json").read_text(encoding="utf-8"))
        replay = json.loads((ROOT / "tests/fixtures/openfoam/synthetic_replay/replay_config_production.json").read_text(encoding="utf-8"))
        reference = {"physical_payload": c1a.physical_payload(case_spec, replay)}
        changed_case = json.loads(json.dumps(case_spec))
        changed_case["regional_2d"]["manning_uniform_s_per_m_one_third"] = 0.03
        with self.assertRaisesRegex(c1a.ConvergenceError, "manning"):
            c1a.assert_physical_invariance(reference, {"physical_payload": c1a.physical_payload(changed_case, replay)})
        changed_replay = json.loads(json.dumps(replay))
        changed_replay["barrier"]["height_m"] += 1.0
        with self.assertRaisesRegex(c1a.ConvergenceError, "barrier"):
            c1a.assert_physical_invariance(reference, {"physical_payload": c1a.physical_payload(case_spec, changed_replay)})

    def test_nrmse_relative_change_and_local_h(self):
        self.assertAlmostEqual(c1a.nrmse([1.0, 2.0, 3.0], [1.0, 2.5, 3.0]), 0.14433756729740643)
        self.assertAlmostEqual(c1a.relative_change(10.0, 8.0), 0.2)
        self.assertAlmostEqual(c1a.local_characteristic_h(8.0, 64), 0.5)

    def test_richardson_gci_reports_monotone_three_level_result(self):
        result = c1a.richardson_gci([1.0, 1.25, 2.0], [0.5, 1.0, 2.0])
        self.assertEqual(result["status"], "computed")
        self.assertGreater(result["observed_order"], 0.0)
        self.assertGreater(result["gci21_percent"], 0.0)


if __name__ == "__main__":
    unittest.main()
