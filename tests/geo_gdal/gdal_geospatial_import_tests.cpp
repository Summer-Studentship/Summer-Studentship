#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <tsunami/core/Identity.hpp>
#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/GeospatialImport.hpp>
#include <tsunami/geo/GeospatialImportSerialisation.hpp>
#include <tsunami/geo_gdal/GdalGeospatialImporter.hpp>

namespace
{
    [[nodiscard]] auto source_root() -> std::filesystem::path
    {
        auto path = std::filesystem::path{__FILE__};
        for (auto current = path.parent_path(); !current.empty(); current = current.parent_path()) {
            if (std::filesystem::exists(current / "schemas/geospatial_import_record/1.0.0/geospatial_import_record.schema.json")) {
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

    [[nodiscard]] auto temp_case_root(std::string_view name) -> std::filesystem::path
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        auto root = std::filesystem::temp_directory_path() / ("tsunami-geo-import-" + std::string{name} + "-" + std::to_string(now));
        std::filesystem::create_directories(root / "inputs/data");
        return root;
    }

    auto assign_epsg_4326(GDALDataset &dataset) -> void
    {
        auto srs = OGRSpatialReference{};
        REQUIRE(srs.SetFromUserInput("EPSG:4326") == OGRERR_NONE);
        char *wkt = nullptr;
        REQUIRE(srs.exportToWkt(&wkt) == OGRERR_NONE);
        REQUIRE(dataset.SetProjection(wkt) == CE_None);
        CPLFree(wkt);
    }

    auto create_geotiff(
        const std::filesystem::path &path,
        int width,
        int height,
        GDALDataType type,
        const std::vector<double> &values,
        const std::array<double, 6> &transform,
        std::optional<double> nodata = std::nullopt,
        int bands = 1) -> void
    {
        GDALAllRegister();
        auto *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        REQUIRE(driver != nullptr);
        std::filesystem::create_directories(path.parent_path());
        std::filesystem::remove(path);
        auto *raw = driver->Create(path.c_str(), width, height, bands, type, nullptr);
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};
        auto mutable_transform = transform;
        REQUIRE(dataset->SetGeoTransform(mutable_transform.data()) == CE_None);
        assign_epsg_4326(*dataset);
        REQUIRE(dataset->SetMetadataItem(GDALMD_AREA_OR_POINT, GDALMD_AOP_AREA) == CE_None);
        auto *band = dataset->GetRasterBand(1);
        REQUIRE(band != nullptr);
        if (nodata) {
            REQUIRE(band->SetNoDataValue(*nodata) == CE_None);
        }
        REQUIRE(band->SetScale(2.0) == CE_None);
        REQUIRE(band->SetOffset(1.0) == CE_None);
        auto copy = values;
        REQUIRE(band->RasterIO(GF_Write, 0, 0, width, height, copy.data(), width, height, GDT_Float64, 0, 0) == CE_None);
    }

    [[nodiscard]] auto evidence(tsunami::geo::DatumEvidenceStatus status = tsunami::geo::DatumEvidenceStatus::authoritative_declared)
        -> tsunami::geo::DatumEvidenceSet
    {
        using namespace tsunami::geo;
        auto horizontal = DatumSourceEvidence{};
        horizontal.component = DatumReferenceComponent::horizontal;
        horizontal.reference_kind = DatumReferenceKind::geodetic_datum;
        horizontal.origin = DatumEvidenceOrigin::authority_registry;
        horizontal.status = status;
        horizontal.datum_name = "WGS 84";
        horizontal.datum_realisation = "EPSG ensemble";
        horizontal.authority_name = "EPSG";
        horizontal.authority_code = "4326";
        horizontal.unit = "degree";
        horizontal.source_document_title = "EPSG registry";
        horizontal.source_document_uri = "https://epsg.org/crs_4326/WGS-84.html";
        horizontal.accessed_at_utc = "2026-07-30T00:00:00Z";

        auto vertical = DatumSourceEvidence{};
        vertical.component = DatumReferenceComponent::vertical;
        vertical.reference_kind = DatumReferenceKind::mean_sea_level;
        vertical.origin = DatumEvidenceOrigin::manifest_declaration;
        vertical.status = tsunami::geo::DatumEvidenceStatus::dataset_declared;
        vertical.datum_name = "mean_sea_level";
        vertical.unit = "m";
        vertical.positive_direction = "up";
        vertical.source_document_title = "Fixture datum register";
        vertical.source_document_uri = "https://example.test/tsunami/fixture-datum-register";
        vertical.accessed_at_utc = "2026-07-30T00:00:00Z";

        return DatumEvidenceSet{std::move(horizontal), std::move(vertical)};
    }

    [[nodiscard]] auto manifest_for(
        std::filesystem::path managed_path,
        tsunami::data::DatasetRepresentationKind representation,
        std::string media_type,
        std::vector<tsunami::data::DatasetRole> roles = {tsunami::data::DatasetRole::bathymetry},
        std::string horizontal_crs = "EPSG:4326",
        std::string vertical_datum = "mean_sea_level",
        std::string vertical_positive = "up") -> tsunami::data::DatasetManifest
    {
        using namespace tsunami::data;
        auto asset = DatasetAsset{};
        asset.asset_id = "asset-primary";
        asset.role = DatasetAssetRole::primary;
        asset.location.kind = DatasetLocationKind::managed_path;
        asset.location.managed_path = std::move(managed_path);
        asset.media_type = std::move(media_type);
        asset.digest = ContentDigest{
            DigestAlgorithm::sha256,
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            DigestOrigin::provider_declared};

        auto dataset = DatasetRecord{};
        dataset.dataset_id = "fixture-dataset";
        dataset.origin_kind = DatasetOriginKind::source;
        dataset.representation = representation;
        dataset.roles = std::move(roles);
        dataset.title = "Generated geospatial import fixture";
        dataset.provider_id = "fixture-provider";
        dataset.licence_id = "fixture-licence";
        dataset.source = SourceAcquisitionRecord{
            "https://example.test/tsunami/geospatial-fixture",
            "2026-07-30T00:00:00Z",
            std::nullopt,
            std::string{"2026-07-30"}};
        dataset.assets = {std::move(asset)};
        dataset.spatial_reference = DatasetSpatialReference{
            SpatialApplicability::spatial,
            std::move(horizontal_crs),
            std::move(vertical_datum),
            std::string{"degree"},
            std::string{"m"},
            std::string{"x_y"},
            std::move(vertical_positive)};
        dataset.resolution.spatial = SpatialResolution{SpatialResolutionKind::grid_spacing, 0.5, 0.5, std::string{"degree"}, std::nullopt};
        dataset.resolution.temporal = TemporalResolution{TemporalResolutionKind::static_dataset, std::nullopt, std::nullopt, std::nullopt};
        dataset.uncertainty = DatasetUncertainty{UncertaintyStatus::not_reported, {}, std::nullopt};

        auto manifest = make_dataset_manifest(
            SchemaIdentity{std::string{dataset_manifest_schema_name}, supported_dataset_manifest_version},
            DatasetManifestCompatibility::exact,
            std::string{supported_dataset_manifest_policy_version},
            DatasetManifestIdentity{
                "fixture-manifest",
                1U,
                CaseRevisionRef{tsunami::core::CaseId::from_string("geo-import-case").value(), 3U},
                "2026-07-30T00:00:00Z",
                "codex"},
            {DatasetProvider{"fixture-provider", "Fixture Provider", std::nullopt, std::string{"https://example.test"}, {}}},
            {DatasetLicence{"fixture-licence", "Fixture Licence", "CC0-1.0", std::nullopt, std::nullopt, {}}},
            {std::move(dataset)},
            {},
            {});
        REQUIRE(manifest.has_value());
        return std::move(manifest.value());
    }

    [[nodiscard]] auto request_for(
        const tsunami::data::DatasetManifest &manifest,
        const std::filesystem::path &case_root,
        tsunami::geo::DatumEvidenceSet datum_evidence = evidence()) -> tsunami::geo::GeospatialImportRequest
    {
        auto request = tsunami::geo::GeospatialImportRequest{};
        request.manifest = &manifest;
        request.dataset_id = "fixture-dataset";
        request.case_root = case_root;
        request.datum_evidence = std::move(datum_evidence);
        request.executed_at_utc = "2026-07-30T01:02:03Z";
        request.import_id = "g1-import";
        request.import_revision = 1U;
        return request;
    }

    auto create_point_gpkg(const std::filesystem::path &path, bool extra_layer = false) -> void
    {
        GDALAllRegister();
        auto *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        REQUIRE(driver != nullptr);
        std::filesystem::create_directories(path.parent_path());
        std::filesystem::remove(path);
        auto *raw = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};

        auto srs = OGRSpatialReference{};
        REQUIRE(srs.SetFromUserInput("EPSG:4326") == OGRERR_NONE);
        auto *layer = dataset->CreateLayer("samples", &srs, wkbPoint, nullptr);
        REQUIRE(layer != nullptr);
        auto name_field = OGRFieldDefn{"name", OFTString};
        auto rank_field = OGRFieldDefn{"rank", OFTInteger};
        auto wet_field = OGRFieldDefn{"wet", OFTInteger};
        wet_field.SetSubType(OFSTBoolean);
        REQUIRE(layer->CreateField(&name_field) == OGRERR_NONE);
        REQUIRE(layer->CreateField(&rank_field) == OGRERR_NONE);
        REQUIRE(layer->CreateField(&wet_field) == OGRERR_NONE);

        auto *feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
        REQUIRE(feature != nullptr);
        feature->SetFID(7);
        feature->SetField("name", "marker");
        feature->SetField("rank", 4);
        feature->SetField("wet", 1);
        auto point = OGRPoint{12.25, 55.5};
        REQUIRE(feature->SetGeometry(&point) == OGRERR_NONE);
        REQUIRE(layer->CreateFeature(feature) == OGRERR_NONE);
        OGRFeature::DestroyFeature(feature);

        if (extra_layer) {
            REQUIRE(dataset->CreateLayer("other", &srs, wkbPoint, nullptr) != nullptr);
        }
    }

