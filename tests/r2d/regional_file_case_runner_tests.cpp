#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tsunami/data/CaseConfigurationParsing.hpp>
#include <tsunami/data/CaseConfigurationSerialisation.hpp>
#include <tsunami/data/DatasetManifestParsing.hpp>
#include <tsunami/data/DatasetManifestSerialisation.hpp>
#include <tsunami/geo/CorridorConstruction.hpp>
#include <tsunami/geo/CorridorConstructionParsing.hpp>
#include <tsunami/geo/CorridorConstructionSerialisation.hpp>
#include <tsunami/geo/TerrainConditioning.hpp>
#include <tsunami/geo/TerrainConditioningParsing.hpp>
#include <tsunami/geo/TerrainConditioningSerialisation.hpp>
#include <tsunami/geo_gdal/GdalConditionedTerrainArtifacts.hpp>
#include <tsunami/geo_gdal/GdalEarthquakeDisplacementArtifacts.hpp>
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

    struct SnapshotCsvRow
    {
        std::size_t step{};
        double time{};
        std::size_t cell{};
        double depth{};
        double momentum_x{};
        double momentum_y{};
        double bed_elevation{};
        double free_surface_elevation{};
    };

    class ScopedEnvironmentFlag
    {
    public:
        explicit ScopedEnvironmentFlag(const char *name)
            : name_{name}
        {
#ifdef _WIN32
            _putenv_s(name_, "1");
#else
            setenv(name_, "1", 1);
#endif
        }

        ScopedEnvironmentFlag(const ScopedEnvironmentFlag &) = delete;
        auto operator=(const ScopedEnvironmentFlag &) -> ScopedEnvironmentFlag & = delete;

        ~ScopedEnvironmentFlag()
        {
#ifdef _WIN32
            _putenv_s(name_, "");
#else
            unsetenv(name_);
#endif
        }

    private:
        const char *name_{};
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

    [[nodiscard]] auto split_csv_line(const std::string &line) -> std::vector<std::string>
    {
        auto values = std::vector<std::string>{};
        auto stream = std::stringstream{line};
        auto value = std::string{};
        while (std::getline(stream, value, ',')) {
            values.push_back(value);
        }
        return values;
    }

    [[nodiscard]] auto read_snapshot_rows(const std::filesystem::path &path) -> std::vector<SnapshotCsvRow>
    {
        auto input = std::ifstream{path};
        REQUIRE(input);
        auto line = std::string{};
        REQUIRE(std::getline(input, line));
        auto rows = std::vector<SnapshotCsvRow>{};
        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }
            const auto values = split_csv_line(line);
            REQUIRE(values.size() == 8U);
            rows.push_back(SnapshotCsvRow{
                static_cast<std::size_t>(std::stoull(values[0])),
                std::stod(values[1]),
                static_cast<std::size_t>(std::stoull(values[2])),
                std::stod(values[3]),
                std::stod(values[4]),
                std::stod(values[5]),
                std::stod(values[6]),
                std::stod(values[7])});
        }
        return rows;
    }

    [[nodiscard]] auto replace_all(std::string text, std::string_view from, std::string_view to) -> std::string
    {
        auto pos = std::size_t{};
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
        return text;
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

    [[nodiscard]] auto asset(
        std::string asset_id,
        std::filesystem::path managed_path,
        std::string digest_value) -> tsunami::data::DatasetAsset
    {
        return tsunami::data::DatasetAsset{
            std::move(asset_id),
            tsunami::data::DatasetAssetRole::primary,
            tsunami::data::DatasetAssetLocation{
                tsunami::data::DatasetLocationKind::managed_path,
                std::move(managed_path),
                std::nullopt},
            "application/octet-stream",
            std::uint64_t{1024U},
            tsunami::data::ContentDigest{
                tsunami::data::DigestAlgorithm::sha256,
                std::move(digest_value),
                tsunami::data::DigestOrigin::provider_declared}};
    }

    [[nodiscard]] auto dataset(
        std::string dataset_id,
        std::string asset_id,
        tsunami::data::DatasetRole role,
        tsunami::data::DatasetRepresentationKind representation,
        std::string digest_value) -> tsunami::data::DatasetRecord
    {
        return tsunami::data::DatasetRecord{
            std::move(dataset_id),
            tsunami::data::DatasetOriginKind::source,
            representation,
            {role},
            "Synthetic Regional2D file-runner source",
            std::string{"Producer-derived file-runner fixture source."},
            "illustrative-provider",
            "illustrative-licence",
            tsunami::data::SourceAcquisitionRecord{
                "https://example.invalid/file-runner-source",
                "2026-07-30T20:05:00Z",
                std::string{"illustrative-v1"},
                std::string{"2026-07-30"}},
            std::nullopt,
            {asset(asset_id, std::filesystem::path{"inputs/data"} / (asset_id + ".bin"), std::move(digest_value))},
            tsunami::data::DatasetSpatialReference{
                tsunami::data::SpatialApplicability::spatial,
                std::string{"TEST:SOURCE"},
                std::string{"synthetic-positive-up"},
                std::string{"m"},
                std::string{"m"},
                std::string{"east_north"},
                std::string{"up"}},
            tsunami::data::DatasetResolution{
                tsunami::data::SpatialResolution{tsunami::data::SpatialResolutionKind::nominal, 10.0, 10.0, std::string{"m"}, std::nullopt},
                tsunami::data::TemporalResolution{tsunami::data::TemporalResolutionKind::static_dataset, std::nullopt, std::nullopt, std::nullopt}},
            tsunami::data::DatasetUncertainty{tsunami::data::UncertaintyStatus::not_reported, {}, std::string{"not_reported"}},
            std::string{"Synthetic citation only"},
            {}};
    }

    [[nodiscard]] auto earthquake_dataset() -> tsunami::data::DatasetRecord
    {
        return tsunami::data::DatasetRecord{
            "tohoku-earthquake-displacement",
            tsunami::data::DatasetOriginKind::generated,
            tsunami::data::DatasetRepresentationKind::raster,
            {tsunami::data::DatasetRole::earthquake_displacement},
            "Synthetic Tohoku vertical seabed displacement",
            std::string{"Fixture artifact representing a generated vertical seabed displacement raster."},
            "illustrative-provider",
            "illustrative-licence",
            std::nullopt,
            std::string{"earthquake-artifact-process"},
            {tsunami::data::DatasetAsset{
                 "tohoku-vertical-displacement",
                 tsunami::data::DatasetAssetRole::primary,
                 tsunami::data::DatasetAssetLocation{
                     tsunami::data::DatasetLocationKind::managed_path,
                     std::filesystem::path{"inputs/data/earthquake/tohoku_vertical_displacement.tif"},
                     std::nullopt},
                 "image/tiff",
                 std::nullopt,
                 tsunami::data::ContentDigest{
                     tsunami::data::DigestAlgorithm::sha256,
                     std::string(64U, '4'),
                     tsunami::data::DigestOrigin::project_computed}},
             tsunami::data::DatasetAsset{
                 "tohoku-vertical-displacement-metadata",
                 tsunami::data::DatasetAssetRole::metadata,
                 tsunami::data::DatasetAssetLocation{
                     tsunami::data::DatasetLocationKind::managed_path,
                     std::filesystem::path{"inputs/data/earthquake/tohoku_vertical_displacement.json"},
                     std::nullopt},
                 "application/json",
                 std::nullopt,
                 tsunami::data::ContentDigest{
                     tsunami::data::DigestAlgorithm::sha256,
                     std::string(64U, '5'),
                     tsunami::data::DigestOrigin::project_computed}}},
            tsunami::data::DatasetSpatialReference{
                tsunami::data::SpatialApplicability::spatial,
                std::string{"TEST:EN-METRIC-1"},
                std::string{"synthetic-positive-up"},
                std::string{"m"},
                std::string{"m"},
                std::string{"east_north"},
                std::string{"up"}},
            tsunami::data::DatasetResolution{
                tsunami::data::SpatialResolution{tsunami::data::SpatialResolutionKind::grid_spacing, 10.0, 10.0, std::string{"m"}, std::nullopt},
                tsunami::data::TemporalResolution{tsunami::data::TemporalResolutionKind::static_dataset, std::nullopt, std::nullopt, std::nullopt}},
            tsunami::data::DatasetUncertainty{tsunami::data::UncertaintyStatus::estimated, {}, std::string{"synthetic fixture"}},
            std::string{"USGS finite-fault fixture lineage; no network access during tests"},
            {}};
    }

    [[nodiscard]] auto earthquake_process() -> tsunami::data::ProcessingRecord
    {
        return tsunami::data::ProcessingRecord{
            "earthquake-artifact-process",
            "earthquake-artifact-produce",
            "2026-08-02T00:00:00Z",
            tsunami::data::ProcessingSoftware{
                "tsunami-tohoku-artifact",
                "0.1.0",
                std::nullopt,
                std::nullopt},
            "{}",
            {"bathymetry-primary"},
            {"tohoku-earthquake-displacement"},
            {}};
    }

    [[nodiscard]] auto dataset_manifest(const tsunami::data::CaseConfiguration &configuration)
        -> tsunami::data::DatasetManifest
    {
        auto made = tsunami::data::make_dataset_manifest(
            tsunami::data::SchemaIdentity{
                std::string{tsunami::data::dataset_manifest_schema_name},
                tsunami::data::supported_dataset_manifest_version},
            tsunami::data::DatasetManifestCompatibility::exact,
            std::string{tsunami::data::supported_dataset_manifest_policy_version},
            tsunami::data::DatasetManifestIdentity{
                "minimal-source-manifest",
                1U,
                case_revision(configuration),
                "2026-07-30T20:00:00Z",
                "fixture-author"},
            {tsunami::data::DatasetProvider{
                "illustrative-provider",
                "Illustrative Provider",
                std::string{"Illustrative Organisation"},
                std::string{"https://example.invalid/provider"},
                {}}},
            {tsunami::data::DatasetLicence{
                "illustrative-licence",
                "Illustrative Licence",
                "LicenseRef-Illustrative",
                std::string{"https://example.invalid/licence"},
                std::string{"Illustrative attribution only"},
                {}}},
            {dataset("bathymetry-primary", "bathymetry-asset", tsunami::data::DatasetRole::bathymetry, tsunami::data::DatasetRepresentationKind::raster, std::string(64U, '0')),
             dataset("topography-primary", "topography-asset", tsunami::data::DatasetRole::topography, tsunami::data::DatasetRepresentationKind::raster, std::string(64U, '1')),
             dataset("epicentre-source-dataset", "epicentre-source-asset", tsunami::data::DatasetRole::auxiliary, tsunami::data::DatasetRepresentationKind::point_series, std::string(64U, '2')),
             dataset("target-source-dataset", "target-source-asset", tsunami::data::DatasetRole::auxiliary, tsunami::data::DatasetRepresentationKind::point_series, std::string(64U, '3'))},
            {},
            {});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto transformed_points(tsunami::geo::Coordinate3D point, std::string code = "EN-METRIC-1")
        -> tsunami::geo::TransformedPointSet
    {
        auto made = tsunami::geo::make_transformed_point_set(reference("SOURCE-" + code), target_reference(), {point});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto transformation_record(
        const tsunami::geo::TransformedPointSet &points,
        const tsunami::data::CaseConfiguration &configuration,
        std::string transformation_id,
        std::string source_dataset_id,
        std::string source_asset_id,
        std::string output_dataset_id) -> tsunami::geo::CoordinateTransformationRecord
    {
        auto record = tsunami::geo::CoordinateTransformationRecord{};
        const auto import_id = output_dataset_id + "-import";
        record.identity = tsunami::tests::r2d_fixtures::transformation_identity(
            std::move(transformation_id),
            import_id,
            std::move(source_dataset_id),
            std::move(source_asset_id),
            case_revision(configuration),
            "minimal-source-manifest",
            1U,
            std::move(output_dataset_id),
            "transform-process");
        record.source_horizontal = points.source_reference();
        record.target = points.target_reference();
        return record;
    }

    [[nodiscard]] auto corridor_fixture(
        const tsunami::data::CaseConfiguration &configuration,
        const tsunami::data::DatasetManifest &manifest) -> CorridorFixture
    {
        const auto epicentre = transformed_points({0.0, 0.0, -5.0});
        const auto target = transformed_points({20.0, 0.0, 2.0});
        const auto epicentre_record = transformation_record(
            epicentre,
            configuration,
            "epicentre-transform",
            "epicentre-source-dataset",
            "epicentre-source-asset",
            "epicentre-transformed-dataset");
        const auto target_record = transformation_record(
            target,
            configuration,
            "target-transform",
            "target-source-dataset",
            "target-source-asset",
            "target-transformed-dataset");
        auto result = tsunami::geo::construct_corridor(tsunami::geo::CorridorConstructionRequest{
            &configuration,
            &manifest,
            tsunami::geo::CorridorReferencePointRequest{
                tsunami::geo::CorridorReferencePointRole::epicentre,
                &epicentre,
                0U,
                &epicentre_record,
                "epicentre-point",
                "synthetic epicentre",
                std::string{"feature-epicentre"},
                "Synthetic file-runner fixture",
                "https://example.test/file-runner/epicentre",
                "2026-07-31T00:00:00Z"},
            tsunami::geo::CorridorReferencePointRequest{
                tsunami::geo::CorridorReferencePointRole::target,
                &target,
                0U,
                &target_record,
                "target-point",
                "synthetic target",
                std::string{"feature-target"},
                "Synthetic file-runner fixture",
                "https://example.test/file-runner/target",
                "2026-07-31T00:00:00Z"},
            tsunami::geo::CorridorConstructionIdentity{
                "corridor-file-runner",
                1U,
                case_revision(configuration),
                configuration.regional_2d().corridor.trajectory_id,
                "corridor-dataset",
                "corridor-process",
                "2026-07-31T00:00:00Z"},
            tsunami::geo::CorridorConstructionPolicy{1.0, 0.001, 0.001, 1.0e-12, 1.0e-7, 1.0e-12, "synthetic metric tolerances"}});
        REQUIRE(result.has_value());
        auto produced = std::move(result).value();
        return CorridorFixture{std::move(produced.corridor), std::move(produced.record)};
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
        auto manifest = dataset_manifest(configuration);
        std::filesystem::create_directories(fixture.root / "manifests");
        REQUIRE(tsunami::data::write_case_configuration(fixture.root / "case.json", configuration).has_value());
        REQUIRE(tsunami::data::write_dataset_manifest(fixture.root / "manifests/datasets.json", manifest).has_value());

        auto corridor = corridor_fixture(configuration, manifest);
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

    template <class Mutator>
    auto rewrite_case_configuration(const FileCaseFixture &fixture, Mutator mutate) -> void
    {
        auto parsed = tsunami::data::read_case_configuration(fixture.root / "case.json");
        REQUIRE(parsed.has_value());
        auto scenario = parsed.value().scenario();
        auto frame = parsed.value().coordinate_frame();
        auto datasets = parsed.value().datasets();
        auto regional = parsed.value().regional_2d();
        auto outputs = parsed.value().outputs();
        mutate(datasets, regional);
        auto made = tsunami::data::make_case_configuration(
            parsed.value().schema_identity(),
            parsed.value().compatibility(),
            std::string{parsed.value().policy_version()},
            parsed.value().identity(),
            scenario,
            frame,
            datasets,
            regional,
            outputs,
            parsed.value().extensions());
        REQUIRE(made.has_value());
        REQUIRE(tsunami::data::write_case_configuration(fixture.root / "case.json", made.value()).has_value());
    }

    auto add_earthquake_artifact(const FileCaseFixture &fixture) -> void
    {
        rewrite_case_configuration(fixture, [](auto &datasets, auto &regional) {
            datasets.earthquake_displacement = "tohoku-earthquake-displacement";
            regional.physics.earthquake.enabled = true;
            regional.physics.earthquake.displacement_binding = "tohoku-earthquake-displacement";
        });

        auto configuration = tsunami::data::read_case_configuration(fixture.root / "case.json");
        auto manifest = tsunami::data::read_dataset_manifest(fixture.root / "manifests/datasets.json");
        auto terrain_record = tsunami::geo::read_terrain_conditioning_record(fixture.root / fixture.terrain_record);
        REQUIRE(configuration.has_value());
        REQUIRE(manifest.has_value());
        REQUIRE(terrain_record.has_value());

        auto datasets = manifest.value().datasets();
        datasets.push_back(earthquake_dataset());
        auto processes = manifest.value().processes();
        processes.push_back(earthquake_process());
        auto made_manifest = tsunami::data::make_dataset_manifest(
            manifest.value().schema_identity(),
            manifest.value().compatibility(),
            std::string{manifest.value().policy_version()},
            manifest.value().identity(),
            manifest.value().providers(),
            manifest.value().licences(),
            std::move(datasets),
            std::move(processes),
            manifest.value().extensions());
        REQUIRE(made_manifest.has_value());
        REQUIRE(tsunami::data::write_dataset_manifest(fixture.root / "manifests/datasets.json", made_manifest.value()).has_value());

        const auto paths = tsunami::geo_gdal::EarthquakeDisplacementArtifactPaths{
            fixture.root / "inputs/data/earthquake/tohoku_vertical_displacement.tif",
            fixture.root / "inputs/data/earthquake/tohoku_vertical_displacement.json"};
        auto displacement = std::vector<double>{0.02, 0.02, 0.01, 0.01, -0.01, -0.01, 0.0, 0.0};
        auto valid = std::vector<std::uint8_t>(displacement.size(), 1U);
        REQUIRE(tsunami::geo_gdal::write_earthquake_displacement_artifact_with_gdal(
                    paths,
                    terrain_record.value().grid,
                    displacement,
                    valid,
                    tsunami::geo_gdal::EarthquakeDisplacementArtifactMetadata{
                        tsunami::geo_gdal::earthquake_displacement_artifact_contract_version,
                        configuration.value().scenario().event_id,
                        "usgs-usp000hvnu-basic-inversion",
                        "USGS finite-fault basic_inversion.param",
                        "TEST:EN-METRIC-1",
                        4U,
                        "m",
                        "https://earthquake.usgs.gov/archive/product/finite-fault/usp000hvnu/us/1539808472261/basic_inversion.param",
                        std::string(64U, '6'),
                        "2026-08-02T00:00:00Z",
                        "synthetic test fixture"})
                    .has_value());
    }

    [[nodiscard]] auto request_for(
        const FileCaseFixture &fixture,
        std::string run_id,
        std::optional<std::filesystem::path> corridor = std::nullopt,
        bool overwrite = false) -> tsunami::r2d_case::RegionalFileCaseRunRequest
    {
        auto strong_run_id = tsunami::core::RunId::from_string(std::move(run_id));
        REQUIRE(strong_run_id.has_value());
        return tsunami::r2d_case::RegionalFileCaseRunRequest{
            fixture.root,
            fixture.terrain_record,
            fixture.mesh,
            std::move(corridor),
            *std::move(strong_run_id),
            tsunami::r2d_case::RegionalFileCaseRunPolicy{
                tsunami::r2d::RegionalCasePreparationPolicy{0.0, 1.0e-6, 1.0e-9, 1.0e-12, 1.0e-12},
                tsunami::r2d::RegionalRasterCellTransferPolicy{1.0e-7, 1.0e-12, 16U}},
            tsunami::r2d::RegionalReconstructionPolicy{},
            overwrite,
            std::nullopt,
            {}};
    }

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }

    [[nodiscard]] auto terrain_artifact_paths(const FileCaseFixture &fixture)
        -> tsunami::geo_gdal::ConditionedTerrainArtifactPaths
    {
        auto record = tsunami::geo::read_terrain_conditioning_record(fixture.root / fixture.terrain_record);
        REQUIRE(record.has_value());
        auto paths = tsunami::geo_gdal::make_conditioned_terrain_artifact_paths(fixture.root, record.value());
        REQUIRE(paths.has_value());
        return paths.value();
    }

    auto require_failure(
        const tsunami::core::Result<tsunami::r2d_case::RegionalFileCaseRunResult> &result,
        std::string_view code,
        std::string_view state_changed) -> const tsunami::core::Error &
    {
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == code);
        CHECK(context_value(result.error(), "state_changed") == state_changed);
        return result.error();
    }

    [[nodiscard]] auto run_output_directory(const FileCaseFixture &fixture, std::string_view run_id) -> std::filesystem::path
    {
        return fixture.root / "runs" / std::string{run_id} / "outputs/regional2d";
    }
} // namespace

TEST_CASE("file-driven Regional2D runner produces deterministic CSV outputs", "[r2d-file-runner]")
{
    const auto fixture = make_fixture("success");

    auto first = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "run-a"));
    REQUIRE(first.has_value());
    CHECK(first.value().diagnostics.case_id == "synthetic-kamaishi-r2d");
    CHECK(first.value().diagnostics.reconstruction_scheme == "first_order");
    CHECK(first.value().diagnostics.corridor_id == "corridor-file-runner");
    CHECK(first.value().diagnostics.terrain_id == "terrain-file-runner");
    CHECK(first.value().diagnostics.mesh_id == "gmsh:regional-square.msh");
    CHECK(first.value().final_simulation_time == Approx(0.02));
    CHECK(first.value().diagnostics.solve.final_integrals.momentum_x == Approx(0.0).margin(1.0e-12));
    CHECK(first.value().diagnostics.solve.final_integrals.momentum_y == Approx(0.0).margin(1.0e-12));
    CHECK(first.value().diagnostics.maximum_final_depth_residual_m == Approx(0.0).margin(1.0e-12));
    CHECK(first.value().diagnostics.maximum_final_momentum_m2_per_s == Approx(0.0).margin(1.0e-12));
    CHECK(first.value().diagnostics.final_water_volume_residual_m3 == Approx(0.0).margin(1.0e-12));
    CHECK(std::filesystem::is_regular_file(first.value().output_artifacts.diagnostics_csv));
    CHECK(std::filesystem::is_regular_file(first.value().output_artifacts.snapshots_csv));
    const auto snapshot_rows = read_snapshot_rows(first.value().output_artifacts.snapshots_csv);
    auto final_row_count = std::size_t{0U};
    for (const auto &row : snapshot_rows) {
        if (row.time == Approx(first.value().final_simulation_time).margin(1.0e-12)) {
            ++final_row_count;
            CHECK(row.depth == Approx(3.0).margin(1.0e-12));
            CHECK(row.momentum_x == Approx(0.0).margin(1.0e-12));
            CHECK(row.momentum_y == Approx(0.0).margin(1.0e-12));
            CHECK(row.bed_elevation == Approx(-3.0).margin(1.0e-12));
            CHECK(row.free_surface_elevation == Approx(0.0).margin(1.0e-12));
        }
    }
    CHECK(final_row_count == 2U);

    auto second_request = request_for(fixture, "run-b", fixture.override_corridor);
    second_request.case_root = fixture.root.lexically_relative(std::filesystem::current_path());
    auto second = tsunami::r2d_case::run_regional_case_from_files(second_request);
    REQUIRE(second.has_value());
    CHECK(read_text(first.value().output_artifacts.diagnostics_csv) ==
          read_text(second.value().output_artifacts.diagnostics_csv));
    CHECK(read_text(first.value().output_artifacts.snapshots_csv) ==
          read_text(second.value().output_artifacts.snapshots_csv));

    auto limited_request = request_for(fixture, "run-c");
    limited_request.reconstruction.scheme = tsunami::r2d::RegionalReconstructionScheme::limited_linear;
    auto limited = tsunami::r2d_case::run_regional_case_from_files(limited_request);
    REQUIRE(limited.has_value());
    CHECK(limited.value().diagnostics.reconstruction_scheme == "limited_linear");
    std::filesystem::remove_all(fixture.root);
}

