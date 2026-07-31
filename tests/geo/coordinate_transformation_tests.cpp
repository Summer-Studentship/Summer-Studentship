#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <tsunami/data/CaseConfiguration.hpp>
#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/CoordinateTransformationPlan.hpp>
#include <tsunami/geo/CoordinateTransformationSerialisation.hpp>
#include <tsunami/geo/TransformedVector.hpp>

namespace
{
    [[nodiscard]] auto source_root() -> std::filesystem::path
    {
        auto path = std::filesystem::path{__FILE__};
        for (auto current = path.parent_path(); !current.empty(); current = current.parent_path()) {
            if (std::filesystem::exists(current / "schemas/coordinate_transformation_record/1.0.0/coordinate_transformation_record.schema.json")) {
                return current;
            }
        }
        return path.parent_path();
    }

    [[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string
    {
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    [[nodiscard]] auto reference(std::string code = "6678") -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"EPSG"},
            std::move(code),
            "JGD2011 / Japan Plane Rectangular CS X",
            std::nullopt,
            std::string{"{\"type\":\"ProjectedCRS\"}"},
            std::string{"Japanese Geodetic Datum 2011"},
            std::string{"JGD2011"},
            2011.395,
            {"Easting", "Northing"},
            {"east", "north"},
            {"metre", "metre"}};
    }