    auto create_polygon_gpkg(const std::filesystem::path &path) -> void
    {
        GDALAllRegister();
        auto *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        REQUIRE(driver != nullptr);
        std::filesystem::create_directories(path.parent_path());
        std::filesystem::remove(path);
        auto *raw = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};
        auto srs = OGRSpatialReference{};
        REQUIRE(srs.SetFromUserInput("EPSG:4326") == OGRERR_NONE);
        auto *layer = dataset->CreateLayer("footprints", &srs, wkbPolygon, nullptr);
        REQUIRE(layer != nullptr);
        auto *feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
        REQUIRE(feature != nullptr);
        auto ring = OGRLinearRing{};
        ring.addPoint(0.0, 0.0);
        ring.addPoint(2.0, 0.0);
        ring.addPoint(2.0, 1.0);
        ring.addPoint(0.0, 0.0);
        auto polygon = OGRPolygon{};
        polygon.addRing(&ring);
        REQUIRE(feature->SetGeometry(&polygon) == OGRERR_NONE);
        REQUIRE(layer->CreateFeature(feature) == OGRERR_NONE);
        OGRFeature::DestroyFeature(feature);
    }

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }
}

TEST_CASE("GDAL geospatial drivers are available in the optional preset", "[geo][gdal][drivers]")
{
    REQUIRE_FALSE(tsunami::geo_gdal::gdal_runtime_version().empty());
    REQUIRE(tsunami::geo_gdal::gdal_driver_available("GTiff"));
    REQUIRE(tsunami::geo_gdal::gdal_driver_available("GPKG"));
}

