#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <cmath>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <tsunami/data/CaseConfiguration.hpp>
#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/CoordinateTransformationSerialisation.hpp>
#include <tsunami/geo/ImportedRaster.hpp>
#include <tsunami/geo/ImportedVector.hpp>
#include <tsunami/geo_proj/ProjCoordinateTransformer.hpp>

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

    [[nodiscard]] auto reference(std::string code, std::string name) -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"EPSG"},
            std::move(code),
            std::move(name),
            std::nullopt,
            std::nullopt,
            std::string{"Japanese Geodetic Datum 2011"},
            std::string{"JGD2011"},
            2011.395,
            {"Longitude", "Latitude"},
            {"east", "north"},
            {"degree", "degree"}};
    }

    [[nodiscard]] auto target() -> tsunami::geo::ComputationalTargetReference
    {
        auto horizontal = reference("6678", "JGD2011 / Japan Plane Rectangular CS X");
        horizontal.axis_names = {"Easting", "Northing"};
        horizontal.axis_directions = {"east", "north"};
        horizontal.axis_units = {"metre", "metre"};
        auto vertical = tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"EPSG"},
            std::string{"6695"},
            "JGD2011 (vertical) height",
            std::nullopt,
            std::nullopt,
            std::string{"JGD2011 height"},
            std::string{"JGD2011"},
            2011.395,
            {"Gravity-related height"},
            {"up"},
            {"metre"}};
        return tsunami::geo::ComputationalTargetReference{
            std::move(horizontal),
            std::move(vertical),
            tsunami::geo::ComputationalAxisConvention::east_north_up,
            "m",
            std::string{"m"},
            std::string{"up"}};
    }

    [[nodiscard]] auto finnish_target() -> tsunami::geo::ComputationalTargetReference
    {
        auto horizontal = tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"EPSG"},
            std::string{"3067"},
            "EUREF-FIN / TM35FIN(E,N)",
            std::nullopt,
            std::nullopt,
            std::string{"European Terrestrial Reference System 1989"},
            std::string{"ETRS89"},
            std::nullopt,
            {"Easting", "Northing"},
            {"east", "north"},
            {"metre", "metre"}};
        return tsunami::geo::ComputationalTargetReference{
            std::move(horizontal),
            target().vertical,
            tsunami::geo::ComputationalAxisConvention::east_north_up,
            "m",
            std::string{"m"},
            std::string{"up"}};
    }

    [[nodiscard]] auto configuration(std::string horizontal_crs = "EPSG:6678") -> tsunami::data::CaseConfiguration
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
        auto made = make_case_configuration(
            SchemaIdentity{std::string{case_configuration_schema_name}, supported_case_configuration_version},
            CaseSchemaCompatibility::exact,
            std::string{supported_case_policy_version},
            CaseIdentity{tsunami::core::CaseId::from_string("crs-case").value(), "crs-case", 1U, "2026-07-31T00:00:00Z", "codex"},
            ScenarioConfiguration{"tohoku-crs", "tohoku-2011", "kamaishi", CaseModelFamily::regional_2d},
            CoordinateFrameConfiguration{std::move(horizontal_crs), "EPSG:6695", "m", "m", HorizontalAxisOrder::east_north, VerticalPositiveDirection::up},
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
        asset.location.managed_path = std::filesystem::path{"inputs/data/source.gpkg"};
        asset.media_type = "application/geopackage+sqlite3";
        asset.digest = ContentDigest{DigestAlgorithm::sha256, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", DigestOrigin::provider_declared};
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
        dataset.spatial_reference = DatasetSpatialReference{SpatialApplicability::spatial, std::string{"EPSG:6668"}, std::string{"EPSG:6695"}, std::string{"degree"}, std::string{"m"}, std::string{"longitude_latitude"}, std::string{"up"}};
        dataset.resolution.spatial = SpatialResolution{SpatialResolutionKind::nominal, 0.5, std::nullopt, std::string{"degree"}, std::nullopt};
        dataset.resolution.temporal = TemporalResolution{TemporalResolutionKind::static_dataset, std::nullopt, std::nullopt, std::nullopt};
        dataset.uncertainty = DatasetUncertainty{UncertaintyStatus::reported, {UncertaintyMeasure{"horizontal_position", 0.5, "m", std::nullopt, std::string{"fixture"}}}, std::nullopt};
        auto made = make_dataset_manifest(
            SchemaIdentity{std::string{dataset_manifest_schema_name}, supported_dataset_manifest_version},
            DatasetManifestCompatibility::exact,
            std::string{supported_dataset_manifest_policy_version},
            DatasetManifestIdentity{"fixture-manifest", 1U, CaseRevisionRef{tsunami::core::CaseId::from_string("crs-case").value(), 1U}, "2026-07-31T00:00:00Z", "codex"},
            {DatasetProvider{"fixture-provider", "Fixture Provider", std::nullopt, std::string{"https://example.test"}, {}}},
            {DatasetLicence{"fixture-licence", "Fixture Licence", "CC0-1.0", std::nullopt, std::nullopt, {}}},
            {dataset},
            {},
            {});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto import_record(std::string source_code = "6668") -> tsunami::geo::GeospatialImportRecord
    {
        using namespace tsunami::geo;
        auto horizontal = DatumSourceEvidence{};
        horizontal.component = DatumReferenceComponent::horizontal;
        horizontal.reference_kind = DatumReferenceKind::geodetic_datum;
        horizontal.origin = DatumEvidenceOrigin::authority_registry;
        horizontal.status = DatumEvidenceStatus::authoritative_declared;
        horizontal.datum_name = source_code == "6668" ? "Japanese Geodetic Datum 2011" : "Japanese Geodetic Datum 2000";
        horizontal.datum_realisation = source_code == "6668" ? "JGD2011" : "JGD2000";
        horizontal.authority_name = "EPSG";
        horizontal.authority_code = source_code;
        horizontal.coordinate_epoch = source_code == "6668" ? "2011.395" : "1997.0";
        horizontal.unit = "degree";
        horizontal.source_document_title = "EPSG registry";
        horizontal.source_document_uri = "https://epsg.org";
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
        vertical.source_document_uri = "https://epsg.org";
        vertical.accessed_at_utc = "2026-07-31T00:00:00Z";
        auto record = GeospatialImportRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{geospatial_import_record_schema_name}, supported_geospatial_import_record_version};
        record.policy_version = supported_geospatial_import_record_policy_version;
        record.identity = GeospatialImportIdentity{"source-import", 1U, tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string("crs-case").value(), 1U}, "fixture-manifest", 1U, "fixture-dataset", "asset-primary", "2026-07-31T00:00:00Z"};
        record.import_kind = GeospatialImportKind::vector;
        record.adapter_name = "fixture";
        record.adapter_version = "0.1";
        record.driver_short_name = "GPKG";
        record.driver_long_name = "GeoPackage";
        record.media_type = "application/geopackage+sqlite3";
        record.managed_path = "inputs/data/source.gpkg";
        record.declared_digest = tsunami::data::ContentDigest{tsunami::data::DigestAlgorithm::sha256, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", tsunami::data::DigestOrigin::provider_declared};
        record.native_spatial_reference = NativeSpatialReference{
            std::string{"EPSG"},
            source_code,
            source_code == "6668" ? std::string{"JGD2011 geographic 2D"} : std::string{"JGD2000 geographic 2D"},
            horizontal.datum_name,
            std::nullopt,
            {"Longitude", "Latitude"},
            {"east", "north"},
            {"degree", "degree"},
            horizontal.coordinate_epoch};
        record.datum_evidence = DatumEvidenceSet{horizontal, vertical};
        record.extent = BoundingBox2D{140.7, 39.9, 141.0, 40.2};
        record.vector = VectorImportSummary{"source", 1U, ImportedGeometryKind::point, 0U, 1U, record.extent};
        return record;
    }

    [[nodiscard]] auto request_with(
        const tsunami::geo::GeospatialImportRecord &record,
        const tsunami::data::CaseConfiguration &config,
        const tsunami::data::DatasetManifest &source_manifest,
        tsunami::geo::ComputationalTargetReference target_reference,
        tsunami::geo::GeographicAreaOfInterest area_of_interest) -> tsunami::geo::CoordinateTransformationRequest
    {
        auto request = tsunami::geo::CoordinateTransformationRequest{};
        request.configuration = &config;
        request.manifest = &source_manifest;
        request.source_import_record = &record;
        request.identity = tsunami::geo::CoordinateTransformationIdentity{"transform-fixture", 1U, tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string("crs-case").value(), 1U}, "fixture-manifest", 1U, "source-import", 1U, "fixture-dataset", "asset-primary", "fixture-dataset-metric", "crs-transform", "2026-07-31T00:00:00Z"};
        request.target = std::move(target_reference);
        request.selection_policy.area_of_interest = area_of_interest;
        request.selection_policy.accuracy.maximum_operation_accuracy_m = 1000.0;
        request.selection_policy.accuracy.require_reported_operation_accuracy = false;
        request.vertical = tsunami::geo::VerticalTransformationSpecification{true, {tsunami::geo::VerticalTransformationStep{tsunami::geo::VerticalTransformationStepKind::identity, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, "EPSG:6695", "EPSG:6695"}}};
        return request;
    }

    [[nodiscard]] auto request_with(const tsunami::geo::GeospatialImportRecord &record) -> tsunami::geo::CoordinateTransformationRequest
    {
        static const auto config = configuration();
        static const auto source_manifest = manifest();
        return request_with(
            record,
            config,
            source_manifest,
            target(),
            tsunami::geo::GeographicAreaOfInterest{140.80, 39.98, 140.90, 40.02});
    }
}

