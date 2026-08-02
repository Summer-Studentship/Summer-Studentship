import csv
import json
import shutil
import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/openfoam"))

import openfoam_replay as replay


FIXTURE = ROOT / "tests/fixtures/openfoam/synthetic_replay"
COUPLING = FIXTURE / "coupling/boundary.offshore"
CONFIG = FIXTURE / "replay_config.json"


class ReplayToolTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tsunami-openfoam-unit-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_valid_coupling_parser(self):
        config = replay.load_replay_config(CONFIG)
        coupling = replay.load_coupling_export(COUPLING, config)
        self.assertEqual(len(coupling.ordered_samples), 3)
        self.assertEqual(coupling.times, [0.0, 0.04, 0.08, 0.12])

    def test_missing_columns_rejected(self):
        work = self.tmp / "bad"
        shutil.copytree(COUPLING, work)
        rows = (work / "samples.csv").read_text().splitlines()
        (work / "samples.csv").write_text(rows[0].replace(",momentum_y", "") + "\n" + "\n".join(line.replace(",0.001", "", 1) for line in rows[1:]) + "\n")
        with self.assertRaises(replay.ReplayError):
            replay.load_coupling_export(work, replay.load_replay_config(CONFIG))

    def test_duplicate_sample_pair_rejected(self):
        work = self.tmp / "bad"
        shutil.copytree(COUPLING, work)
        text = (work / "samples.csv").read_text()
        (work / "samples.csv").write_text(text + text.splitlines()[1] + "\n")
        with self.assertRaises(replay.ReplayError):
            replay.load_coupling_export(work, replay.load_replay_config(CONFIG))

    def test_eta_inconsistency_rejected(self):
        work = self.tmp / "bad"
        shutil.copytree(COUPLING, work)
        text = (work / "samples.csv").read_text().replace(",0,0.06", ",0,0.07", 1)
        (work / "samples.csv").write_text(text)
        with self.assertRaises(replay.ReplayError):
            replay.load_coupling_export(work, replay.load_replay_config(CONFIG))

    def test_history_inconsistency_rejected(self):
        work = self.tmp / "bad"
        shutil.copytree(COUPLING, work)
        text = (work / "history.csv").read_text().replace("0.34", "9.0", 1)
        (work / "history.csv").write_text(text)
        with self.assertRaises(replay.ReplayError):
            replay.load_coupling_export(work, replay.load_replay_config(CONFIG))

    def test_mapping_frame_validation(self):
        config = json.loads(CONFIG.read_text())
        config["local"]["vertical_axis"] = [0.0, 1.0, 0.0]
        path = self.tmp / "bad-config.json"
        path.write_text(json.dumps(config))
        with self.assertRaises(replay.ReplayError):
            replay.load_replay_config(path)

    def test_support_widths_are_deterministic(self):
        config = replay.load_replay_config(CONFIG)
        coupling = replay.load_coupling_export(COUPLING, config)
        widths = [round(item["support_width_m"], 12) for item in coupling.supports]
        self.assertEqual(widths, [0.2, 0.2, 0.2])

    def test_vertical_reconstruction_partial_and_dry(self):
        self.assertEqual(replay._face_fraction(0.0, 0.0, 0.05), 0.0)
        self.assertEqual(replay._face_fraction(0.05, 0.0, 0.05), 1.0)
        self.assertAlmostEqual(replay._face_fraction(0.025, 0.0, 0.05), 0.5)

    def test_internal_scalar_field_reader_ignores_nonuniform_count(self):
        path = self.tmp / "alpha.water"
        path.write_text("""dimensions      [0 0 0 0 0 0 0];

internalField   nonuniform List<scalar>
3
(
0
0.5
1.000000018
)
;

boundaryField
{
}
""")
        self.assertEqual(replay._read_internal_scalar_field(path), [0.0, 0.5, 1.000000018])

    def test_boundary_data_writer_outputs_deterministic_files(self):
        conversion = replay.convert_boundary_data(COUPLING, CONFIG, self.tmp / "replay")
        inlet = self.tmp / "replay/constant/boundaryData/inlet"
        self.assertTrue((inlet / "points").is_file())
        self.assertTrue((inlet / "0/U").is_file())
        self.assertTrue((inlet / "0/alpha.water").is_file())
        self.assertEqual(conversion["local_inlet_face_count"], 72)
        first = (inlet / "0/alpha.water").read_text()
        second_root = self.tmp / "replay2"
        replay.convert_boundary_data(COUPLING, CONFIG, second_root)
        self.assertEqual(first, (second_root / "constant/boundaryData/inlet/0/alpha.water").read_text())

    def test_replay_diagnostics_discharge_residuals_are_small(self):
        replay.convert_boundary_data(COUPLING, CONFIG, self.tmp / "replay")
        with (self.tmp / "replay/replay_diagnostics.csv").open() as handle:
            rows = list(csv.DictReader(handle))
        self.assertGreater(len(rows), 0)
        for row in rows:
            self.assertLess(abs(float(row["normal_discharge_residual"])), 1.0e-10)
            self.assertLess(abs(float(row["tangential_discharge_residual"])), 1.0e-10)

    def test_case_generator_variants(self):
        replay.convert_boundary_data(COUPLING, CONFIG, self.tmp / "replay")
        no_defence = self.tmp / "no_defence"
        barrier = self.tmp / "barrier"
        replay.generate_case(self.tmp / "replay", CONFIG, no_defence, "no_defence")
        replay.generate_case(self.tmp / "replay", CONFIG, barrier, "simple_rigid_barrier")
        replay.validate_generated_case(no_defence, "no_defence")
        replay.validate_generated_case(barrier, "simple_rigid_barrier")
        self.assertNotIn("barrier", (no_defence / "system/blockMeshDict").read_text())
        self.assertIn("barrier", (barrier / "system/blockMeshDict").read_text())
        summary = json.loads((barrier / "openfoam_case_summary.json").read_text())
        self.assertGreater(summary["k"], 0.0)
        self.assertGreater(summary["omega"], 0.0)


if __name__ == "__main__":
    unittest.main()