TEST_CASE("GeoTIFF import preserves native raster values, affine metadata and import record", "[geo][gdal][raster]")
{
    const auto root = temp_case_root("raster");
    const auto relative = std::filesystem::path{"inputs/data/raster/sample.tif"};
    create_geotiff(root / relative, 3, 2, GDT_Float32, {1.0, -9999.0, 3.0, 4.0, 5.0, 6.0}, {10.0, 0.5, 0.0, 20.0, 0.0, -0.25}, -9999.0);
    const auto manifest = manifest_for(relative, tsunami::data::DatasetRepresentationKind::raster, "image/tiff");

    auto imported = tsunami::geo_gdal::import_geotiff_with_gdal(request_for(manifest, root));
    REQUIRE(imported.has_value());
    const auto &raster = imported.value().raster;
    REQUIRE(raster.width() == 3U);
    REQUIRE(raster.height() == 2U);
    REQUIRE(raster.band().native_type == tsunami::geo::NativeRasterDataType::float32);
    REQUIRE(raster.band().values == std::vector<double>{1.0, -9999.0, 3.0, 4.0, 5.0, 6.0});
    REQUIRE(raster.band().valid_mask == std::vector<std::uint8_t>{1U, 0U, 1U, 1U, 1U, 1U});
    REQUIRE(raster.transform().origin_x == Catch::Approx(10.0));
    REQUIRE(raster.transform().pixel_width == Catch::Approx(0.5));
    REQUIRE(raster.extent().minimum_x == Catch::Approx(10.0));
    REQUIRE(raster.extent().maximum_x == Catch::Approx(11.5));
    REQUIRE(raster.extent().minimum_y == Catch::Approx(19.5));
    REQUIRE(raster.extent().maximum_y == Catch::Approx(20.0));
    REQUIRE(raster.registration() == tsunami::geo::RasterCellRegistration::pixel_is_area);
    REQUIRE(raster.spatial_reference().authority_name == "EPSG");
    REQUIRE(raster.spatial_reference().authority_code == "4326");

    const auto &record = imported.value().record;
    REQUIRE(record.import_kind == tsunami::geo::GeospatialImportKind::raster);
    REQUIRE(record.driver_short_name == "GTiff");
    REQUIRE(record.digest_verification_status == "not_verified");
    REQUIRE(record.identity.case_revision.revision == 3U);
    REQUIRE(record.raster.has_value());
    REQUIRE_FALSE(record.vector.has_value());
    REQUIRE(record.raster->cell_count == 6U);
    REQUIRE(record.raster->has_nodata);
    REQUIRE(record.raster->nodata_value == -9999.0);
    REQUIRE(record.warnings.size() == 1U);
    REQUIRE(record.warnings.front().code == "geo.import.warning.byte_size_missing");

    auto bytes = tsunami::geo::serialise_geospatial_import_record(record);
    REQUIRE(bytes.has_value());
    REQUIRE(bytes.value().find("tsunami.geospatial_import_record") != std::string::npos);
    REQUIRE(bytes.value().find("\"digest_verification_status\": \"not_verified\"") != std::string::npos);
    REQUIRE(bytes.value().back() == '\n');
    const auto record_path = root / tsunami::geo::default_geospatial_import_record_path(record.identity.dataset_id, record.identity.asset_id);
    REQUIRE(tsunami::geo::write_geospatial_import_record(record_path, record).has_value());
    REQUIRE(read_text(record_path) == bytes.value());
}