    [[nodiscard]] auto vertical_reference() -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"EPSG"},
            std::string{"6695"},
            "JGD2011 (vertical) height",
            std::nullopt,
            std::string{"{\"type\":\"VerticalCRS\"}"},
            std::string{"JGD2011 height"},
            std::string{"JGD2011"},
            2011.395,
            {"Gravity-related height"},
            {"up"},
            {"metre"}};
    }

    [[nodiscard]] auto target() -> tsunami::geo::ComputationalTargetReference
    {
        return tsunami::geo::ComputationalTargetReference{
            reference(),
            vertical_reference(),
            tsunami::geo::ComputationalAxisConvention::east_north_up,
            "m",
            std::string{"m"},
            std::string{"up"}};
    }

    [[nodiscard]] auto case_configuration() -> tsunami::data::CaseConfiguration
    {
        using namespace tsunami::data;
        auto regional = Regional2DCaseConfiguration{};
        regional.corridor = CorridorRequest{
            "kamaishi",
            CorridorOrigin{0.0, 0.0},
            0.0,
            50000.0,
            200000.0,
            10000.0,
            CorridorNarrowingConfiguration{false, std::nullopt},
            CorridorSpongeConfiguration{0.0, 0.0}};
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
            CaseIdentity{
                tsunami::core::CaseId::from_string("crs-case").value(),
                "crs-case",
                1U,
                "2026-07-31T00:00:00Z",
                "codex"},
            ScenarioConfiguration{"tohoku-crs", "tohoku-2011", "kamaishi", CaseModelFamily::regional_2d},
            CoordinateFrameConfiguration{
                "EPSG:6678",
                "EPSG:6695",
                "m",
                "m",
                HorizontalAxisOrder::east_north,
                VerticalPositiveDirection::up},
            DatasetBindings{
                std::filesystem::path{"manifests/datasets.json"},
                "bathymetry-primary",
                "topography-primary",
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {}},
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
        asset.location.managed_path = std::filesystem::path{"inputs/data/source.gpkg"};
        asset.media_type = "application/geopackage+sqlite3";
        asset.digest = ContentDigest{
            DigestAlgorithm::sha256,
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            DigestOrigin::provider_declared};
        auto dataset = DatasetRecord{};
        dataset.dataset_id = "fixture-dataset";
        dataset.origin_kind = DatasetOriginKind::source;
        dataset.representation = DatasetRepresentationKind::vector;
        dataset.roles = {DatasetRole::auxiliary};
        dataset.title = "CRS fixture";
        dataset.provider_id = "fixture-provider";
        dataset.licence_id = "fixture-licence";
        dataset.source = SourceAcquisitionRecord{"https://example.test/source", "2026-07-31T00:00:00Z", std::nullopt, std::nullopt};
        dataset.assets = {asset};
        dataset.spatial_reference = DatasetSpatialReference{
            SpatialApplicability::spatial,
            std::string{"EPSG:6668"},
            std::string{"EPSG:6695"},
            std::string{"degree"},
            std::string{"m"},
            std::string{"longitude_latitude"},
            std::string{"up"}};
        dataset.resolution.spatial = SpatialResolution{SpatialResolutionKind::nominal, 0.5, std::nullopt, std::string{"degree"}, std::nullopt};
        dataset.resolution.temporal = TemporalResolution{TemporalResolutionKind::static_dataset, std::nullopt, std::nullopt, std::nullopt};
        dataset.uncertainty = DatasetUncertainty{UncertaintyStatus::reported, {UncertaintyMeasure{"horizontal_position", 0.5, "m", std::nullopt, std::string{"fixture"}}}, std::nullopt};
        auto made = make_dataset_manifest(
            SchemaIdentity{std::string{dataset_manifest_schema_name}, supported_dataset_manifest_version},
            DatasetManifestCompatibility::exact,
            std::string{supported_dataset_manifest_policy_version},
            DatasetManifestIdentity{
                "fixture-manifest",
                1U,
                CaseRevisionRef{tsunami::core::CaseId::from_string("crs-case").value(), 1U},
                "2026-07-31T00:00:00Z",
                "codex"},
            {DatasetProvider{"fixture-provider", "Fixture Provider", std::nullopt, std::string{"https://example.test"}, {}}},
            {DatasetLicence{"fixture-licence", "Fixture Licence", "CC0-1.0", std::nullopt, std::nullopt, {}}},
            {dataset},
            {},
            {});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto import_record() -> tsunami::geo::GeospatialImportRecord
    {
        using namespace tsunami::geo;
        auto horizontal = DatumSourceEvidence{};
        horizontal.component = DatumReferenceComponent::horizontal;
        horizontal.reference_kind = DatumReferenceKind::geodetic_datum;
        horizontal.origin = DatumEvidenceOrigin::authority_registry;
        horizontal.status = DatumEvidenceStatus::authoritative_declared;
        horizontal.datum_name = "Japanese Geodetic Datum 2011";
        horizontal.datum_realisation = "JGD2011";
        horizontal.authority_name = "EPSG";
        horizontal.authority_code = "6668";
        horizontal.coordinate_epoch = "2011.395";
        horizontal.unit = "degree";
        horizontal.source_document_title = "EPSG registry";
        horizontal.source_document_uri = "https://epsg.org/crs_6668/JGD2011.html";
        horizontal.accessed_at_utc = "2026-07-31T00:00:00Z";
        auto vertical = DatumSourceEvidence{};
        vertical.component = DatumReferenceComponent::vertical;
        vertical.reference_kind = DatumReferenceKind::orthometric_height;
        vertical.origin = DatumEvidenceOrigin::authority_registry;
        vertical.status = DatumEvidenceStatus::authoritative_declared;
        vertical.datum_name = "JGD2011 height";
        vertical.datum_realisation = "JGD2011";
        vertical.authority_name = "EPSG";
        vertical.authority_code = "6695";
        vertical.unit = "m";
        vertical.positive_direction = "up";
        vertical.source_document_title = "EPSG registry";
        vertical.source_document_uri = "https://epsg.org/crs_6695/JGD2011-vertical-height.html";
        vertical.accessed_at_utc = "2026-07-31T00:00:00Z";
        auto record = GeospatialImportRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{geospatial_import_record_schema_name}, supported_geospatial_import_record_version};
        record.policy_version = supported_geospatial_import_record_policy_version;
        record.identity = GeospatialImportIdentity{
            "source-import",
            1U,
            tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string("crs-case").value(), 1U},
            "fixture-manifest",
            1U,
            "fixture-dataset",
            "asset-primary",
            "2026-07-31T00:00:00Z"};
        record.import_kind = GeospatialImportKind::vector;
        record.adapter_name = "fixture";
        record.adapter_version = "0.1";
        record.driver_short_name = "GPKG";
        record.driver_long_name = "GeoPackage";
        record.media_type = "application/geopackage+sqlite3";
        record.managed_path = "inputs/data/source.gpkg";
        record.declared_digest = tsunami::data::ContentDigest{
            tsunami::data::DigestAlgorithm::sha256,
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            tsunami::data::DigestOrigin::provider_declared};
        record.native_spatial_reference = NativeSpatialReference{
            std::string{"EPSG"},
            std::string{"6668"},
            std::string{"JGD2011 geographic 2D"},
            std::string{"Japanese Geodetic Datum 2011"},
            std::nullopt,
            {"Longitude", "Latitude"},
            {"east", "north"},
            {"degree", "degree"},
            std::string{"2011.395"}};
        record.datum_evidence = DatumEvidenceSet{horizontal, vertical};
        record.extent = BoundingBox2D{140.7, 39.9, 141.0, 40.2};
        record.vector = VectorImportSummary{"source", 1U, ImportedGeometryKind::point, 0U, 1U, record.extent};
        return record;
    }

    [[nodiscard]] auto request() -> tsunami::geo::CoordinateTransformationRequest
    {
        static const auto configuration = case_configuration();
        static const auto source_manifest = manifest();
        static const auto source_record = import_record();
        auto request = tsunami::geo::CoordinateTransformationRequest{};
        request.configuration = &configuration;
        request.manifest = &source_manifest;
        request.source_import_record = &source_record;
        request.identity = tsunami::geo::CoordinateTransformationIdentity{
            "transform-fixture",
            1U,
            tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string("crs-case").value(), 1U},
            "fixture-manifest",
            1U,
            "source-import",
            1U,
            "fixture-dataset",
            "asset-primary",
            "fixture-dataset-metric",
            "crs-transform",
            "2026-07-31T00:00:00Z"};
        request.target = target();
        request.selection_policy.area_of_interest = tsunami::geo::GeographicAreaOfInterest{140.7, 39.9, 141.0, 40.2};
        request.selection_policy.accuracy.maximum_operation_accuracy_m = 1.0;
        request.vertical = tsunami::geo::VerticalTransformationSpecification{
            true,
            {tsunami::geo::VerticalTransformationStep{
                tsunami::geo::VerticalTransformationStepKind::identity,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                "EPSG:6695",
                "EPSG:6695"}}};
        return request;
    }
}