TEST_CASE("PROJ adapter exposes runtime metadata and disables network", "[geo][crs][proj]")
{
    REQUIRE_FALSE(tsunami::geo_proj::transformation_runtime_version().empty());
    REQUIRE_FALSE(tsunami::geo_proj::transformation_network_enabled_by_default());
}

TEST_CASE("PROJ point transformation passes the Zone X natural-origin control", "[geo][crs][proj][known point]")
{
    auto record = import_record();
    auto request = request_with(record);
    const auto natural_origin_lon = 140.0 + 50.0 / 60.0;
    const auto points = std::vector<tsunami::geo::Coordinate3D>{{natural_origin_lon, 40.0, 5.0}};
    auto result = tsunami::geo_proj::transform_points_with_proj(request, points);
    const auto error_code = result.has_value() ? std::string{} : result.error().code();
    const auto error_message = result.has_value() ? std::string{} : result.error().message();
    INFO(error_code);
    INFO(error_message);
    REQUIRE(result.has_value());
    REQUIRE(result.value().points.coordinates().size() == 1U);
    CHECK(result.value().points.coordinates().front().x == Catch::Approx(0.0).margin(0.001));
    CHECK(result.value().points.coordinates().front().y == Catch::Approx(0.0).margin(0.001));
    CHECK(result.value().record.horizontal_operation.ballpark == false);
    CHECK(result.value().record.storage_axes == tsunami::geo::ComputationalAxisConvention::east_north_up);

    auto round_trip = result.value().points.coordinates().front();
    CHECK(std::isfinite(round_trip.x));
    CHECK(std::isfinite(round_trip.y));
}

