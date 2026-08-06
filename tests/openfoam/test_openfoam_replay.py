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

    def _production_config_path(self, **local_case_overrides) -> Path:
        config = json.loads(CONFIG.read_text(encoding="utf-8"))
        config["schema"]["version"] = "1.1.0"
        config["boundary_policy"] = {
            "mode": "open_ocean_damped",
            "outlet": "open_ocean",
            "laterals": "open_ocean",
            "atmosphere": "open_atmosphere",
            "policy_version": "1.0.0",
        }
        config["damping_policy"] = {
            "enabled": True,
            "model": "isotropicDamping",
            "profile": "halfCosineRamp",
            "outlet_width_fraction": 0.15,
            "lateral_width_fraction": 0.10,
            "target_e_folds": 4.0,
        }
        config["wall_function_policy"] = {
            "mode": "continuous_spalding",
            "k": "kqRWallFunction",
            "omega": "omegaWallFunction",
            "nut": "nutUSpaldingWallFunction",
        }
        config["timestep_policy"] = {
            "adjust_time_step": True,
            "target_max_co": 0.25,
            "target_max_alpha_co": 0.25,
            "minimum_timestep_s": 1.0e-7,
        }
        config["local_case"] = {
            "streamwise_length_m": 2.0,
            "streamwise_cells": 40,
            "span_cells": 60,
            "vertical_cells": 12,
            "end_time_s": 0.12,
            "maximum_timestep_s": 0.02,
            "write_interval_s": 0.04,
            "initial_water_level_m": 0.18,
            **local_case_overrides,
        }
        path = self.tmp / "production_config.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        return path

    def test_valid_coupling_parser(self):
        config = replay.load_replay_config(CONFIG)
        coupling = replay.load_coupling_export(COUPLING, config)
        self.assertEqual(len(coupling.ordered_samples), 3)
        self.assertEqual(coupling.times, [0.0, 0.04, 0.08, 0.12])

    def test_legacy_configuration_normalises_to_symmetry_policy(self):
        config = replay.load_replay_config(CONFIG)
        self.assertEqual(config["schema"]["version"], "1.0.0")
        self.assertEqual(config["boundary_policy"]["mode"], "symmetry_test")
        self.assertFalse(config["damping_policy"]["enabled"])
        self.assertEqual(config["wall_function_policy"]["nut"], "nutkWallFunction")

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

    def test_production_config_generates_open_boundaries_and_records(self):
        config_path = self._production_config_path()
        replay.convert_boundary_data(COUPLING, config_path, self.tmp / "replay")
        case = self.tmp / "production"
        summary = replay.generate_case(self.tmp / "replay", config_path, case, "simple_rigid_barrier")
        replay.validate_generated_case(case, "simple_rigid_barrier")
        boundary = json.loads((case / "boundary_policy.json").read_text(encoding="utf-8"))
        wall = json.loads((case / "wall_function_policy.json").read_text(encoding="utf-8"))
        timestep = json.loads((case / "timestep_policy.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["boundary_mode"], "open_ocean_damped")
        self.assertEqual(boundary["mesh_patch_types"]["sideLeft"], "patch")
        self.assertEqual(boundary["field_boundary_types"]["U"]["outlet"], "pressureInletOutletVelocity")
        self.assertEqual(boundary["field_boundary_types"]["p_rgh"]["sideRight"], "prghTotalPressure")
        self.assertEqual(boundary["field_boundary_types"]["alpha.water"]["sideLeft"], "variableHeightFlowRate")
        self.assertEqual(boundary["field_boundary_types"]["k"]["outlet"], "inletOutlet")
        self.assertTrue(boundary["damping_configuration"]["enabled"])
        self.assertIn("isotropicDamping", (case / "constant/fvModels").read_text(encoding="utf-8"))
        self.assertEqual(wall["field_types"]["nut"]["barrier"], "nutUSpaldingWallFunction")
        self.assertEqual(timestep["maxCo"], 0.25)
        self.assertEqual(timestep["maxAlphaCo"], 0.25)
        self.assertIn("yPlus", (case / "system/controlDict").read_text(encoding="utf-8"))

    def test_production_damping_rejects_coarse_lateral_core(self):
        config_path = self._production_config_path(span_cells=20)
        replay.convert_boundary_data(COUPLING, config_path, self.tmp / "replay")
        with self.assertRaisesRegex(replay.ReplayError, "lateral damping"):
            replay.generate_case(self.tmp / "replay", config_path, self.tmp / "bad-production", "no_defence")

    def test_yplus_evidence_requires_expected_patches_and_non_negative_values(self):
        samples = [
            {"time": 0.0, "patch": "terrain", "values": [2.0, 12.0, 44.0]},
            {"time": 0.12, "patch": "terrain", "values": [4.0, 18.0, 80.0]},
            {"time": 0.0, "patch": "barrier", "values": [1.0, 9.0, 22.0]},
            {"time": 0.12, "patch": "barrier", "values": [3.0, 16.0, 55.0]},
        ]
        evidence = replay.summarise_yplus_samples(samples, ["terrain", "barrier"], 0.12, None)
        self.assertEqual(evidence["patches"]["terrain"]["time_count"], 2)
        with self.assertRaisesRegex(replay.ReplayError, "missing yPlus"):
            replay.summarise_yplus_samples(samples[:2], ["terrain", "barrier"], 0.12, None)
        samples[0]["values"] = [-1.0]
        with self.assertRaisesRegex(replay.ReplayError, "negative"):
            replay.summarise_yplus_samples(samples, ["terrain", "barrier"], 0.12, None)

    def test_timestep_evidence_records_diffusion_and_rollback_disposition(self):
        policy = {
            "minimum_accepted_timestep_s": 1.0e-7,
            "diagnostic_viscous_timescale_s": 12.0,
            "diagnostic_diffusive_margin": 600.0,
            "diffusion_disposition": "implicit diffusion diagnostic only",
            "rejected_step_disposition": "no exact rollback",
        }
        log = """Time = 0
Courant Number mean: 0 max: 0.1
Interface Courant Number mean: 0 max: 0.08
deltaT = 0.02
Time = 0.02
Courant Number mean: 0 max: 0.12
Interface Courant Number mean: 0 max: 0.09
deltaT = 0.018
"""
        rows = replay.parse_timestep_series_from_log(log)
        evidence = replay.write_timestep_evidence(self.tmp, rows, policy)
        self.assertAlmostEqual(evidence["minimum_observed_timestep_s"], 0.018)
        self.assertIn("diffusion", evidence["diffusion_disposition"])
        self.assertIn("rollback", evidence["rejected_step_disposition"])

    def test_generator_rejects_requested_time_beyond_boundary_data(self):
        replay.convert_boundary_data(COUPLING, CONFIG, self.tmp / "replay")
        config = json.loads(CONFIG.read_text())
        config["local_case"] = {"end_time_s": 1.0}
        config_path = self.tmp / "too-long.json"
        config_path.write_text(json.dumps(config))
        with self.assertRaisesRegex(replay.ReplayError, "boundaryData maximum time"):
            replay.generate_case(self.tmp / "replay", config_path, self.tmp / "too-long", "no_defence")

    def test_generator_rejects_requested_time_shorter_than_replay_peak(self):
        replay.convert_boundary_data(COUPLING, CONFIG, self.tmp / "replay")
        config = json.loads(CONFIG.read_text())
        config["local_case"] = {"end_time_s": 0.06}
        config["replay_window"] = {"shifted_duration_s": 0.06, "peak_shifted_time_s": 0.08}
        config_path = self.tmp / "misses-peak.json"
        config_path.write_text(json.dumps(config))
        with self.assertRaisesRegex(replay.ReplayError, "major replay peak"):
            replay.generate_case(self.tmp / "replay", config_path, self.tmp / "misses-peak", "no_defence")

    def test_smoke_validation_rejects_solver_short_of_replay_duration(self):
        case = self.tmp / "short-solver"
        case.mkdir()
        (case / "log.foamRun").write_text("Time = 60\n", encoding="utf-8")
        (case / "60").mkdir()
        (case / "openfoam_case_summary.json").write_text(json.dumps({"end_time": 300.0}), encoding="utf-8")
        with self.assertRaisesRegex(replay.ReplayError, "did not reach"):
            replay.validate_smoke_case(case, "no_defence")

    def test_smoke_validation_requires_boundary_data_to_cover_request(self):
        case = self.tmp / "short-boundary"
        (case / "300").mkdir(parents=True)
        boundary = case / "constant/boundaryData/inlet"
        (boundary / "0").mkdir(parents=True)
        (boundary / "60").mkdir()
        (case / "log.foamRun").write_text("Time = 300\n", encoding="utf-8")
        (case / "openfoam_case_summary.json").write_text(json.dumps({"end_time": 300.0}), encoding="utf-8")
        with self.assertRaisesRegex(replay.ReplayError, "boundaryData ends"):
            replay.validate_smoke_case(case, "no_defence")


if __name__ == "__main__":
    unittest.main()