TEST_CASE("GeoTIFF import uses all transformed corners for rotated extents", "[geo][gdal][raster]")
{
    const auto root = temp_case_root("rotated-raster");
    const auto relative = std::filesystem::path{"inputs/data/raster/rotated.tiff"};
    create_geotiff(root / relative, 2, 2, GDT_Float64, {1.0, 2.0, 3.0, 4.0}, {100.0, 2.0, 0.5, 200.0, -0.25, -1.0});
    const auto manifest = manifest_for(relative, tsunami::data::DatasetRepresentationKind::raster, "image/geotiff");

    auto imported = tsunami::geo_gdal::import_geotiff_with_gdal(request_for(manifest, root));
    REQUIRE(imported.has_value());
    REQUIRE(imported.value().raster.extent().minimum_x == Catch::Approx(100.0));
    REQUIRE(imported.value().raster.extent().maximum_x == Catch::Approx(105.0));
    REQUIRE(imported.value().raster.extent().minimum_y == Catch::Approx(197.5));
    REQUIRE(imported.value().raster.extent().maximum_y == Catch::Approx(200.0));
}

TEST_CASE("GeoTIFF import rejects unsupported rasters and conflicting datum evidence", "[geo][gdal][raster][validation]")
{
    const auto root = temp_case_root("raster-reject");
    const auto relative = std::filesystem::path{"inputs/data/raster/multiband.tif"};
    create_geotiff(root / relative, 2, 2, GDT_Float64, {1.0, 2.0, 3.0, 4.0}, {0.0, 1.0, 0.0, 0.0, 0.0, -1.0}, std::nullopt, 2);
    auto manifest = manifest_for(relative, tsunami::data::DatasetRepresentationKind::raster, "image/tiff");

    auto request = request_for(manifest, root);
    auto imported = tsunami::geo_gdal::import_geotiff_with_gdal(request);
    REQUIRE_FALSE(imported.has_value());
    REQUIRE(imported.error().code() == "geo.import.raster_band_count_unsupported");
    REQUIRE(context_value(imported.error(), "state_changed") == "false");

    const auto single_relative = std::filesystem::path{"inputs/data/raster/single.tif"};
    create_geotiff(root / single_relative, 2, 2, GDT_Float64, {1.0, 2.0, 3.0, 4.0}, {0.0, 1.0, 0.0, 0.0, 0.0, -1.0});
    manifest = manifest_for(single_relative, tsunami::data::DatasetRepresentationKind::raster, "image/tiff", {tsunami::data::DatasetRole::bathymetry}, "EPSG:3857");
    imported = tsunami::geo_gdal::import_geotiff_with_gdal(request_for(manifest, root));
    REQUIRE_FALSE(imported.has_value());
    REQUIRE(imported.error().code() == "geo.import.datum_conflict");

    auto datum = evidence();
    datum.vertical->positive_direction = "down";
    manifest = manifest_for(single_relative, tsunami::data::DatasetRepresentationKind::raster, "image/tiff");
    imported = tsunami::geo_gdal::import_geotiff_with_gdal(request_for(manifest, root, std::move(datum)));
    REQUIRE_FALSE(imported.has_value());
    REQUIRE(imported.error().code() == "geo.import.vertical_sign_conflict");

    auto inferred = evidence(tsunami::geo::DatumEvidenceStatus::inferred);
    imported = tsunami::geo_gdal::import_geotiff_with_gdal(request_for(manifest, root, std::move(inferred)));
    REQUIRE_FALSE(imported.has_value());
    REQUIRE(imported.error().code() == "geo.import.datum_inferred");
}