TEST_CASE("file-driven Regional2D runner initialises earthquake artifact and exports coupling section", "[r2d-file-runner]")
{
    const auto fixture = make_fixture("earthquake-coupling");
    add_earthquake_artifact(fixture);

    auto request = request_for(fixture, "quake-section");
    request.coupling_section = tsunami::coupling::RegionalCouplingSectionRequest{
        "boundary.offshore",
        "boundary.offshore"};

    auto result = tsunami::r2d_case::run_regional_case_from_files(request);
    REQUIRE(result.has_value());
    CHECK(result.value().diagnostics.earthquake_initialised);
    CHECK(result.value().diagnostics.earthquake_event_id == "synthetic-tohoku");
    CHECK(result.value().diagnostics.earthquake_model_id == "usgs-usp000hvnu-basic-inversion");
    CHECK(result.value().output_artifacts.earthquake_initialisation_csv.has_value());
    REQUIRE(result.value().output_artifacts.coupling_section.has_value());
    CHECK(std::filesystem::is_regular_file(*result.value().output_artifacts.earthquake_initialisation_csv));
    CHECK(std::filesystem::is_regular_file(result.value().output_artifacts.coupling_section->metadata_json));
    CHECK(std::filesystem::is_regular_file(result.value().output_artifacts.coupling_section->samples_csv));
    CHECK(std::filesystem::is_regular_file(result.value().output_artifacts.coupling_section->history_csv));
    REQUIRE(result.value().diagnostics.coupling_section.has_value());
    CHECK(result.value().diagnostics.coupling_section->section_id == "boundary.offshore");
    CHECK(result.value().diagnostics.coupling_section->sample_count == 1U);
    CHECK(read_text(result.value().output_artifacts.coupling_section->samples_csv).find("free_surface_elevation") != std::string::npos);
    CHECK(read_text(result.value().output_artifacts.coupling_section->history_csv).find("maximum_speed") != std::string::npos);
    std::filesystem::remove_all(fixture.root);
}

