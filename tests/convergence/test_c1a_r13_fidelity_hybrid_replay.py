import csv
import shutil
import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/verification/convergence"))

import c1a_r13_fidelity_hybrid_replay as r13


class R13FidelityHybridReplayTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="tsunami-r13-unit-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_projection_metrics_include_shape_descriptors(self):
        result = r13.projection_metrics([0.0, 1.0, 0.0], [0.0, 0.5, 0.0], [0.0, 1.0, 2.0])
        self.assertEqual(result["status"], "computed")
        self.assertAlmostEqual(result["bias"], 1.0 / 6.0)
        self.assertAlmostEqual(result["Linf"], 0.5)
        self.assertEqual(result["local_extrema_count"], 1)
        self.assertGreater(result["total_variation"], 0.0)

    def test_projection_classifies_h250_ceiling_when_error_grows(self):
        metrics = {"overall": {"bed": {}, "source": {}}}
        for field in ("bed", "source"):
            for level, l2 in zip(r13.LEVELS, [4.0, 3.0, 2.0, 2.5]):
                metrics["overall"][field][level] = {"L2": l2}
        classified = r13.classify_projection(metrics)
        self.assertEqual(classified["combined"], "PROJECTION_FIDELITY_CEILING")
        self.assertFalse(classified["fields"]["bed"]["h250_improves_h300"])

    def test_selected_window_coupling_shifts_245_to_zero(self):
        source = self.tmp / "source"
        source.mkdir()
        (source / "metadata.json").write_text(
            '{"contract_version":"tsunami.g3.coupling_export.v1","section_id":"kamaishi-nearshore-interface","boundary_patch_name":"boundary.inland","mesh_id":"m","sample_count":1,"samples":[{"local_index":0,"cell":1,"face":2,"x_m":0,"y_m":0}]}\n',
            encoding="utf-8",
        )
        sample_fields = ["step", "time", "section_id", "local_index", "cell", "face", "x_m", "y_m", "depth", "momentum_x", "momentum_y", "bed_elevation", "free_surface_elevation"]
        with (source / "samples.csv").open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=sample_fields)
            writer.writeheader()
            for step, time in enumerate([240.0, 245.0, 250.0, 545.0, 550.0]):
                writer.writerow({"step": step, "time": time, "section_id": "kamaishi-nearshore-interface", "local_index": 0, "cell": 1, "face": 2, "x_m": 0, "y_m": 0, "depth": 1, "momentum_x": 0, "momentum_y": 0, "bed_elevation": -1, "free_surface_elevation": 0})
        history_fields = ["step", "time", "section_id", "sample_count", "maximum_depth", "maximum_speed"]
        with (source / "history.csv").open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=history_fields)
            writer.writeheader()
            for step, time in enumerate([240.0, 245.0, 250.0, 545.0, 550.0]):
                writer.writerow({"step": step, "time": time, "section_id": "kamaishi-nearshore-interface", "sample_count": 1, "maximum_depth": 1, "maximum_speed": 0})
        manifest = r13.selected_window_coupling(source, self.tmp / "selected")
        with (self.tmp / "selected/samples.csv").open(encoding="utf-8", newline="") as handle:
            rows = list(csv.DictReader(handle))
        self.assertEqual([row["time"] for row in rows], ["0", "5", "300"])
        self.assertEqual(manifest["shifted_window_s"], [0.0, 300.0])

    def test_parse_foamrun_log_reports_alpha_bounds_failure(self):
        log = self.tmp / "log.foamRun"
        log.write_text(
            "\n".join(
                [
                    "Courant Number mean: 0.0001 max: 0.0042",
                    "Interface Courant Number mean: 0.00001 max: 0.0025",
                    "Time = 1s",
                    "Phase-1 volume fraction = 0.38  Min(alpha.water) = -8.7e-05  Max(alpha.water) = 1.000087",
                    "End",
                ]
            ),
            encoding="utf-8",
        )
        parsed = r13.parse_foamrun_log(log)
        self.assertTrue(parsed["ended_normally"])
        self.assertAlmostEqual(parsed["final_time_s"], 1.0)
        self.assertAlmostEqual(parsed["final_alpha_min"], -8.7e-05)
        self.assertFalse(parsed["alpha_bounds_accepted"])

    def test_discover_local3d_smoke_records_vtk_alpha_status(self):
        case = self.tmp / "local3d-smoke/simple_rigid_barrier_1s"
        (case / "VTK").mkdir(parents=True)
        (case / "VTK/case_0.vtk").write_text("# vtk\n", encoding="utf-8")
        (case / "openfoam_case_summary.json").write_text('{"variant":"simple_rigid_barrier"}\n', encoding="utf-8")
        (case / "log.foamRun").write_text(
            "\n".join(
                [
                    "Courant Number mean: 0.0001 max: 0.0042",
                    "Interface Courant Number mean: 0.00001 max: 0.0025",
                    "Time = 1s",
                    "Phase-1 volume fraction = 0.38  Min(alpha.water) = -8.7e-05  Max(alpha.water) = 1.000087",
                    "End",
                ]
            ),
            encoding="utf-8",
        )
        args = type("Args", (), {"external_root": self.tmp})()
        smoke = r13.discover_local3d_smoke(args)
        self.assertEqual(smoke["smoke_result"], "completed_not_accepted_alpha_bounds")
        self.assertEqual(smoke["smoke_attempts"][0]["status"], "completed_vtk_exported_validator_failed_alpha_bounds")


if __name__ == "__main__":
    unittest.main()