TEST_CASE("GeoPackage vector import preserves selected layer geometry and attributes", "[geo][gdal][vector]")
{
    const auto root = temp_case_root("vector");
    const auto relative = std::filesystem::path{"inputs/data/vector/sample.gpkg"};
    create_point_gpkg(root / relative);
    const auto manifest = manifest_for(relative, tsunami::data::DatasetRepresentationKind::vector, "application/geopackage+sqlite3", {tsunami::data::DatasetRole::observation});

    auto imported = tsunami::geo_gdal::import_geopackage_vector_with_gdal(request_for(manifest, root));
    REQUIRE(imported.has_value());
    const auto &layer = imported.value().vector_layer;
    REQUIRE(layer.layer_name() == "samples");
    REQUIRE(layer.geometry_kind() == tsunami::geo::ImportedGeometryKind::point);
    REQUIRE(layer.feature_count() == 1U);
    REQUIRE(layer.field_schema().size() == 3U);
    REQUIRE(layer.field_schema()[0].type == tsunami::geo::ImportedFieldType::string);
    REQUIRE(layer.field_schema()[2].type == tsunami::geo::ImportedFieldType::boolean);
    const auto &feature = layer.features().front();
    REQUIRE(feature.feature_id == 7);
    const auto &point = std::get<tsunami::geo::Point2D>(feature.geometry);
    REQUIRE(point.x == Catch::Approx(12.25));
    REQUIRE(point.y == Catch::Approx(55.5));
    REQUIRE(std::get<std::string>(feature.attributes[0].value) == "marker");
    REQUIRE(std::get<std::int64_t>(feature.attributes[1].value) == 4);
    REQUIRE(std::get<bool>(feature.attributes[2].value));

    const auto &record = imported.value().record;
    REQUIRE(record.import_kind == tsunami::geo::GeospatialImportKind::vector);
    REQUIRE(record.driver_short_name == "GPKG");
    REQUIRE_FALSE(record.raster.has_value());
    REQUIRE(record.vector.has_value());
    REQUIRE(record.vector->layer_name == "samples");
    REQUIRE(record.vector->coordinate_count == 1U);
}