TEST_CASE("file-driven Regional2D runner rejects unsupported physics before outputs", "[r2d-file-runner]")
{
    SECTION("earthquake enabled without a generated artifact is rejected")
    {
        const auto fixture = make_fixture("unsupported-earthquake");
        rewrite_case_configuration(fixture, [](auto &datasets, auto &regional) {
            datasets.earthquake_displacement = "earthquake-displacement";
            regional.physics.earthquake.enabled = true;
            regional.physics.earthquake.displacement_binding = "earthquake-displacement";
        });

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "unsupported-earthquake"));
        require_failure(result, "r2d.file_case.manifest_read_failed", "false");
        CHECK_FALSE(std::filesystem::exists(run_output_directory(fixture, "unsupported-earthquake")));
        auto reread = tsunami::data::read_case_configuration(fixture.root / "case.json");
        REQUIRE(reread.has_value());
        CHECK(reread.value().regional_2d().physics.earthquake.enabled);
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("prescribed surface transfer is rejected")
    {
        const auto fixture = make_fixture("unsupported-prescribed-surface");
        rewrite_case_configuration(fixture, [](auto &datasets, auto &regional) {
            datasets.earthquake_displacement = "earthquake-displacement";
            datasets.prescribed_surface = "prescribed-surface";
            regional.physics.earthquake.enabled = true;
            regional.physics.earthquake.displacement_binding = "earthquake-displacement";
            regional.physics.earthquake.surface_transfer = tsunami::data::SurfaceTransfer::prescribed;
            regional.physics.earthquake.prescribed_surface_binding = "prescribed-surface";
        });

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "unsupported-prescribed"));
        require_failure(result, "r2d.file_case.unsupported_prescribed_surface_transfer", "false");
        CHECK_FALSE(std::filesystem::exists(run_output_directory(fixture, "unsupported-prescribed")));
        auto reread = tsunami::data::read_case_configuration(fixture.root / "case.json");
        REQUIRE(reread.has_value());
        CHECK(reread.value().regional_2d().physics.earthquake.surface_transfer == tsunami::data::SurfaceTransfer::prescribed);
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("dataset-backed Manning is rejected")
    {
        const auto fixture = make_fixture("unsupported-manning");
        rewrite_case_configuration(fixture, [](auto &datasets, auto &regional) {
            datasets.manning = "manning";
            regional.physics.manning = tsunami::data::ManningConfiguration{
                tsunami::data::ManningConfigurationKind::dataset,
                std::nullopt,
                std::string{"manning"}};
        });

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "unsupported-manning"));
        require_failure(result, "r2d.file_case.unsupported_manning_dataset_source", "false");
        CHECK_FALSE(std::filesystem::exists(run_output_directory(fixture, "unsupported-manning")));
        auto reread = tsunami::data::read_case_configuration(fixture.root / "case.json");
        REQUIRE(reread.has_value());
        CHECK(reread.value().regional_2d().physics.manning.kind == tsunami::data::ManningConfigurationKind::dataset);
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("dataset-backed Coriolis is rejected")
    {
        const auto fixture = make_fixture("unsupported-coriolis");
        rewrite_case_configuration(fixture, [](auto &datasets, auto &regional) {
            datasets.coriolis = "coriolis";
            regional.physics.coriolis = tsunami::data::CoriolisConfiguration{
                tsunami::data::CoriolisConfigurationKind::dataset,
                std::nullopt,
                std::string{"coriolis"}};
        });

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "unsupported-coriolis"));
        require_failure(result, "r2d.file_case.unsupported_coriolis_dataset_source", "false");
        CHECK_FALSE(std::filesystem::exists(run_output_directory(fixture, "unsupported-coriolis")));
        auto reread = tsunami::data::read_case_configuration(fixture.root / "case.json");
        REQUIRE(reread.has_value());
        CHECK(reread.value().regional_2d().physics.coriolis.kind == tsunami::data::CoriolisConfigurationKind::dataset);
        std::filesystem::remove_all(fixture.root);
    }
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