TEST_CASE("PROJ vector transformation preserves identity and attributes", "[geo][crs][proj][vector transform]")
{
    auto record = import_record();
    auto request = request_with(record);
    const auto lon = 140.0 + 50.0 / 60.0;
    auto feature = tsunami::geo::ImportedVectorFeature{
        7,
        tsunami::geo::Point2D{lon, 40.0},
        {tsunami::geo::ImportedAttribute{"name", std::string{"origin"}}}};
    auto source = tsunami::geo::make_imported_vector_layer(
        "controls",
        tsunami::geo::ImportedGeometryKind::point,
        tsunami::geo::BoundingBox2D{lon, 40.0, lon, 40.0},
        import_record().native_spatial_reference,
        {tsunami::geo::ImportedFieldSchema{"name", tsunami::geo::ImportedFieldType::string}},
        {feature});
    REQUIRE(source.has_value());
    auto result = tsunami::geo_proj::transform_vector_layer_with_proj(request, source.value());
    REQUIRE(result.has_value());
    REQUIRE(result.value().layer.features().front().feature_id == 7);
    REQUIRE(result.value().layer.field_schema() == source.value().field_schema());
    REQUIRE(result.value().layer.features().front().attributes == feature.attributes);
    auto point = std::get<tsunami::geo::Point2D>(result.value().layer.features().front().geometry);
    CHECK(point.x == Catch::Approx(0.0).margin(0.001));
    CHECK(point.y == Catch::Approx(0.0).margin(0.001));
}

