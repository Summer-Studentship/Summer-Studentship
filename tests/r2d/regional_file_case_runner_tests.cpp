#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tsunami/data/CaseConfigurationParsing.hpp>
#include <tsunami/data/CaseConfigurationSerialisation.hpp>
#include <tsunami/data/DatasetManifestParsing.hpp>
#include <tsunami/data/DatasetManifestSerialisation.hpp>
#include <tsunami/geo/CorridorConstructionSerialisation.hpp>
#include <tsunami/geo/TerrainConditioning.hpp>
#include <tsunami/geo/TerrainConditioningSerialisation.hpp>
#include <tsunami/geo_gdal/GdalConditionedTerrainArtifacts.hpp>
#include <tsunami/r2d_case/RegionalFileCaseRunner.hpp>

#include "geospatial_record_fixtures.hpp"

using Catch::Approx;

namespace
{
    struct CorridorFixture
    {
        tsunami::geo::ConstructedCorridor corridor;
        tsunami::geo::CorridorConstructionRecord record;
    };

    struct FileCaseFixture
    {
        std::filesystem::path root;
        std::filesystem::path terrain_record{"manifests/terrain/conditioned-terrain.json"};
        std::filesystem::path mesh{"meshes/regional-square.msh"};
        std::filesystem::path override_corridor{"manifests/corridors/override-corridor.json"};
    };

    [[nodiscard]] auto source_root() -> std::filesystem::path
    {
        auto path = std::filesystem::path{__FILE__};
        for (auto current = path.parent_path(); !current.empty(); current = current.parent_path()) {
            if (std::filesystem::exists(current / "schemas/case/1.0.0/case.schema.json")) {
                return current;
            }
        }
        return path.parent_path();
    }