TEST_CASE("GeoPackage vector import enforces layer and geometry guardrails", "[geo][gdal][vector][validation]")
{
    const auto root = temp_case_root("vector-reject");
    const auto relative = std::filesystem::path{"inputs/data/vector/multi.gpkg"};
    create_point_gpkg(root / relative, true);
    const auto manifest = manifest_for(relative, tsunami::data::DatasetRepresentationKind::vector, "application/geopackage+sqlite3", {tsunami::data::DatasetRole::observation});

    auto request = request_for(manifest, root);
    auto imported = tsunami::geo_gdal::import_geopackage_vector_with_gdal(request);
    REQUIRE_FALSE(imported.has_value());
    REQUIRE(imported.error().code() == "geo.import.vector_layer_ambiguous");

    request.layer_name = "samples";
    imported = tsunami::geo_gdal::import_geopackage_vector_with_gdal(request);
    REQUIRE(imported.has_value());
    REQUIRE(imported.value().vector_layer.layer_name() == "samples");

    request.layer_name = "missing";
    imported = tsunami::geo_gdal::import_geopackage_vector_with_gdal(request);
    REQUIRE_FALSE(imported.has_value());
    REQUIRE(imported.error().code() == "geo.import.vector_layer_missing");

    const auto polygon_relative = std::filesystem::path{"inputs/data/vector/polygon.gpkg"};
    create_polygon_gpkg(root / polygon_relative);
    auto polygon_manifest = manifest_for(polygon_relative, tsunami::data::DatasetRepresentationKind::vector, "application/geopackage+sqlite3", {tsunami::data::DatasetRole::observation});
    auto polygon_request = request_for(polygon_manifest, root);
    polygon_request.maximum_geometry_coordinates = 2U;
    imported = tsunami::geo_gdal::import_geopackage_vector_with_gdal(polygon_request);
    REQUIRE_FALSE(imported.has_value());
    REQUIRE(imported.error().code() == "geo.import.vector_coordinate_limit");
}

TEST_CASE("Method-neutral geospatial headers keep external adapter types out", "[geo][contracts]")
{
    for (const auto &relative : {
             "src/geo/include/tsunami/geo/GeospatialImport.hpp",
             "src/geo/include/tsunami/geo/GeospatialImportRecord.hpp",
             "src/geo/include/tsunami/geo/GeospatialImportSerialisation.hpp",
             "src/geo/include/tsunami/geo/ImportedRaster.hpp",
             "src/geo/include/tsunami/geo/ImportedVector.hpp",
             "src/geo/include/tsunami/geo/SpatialReferenceEvidence.hpp"}) {
        INFO(relative);
        const auto text = read_text(source_root() / relative);
        for (const auto token : {"GDAL", "OGR", "OSR", "CPL", "PROJ", "H5", "QObject", "QString", "QVariant", "nlohmann::"}) {
            REQUIRE(text.find(token) == std::string::npos);
        }
    }

    const auto adapter_header = read_text(source_root() / "src/geo_gdal/include/tsunami/geo_gdal/GdalGeospatialImporter.hpp");
    for (const auto token : {"GDALDataset", "OGRLayer", "OGRFeature", "OGRGeometry", "OGRSpatialReference", "CPL"}) {
        REQUIRE(adapter_header.find(token) == std::string::npos);
    }
}