TEST_CASE("file-driven Regional2D runner enforces physical containment", "[r2d-file-runner]")
{
    SECTION("case.json symlink outside root is rejected")
    {
        const auto fixture = make_fixture("case-symlink");
        const auto outside = std::filesystem::temp_directory_path() / "tsunami-r2d-file-runner-case-outside.json";
        write_text(outside, read_text(fixture.root / "case.json"));
        std::filesystem::remove(fixture.root / "case.json");
        std::filesystem::create_symlink(outside, fixture.root / "case.json");

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "case-escape"));
        const auto &error = require_failure(result, "r2d.file_case.path_invalid", "false");
        CHECK(context_value(error, "path_failure") == "path_escape");
        CHECK_FALSE(std::filesystem::exists(fixture.root / "runs/case-escape"));
        CHECK(read_text(outside) == read_text(outside));
        std::filesystem::remove(outside);
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("terrain artefact symlink outside root is rejected")
    {
        const auto fixture = make_fixture("artifact-symlink");
        const auto paths = terrain_artifact_paths(fixture);
        const auto outside = std::filesystem::temp_directory_path() / "tsunami-r2d-file-runner-artifact-outside.tif";
        write_text(outside, "outside sentinel\n");
        std::filesystem::remove(paths.terrain_path);
        std::filesystem::create_symlink(outside, paths.terrain_path);

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "artifact-escape"));
        const auto &error = require_failure(result, "r2d.file_case.path_invalid", "false");
        CHECK(context_value(error, "path_failure") == "path_escape");
        CHECK(read_text(outside) == "outside sentinel\n");
        std::filesystem::remove(outside);
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("runs symlink outside root is rejected before output mutation")
    {
        const auto fixture = make_fixture("runs-symlink");
        const auto outside = std::filesystem::temp_directory_path() / "tsunami-r2d-file-runner-runs-outside";
        std::filesystem::remove_all(outside);
        std::filesystem::create_directories(outside);
        write_text(outside / "sentinel.txt", "preserve\n");
        std::filesystem::create_directory_symlink(outside, fixture.root / "runs");

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "runs-escape"));
        const auto &error = require_failure(result, "r2d.file_case.path_invalid", "false");
        CHECK(context_value(error, "path_failure") == "path_escape");
        CHECK(read_text(outside / "sentinel.txt") == "preserve\n");
        std::filesystem::remove_all(outside);
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("run-specific directory symlink outside root is rejected")
    {
        const auto fixture = make_fixture("run-symlink");
        const auto outside = std::filesystem::temp_directory_path() / "tsunami-r2d-file-runner-run-outside";
        std::filesystem::remove_all(outside);
        std::filesystem::create_directories(outside);
        write_text(outside / "sentinel.txt", "preserve\n");
        std::filesystem::create_directories(fixture.root / "runs");
        std::filesystem::create_directory_symlink(outside, fixture.root / "runs/run-escape");

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "run-escape"));
        const auto &error = require_failure(result, "r2d.file_case.path_invalid", "false");
        CHECK(context_value(error, "path_failure") == "path_escape");
        CHECK(read_text(outside / "sentinel.txt") == "preserve\n");
        std::filesystem::remove_all(outside);
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("runs symlink inside root is accepted when containment is proven")
    {
        const auto fixture = make_fixture("safe-symlink");
        std::filesystem::create_directories(fixture.root / "safe-runs");
        std::filesystem::create_directory_symlink(fixture.root / "safe-runs", fixture.root / "runs");

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "safe-link"));
        REQUIRE(result.has_value());
        CHECK(std::filesystem::is_regular_file(fixture.root / "safe-runs/safe-link/outputs/regional2d/diagnostics.csv"));
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("parent traversal and directory inputs are rejected")
    {
        const auto fixture = make_fixture("traversal-directory");
        auto traversal = request_for(fixture, "traversal");
        traversal.terrain_record_path = "../terrain.json";
        auto traversal_result = tsunami::r2d_case::run_regional_case_from_files(traversal);
        const auto &traversal_error = require_failure(traversal_result, "r2d.file_case.path_invalid", "false");
        CHECK(context_value(traversal_error, "path_failure") == "path_escape");

        auto directory = request_for(fixture, "directory");
        directory.mesh_path = "meshes";
        auto directory_result = tsunami::r2d_case::run_regional_case_from_files(directory);
        const auto &directory_error = require_failure(directory_result, "r2d.file_case.path_invalid", "false");
        CHECK(context_value(directory_error, "path_failure") == "non_regular_file");
        std::filesystem::remove_all(fixture.root);
    }
}