    [[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string
    {
        auto file = std::ifstream{path, std::ios::binary};
        REQUIRE(file);
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

    auto write_text(const std::filesystem::path &path, std::string_view text) -> void
    {
        std::filesystem::create_directories(path.parent_path());
        auto file = std::ofstream{path, std::ios::binary};
        REQUIRE(file);
        file << text;
        REQUIRE(file.good());
    }

    [[nodiscard]] auto case_revision(const tsunami::data::CaseConfiguration &configuration)
        -> tsunami::data::CaseRevisionRef
    {
        return tsunami::data::CaseRevisionRef{
            configuration.identity().case_id,
            configuration.identity().revision};
    }

    [[nodiscard]] auto reference(std::string code = "EN-METRIC-1")
        -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::tests::r2d_fixtures::horizontal_reference(std::move(code));
    }

    [[nodiscard]] auto target_reference() -> tsunami::geo::ComputationalTargetReference
    {
        return tsunami::tests::r2d_fixtures::target_reference();
    }

    [[nodiscard]] auto transformation_identity(
        std::string id,
        const tsunami::data::CaseConfiguration &configuration)
        -> tsunami::geo::CoordinateTransformationIdentity
    {
        const auto prefix = id;
        return tsunami::tests::r2d_fixtures::transformation_identity(
            std::move(id),
            prefix + "-import",
            prefix + "-source-dataset",
            prefix + "-source-asset",
            case_revision(configuration),
            "minimal-source-manifest",
            1U,
            prefix + "-transformed-dataset",
            prefix + "-transform-process");
    }

    [[nodiscard]] auto corridor_fixture(
        const tsunami::data::CaseConfiguration &configuration) -> CorridorFixture
    {
        const auto polygon = tsunami::geo::Polygon2D{
            {
                {-10.0, 5.0},
                {-10.0, -5.0},
                {30.0, -5.0},
                {30.0, 5.0},
                {-10.0, 5.0},
            },
            {}};
        const auto extent = tsunami::geo::BoundingBox2D{-10.0, -5.0, 30.0, 5.0};
        const auto basis = tsunami::geo::CorridorLocalBasis{{1.0, 0.0}, {0.0, 1.0}, 20.0, 90.0};
        const auto stations = tsunami::geo::CorridorLongitudinalStations{-10.0, 0.0, 20.0, 30.0};
        const auto sponge = tsunami::geo::CorridorSpongeLimits{-10.0, -10.0, 0.0, 10.0};
        auto corridor = tsunami::geo::ConstructedCorridor{
            polygon,
            extent,
            basis,
            stations,
            sponge,
            10.0,
            10.0,
            40.0,
            400.0,
            100.0};

        auto record = tsunami::geo::CorridorConstructionRecord{};
        record.schema = tsunami::data::SchemaIdentity{
            std::string{tsunami::geo::corridor_construction_record_schema_name},
            tsunami::geo::supported_corridor_construction_record_version};
        record.policy_version = tsunami::geo::supported_corridor_construction_record_policy_version;
        record.identity = tsunami::geo::CorridorConstructionIdentity{
            "corridor-file-runner",
            1U,
            case_revision(configuration),
            configuration.regional_2d().corridor.trajectory_id,
            "corridor-dataset",
            "corridor-process",
            "2026-07-31T00:00:00Z"};
        record.scenario_id = configuration.scenario().scenario_id;
        record.target_site = configuration.scenario().target_site;
        record.epicentre = tsunami::geo::CorridorReferencePointEvidence{
            tsunami::geo::CorridorReferencePointRole::epicentre,
            "epicentre-point",
            "synthetic epicentre",
            {0.0, 0.0, -5.0},
            0U,
            std::string{"feature-epicentre"},
            transformation_identity("epicentre-transform", configuration),
            reference("SOURCE"),
            target_reference(),
            "Synthetic file-runner fixture",
            "https://example.test/file-runner/epicentre",
            "2026-07-31T00:00:00Z"};
        record.target = tsunami::geo::CorridorReferencePointEvidence{
            tsunami::geo::CorridorReferencePointRole::target,
            "target-point",
            "synthetic target",
            {20.0, 0.0, 2.0},
            0U,
            std::string{"feature-target"},
            transformation_identity("target-transform", configuration),
            reference("SOURCE"),
            target_reference(),
            "Synthetic file-runner fixture",
            "https://example.test/file-runner/target",
            "2026-07-31T00:00:00Z"};
        record.target_reference = target_reference();
        record.policy = tsunami::geo::CorridorConstructionPolicy{1.0, 0.001, 0.001, 1.0e-12, 1.0e-7, 1.0e-12, "synthetic metric tolerances"};
        record.configured_origin = {0.0, 0.0};
        record.configured_bearing_degrees = 90.0;
        record.derived_bearing_degrees = 90.0;
        record.offshore_extent_m = 10.0;
        record.epicentre_target_distance_m = 20.0;
        record.inland_extent_m = 10.0;
        record.total_length_m = 40.0;
        record.offshore_width_m = 10.0;
        record.inland_width_m = 10.0;
        record.narrowing_rule = "constant_width";
        record.local_basis = basis;
        record.stations = stations;
        record.sponge_limits = sponge;
        record.polygon = polygon;
        record.vertex_order_convention = "counter_clockwise_closed_offshore_left_offshore_right_inland_right_inland_left";
        record.extent = extent;
        record.area_m2 = 400.0;
        record.perimeter_m = 100.0;
        record.diagnostics.analytic_area_m2 = 400.0;
        record.diagnostics.polygon_area_m2 = 400.0;
        record.diagnostics.analytic_perimeter_m = 100.0;
        record.diagnostics.polygon_perimeter_m = 100.0;
        record.configured_field_paths = {"/regional_2d/corridor"};
        return CorridorFixture{std::move(corridor), std::move(record)};
    }

    [[nodiscard]] auto terrain_grid() -> tsunami::geo::TerrainTargetGrid
    {
        return tsunami::geo::TerrainTargetGrid{
            4U,
            2U,
            10.0,
            tsunami::geo::RasterAffineTransform{-10.0, 10.0, 0.0, 10.0, 0.0, -10.0},
            tsunami::geo::BoundingBox2D{-10.0, -10.0, 30.0, 10.0},
            target_reference(),
            -10.0,
            30.0,
            -10.0,
            10.0,
            0.0,
            10.0};
    }

    [[nodiscard]] auto case_configuration() -> tsunami::data::CaseConfiguration
    {
        auto parsed = tsunami::data::parse_case_configuration(
            read_text(source_root() / "tests/fixtures/cases/valid/minimal_regional_2d.json"),
            "minimal-regional");
        REQUIRE(parsed.has_value());

        auto scenario = parsed.value().scenario();
        scenario.scenario_id = "r2d-scenario";
        scenario.target_site = "r2d-target";
        auto regional = parsed.value().regional_2d();
        regional.corridor.trajectory_id = "file-runner-axis";
        regional.corridor.origin = {0.0, 0.0};
        regional.corridor.bearing_degrees_clockwise_from_north = 90.0;
        regional.corridor.width_m = 10.0;
        regional.corridor.offshore_extent_m = 10.0;
        regional.corridor.inland_extent_m = 10.0;
        regional.corridor.sponge = {0.0, 0.0};
        regional.physics.manning = tsunami::data::ManningConfiguration{
            tsunami::data::ManningConfigurationKind::uniform,
            0.025,
            std::nullopt};
        regional.physics.coriolis = tsunami::data::CoriolisConfiguration{
            tsunami::data::CoriolisConfigurationKind::constant,
            1.0e-4,
            std::nullopt};
        regional.numerics.final_time_s = 0.02;
        regional.numerics.maximum_steps = 20U;
        regional.numerics.maximum_timestep_s = 0.01;
        regional.numerics.minimum_timestep_s = 1.0e-8;
        auto outputs = parsed.value().outputs();
        outputs.snapshot_interval_s = 0.01;
        auto frame = parsed.value().coordinate_frame();
        frame.horizontal_crs = "TEST:EN-METRIC-1";
        frame.vertical_datum = "synthetic-positive-up";
        frame.horizontal_unit = "m";
        frame.vertical_unit = "m";

        auto made = tsunami::data::make_case_configuration(
            parsed.value().schema_identity(),
            parsed.value().compatibility(),
            std::string{parsed.value().policy_version()},
            parsed.value().identity(),
            scenario,
            frame,
            parsed.value().datasets(),
            regional,
            outputs,
            parsed.value().extensions());
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto terrain_fixture(
        const tsunami::data::CaseConfiguration &configuration,
        const tsunami::geo::CorridorConstructionRecord &corridor_record)
        -> tsunami::geo::TerrainConditioningResult
    {
        auto grid = terrain_grid();
        auto preparation = tsunami::geo::TerrainConditioningPreparation{};
        preparation.grid = grid;
        preparation.coverage = tsunami::geo::TerrainCorridorCoverage{
            grid,
            std::vector<double>(8U, 1.0),
            std::vector<tsunami::geo::TerrainCorridorCellClass>(8U, tsunami::geo::TerrainCorridorCellClass::active),
            8U,
            0U,
            0U};
        preparation.identity = tsunami::geo::TerrainConditioningIdentity{
            "terrain-file-runner",
            1U,
            case_revision(configuration),
            "minimal-source-manifest",
            1U,
            "conditioned-terrain",
            "terrain-conditioning-process",
            "2026-07-31T00:00:00Z"};
        preparation.policy.grid = tsunami::geo::TerrainTargetGridPolicy{10.0, 0.5, 4.0, 4096U, 1.0e-9, 1.0e-12, "file-runner grid policy"};
        preparation.policy.merge = tsunami::geo::TerrainMergePolicy{
            "bathymetry-primary",
            "topography-primary",
            100.0,
            tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning,
            "bathymetry priority"};
        preparation.policy.gaps = tsunami::geo::TerrainGapResolutionPolicy{
            tsunami::geo::TerrainGapResolutionKind::reject,
            0.0,
            0.0,
            0U,
            0U,
            0.0,
            0.0,
            "reject gaps"};
        preparation.corridor_identity = corridor_record.identity;
        preparation.scenario_id = configuration.scenario().scenario_id;
        preparation.target_site = configuration.scenario().target_site;
        preparation.output_uncertainty = tsunami::data::DatasetUncertainty{
            tsunami::data::UncertaintyStatus::not_reported,
            {},
            std::string{"not_reported"}};
        preparation.output_path = tsunami::geo::default_conditioned_terrain_path("conditioned-terrain");

        auto bathymetry = tsunami::tests::r2d_fixtures::resampled_source(
            grid,
            preparation.policy.grid,
            preparation.identity.case_revision,
            preparation.identity.manifest_id,
            preparation.identity.manifest_revision,
            "bathymetry-primary",
            tsunami::geo::TerrainSourceRole::bathymetry,
            std::vector<double>(8U, -3.0),
            std::vector<std::uint8_t>(8U, 1U));
        auto topography = tsunami::tests::r2d_fixtures::resampled_source(
            grid,
            preparation.policy.grid,
            preparation.identity.case_revision,
            preparation.identity.manifest_id,
            preparation.identity.manifest_revision,
            "topography-primary",
            tsunami::geo::TerrainSourceRole::topography,
            std::vector<double>(8U, 2.0),
            std::vector<std::uint8_t>(8U, 0U));
        auto result = tsunami::geo::condition_terrain_from_resampled_sources(preparation, bathymetry, topography);
        REQUIRE(result.has_value());
        return std::move(result).value();
    }

    [[nodiscard]] auto gmsh_text() -> std::string
    {
        return R"($MeshFormat
4.1 0 8
$EndMeshFormat
$PhysicalNames
5
2 1 "region.domain"
1 2 "boundary.offshore"
1 3 "boundary.inland"
1 4 "boundary.left_side"
1 5 "boundary.right_side"
$EndPhysicalNames
$Entities
0 4 1 0
1 0 -4 0 0 4 0 1 2 0
2 20 -4 0 20 4 0 1 3 0
3 0 4 0 20 4 0 1 4 0
4 0 -4 0 20 -4 0 1 5 0
1 0 -4 0 20 4 0 1 1 0
$EndEntities
$Nodes
1 4 10 40
2 1 0 4
10
20
30
40
0 -4 0
20 -4 0
20 4 0
0 4 0
$EndNodes
$Elements
5 6 100 201
1 1 1 1
100 10 40
1 2 1 1
101 20 30
1 3 1 1
102 30 40
1 4 1 1
103 10 20
2 1 2 2
200 10 20 30
201 10 30 40
$EndElements
)";
    }

    [[nodiscard]] auto make_fixture(std::string_view name) -> FileCaseFixture
    {
        auto fixture = FileCaseFixture{};
        fixture.root = std::filesystem::current_path() / ("tsunami-r2d-file-runner-" + std::string{name});
        std::filesystem::remove_all(fixture.root);
        std::filesystem::create_directories(fixture.root);

        auto configuration = case_configuration();
        auto manifest = tsunami::data::parse_dataset_manifest(
            read_text(source_root() / "tests/fixtures/manifests/valid/minimal_source_manifest.json"),
            "minimal-manifest");
        REQUIRE(manifest.has_value());
        std::filesystem::create_directories(fixture.root / "manifests");
        REQUIRE(tsunami::data::write_case_configuration(fixture.root / "case.json", configuration).has_value());
        REQUIRE(tsunami::data::write_dataset_manifest(fixture.root / "manifests/datasets.json", manifest.value()).has_value());

        auto corridor = corridor_fixture(configuration);
        const auto default_corridor = tsunami::geo::default_corridor_construction_record_path(
            configuration.regional_2d().corridor.trajectory_id);
        REQUIRE(tsunami::geo::write_corridor_construction_record(fixture.root / default_corridor, corridor.record).has_value());
        REQUIRE(tsunami::geo::write_corridor_construction_record(fixture.root / fixture.override_corridor, corridor.record).has_value());

        auto terrain = terrain_fixture(configuration, corridor.record);
        REQUIRE(tsunami::geo::write_terrain_conditioning_record(fixture.root / fixture.terrain_record, terrain.record).has_value());
        auto artifact_paths = tsunami::geo_gdal::make_conditioned_terrain_artifact_paths(fixture.root, terrain.record);
        REQUIRE(artifact_paths.has_value());
        REQUIRE(tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(artifact_paths.value(), terrain.terrain, terrain.record).has_value());
        write_text(fixture.root / fixture.mesh, gmsh_text());
        return fixture;
    }

    [[nodiscard]] auto request_for(
        const FileCaseFixture &fixture,
        std::string run_id,
        std::optional<std::filesystem::path> corridor = std::nullopt,
        bool overwrite = false) -> tsunami::r2d_case::RegionalFileCaseRunRequest
    {
        return tsunami::r2d_case::RegionalFileCaseRunRequest{
            fixture.root,
            fixture.terrain_record,
            fixture.mesh,
            std::move(corridor),
            std::move(run_id),
            tsunami::r2d_case::RegionalFileCaseRunPolicy{
                tsunami::r2d::RegionalCasePreparationPolicy{0.0, 1.0e-6, 1.0e-9, 1.0e-12, 1.0e-12},
                tsunami::r2d::RegionalRasterCellTransferPolicy{1.0e-7, 1.0e-12, 16U}},
            overwrite,
            {}};
    }

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }
} // namespace

TEST_CASE("file-driven Regional2D runner produces deterministic CSV outputs", "[r2d-file-runner]")
{
    const auto fixture = make_fixture("success");

    auto first = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "run-a"));
    REQUIRE(first.has_value());
    CHECK(first.value().diagnostics.case_id == "synthetic-kamaishi-r2d");
    CHECK(first.value().diagnostics.corridor_id == "corridor-file-runner");
    CHECK(first.value().diagnostics.terrain_id == "terrain-file-runner");
    CHECK(first.value().diagnostics.mesh_id == "gmsh:regional-square.msh");
    CHECK(first.value().final_simulation_time == Approx(0.02));
    CHECK(first.value().diagnostics.solve.final_integrals.momentum_x == Approx(0.0).margin(1.0e-12));
    CHECK(first.value().diagnostics.solve.final_integrals.momentum_y == Approx(0.0).margin(1.0e-12));
    CHECK(std::filesystem::is_regular_file(first.value().output_artifacts.diagnostics_csv));
    CHECK(std::filesystem::is_regular_file(first.value().output_artifacts.snapshots_csv));

    auto second_request = request_for(fixture, "run-b", fixture.override_corridor);
    second_request.case_root = fixture.root.lexically_relative(std::filesystem::current_path());
    auto second = tsunami::r2d_case::run_regional_case_from_files(second_request);
    REQUIRE(second.has_value());
    CHECK(read_text(first.value().output_artifacts.diagnostics_csv) ==
          read_text(second.value().output_artifacts.diagnostics_csv));
    CHECK(read_text(first.value().output_artifacts.snapshots_csv) ==
          read_text(second.value().output_artifacts.snapshots_csv));
    std::filesystem::remove_all(fixture.root);
}

TEST_CASE("file-driven Regional2D runner rejects unsafe paths before outputs", "[r2d-file-runner]")
{
    const auto fixture = make_fixture("path-safety");
    auto request = request_for(fixture, "unsafe");
    request.terrain_record_path = fixture.root / fixture.terrain_record;

    auto result = tsunami::r2d_case::run_regional_case_from_files(request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == "r2d.file_case.path_invalid");
    CHECK(context_value(result.error(), "state_changed") == "false");
    CHECK_FALSE(std::filesystem::exists(fixture.root / "runs/unsafe/outputs/regional2d"));
    std::filesystem::remove_all(fixture.root);
}

TEST_CASE("file-driven Regional2D runner preserves existing outputs when overwrite is disabled", "[r2d-file-runner]")
{
    const auto fixture = make_fixture("overwrite");
    auto first = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "existing"));
    REQUIRE(first.has_value());
    write_text(first.value().output_directory / "sentinel.txt", "preserve me\n");

    auto second = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "existing"));
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().code() == "r2d.file_case.output_prepare_failed");
    CHECK(context_value(second.error(), "state_changed") == "false");
    CHECK(read_text(first.value().output_directory / "sentinel.txt") == "preserve me\n");
    std::filesystem::remove_all(fixture.root);
}
