#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <tsunami/geo/CorridorConstruction.hpp>
#include <tsunami/geo/CorridorConstructionSerialisation.hpp>

namespace
{
    [[nodiscard]] auto reference(std::string code = "EN-METRIC-1") -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"TEST"},
            std::move(code),
            "Synthetic east-north metric reference",
            std::nullopt,
            std::string{"{\"type\":\"ProjectedCRS\"}"},
            std::string{"Synthetic datum"},
            std::string{"Synthetic realisation"},
            2026.0,
            {"Easting", "Northing"},
            {"east", "north"},
            {"metre", "metre"}};
    }

    [[nodiscard]] auto target_reference(std::string code = "EN-METRIC-1") -> tsunami::geo::ComputationalTargetReference
    {
        return tsunami::geo::ComputationalTargetReference{
            reference(std::move(code)),
            std::nullopt,
            tsunami::geo::ComputationalAxisConvention::east_north,
            "m",
            std::nullopt,
            std::nullopt};
    }

    [[nodiscard]] auto case_configuration(
        tsunami::data::CorridorRequest corridor = tsunami::data::CorridorRequest{
            "axis-aligned",
            tsunami::data::CorridorOrigin{0.0, 0.0},
            90.0,
            100.0,
            200.0,
            300.0,
            tsunami::data::CorridorNarrowingConfiguration{false, std::nullopt},
            tsunami::data::CorridorSpongeConfiguration{0.0, 0.0}}) -> tsunami::data::CaseConfiguration
    {
        using namespace tsunami::data;
        auto regional = Regional2DCaseConfiguration{};
        regional.corridor = std::move(corridor);
        regional.physics = RegionalPhysicsConfiguration{};
        regional.numerics = RegionalNumericsConfiguration{
            RegionalTimeScheme::ssprk3,
            0.45,
            0.95,
            1.0,
            1.0,
            0.001,
            1.0,
            60.0,
            1000U};
        regional.boundaries = CorridorBoundaryConfiguration{};
        regional.relaxation = RelaxationConfiguration{false, std::nullopt, std::nullopt};
        auto made = make_case_configuration(
            SchemaIdentity{std::string{case_configuration_schema_name}, supported_case_configuration_version},
            CaseSchemaCompatibility::exact,
            std::string{supported_case_policy_version},
            CaseIdentity{tsunami::core::CaseId::from_string("corridor-case").value(), "corridor-case", 1U, "2026-07-31T00:00:00Z", "codex"},
            ScenarioConfiguration{"corridor-scenario", "tohoku-2011", "kamaishi", CaseModelFamily::regional_2d},
            CoordinateFrameConfiguration{"TEST:EN-METRIC-1", "none", "m", "m", HorizontalAxisOrder::east_north, VerticalPositiveDirection::up},
            DatasetBindings{std::filesystem::path{"manifests/datasets.json"}, "bathymetry-primary", "topography-primary", std::nullopt, std::nullopt, std::nullopt, std::nullopt, {}},
            regional,
            CaseOutputConfiguration{30.0, true, true, std::nullopt},
            CaseExtensions{});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto manifest() -> tsunami::data::DatasetManifest
    {
        using namespace tsunami::data;
        auto asset = DatasetAsset{};
        asset.asset_id = "asset-primary";
        asset.role = DatasetAssetRole::primary;
        asset.location.kind = DatasetLocationKind::managed_path;
        asset.location.managed_path = std::filesystem::path{"inputs/data/source.geojson"};
        asset.media_type = "application/geo+json";
        asset.digest = ContentDigest{
            DigestAlgorithm::sha256,
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            DigestOrigin::provider_declared};
        auto dataset = DatasetRecord{};
        dataset.dataset_id = "fixture-source";
        dataset.origin_kind = DatasetOriginKind::source;
        dataset.representation = DatasetRepresentationKind::vector;
        dataset.roles = {DatasetRole::auxiliary};
        dataset.title = "Synthetic corridor source";
        dataset.provider_id = "fixture-provider";
        dataset.licence_id = "fixture-licence";
        dataset.source = SourceAcquisitionRecord{"https://example.test/source", "2026-07-31T00:00:00Z", std::nullopt, std::nullopt};
        dataset.assets = {asset};
        dataset.spatial_reference = DatasetSpatialReference{
            SpatialApplicability::spatial,
            std::string{"TEST:SOURCE"},
            std::nullopt,
            std::string{"m"},
            std::nullopt,
            std::string{"east_north"},
            std::nullopt};
        dataset.resolution.spatial = SpatialResolution{SpatialResolutionKind::nominal, 1.0, 1.0, std::string{"m"}, std::nullopt};
        dataset.resolution.temporal = TemporalResolution{TemporalResolutionKind::static_dataset, std::nullopt, std::nullopt, std::nullopt};
        dataset.uncertainty = DatasetUncertainty{UncertaintyStatus::not_reported, {}, std::nullopt};
        auto made = make_dataset_manifest(
            SchemaIdentity{std::string{dataset_manifest_schema_name}, supported_dataset_manifest_version},
            DatasetManifestCompatibility::exact,
            std::string{supported_dataset_manifest_policy_version},
            DatasetManifestIdentity{"corridor-manifest", 1U, CaseRevisionRef{tsunami::core::CaseId::from_string("corridor-case").value(), 1U}, "2026-07-31T00:00:00Z", "codex"},
            {DatasetProvider{"fixture-provider", "Fixture Provider", std::nullopt, std::string{"https://example.test"}, {}}},
            {DatasetLicence{"fixture-licence", "Fixture Licence", "CC0-1.0", std::nullopt, std::nullopt, {}}},
            {dataset},
            {},
            {});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto transformed_points(tsunami::geo::Coordinate3D point, std::string code = "EN-METRIC-1")
        -> tsunami::geo::TransformedPointSet
    {
        auto made = tsunami::geo::make_transformed_point_set(reference("SOURCE-" + code), target_reference(std::move(code)), {point});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto transformation_record(
        const tsunami::geo::TransformedPointSet &points,
        std::string transformation_id,
        std::string output_dataset_id) -> tsunami::geo::CoordinateTransformationRecord
    {
        auto record = tsunami::geo::CoordinateTransformationRecord{};
        record.identity = tsunami::geo::CoordinateTransformationIdentity{
            std::move(transformation_id),
            1U,
            tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string("corridor-case").value(), 1U},
            "corridor-manifest",
            1U,
            "source-import",
            1U,
            "source-" + output_dataset_id,
            "asset-" + output_dataset_id,
            std::move(output_dataset_id),
            "transform-process",
            "2026-07-31T00:00:00Z"};
        record.source_horizontal = points.source_reference();
        record.target = points.target_reference();
        return record;
    }

    [[nodiscard]] auto policy() -> tsunami::geo::CorridorConstructionPolicy
    {
        return tsunami::geo::CorridorConstructionPolicy{1.0, 0.001, 0.001, 1.0e-12, 1.0e-7, 1.0e-12, "unit-test metric tolerances"};
    }

    [[nodiscard]] auto identity(std::string trajectory_id = "axis-aligned") -> tsunami::geo::CorridorConstructionIdentity
    {
        return tsunami::geo::CorridorConstructionIdentity{
            "corridor-" + trajectory_id,
            1U,
            tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string("corridor-case").value(), 1U},
            std::move(trajectory_id),
            "corridor-dataset",
            "corridor-process",
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard]] auto request(
        const tsunami::data::CaseConfiguration &configuration,
        const tsunami::data::DatasetManifest &datasets,
        const tsunami::geo::TransformedPointSet &epicentre_points,
        const tsunami::geo::CoordinateTransformationRecord &epicentre_record,
        const tsunami::geo::TransformedPointSet &target_points,
        const tsunami::geo::CoordinateTransformationRecord &target_record,
        std::string trajectory_id = "axis-aligned") -> tsunami::geo::CorridorConstructionRequest
    {
        return tsunami::geo::CorridorConstructionRequest{
            &configuration,
            &datasets,
            tsunami::geo::CorridorReferencePointRequest{
                tsunami::geo::CorridorReferencePointRole::epicentre,
                &epicentre_points,
                0U,
                &epicentre_record,
                "epicentre-point",
                "illustrative non-authoritative transformed epicentre reference point",
                std::string{"feature-epicentre"},
                "Synthetic corridor fixture",
                "https://example.test/corridor/epicentre",
                "2026-07-31T00:00:00Z"},
            tsunami::geo::CorridorReferencePointRequest{
                tsunami::geo::CorridorReferencePointRole::target,
                &target_points,
                0U,
                &target_record,
                "target-point",
                "illustrative non-authoritative transformed target reference point",
                std::string{"feature-target"},
                "Synthetic corridor fixture",
                "https://example.test/corridor/target",
                "2026-07-31T00:00:00Z"},
            identity(std::move(trajectory_id)),
            policy()};
    }

    [[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string
    {
        auto file = std::ifstream{path, std::ios::binary};
        REQUIRE(file);
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }
}

TEST_CASE("corridor local basis uses east-north evidence and clockwise-from-north bearings", "[geo][corridor][basis][bearing]")
{
    const auto p = policy();
    const auto east = tsunami::geo::make_corridor_local_basis({0.0, 0.0}, {10.0, 0.0}, p).value();
    CHECK(east.tangent == tsunami::geo::Point2D{1.0, 0.0});
    CHECK(east.left_normal == tsunami::geo::Point2D{-0.0, 1.0});
    CHECK(east.derived_bearing_degrees_clockwise_from_north == 90.0);

    const auto north = tsunami::geo::make_corridor_local_basis({0.0, 0.0}, {0.0, 5.0}, p).value();
    CHECK(north.derived_bearing_degrees_clockwise_from_north == 0.0);

    const auto southwest = tsunami::geo::make_corridor_local_basis({0.0, 0.0}, {-3.0, -4.0}, p).value();
    CHECK(southwest.epicentre_target_distance_m == 5.0);
    CHECK(southwest.derived_bearing_degrees_clockwise_from_north == 216.86989764584402);

    const auto rotated = tsunami::geo::make_corridor_local_basis({1000.0, 2000.0}, {4000.0, 6000.0}, p).value();
    CHECK(rotated.epicentre_target_distance_m == 5000.0);
    CHECK(std::abs(rotated.derived_bearing_degrees_clockwise_from_north - 36.86989764584402) < 1.0e-12);
    CHECK(std::abs(std::hypot(rotated.tangent.x, rotated.tangent.y) - 1.0) < 1.0e-12);
    CHECK(std::abs(std::hypot(rotated.left_normal.x, rotated.left_normal.y) - 1.0) < 1.0e-12);
    CHECK(std::abs((rotated.tangent.x * rotated.left_normal.x) + (rotated.tangent.y * rotated.left_normal.y)) < 1.0e-12);
    CHECK(std::abs((rotated.tangent.x * rotated.left_normal.y) - (rotated.tangent.y * rotated.left_normal.x) - 1.0) < 1.0e-12);

    const auto local = tsunami::geo::to_corridor_local_coordinates({4000.0, 6000.0}, {1000.0, 2000.0}, rotated);
    CHECK(local.x == 5000.0);
    CHECK(std::abs(local.y) < 1.0e-12);
    const auto global = tsunami::geo::from_corridor_local_coordinates(local, {1000.0, 2000.0}, rotated);
    CHECK(std::abs(global.x - 4000.0) < 1.0e-9);
    CHECK(std::abs(global.y - 6000.0) < 1.0e-9);
    CHECK(std::abs(tsunami::geo::circular_bearing_residual_degrees(359.9, 0.1) - 0.2) < 1.0e-12);
}

TEST_CASE("corridor construction validates provenance origin bearing and constant-width geometry", "[geo][corridor][provenance][origin][polygon]")
{
    const auto config = case_configuration();
    const auto datasets = manifest();
    const auto ep = transformed_points({0.0, 0.0, -10.0});
    const auto tg = transformed_points({1000.0, 0.0, 5.0});
    const auto ep_record = transformation_record(ep, "epicentre-transform", "epicentre-transformed");
    const auto tg_record = transformation_record(tg, "target-transform", "target-transformed");
    auto result = tsunami::geo::construct_corridor(request(config, datasets, ep, ep_record, tg, tg_record));
    REQUIRE(result.has_value());
    const auto &corridor = result.value().corridor;
    REQUIRE(corridor.polygon().exterior_ring.size() == 5U);
    CHECK(corridor.polygon().exterior_ring[0] == tsunami::geo::Point2D{-200.0, 50.0});
    CHECK(corridor.polygon().exterior_ring[1] == tsunami::geo::Point2D{-200.0, -50.0});
    CHECK(corridor.polygon().exterior_ring[2] == tsunami::geo::Point2D{1300.0, -50.0});
    CHECK(corridor.polygon().exterior_ring[3] == tsunami::geo::Point2D{1300.0, 50.0});
    CHECK(corridor.polygon().exterior_ring.front() == corridor.polygon().exterior_ring.back());
    CHECK(corridor.extent() == tsunami::geo::BoundingBox2D{-200.0, -50.0, 1300.0, 50.0});
    CHECK(corridor.area_m2() == 150000.0);
    CHECK(corridor.perimeter_m() == 3200.0);
    CHECK(result.value().record.epicentre.coordinate.z == -10.0);
    CHECK(result.value().record.target.transformation_identity.source_asset_id == "asset-target-transformed");

    auto origin_bad = case_configuration(tsunami::data::CorridorRequest{"axis-aligned", {2.0, 0.0}, 90.0, 100.0, 200.0, 300.0, {false, std::nullopt}, {0.0, 0.0}});
    CHECK_FALSE(tsunami::geo::construct_corridor(request(origin_bad, datasets, ep, ep_record, tg, tg_record)).has_value());
    auto bearing_bad = case_configuration(tsunami::data::CorridorRequest{"axis-aligned", {0.0, 0.0}, 91.0, 100.0, 200.0, 300.0, {false, std::nullopt}, {0.0, 0.0}});
    auto bearing_failure = tsunami::geo::construct_corridor(request(bearing_bad, datasets, ep, ep_record, tg, tg_record));
    REQUIRE_FALSE(bearing_failure.has_value());
    CHECK(bearing_failure.error().code() == "geo.corridor.bearing_mismatch");
}

TEST_CASE("corridor narrowing and sponge limits follow configured stations without changing geometry", "[geo][corridor][narrowing][sponge]")
{
    const auto config = case_configuration(tsunami::data::CorridorRequest{
        "narrowed",
        {0.0, 0.0},
        90.0,
        100.0,
        200.0,
        0.0,
        {true, 40.0},
        {10.0, 5.0}});
    const auto datasets = manifest();
    const auto ep = transformed_points({0.0, 0.0, 0.0});
    const auto tg = transformed_points({1000.0, 0.0, 0.0});
    const auto ep_record = transformation_record(ep, "epicentre-transform", "epicentre-transformed");
    const auto tg_record = transformation_record(tg, "target-transform", "target-transformed");
    auto result = tsunami::geo::construct_corridor(request(config, datasets, ep, ep_record, tg, tg_record, "narrowed"));
    REQUIRE(result.has_value());
    const auto &corridor = result.value().corridor;
    REQUIRE(corridor.polygon().exterior_ring.size() == 7U);
    CHECK(corridor.polygon().exterior_ring[0] == tsunami::geo::Point2D{-200.0, 50.0});
    CHECK(corridor.polygon().exterior_ring[2] == tsunami::geo::Point2D{0.0, -50.0});
    CHECK(corridor.polygon().exterior_ring[3] == tsunami::geo::Point2D{1000.0, -20.0});
    CHECK(corridor.polygon().exterior_ring[4] == tsunami::geo::Point2D{1000.0, 20.0});
    CHECK(corridor.area_m2() == 90000.0);
    CHECK(std::abs(corridor.perimeter_m() - 2540.8997975910737) < 1.0e-9);
    CHECK(corridor.sponge_limits().offshore_start_xi_m == -200.0);
    CHECK(corridor.sponge_limits().offshore_end_xi_m == -190.0);
    CHECK(corridor.sponge_limits().minimum_unsponge_width_m == 30.0);

    auto equal_width = case_configuration(tsunami::data::CorridorRequest{"narrowed", {0.0, 0.0}, 90.0, 100.0, 200.0, 0.0, {true, 100.0}, {0.0, 0.0}});
    auto invalid = tsunami::geo::construct_corridor(request(equal_width, datasets, ep, ep_record, tg, tg_record, "narrowed"));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().code() == "geo.corridor.narrowing_invalid");
}

TEST_CASE("corridor records serialise deterministically and write transactionally", "[geo][corridor][record]")
{
    const auto config = case_configuration();
    const auto datasets = manifest();
    const auto ep = transformed_points({0.0, 0.0, 0.0});
    const auto tg = transformed_points({1000.0, 0.0, 0.0});
    const auto ep_record = transformation_record(ep, "epicentre-transform", "epicentre-transformed");
    const auto tg_record = transformation_record(tg, "target-transform", "target-transformed");
    auto result = tsunami::geo::construct_corridor(request(config, datasets, ep, ep_record, tg, tg_record));
    REQUIRE(result.has_value());
    auto bytes = tsunami::geo::serialise_corridor_construction_record(result.value().record);
    auto again = tsunami::geo::serialise_corridor_construction_record(result.value().record);
    REQUIRE(bytes.has_value());
    REQUIRE(again.has_value());
    CHECK(bytes.value() == again.value());
    CHECK(bytes.value().back() == '\n');
    CHECK(bytes.value().find("\r") == std::string::npos);
    CHECK(bytes.value().find("\"source_feature_id\": \"feature-epicentre\"") != std::string::npos);
    CHECK(tsunami::geo::default_corridor_construction_record_path("axis-aligned") == std::filesystem::path{"manifests/corridors/axis-aligned.json"});

    const auto directory = std::filesystem::temp_directory_path() / "tsunami-corridor-record-tests";
    std::filesystem::create_directories(directory);
    const auto path = directory / "record.json";
    REQUIRE(tsunami::geo::write_corridor_construction_record(path, result.value().record).has_value());
    CHECK(read_text(path) == bytes.value());
    auto invalid_record = result.value().record;
    invalid_record.identity.corridor_id.clear();
    auto failed = tsunami::geo::write_corridor_construction_record(path, invalid_record);
    REQUIRE_FALSE(failed.has_value());
    CHECK(read_text(path) == bytes.value());
}

TEST_CASE("corridor public headers avoid adapter and UI dependencies", "[geo][corridor][contracts]")
{
    const auto root = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path();
    const auto headers = {
        root / "src/geo/include/tsunami/geo/ConstructedCorridor.hpp",
        root / "src/geo/include/tsunami/geo/CorridorConstruction.hpp",
        root / "src/geo/include/tsunami/geo/CorridorConstructionRecord.hpp",
        root / "src/geo/include/tsunami/geo/CorridorConstructionSerialisation.hpp"};
    for (const auto &header : headers) {
        const auto text = read_text(header);
        CHECK(text.find("GDAL") == std::string::npos);
        CHECK(text.find("PROJ") == std::string::npos);
        CHECK(text.find("nlohmann::") == std::string::npos);
        CHECK(text.find("QString") == std::string::npos);
        CHECK(text.find("H5") == std::string::npos);
    }
}