TEST_CASE("PROJ raster transformation plans densified bounds without resampling", "[geo][crs][proj][raster plan]")
{
    auto record = import_record();
    auto request = request_with(record);
    auto band = tsunami::geo::ImportedRasterBand{};
    band.name = "z";
    band.native_type = tsunami::geo::NativeRasterDataType::float64;
    band.values = {1.0, 2.0, 3.0, 4.0};
    band.valid_mask = {1U, 1U, 1U, 1U};
    auto raster = tsunami::geo::make_imported_raster(
        2U,
        2U,
        tsunami::geo::RasterAffineTransform{140.80, 0.01, 0.0, 40.02, 0.0, -0.01},
        tsunami::geo::BoundingBox2D{140.80, 40.00, 140.82, 40.02},
        tsunami::geo::RasterCellRegistration::pixel_is_area,
        import_record().native_spatial_reference,
        std::move(band));
    REQUIRE(raster.has_value());
    auto result = tsunami::geo_proj::plan_raster_transformation_with_proj(request, raster.value());
    REQUIRE(result.has_value());
    REQUIRE(result.value().plan.source_width == 2U);
    REQUIRE(result.value().plan.source_height == 2U);
    REQUIRE(result.value().plan.boundary_densification_points == 21U);
    REQUIRE(result.value().plan.transformed_boundary.size() == 84U);
    REQUIRE(raster.value().band().values[0] == 1.0);
}

TEST_CASE("PROJ resource-dependent projected path rejects missing or unverified grids", "[geo][crs][proj][datum]")
{
    static const auto config = configuration("EPSG:3067");
    static const auto source_manifest = manifest();
    auto record = import_record("2393");
    auto request = request_with(
        record,
        config,
        source_manifest,
        finnish_target(),
        tsunami::geo::GeographicAreaOfInterest{19.0, 59.0, 32.0, 70.0});
    const auto points = std::vector<tsunami::geo::Coordinate3D>{{3380000.0, 6670000.0, 0.0}};
    auto result = tsunami::geo_proj::transform_points_with_proj(request, points);
    REQUIRE_FALSE(result.has_value());
    const auto code = result.error().code();
    INFO(code);
    INFO(result.error().message());
    REQUIRE((code == "geo.crs.resource_missing" || code == "geo.crs.resource_unverified" || code == "geo.crs.operation_not_found"));
}

TEST_CASE("PROJ adapter public header exposes no native transformation types", "[geo][crs][contracts]")
{
    const auto text = read_text(source_root() / "src/geo_proj/include/tsunami/geo_proj/ProjCoordinateTransformer.hpp");
    for (const auto token : {"PJ", "PJ_CONTEXT", "proj_create", "proj_trans"}) {
        REQUIRE(text.find(token) == std::string::npos);
    }
}