TEST_CASE("Coordinate transformation domain validates references, policies and case target", "[geo][crs][domain]")
{
    auto area = tsunami::geo::GeographicAreaOfInterest{140.0, 39.0, 141.0, 40.0};
    REQUIRE(tsunami::geo::validate_geographic_area_of_interest(area));
    area.east_longitude_degrees = 139.0;
    REQUIRE_FALSE(tsunami::geo::validate_geographic_area_of_interest(area));

    REQUIRE(tsunami::geo::validate_coordinate_reference_descriptor(reference()));
    auto invalid_reference = reference();
    invalid_reference.axis_units.pop_back();
    REQUIRE_FALSE(tsunami::geo::validate_coordinate_reference_descriptor(invalid_reference));

    const auto configuration = case_configuration();
    REQUIRE(tsunami::geo::validate_transformation_target_for_case(target(), configuration));
    auto wrong_target = target();
    wrong_target.horizontal.authority_code = "6679";
    REQUIRE_FALSE(tsunami::geo::validate_transformation_target_for_case(wrong_target, configuration));

    auto policy = tsunami::geo::CoordinateTransformationAccuracyPolicy{};
    policy.maximum_operation_accuracy_m = 1.0;
    REQUIRE(tsunami::geo::validate_accuracy_policy(policy));
    policy.maximum_operation_accuracy_m = 0.0;
    REQUIRE_FALSE(tsunami::geo::validate_accuracy_policy(policy));
}