TEST_CASE("file-driven Regional2D runner binds persisted provenance to the manifest", "[r2d-file-runner]")
{
    SECTION("stale corridor transformation case revision is rejected")
    {
        const auto fixture = make_fixture("stale-corridor-case");
        const auto path = fixture.root / std::filesystem::path{"manifests/corridors/file-runner-axis.json"};
        auto record = tsunami::geo::read_corridor_construction_record(path);
        REQUIRE(record.has_value());
        record.value().epicentre.transformation_identity.case_revision.revision += 1U;
        REQUIRE(tsunami::geo::write_corridor_construction_record(path, record.value()).has_value());

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "stale-corridor-case"));
        const auto &error = require_failure(result, "r2d.file_case.contract_mismatch", "false");
        CHECK(context_value(error, "field") == "corridor.epicentre.transformation.case_revision");
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("stale terrain manifest identity is rejected")
    {
        const auto fixture = make_fixture("stale-terrain-manifest");
        const auto path = fixture.root / fixture.terrain_record;
        write_text(path, replace_all(read_text(path), "\"manifest_revision\": 1", "\"manifest_revision\": 2"));

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "stale-terrain-manifest"));
        const auto &error = require_failure(result, "r2d.file_case.contract_mismatch", "false");
        CHECK(context_value(error, "field") == "terrain.identity.manifest");
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("missing terrain source asset is rejected")
    {
        const auto fixture = make_fixture("missing-terrain-asset");
        const auto path = fixture.root / fixture.terrain_record;
        auto record = tsunami::geo::read_terrain_conditioning_record(path);
        REQUIRE(record.has_value());
        record.value().bathymetry_asset_id = "missing-bathymetry-asset";
        record.value().bathymetry_import_identity.asset_id = record.value().bathymetry_asset_id;
        record.value().bathymetry_transformation_identity.source_asset_id = record.value().bathymetry_asset_id;
        record.value().bathymetry_resampling.asset_id = record.value().bathymetry_asset_id;
        record.value().bathymetry_resampling.import_identity = record.value().bathymetry_import_identity;
        record.value().bathymetry_resampling.transformation_identity = record.value().bathymetry_transformation_identity;
        REQUIRE(tsunami::geo::write_terrain_conditioning_record(path, record.value()).has_value());

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "missing-terrain-asset"));
        const auto &error = require_failure(result, "r2d.file_case.contract_mismatch", "false");
        CHECK(context_value(error, "field") == "terrain.bathymetry");
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("wrong terrain corridor identity is rejected")
    {
        const auto fixture = make_fixture("wrong-corridor-identity");
        const auto path = fixture.root / fixture.terrain_record;
        auto record = tsunami::geo::read_terrain_conditioning_record(path);
        REQUIRE(record.has_value());
        record.value().corridor_identity.corridor_id = "other-corridor";
        REQUIRE(tsunami::geo::write_terrain_conditioning_record(path, record.value()).has_value());

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "wrong-corridor-identity"));
        const auto &error = require_failure(result, "r2d.file_case.contract_mismatch", "false");
        CHECK(context_value(error, "field") == "terrain.corridor_identity");
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("wrong terrain target reference is rejected")
    {
        const auto fixture = make_fixture("wrong-target-reference");
        const auto path = fixture.root / fixture.terrain_record;
        write_text(path, replace_all(read_text(path), "Synthetic east-north metric reference", "Other target reference"));

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "wrong-target-reference"));
        const auto &error = require_failure(result, "r2d.file_case.contract_mismatch", "false");
        CHECK(context_value(error, "field") == "terrain.target_reference");
        std::filesystem::remove_all(fixture.root);
    }
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

