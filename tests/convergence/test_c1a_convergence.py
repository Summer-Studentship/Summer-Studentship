import json
import shutil
import tempfile
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/verification/convergence"))
sys.path.insert(0, str(ROOT / "tools/cases"))

import c1a_convergence as c1a
import kamaishi_delivery as kd


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

    def test_requested_solver_mesh_resolution_is_preserved(self):
        for requested in (1000.0, 800.0, 750.0):
            with self.subTest(requested=requested):
                contract = c1a.regional_resolution_contract(
                    requested_solver_mesh_size_m=requested,
                    terrain_processing_resolution_m=requested,
                )
                self.assertEqual(contract["terrain_processing_resolution"]["requested_m"], requested)
                self.assertEqual(contract["solver_mesh_target_size"]["requested_m"], requested)
                self.assertEqual(contract["solver_mesh_target_size"]["configured_m"], requested)
                self.assertEqual(contract["tier_mapping"], "none")

    def test_requested_solver_mesh_contract_rejects_silent_tier_mapping(self):
        contract = c1a.regional_resolution_contract(requested_solver_mesh_size_m=750.0)
        contract["solver_mesh_target_size"]["configured_m"] = 500.0
        with self.assertRaisesRegex(c1a.ConvergenceError, "without an explicit tier mapping"):
            c1a.assert_regional_resolution_contract(contract)

    def test_frozen_terrain_contract_decouples_terrain_processing_from_solver_target(self):
        authority = {
            "terrain": {
                "path": "/external/g6/conditioned-terrain.tif",
                "sha256": "terrain-sha",
                "record_sha256": "record-sha",
                "metadata_sha256": "terrain-meta-sha",
                "processing_resolution_m": 1000.0,
            },
            "source": {
                "path": "/external/g6/tohoku_vertical_displacement.tif",
                "sha256": "source-sha",
                "metadata_sha256": "source-meta-sha",
                "representation_policy": "fixed G6 coseismic displacement raster projected to each solver mesh",
            },
        }
        contract = c1a.regional_frozen_terrain_resolution_contract(
            requested_solver_mesh_size_m=800.0,
            frozen_authority=authority,
            actual_characteristic_mesh_size_m=514.5,
        )
        self.assertEqual(contract["terrain_processing_resolution"]["requested_m"], 1000.0)
        self.assertEqual(contract["solver_mesh_target_size"]["requested_m"], 800.0)
        self.assertEqual(contract["frozen_terrain"]["sha256"], "terrain-sha")
        self.assertIn("mesh-dependent projection Pi_h", contract["varied_only"])

    def test_frozen_family_invariance_allows_only_solver_mesh_variation(self):
        base = {
            "frozen_terrain": {"sha256": "terrain", "metadata_sha256": "terrain-meta", "processing_resolution_m": 1000.0},
            "frozen_source": {"sha256": "source", "metadata_sha256": "source-meta", "representation_policy": "fixed raster"},
            "physical_configuration_sha256": "physical",
            "domain_sha256": "domain",
            "coupling_section_sha256": "section",
            "solver_mesh_target_size": {"requested_m": 1000.0},
        }
        finer = json.loads(json.dumps(base))
        finer["solver_mesh_target_size"]["requested_m"] = 800.0
        c1a.assert_regional_frozen_family_invariance([base, finer])
        changed = json.loads(json.dumps(finer))
        changed["frozen_terrain"]["sha256"] = "different"
        with self.assertRaisesRegex(c1a.ConvergenceError, "frozen Regional family invariance failed"):
            c1a.assert_regional_frozen_family_invariance([base, changed])

    def test_phase_diagnostic_keeps_formal_metric_unshifted(self):
        reference = [0.0, 0.0, 1.0, 0.0, 0.0]
        candidate = [0.0, 1.0, 0.0, 0.0, 0.0]
        diagnostic = c1a.phase_alignment_diagnostic(candidate, reference, 5.0, max_lag_steps=2)
        self.assertGreater(diagnostic["unshifted_nrmse"], 0.0)
        self.assertEqual(diagnostic["optimal_lag_s"], 5.0)
        self.assertLess(diagnostic["phase_aligned_nrmse"], diagnostic["unshifted_nrmse"])
        self.assertFalse(diagnostic["formal_metric_shifted"])

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

    def test_section_discharge_units_and_integration(self):
        result = c1a.section_integrated_discharge([1.0, 2.0, 3.0], [10.0, 20.0, 30.0])
        self.assertEqual(result["Q_n_units"], "m^3/s")
        self.assertEqual(result["qbar_n_units"], "m^2/s")
        self.assertAlmostEqual(result["Q_n"], 140.0)
        self.assertAlmostEqual(result["qbar_n"], 140.0 / 60.0)

    def test_fixed_common_support_spans_section_width(self):
        support = c1a.fixed_common_support(8000.0, 5)
        self.assertEqual(support, [0.0, 2000.0, 4000.0, 6000.0, 8000.0])

    def test_richardson_gci_reports_monotone_three_level_result(self):
        result = c1a.richardson_gci([1.0, 1.25, 2.0], [0.5, 1.0, 2.0])
        self.assertEqual(result["status"], "computed")
        self.assertGreater(result["observed_order"], 0.0)
        self.assertGreater(result["gci21_percent"], 0.0)

    def test_corridor_domain_is_invariant_to_grid_resolution(self):
        spec = {
            "corridor": {
                "width_m": 8000.0,
                "source_side_pre_extent_m": 15000.0,
                "inland_extent_m": 500.0,
                "offshore_sponge_width_m": 10000.0,
                "side_sponge_width_m": 1000.0,
            }
        }
        trajectory = kd.Trajectory(
            epicentre_wgs84=(142.373, 38.297),
            proxy_wgs84=(141.8858, 39.2757),
            epicentre=kd.Point(1000.0, 2000.0),
            proxy=kd.Point(9000.0, 12000.0),
            selected=kd.Point(11000.0, 2000.0),
            selected_wgs84=(141.9207, 39.2065),
            unit=kd.Point(1.0, 0.0),
            left=kd.Point(0.0, 1.0),
            distance_m=10000.0,
            proxy_distance_to_interface_m=2500.0,
            bearing_degrees=90.0,
            selected_bed_elevation_m=-8.0,
            selected_depth_m=8.0,
            selection_fallback=True,
            selection_reason="synthetic unit-test trajectory",
            cross_section_sample_count=8,
            cross_section_min_depth_m=5.0,
            cross_section_max_depth_m=50.0,
            cross_section_min_bed_elevation_m=-50.0,
            cross_section_max_bed_elevation_m=-5.0,
        )

        def grid(spacing_m: float) -> kd.Grid:
            return kd.Grid(
                width=int(32000 / spacing_m),
                height=int(8000 / spacing_m),
                spacing_m=spacing_m,
                xi_min_m=-15000.0,
                xi_max_m=10500.0,
                eta_bottom_m=-4000.0,
                eta_top_m=4000.0,
                affine=(0.0, spacing_m, 0.0, 0.0, 0.0, spacing_m),
                extent={"minimum_x": 0.0, "minimum_y": 0.0, "maximum_x": 1.0, "maximum_y": 1.0},
            )

        coarse = self.tmp / "coarse"
        fine = self.tmp / "fine"
        kd.write_corridor_record(coarse, trajectory, spec, grid(2000.0), "2026-08-07T00:00:00Z")
        kd.write_corridor_record(fine, trajectory, spec, grid(1000.0), "2026-08-07T00:00:00Z")

        coarse_record = json.loads((coarse / "manifests/corridors/tohoku-kamaishi-centreline.json").read_text(encoding="utf-8"))
        fine_record = json.loads((fine / "manifests/corridors/tohoku-kamaishi-centreline.json").read_text(encoding="utf-8"))
        for key in ("area_m2", "perimeter_m", "extent", "polygon", "local_basis", "stations"):
            self.assertEqual(coarse_record[key], fine_record[key])

        coarse_evidence = json.loads((coarse / "manifests/corridors/kamaishi-delivery-corridor-evidence.json").read_text(encoding="utf-8"))
        fine_evidence = json.loads((fine / "manifests/corridors/kamaishi-delivery-corridor-evidence.json").read_text(encoding="utf-8"))
        self.assertEqual(coarse_evidence["corridor"], fine_evidence["corridor"])
        self.assertEqual(coarse_evidence["basis"], fine_evidence["basis"])
        self.assertNotEqual(coarse_evidence["grid"]["profile"], fine_evidence["grid"]["profile"])


if __name__ == "__main__":
    unittest.main()