TEST_CASE("Coordinate transformation records serialise canonically and transactionally", "[geo][crs][record]")
{
    auto operation = tsunami::geo::CoordinateOperationRecord{};
    operation.operation_name = "JGD2011 geographic 2D to Plane Rectangular CS X";
    operation.operation_authority = "EPSG";
    operation.operation_code = "fixture";
    operation.operation_method = "Transverse Mercator";
    operation.operation_accuracy_m = 0.0;
    operation.scope = "fixture control";
    operation.area_of_use = tsunami::geo::GeographicAreaOfInterest{140.0, 39.0, 142.0, 41.0};
    operation.canonical_wkt2 = "CONVERSION[\"fixture\"]";
    operation.canonical_projjson = "{\"type\":\"Conversion\"}";
    operation.canonical_pipeline = "+proj=pipeline";
    operation.ballpark = false;
    operation.source_crs = reference("6668");
    operation.target_crs = reference();
    operation.engine_name = "fixture-engine";
    operation.engine_version = "1.0";
    operation.database_version = "fixture-db";
    auto diagnostics = tsunami::geo::CoordinateTransformationDiagnostics{
        1U,
        1U,
        0U,
        0.0,
        0.0,
        0.0,
        tsunami::geo::BoundingBox2D{140.0, 40.0, 140.0, 40.0},
        tsunami::geo::BoundingBox2D{0.0, 0.0, 0.0, 0.0},
        {"geo.crs.warning.fixture"}};
    auto record = tsunami::geo::CoordinateTransformationRecord{};
    record.schema = tsunami::data::SchemaIdentity{
        std::string{tsunami::geo::coordinate_transformation_record_schema_name},
        tsunami::geo::supported_coordinate_transformation_record_version};
    record.policy_version = tsunami::geo::supported_coordinate_transformation_record_policy_version;
    record.identity = request().identity;
    record.source_horizontal = reference("6668");
    record.source_vertical = vertical_reference();
    record.target = target();
    record.area_of_interest = tsunami::geo::GeographicAreaOfInterest{140.0, 39.0, 142.0, 41.0};
    record.horizontal_operation = operation;
    record.vertical_operation = request().vertical;
    record.storage_axes = tsunami::geo::ComputationalAxisConvention::east_north_up;
    record.source_extent = diagnostics.source_extent;
    record.target_extent = diagnostics.target_extent;
    record.diagnostics = diagnostics;
    record.warnings = {"geo.crs.warning.fixture"};

    auto bytes = tsunami::geo::serialise_coordinate_transformation_record(record);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes.value() == tsunami::geo::serialise_coordinate_transformation_record(record).value());
    REQUIRE(bytes.value().ends_with('\n'));
    REQUIRE(bytes.value().find("\"source_vertical\"") != std::string::npos);

    const auto path = std::filesystem::temp_directory_path() / "tsunami-crs-record-test" / "record.json";
    REQUIRE(tsunami::geo::write_coordinate_transformation_record(path, record));
    REQUIRE(read_text(path) == bytes.value());

    auto invalid = record;
    invalid.identity.transformation_id.clear();
    REQUIRE_FALSE(tsunami::geo::write_coordinate_transformation_record(path, invalid));
    REQUIRE(read_text(path) == bytes.value());
}

TEST_CASE("Coordinate transformation public headers keep adapter types private", "[geo][crs][contracts]")
{
    const auto include_dir = source_root() / "src/geo/include/tsunami/geo";
    const auto forbidden = std::vector<std::string>{
        "PJ",
        "PJ_CONTEXT",
        "PROJ",
        "proj_",
        "GDAL",
        "OGR",
        "OSR",
        "CPL",
        "H5",
        "QObject",
        "QString",
        "QVariant",
        "nlohmann::"};
    for (const auto &entry : std::filesystem::directory_iterator(include_dir)) {
        if (entry.path().extension() != ".hpp") {
            continue;
        }
        const auto text = read_text(entry.path());
        for (const auto &token : forbidden) {
            REQUIRE(text.find(token) == std::string::npos);
        }
    }
}