TEST_CASE("file-driven Regional2D runner covers request, preflight and overwrite branches", "[r2d-file-runner]")
{
    SECTION("default RunId is rejected before file access")
    {
        const auto missing_root = std::filesystem::temp_directory_path() / "tsunami-r2d-file-runner-no-such-case-root";
        std::filesystem::remove_all(missing_root);
        auto request = tsunami::r2d_case::RegionalFileCaseRunRequest{
            missing_root,
            "manifests/terrain/conditioned-terrain.json",
            "meshes/regional-square.msh",
            std::nullopt,
            tsunami::core::RunId{},
            tsunami::r2d_case::RegionalFileCaseRunPolicy{
                tsunami::r2d::RegionalCasePreparationPolicy{0.0, 1.0e-6, 1.0e-9, 1.0e-12, 1.0e-12},
                tsunami::r2d::RegionalRasterCellTransferPolicy{1.0e-7, 1.0e-12, 16U}},
            tsunami::r2d::RegionalReconstructionPolicy{},
            false,
            std::nullopt,
            {}};

        auto result = tsunami::r2d_case::run_regional_case_from_files(request);
        const auto &error = require_failure(result, "r2d.file_case.request_invalid", "false");
        CHECK(context_value(error, "stage") == "request");
        CHECK_FALSE(std::filesystem::exists(missing_root));
    }

    SECTION("missing required mesh physical group preserves the preflight cause")
    {
        const auto fixture = make_fixture("missing-physical-group");
        auto mesh = gmsh_text();
        mesh = replace_all(mesh, "$PhysicalNames\n5\n", "$PhysicalNames\n4\n");
        mesh = replace_all(mesh, "1 5 \"boundary.right_side\"\n", "");
        write_text(fixture.root / fixture.mesh, mesh);

        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "missing-physical"));
        const auto &error = require_failure(result, "r2d.file_case.preflight_failed", "false");
        REQUIRE(error.cause_code().has_value());
        CHECK(*error.cause_code() == "mesh.gmsh.physical_name_missing");
        CHECK_FALSE(std::filesystem::exists(run_output_directory(fixture, "missing-physical")));
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("overwrite enabled replaces CSV outputs without touching unrelated files")
    {
        const auto fixture = make_fixture("overwrite-enabled");
        auto first = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "replace"));
        REQUIRE(first.has_value());
        write_text(first.value().output_artifacts.diagnostics_csv, "old diagnostics\n");
        write_text(first.value().output_artifacts.snapshots_csv, "old snapshots\n");
        write_text(first.value().output_directory / "sentinel.txt", "preserve run sentinel\n");
        const auto sibling = fixture.root / "runs/sibling/outputs/regional2d";
        write_text(sibling / "sentinel.txt", "preserve sibling sentinel\n");

        auto second = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "replace", std::nullopt, true));
        REQUIRE(second.has_value());
        CHECK(read_text(second.value().output_artifacts.diagnostics_csv).find("step,start_time,end_time") != std::string::npos);
        CHECK(read_text(second.value().output_artifacts.snapshots_csv).find("step,time,cell,depth") != std::string::npos);
        CHECK(read_text(second.value().output_artifacts.diagnostics_csv) != "old diagnostics\n");
        CHECK(read_text(second.value().output_artifacts.snapshots_csv) != "old snapshots\n");
        CHECK(read_text(second.value().output_directory / "sentinel.txt") == "preserve run sentinel\n");
        CHECK(read_text(sibling / "sentinel.txt") == "preserve sibling sentinel\n");
        std::filesystem::remove_all(fixture.root);
    }
}

TEST_CASE("file-driven Regional2D runner reports output mutation state truthfully", "[r2d-file-runner]")
{
    SECTION("CSV preparation failure after directory creation reports mutation")
    {
        const auto fixture = make_fixture("prepare-mutated");
        const auto seam = ScopedEnvironmentFlag{"TSUNAMI_R2D_CSV_FAIL_PREPARE_AFTER_CREATE"};
        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "prepare-mutated"));
        require_failure(result, "r2d.file_case.output_prepare_failed", "true");
        CHECK(std::filesystem::is_directory(fixture.root / "runs/prepare-mutated/outputs/regional2d"));
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("CSV callback failure propagates through solve with writer cause")
    {
        const auto fixture = make_fixture("callback-failure");
        const auto seam = ScopedEnvironmentFlag{"TSUNAMI_R2D_CSV_FAIL_SNAPSHOT_WRITE"};
        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "callback-failure"));
        const auto &error = require_failure(result, "r2d.file_case.solve_failed", "true");
        REQUIRE(error.cause_code().has_value());
        CHECK(*error.cause_code() == "r2d.io.csv.write_failed");
        std::filesystem::remove_all(fixture.root);
    }

    SECTION("exception after output preparation reports mutation")
    {
        const auto fixture = make_fixture("exception-mutated");
        const auto seam = ScopedEnvironmentFlag{"TSUNAMI_R2D_FILE_RUNNER_THROW_AFTER_OUTPUT_PREPARE"};
        auto result = tsunami::r2d_case::run_regional_case_from_files(request_for(fixture, "exception-mutated"));
        require_failure(result, "r2d.file_case.request_invalid", "true");
        std::filesystem::remove_all(fixture.root);
    }
}
