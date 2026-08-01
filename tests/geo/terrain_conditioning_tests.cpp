#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <tsunami/geo/CorridorConstruction.hpp>
#include <tsunami/geo/TerrainConditioning.hpp>
#include <tsunami/geo/TerrainConditioningParsing.hpp>
#include <tsunami/geo/TerrainConditioningSerialisation.hpp>

#ifdef TSUNAMI_ENABLE_GEOSPATIAL
#include <cpl_conv.h>
#include <gdal_priv.h>
#include <tsunami/fvm/FiniteVolumeMesh.hpp>
#include <tsunami/geo_gdal/GdalGeospatialImporter.hpp>
#include <tsunami/geo_gdal/GdalTerrainResampler.hpp>
#include <tsunami/r2d/RegionalGeometryPreflight.hpp>
#include <tsunami/r2d/RegionalTerrainTransfer.hpp>
#endif

namespace
{
    [[nodiscard]] auto reference(std::string code = "EN-METRIC-1") -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"TEST"},
            std::move(code),
            "Synthetic metric reference",
            std::string{"LOCAL_CS[\"Synthetic metric\"]"},
            std::nullopt,
            std::string{"Synthetic datum"},
            std::string{"Synthetic realisation"},
            2026.0,
            {"Easting", "Northing"},
            {"east", "north"},
            {"metre", "metre"}};
    }

    [[nodiscard]] auto target_reference() -> tsunami::geo::ComputationalTargetReference
    {
        return tsunami::geo::ComputationalTargetReference{
            reference(),
            tsunami::geo::CoordinateReferenceDescriptor{
                std::nullopt,
                std::nullopt,
                "synthetic-positive-up",
                std::string{"VERT_CS[\"synthetic-positive-up\"]"},
                std::nullopt,
                std::string{"Synthetic vertical datum"},
                std::string{"Synthetic vertical realisation"},
                2026.0,
                {"Gravity-related height"},
                {"up"},
                {"metre"}},
            tsunami::geo::ComputationalAxisConvention::east_north,
            "m",
            std::string{"m"},
            std::string{"up"}};
    }

    [[nodiscard]] auto case_configuration(
        tsunami::data::CorridorRequest corridor = tsunami::data::CorridorRequest{
            "terrain-axis",
            tsunami::data::CorridorOrigin{0.0, 0.0},
            90.0,
            20.0,
            10.0,
            10.0,
            tsunami::data::CorridorNarrowingConfiguration{false, std::nullopt},
            tsunami::data::CorridorSpongeConfiguration{0.0, 0.0}}) -> tsunami::data::CaseConfiguration
    {
        using namespace tsunami::data;
        auto regional = Regional2DCaseConfiguration{};
        regional.corridor = std::move(corridor);
        regional.physics = RegionalPhysicsConfiguration{};
        regional.numerics = RegionalNumericsConfiguration{RegionalTimeScheme::ssprk3, 0.45, 0.95, 1.0, 1.0, 0.001, 1.0, 60.0, 1000U};
        regional.boundaries = CorridorBoundaryConfiguration{};
        regional.relaxation = RelaxationConfiguration{false, std::nullopt, std::nullopt};
        auto made = make_case_configuration(
            SchemaIdentity{std::string{case_configuration_schema_name}, supported_case_configuration_version},
            CaseSchemaCompatibility::exact,
            std::string{supported_case_policy_version},
            CaseIdentity{tsunami::core::CaseId::from_string("terrain-case").value(), "terrain-case", 1U, "2026-07-31T00:00:00Z", "codex"},
            ScenarioConfiguration{"terrain-scenario", "tohoku-2011", "kamaishi", CaseModelFamily::regional_2d},
            CoordinateFrameConfiguration{"TEST:EN-METRIC-1", "synthetic-positive-up", "m", "m", HorizontalAxisOrder::east_north, VerticalPositiveDirection::up},
            DatasetBindings{std::filesystem::path{"manifests/datasets.json"}, "bathymetry-primary", "topography-primary", std::nullopt, std::nullopt, std::nullopt, std::nullopt, {}},
            regional,
            CaseOutputConfiguration{30.0, true, true, std::nullopt},
            CaseExtensions{});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto asset(std::string id) -> tsunami::data::DatasetAsset
    {
        using namespace tsunami::data;
        auto out = DatasetAsset{};
        out.asset_id = std::move(id);
        out.role = DatasetAssetRole::primary;
        out.location.kind = DatasetLocationKind::managed_path;
        out.location.managed_path = std::filesystem::path{"inputs/data/source.tif"};
        out.media_type = "image/tiff";
        out.digest = ContentDigest{DigestAlgorithm::sha256, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", DigestOrigin::provider_declared};
        return out;
    }

    [[nodiscard]] auto dataset(std::string id, tsunami::data::DatasetRole role, std::string asset_id)
        -> tsunami::data::DatasetRecord
    {
        using namespace tsunami::data;
        auto out = DatasetRecord{};
        out.dataset_id = std::move(id);
        out.origin_kind = DatasetOriginKind::source;
        out.representation = DatasetRepresentationKind::raster;
        out.roles = {role};
        out.title = "Synthetic terrain source";
        out.provider_id = "fixture-provider";
        out.licence_id = "fixture-licence";
        out.source = SourceAcquisitionRecord{"https://example.test/terrain", "2026-07-31T00:00:00Z", std::nullopt, std::nullopt};
        out.assets = {asset(std::move(asset_id))};
        out.spatial_reference = DatasetSpatialReference{SpatialApplicability::spatial, std::string{"TEST:EN-METRIC-1"}, std::string{"synthetic-positive-up"}, std::string{"m"}, std::string{"m"}, std::string{"east_north"}, std::string{"up"}};
        out.resolution.spatial = SpatialResolution{SpatialResolutionKind::grid_spacing, 10.0, 10.0, std::string{"m"}, std::nullopt};
        out.resolution.temporal = TemporalResolution{TemporalResolutionKind::static_dataset, std::nullopt, std::nullopt, std::nullopt};
        out.uncertainty = DatasetUncertainty{UncertaintyStatus::not_reported, {}, std::string{"not_reported"}};
        return out;
    }

    [[nodiscard]] auto manifest() -> tsunami::data::DatasetManifest
    {
        using namespace tsunami::data;
        auto made = make_dataset_manifest(
            SchemaIdentity{std::string{dataset_manifest_schema_name}, supported_dataset_manifest_version},
            DatasetManifestCompatibility::exact,
            std::string{supported_dataset_manifest_policy_version},
            DatasetManifestIdentity{"terrain-manifest", 1U, CaseRevisionRef{tsunami::core::CaseId::from_string("terrain-case").value(), 1U}, "2026-07-31T00:00:00Z", "codex"},
            {DatasetProvider{"fixture-provider", "Fixture Provider", std::nullopt, std::string{"https://example.test"}, {}}},
            {DatasetLicence{"fixture-licence", "Fixture Licence", "CC0-1.0", std::nullopt, std::nullopt, {}}},
            {
                dataset("bathymetry-primary", DatasetRole::bathymetry, "bathymetry-asset"),
                dataset("topography-primary", DatasetRole::topography, "topography-asset"),
            },
            {},
            {});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto transformed_points(tsunami::geo::Coordinate3D point)
        -> tsunami::geo::TransformedPointSet
    {
        auto made = tsunami::geo::make_transformed_point_set(reference("SOURCE"), target_reference(), {point});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto transformation_record(
        const tsunami::geo::CoordinateReferenceDescriptor &source,
        std::string transformation_id,
        std::string import_id,
        std::string dataset_id,
        std::string asset_id,
        tsunami::geo::VerticalTransformationSpecification vertical = {}) -> tsunami::geo::CoordinateTransformationRecord
    {
        auto record = tsunami::geo::CoordinateTransformationRecord{};
        record.identity = tsunami::geo::CoordinateTransformationIdentity{
            std::move(transformation_id),
            1U,
            tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string("terrain-case").value(), 1U},
            "terrain-manifest",
            1U,
            std::move(import_id),
            1U,
            std::move(dataset_id),
            std::move(asset_id),
            "transformed-terrain",
            "terrain-transform-process",
            "2026-07-31T00:00:00Z"};
        record.source_horizontal = source;
        record.target = target_reference();
        record.horizontal_operation.operation_name = "Synthetic identity operation";
        record.horizontal_operation.source_crs = source;
        record.horizontal_operation.target_crs = target_reference().horizontal;
        record.horizontal_operation.engine_name = "fixture";
        record.horizontal_operation.engine_version = "1.0";
        record.vertical_operation = std::move(vertical);
        return record;
    }

    [[nodiscard]] auto corridor_result(
        tsunami::data::CorridorRequest corridor = tsunami::data::CorridorRequest{
            "terrain-axis",
            tsunami::data::CorridorOrigin{0.0, 0.0},
            90.0,
            20.0,
            10.0,
            10.0,
            tsunami::data::CorridorNarrowingConfiguration{false, std::nullopt},
            tsunami::data::CorridorSpongeConfiguration{0.0, 0.0}}) -> tsunami::geo::CorridorConstructionResult
    {
        const auto config = case_configuration(std::move(corridor));
        const auto data = manifest();
        const auto epicentre = transformed_points({0.0, 0.0, -5.0});
        const auto target_coordinate = std::abs(config.regional_2d().corridor.bearing_degrees_clockwise_from_north - 45.0) < 1.0e-12
            ? tsunami::geo::Coordinate3D{20.0, 20.0, 2.0}
            : tsunami::geo::Coordinate3D{20.0, 0.0, 2.0};
        const auto target = transformed_points(target_coordinate);
        const auto epicentre_record = transformation_record(epicentre.source_reference(), "epicentre-transform", "epicentre-import", "epicentre-source", "epicentre-asset");
        const auto target_record = transformation_record(target.source_reference(), "target-transform", "target-import", "target-source", "target-asset");
        const auto request = tsunami::geo::CorridorConstructionRequest{
            &config,
            &data,
            tsunami::geo::CorridorReferencePointRequest{tsunami::geo::CorridorReferencePointRole::epicentre, &epicentre, 0U, &epicentre_record, "epicentre-point", "reported earthquake epicentre", std::string{"feature-epicentre"}, "Synthetic terrain fixture", "https://example.test/terrain/epicentre", "2026-07-31T00:00:00Z"},
            tsunami::geo::CorridorReferencePointRequest{tsunami::geo::CorridorReferencePointRole::target, &target, 0U, &target_record, "target-point", "coastal target point", std::string{"feature-target"}, "Synthetic terrain fixture", "https://example.test/terrain/target", "2026-07-31T00:00:00Z"},
            tsunami::geo::CorridorConstructionIdentity{"corridor-" + config.regional_2d().corridor.trajectory_id, 1U, tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string("terrain-case").value(), 1U}, config.regional_2d().corridor.trajectory_id, "corridor-dataset", "corridor-process", "2026-07-31T00:00:00Z"},
            tsunami::geo::CorridorConstructionPolicy{1.0, 0.001, 0.001, 1.0e-12, 1.0e-7, 1.0e-12, "terrain fixture tolerance"}};
        auto result = tsunami::geo::construct_corridor(request);
        REQUIRE(result.has_value());
        return std::move(result).value();
    }

    [[nodiscard]] auto grid_policy(double spacing = 10.0) -> tsunami::geo::TerrainTargetGridPolicy
    {
        return tsunami::geo::TerrainTargetGridPolicy{spacing, 0.5, 4.0, 4096U, 1.0e-9, 1.0e-12, "explicit fixture grid policy"};
    }

    [[nodiscard]] auto terrain_identity() -> tsunami::geo::TerrainConditioningIdentity
    {
        return tsunami::geo::TerrainConditioningIdentity{
            "terrain-fixture",
            1U,
            tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string("terrain-case").value(), 1U},
            "terrain-manifest",
            1U,
            "conditioned-terrain",
            "terrain-conditioning-process",
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard, maybe_unused]] auto raster(
        std::vector<double> values,
        std::vector<std::uint8_t> mask,
        tsunami::geo::RasterCellRegistration registration = tsunami::geo::RasterCellRegistration::pixel_is_area)
        -> tsunami::geo::ImportedRaster
    {
        const auto transform = tsunami::geo::RasterAffineTransform{-10.0, 10.0, 0.0, 10.0, 0.0, -10.0};
        auto extent = tsunami::geo::raster_extent_from_corners(4U, 2U, transform).value();
        auto band = tsunami::geo::ImportedRasterBand{"bed_elevation", tsunami::geo::NativeRasterDataType::float64, std::nullopt, std::nullopt, std::nullopt, std::move(values), std::move(mask)};
        auto made = tsunami::geo::make_imported_raster(
            4U,
            2U,
            transform,
            extent,
            registration,
            tsunami::geo::NativeSpatialReference{std::string{"TEST"}, std::string{"EN-METRIC-1"}, std::string{"Synthetic metric"}, std::string{"Synthetic datum"}, std::string{"PROJCRS[\"Synthetic metric\"]"}, {"Easting", "Northing"}, {"east", "north"}, {"metre", "metre"}, std::string{"2026.0"}},
            std::move(band));
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard, maybe_unused]] auto import_record(
        const tsunami::geo::ImportedRaster &source,
        std::string import_id,
        std::string dataset_id,
        std::string asset_id) -> tsunami::geo::GeospatialImportRecord
    {
        auto record = tsunami::geo::GeospatialImportRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{tsunami::geo::geospatial_import_record_schema_name}, tsunami::geo::supported_geospatial_import_record_version};
        record.policy_version = tsunami::geo::supported_geospatial_import_record_policy_version;
        record.identity = tsunami::geo::GeospatialImportIdentity{std::move(import_id), 1U, tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string("terrain-case").value(), 1U}, "terrain-manifest", 1U, std::move(dataset_id), std::move(asset_id), "2026-07-31T00:00:00Z"};
        record.import_kind = tsunami::geo::GeospatialImportKind::raster;
        record.native_spatial_reference = source.spatial_reference();
        record.extent = source.extent();
        record.raster = tsunami::geo::RasterImportSummary{source.width(), source.height(), source.cell_count(), 1U, source.band().native_type, source.transform(), source.registration(), false, std::nullopt, source.band().scale, source.band().offset, tsunami::data::SpatialResolution{tsunami::data::SpatialResolutionKind::grid_spacing, 10.0, 10.0, std::string{"m"}, std::nullopt}};
        return record;
    }

    [[nodiscard, maybe_unused]] auto plan(const tsunami::geo::ImportedRaster &source) -> tsunami::geo::RasterTransformationPlan
    {
        return tsunami::geo::RasterTransformationPlan{reference(), target_reference(), source.transform(), source.extent(), source.extent(), {}, source.width(), source.height(), 21U, {}, {}};
    }

    [[nodiscard]] auto resampled(
        const tsunami::geo::TerrainConditioningPreparation &preparation,
        std::string dataset_id,
        tsunami::geo::TerrainSourceRole role,
        std::vector<double> values,
        std::vector<std::uint8_t> mask,
        tsunami::geo::ResampledTerrainCellStatus invalid_status = tsunami::geo::ResampledTerrainCellStatus::source_nodata) -> tsunami::geo::ResampledTerrainSource
    {
        const auto &grid = preparation.grid;
        REQUIRE(values.size() == static_cast<std::size_t>(grid.cell_count()));
        REQUIRE(mask.size() == static_cast<std::size_t>(grid.cell_count()));
        REQUIRE(invalid_status != tsunami::geo::ResampledTerrainCellStatus::valid_resampled);
        const auto asset_id = role == tsunami::geo::TerrainSourceRole::bathymetry ? std::string{"bathymetry-asset"} : std::string{"topography-asset"};
        auto cell_status = std::vector<tsunami::geo::ResampledTerrainCellStatus>{};
        cell_status.reserve(mask.size());
        auto output_valid_count = std::uint64_t{};
        auto source_nodata_count = std::uint64_t{};
        auto outside_coverage_count = std::uint64_t{};
        for (const auto valid : mask) {
            if (valid != 0U) {
                cell_status.push_back(tsunami::geo::ResampledTerrainCellStatus::valid_resampled);
                ++output_valid_count;
            } else {
                cell_status.push_back(invalid_status);
                if (invalid_status == tsunami::geo::ResampledTerrainCellStatus::source_nodata) {
                    ++source_nodata_count;
                } else {
                    ++outside_coverage_count;
                }
            }
        }
        auto record = tsunami::geo::RasterResamplingRecord{};
        record.dataset_id = dataset_id;
        record.asset_id = asset_id;
        record.import_identity = tsunami::geo::GeospatialImportIdentity{
            dataset_id + "-import",
            1U,
            preparation.identity.case_revision,
            preparation.identity.manifest_id,
            preparation.identity.manifest_revision,
            dataset_id,
            asset_id,
            "2026-07-31T00:00:00Z"};
        record.transformation_identity = tsunami::geo::CoordinateTransformationIdentity{
            dataset_id + "-transform",
            1U,
            preparation.identity.case_revision,
            preparation.identity.manifest_id,
            preparation.identity.manifest_revision,
            record.import_identity.import_id,
            record.import_identity.import_revision,
            dataset_id,
            asset_id,
            dataset_id + "-projected",
            dataset_id + "-transform-process",
            "2026-07-31T00:00:00Z"};
        record.role = role;
        record.kernel = tsunami::geo::RasterResamplingKernel::bilinear;
        record.source_registration = tsunami::geo::RasterCellRegistration::pixel_is_area;
        record.target_registration = tsunami::geo::RasterCellRegistration::pixel_is_area;
        record.minimum_source_spacing_m = grid.spacing_m();
        record.maximum_source_spacing_m = grid.spacing_m();
        record.nominal_source_spacing_m = grid.spacing_m();
        record.target_spacing_m = grid.spacing_m();
        record.maximum_upsampling_factor = preparation.policy.grid.maximum_upsampling_factor;
        record.source_valid_cell_count = output_valid_count;
        record.output_valid_cell_count = output_valid_count;
        record.source_nodata_cell_count = source_nodata_count;
        record.outside_coverage_cell_count = outside_coverage_count;
        record.operation = tsunami::geo::CoordinateOperationRecord{
            dataset_id + " synthetic operation",
            std::string{"TEST"},
            std::string{"1001"},
            std::string{"Synthetic fixture method"},
            0.0,
            std::string{"terrain fixture operation scope"},
            tsunami::geo::GeographicAreaOfInterest{-1.0, -1.0, 1.0, 1.0},
            std::nullopt,
            std::string{"{\"type\":\"Conversion\"}"},
            std::string{"+proj=noop"},
            false,
            reference("SOURCE-" + dataset_id),
            grid.target_reference().horizontal,
            {},
            "fixture-engine",
            "1.0",
            std::string{"fixture-db"}};
        record.vertical_steps = tsunami::geo::VerticalTransformationSpecification{false, {}};
        record.adapter_name = "fixture";
        record.adapter_version = "1.0";
        return tsunami::geo::ResampledTerrainSource{std::move(dataset_id), role, grid, std::move(values), std::move(mask), std::move(cell_status), std::move(record)};
    }

    [[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string
    {
        auto resolved = path;
        auto file = std::ifstream{resolved, std::ios::binary};
        auto current = std::filesystem::current_path();
        for (auto depth = 0; !file && depth < 6; ++depth) {
            resolved = current / path;
            file = std::ifstream{resolved, std::ios::binary};
            current = current.parent_path();
        }
        REQUIRE(file);
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

    auto check_resampling_fields_equal(
        const tsunami::geo::RasterResamplingRecord &left,
        const tsunami::geo::RasterResamplingRecord &right) -> void
    {
        CHECK(left.dataset_id == right.dataset_id);
        CHECK(left.asset_id == right.asset_id);
        CHECK(left.import_identity == right.import_identity);
        CHECK(left.transformation_identity == right.transformation_identity);
        CHECK(left.role == right.role);
        CHECK(left.kernel == right.kernel);
        CHECK(left.source_registration == right.source_registration);
        CHECK(left.target_registration == right.target_registration);
        CHECK(left.source_scale == right.source_scale);
        CHECK(left.source_offset == right.source_offset);
        CHECK(left.minimum_source_spacing_m == right.minimum_source_spacing_m);
        CHECK(left.maximum_source_spacing_m == right.maximum_source_spacing_m);
        CHECK(left.nominal_source_spacing_m == right.nominal_source_spacing_m);
        CHECK(left.target_spacing_m == right.target_spacing_m);
        CHECK(left.maximum_upsampling_factor == right.maximum_upsampling_factor);
        CHECK(left.source_valid_cell_count == right.source_valid_cell_count);
        CHECK(left.output_valid_cell_count == right.output_valid_cell_count);
        CHECK(left.source_nodata_cell_count == right.source_nodata_cell_count);
        CHECK(left.outside_coverage_cell_count == right.outside_coverage_cell_count);
        CHECK(left.operation == right.operation);
        CHECK(left.vertical_steps == right.vertical_steps);
        CHECK(left.adapter_name == right.adapter_name);
        CHECK(left.adapter_version == right.adapter_version);
    }

    auto check_record_fields_equal(
        const tsunami::geo::TerrainConditioningRecord &left,
        const tsunami::geo::TerrainConditioningRecord &right) -> void
    {
        CHECK(left.schema == right.schema);
        CHECK(left.policy_version == right.policy_version);
        CHECK(left.formula_version == right.formula_version);
        CHECK(left.identity == right.identity);
        CHECK(left.scenario_id == right.scenario_id);
        CHECK(left.target_site == right.target_site);
        CHECK(left.bathymetry_dataset_id == right.bathymetry_dataset_id);
        CHECK(left.bathymetry_asset_id == right.bathymetry_asset_id);
        CHECK(left.bathymetry_import_identity == right.bathymetry_import_identity);
        CHECK(left.bathymetry_transformation_identity == right.bathymetry_transformation_identity);
        CHECK(left.topography_dataset_id == right.topography_dataset_id);
        CHECK(left.topography_asset_id == right.topography_asset_id);
        CHECK(left.topography_import_identity == right.topography_import_identity);
        CHECK(left.topography_transformation_identity == right.topography_transformation_identity);
        CHECK(left.corridor_identity == right.corridor_identity);
        CHECK(left.target_reference == right.target_reference);
        CHECK(left.grid == right.grid);
        CHECK(left.grid_policy == right.grid_policy);
        check_resampling_fields_equal(left.bathymetry_resampling, right.bathymetry_resampling);
        check_resampling_fields_equal(left.topography_resampling, right.topography_resampling);
        CHECK(left.merge_policy == right.merge_policy);
        CHECK(left.gap_policy == right.gap_policy);
        CHECK(left.diagnostics == right.diagnostics);
        CHECK(left.output_uncertainty == right.output_uncertainty);
        CHECK(left.output_media_type == right.output_media_type);
        CHECK(left.output_path == right.output_path);
        CHECK(left.digest_status == right.digest_status);
        CHECK(left.warnings == right.warnings);
    }
}

TEST_CASE("terrain target grids are corridor aligned pixel-is-area rasters with exact coverage", "[geo][terrain][target grid][coverage]")
{
    const auto corridor = corridor_result();
    const auto grid = tsunami::geo::build_corridor_aligned_terrain_grid(corridor.corridor, corridor.record, grid_policy()).value();
    CHECK(grid.width() == 4U);
    CHECK(grid.height() == 2U);
    CHECK(grid.cell_count() == 8U);
    CHECK(grid.registration() == tsunami::geo::RasterCellRegistration::pixel_is_area);
    CHECK(grid.xi_min_m() == -10.0);
    CHECK(grid.xi_max_m() == 30.0);
    CHECK(grid.eta_top_m() == 10.0);
    CHECK(grid.eta_bottom_m() == -10.0);
    CHECK(grid.transform() == tsunami::geo::RasterAffineTransform{-10.0, 10.0, -0.0, 10.0, 0.0, -10.0});
    CHECK(tsunami::geo::terrain_grid_cell_centre(grid, 0U, 0U) == tsunami::geo::Point2D{-5.0, 5.0});

    const auto coverage = tsunami::geo::calculate_corridor_coverage(corridor.corridor, corridor.record, grid, grid_policy()).value();
    CHECK(coverage.active_cell_count == 8U);
    CHECK(coverage.outside_cell_count == 0U);
    CHECK(coverage.excluded_boundary_cell_count == 0U);
    CHECK(std::all_of(coverage.fractions.begin(), coverage.fractions.end(), [](double value) { return std::abs(value - 1.0) < 1.0e-12; }));

    auto rotated_corridor_request = tsunami::data::CorridorRequest{"terrain-rotated", tsunami::data::CorridorOrigin{0.0, 0.0}, 45.0, 20.0, 10.0, 10.0, tsunami::data::CorridorNarrowingConfiguration{false, std::nullopt}, tsunami::data::CorridorSpongeConfiguration{0.0, 0.0}};
    auto rotated = corridor_result(rotated_corridor_request);
    rotated.record.configured_bearing_degrees = rotated.record.derived_bearing_degrees;
    const auto rotated_grid = tsunami::geo::build_corridor_aligned_terrain_grid(rotated.corridor, rotated.record, grid_policy(10.0)).value();
    CHECK(std::abs(rotated_grid.transform().row_rotation) > 1.0);
    CHECK(std::abs(rotated_grid.transform().column_rotation) > 1.0);
}

TEST_CASE("terrain domain merge records overlap diagnostics and rejects unresolved nodata by default", "[geo][terrain][merge][overlap][nodata][lineage]")
{
    const auto corridor = corridor_result();
    const auto grid = tsunami::geo::build_corridor_aligned_terrain_grid(corridor.corridor, corridor.record, grid_policy()).value();
    const auto coverage = tsunami::geo::calculate_corridor_coverage(corridor.corridor, corridor.record, grid, grid_policy()).value();
    auto preparation = tsunami::geo::TerrainConditioningPreparation{};
    preparation.grid = grid;
    preparation.coverage = coverage;
    preparation.identity = terrain_identity();
    preparation.policy.grid = grid_policy();
    preparation.policy.merge = tsunami::geo::TerrainMergePolicy{"bathymetry-primary", "topography-primary", 20.0, tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning, "bathymetry preferred in fixture overlap"};
    preparation.policy.gaps = tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::reject, 0.0, 0.0, 0U, 0U, 0.0, 0.0, "reject unresolved fixture gaps"};
    preparation.corridor_identity = corridor.record.identity;
    preparation.scenario_id = "terrain-scenario";
    preparation.target_site = "kamaishi";
    preparation.output_uncertainty = tsunami::data::DatasetUncertainty{tsunami::data::UncertaintyStatus::not_reported, {}, std::string{"not_reported"}};
    preparation.output_path = tsunami::geo::default_conditioned_terrain_path("conditioned-terrain");

    auto bathy = resampled(preparation, "bathymetry-primary", tsunami::geo::TerrainSourceRole::bathymetry, {-5.0, -4.0, -3.0, -2.0, -6.0, -5.0, -4.0, -3.0}, {1U, 1U, 1U, 0U, 1U, 1U, 0U, 0U});
    auto topo = resampled(preparation, "topography-primary", tsunami::geo::TerrainSourceRole::topography, {1.0, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0, 5.0}, {0U, 0U, 1U, 1U, 0U, 1U, 1U, 1U});
    const auto result = tsunami::geo::condition_terrain_from_resampled_sources(preparation, bathy, topo).value();
    CHECK(result.diagnostics.unresolved_cell_count == 0U);
    CHECK(result.diagnostics.overlap_cell_count == 2U);
    CHECK(result.diagnostics.overlap_conflict_cell_count == 0U);
    CHECK(result.diagnostics.bathymetry_selected_cell_count == 5U);
    CHECK(result.diagnostics.topography_selected_cell_count == 3U);
    CHECK(result.terrain.minimum_elevation_m() == -6.0);
    CHECK(result.terrain.maximum_elevation_m() == 5.0);
    CHECK(result.record.formula_version == tsunami::geo::terrain_conditioning_formula_version);

    topo.valid_mask.assign(static_cast<std::size_t>(grid.cell_count()), 0U);
    CHECK_FALSE(tsunami::geo::condition_terrain_from_resampled_sources(preparation, bathy, topo).has_value());
}

TEST_CASE("terrain bounded gap fill uses one donor family and preserves complete lineage", "[geo][terrain][gap fill][lineage]")
{
    const auto corridor = corridor_result(tsunami::data::CorridorRequest{"terrain-fill", tsunami::data::CorridorOrigin{0.0, 0.0}, 90.0, 30.0, 10.0, 10.0, tsunami::data::CorridorNarrowingConfiguration{false, std::nullopt}, tsunami::data::CorridorSpongeConfiguration{0.0, 0.0}});
    const auto policy = grid_policy(10.0);
    const auto grid = tsunami::geo::build_corridor_aligned_terrain_grid(corridor.corridor, corridor.record, policy).value();
    auto coverage = tsunami::geo::calculate_corridor_coverage(corridor.corridor, corridor.record, grid, policy).value();
    REQUIRE(grid.width() == 4U);
    REQUIRE(grid.height() == 3U);
    auto preparation = tsunami::geo::TerrainConditioningPreparation{};
    preparation.grid = grid;
    preparation.coverage = coverage;
    preparation.identity = terrain_identity();
    preparation.policy.grid = policy;
    preparation.policy.merge = tsunami::geo::TerrainMergePolicy{"bathymetry-primary", "topography-primary", 100.0, tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning, "bathymetry preferred"};
    preparation.policy.gaps = tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::bounded_inverse_distance, 15.0, 1.0, 1U, 4U, 2.0, 0.1, "single-cell fixture fill"};
    preparation.corridor_identity = corridor.record.identity;
    preparation.scenario_id = "terrain-scenario";
    preparation.target_site = "kamaishi";
    preparation.output_uncertainty = tsunami::data::DatasetUncertainty{tsunami::data::UncertaintyStatus::not_reported, {}, std::string{"not_reported"}};
    preparation.output_path = tsunami::geo::default_conditioned_terrain_path("conditioned-terrain");
    auto values = std::vector<double>(12U, -4.0);
    auto mask = std::vector<std::uint8_t>(12U, 1U);
    mask[5U] = 0U;
    auto bathy = resampled(preparation, "bathymetry-primary", tsunami::geo::TerrainSourceRole::bathymetry, values, mask);
    auto topo = resampled(preparation, "topography-primary", tsunami::geo::TerrainSourceRole::topography, std::vector<double>(12U, 1.0), std::vector<std::uint8_t>(12U, 0U));
    const auto result = tsunami::geo::condition_terrain_from_resampled_sources(preparation, bathy, topo).value();
    CHECK(result.diagnostics.initially_unresolved_cell_count == 1U);
    CHECK(result.diagnostics.filled_cell_count == 1U);
    CHECK(result.diagnostics.unresolved_cell_count == 0U);
    CHECK(result.terrain.cell_lineage()[5U] == tsunami::geo::TerrainCellLineage::filled_from_bathymetry_neighbourhood);

    mask[6U] = 0U;
    auto topo_mask = std::vector<std::uint8_t>(12U, 0U);
    topo_mask[6U] = 1U;
    auto mixed_bathy = resampled(preparation, "bathymetry-primary", tsunami::geo::TerrainSourceRole::bathymetry, values, mask);
    auto mixed_topo = resampled(preparation, "topography-primary", tsunami::geo::TerrainSourceRole::topography, std::vector<double>(12U, 1.0), topo_mask);
    const auto mixed = tsunami::geo::condition_terrain_from_resampled_sources(preparation, mixed_bathy, mixed_topo);
    REQUIRE_FALSE(mixed.has_value());
    CHECK(mixed.error().code() == "geo.terrain.gap_donor_lineage_mixed");
}

TEST_CASE("terrain records serialise deterministically and write transactionally", "[geo][terrain record][serialisation]")
{
    const auto corridor = corridor_result();
    const auto grid = tsunami::geo::build_corridor_aligned_terrain_grid(corridor.corridor, corridor.record, grid_policy()).value();
    const auto coverage = tsunami::geo::calculate_corridor_coverage(corridor.corridor, corridor.record, grid, grid_policy()).value();
    auto preparation = tsunami::geo::TerrainConditioningPreparation{};
    preparation.grid = grid;
    preparation.coverage = coverage;
    preparation.identity = terrain_identity();
    preparation.policy.grid = grid_policy();
    preparation.policy.merge = tsunami::geo::TerrainMergePolicy{"bathymetry-primary", "topography-primary", 100.0, tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning, "bathymetry preferred"};
    preparation.policy.gaps = tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::reject, 0.0, 0.0, 0U, 0U, 0.0, 0.0, "reject"};
    preparation.corridor_identity = corridor.record.identity;
    preparation.scenario_id = "terrain-scenario";
    preparation.target_site = "kamaishi";
    preparation.output_uncertainty = tsunami::data::DatasetUncertainty{tsunami::data::UncertaintyStatus::not_reported, {}, std::string{"not_reported"}};
    preparation.output_path = tsunami::geo::default_conditioned_terrain_path("conditioned-terrain");
    auto bathy = resampled(preparation, "bathymetry-primary", tsunami::geo::TerrainSourceRole::bathymetry, std::vector<double>(8U, -3.0), std::vector<std::uint8_t>(8U, 1U));
    auto topo = resampled(preparation, "topography-primary", tsunami::geo::TerrainSourceRole::topography, std::vector<double>(8U, 2.0), std::vector<std::uint8_t>(8U, 0U));
    const auto result = tsunami::geo::condition_terrain_from_resampled_sources(preparation, bathy, topo).value();
    const auto first = tsunami::geo::serialise_terrain_conditioning_record(result.record).value();
    const auto second = tsunami::geo::serialise_terrain_conditioning_record(result.record).value();
    CHECK(first == second);
    CHECK(first.ends_with('\n'));
    CHECK(first.find("\"formula_version\": \"corridor-grid-priority-merge-v1\"") != std::string::npos);
    const auto out = std::filesystem::temp_directory_path() / "tsunami-terrain-record-test" / "record.json";
    REQUIRE(tsunami::geo::write_terrain_conditioning_record(out, result.record).has_value());
    CHECK(read_text(out) == first);
    auto invalid = result.record;
    invalid.diagnostics.unresolved_cell_count = 1U;
    CHECK_FALSE(tsunami::geo::write_terrain_conditioning_record(out, invalid).has_value());
    CHECK(read_text(out) == first);
}

TEST_CASE("actual terrain producer records parse and reserialise byte-identically", "[geo][terrain record][readback]")
{
    const auto corridor = corridor_result();
    const auto policy = grid_policy();
    const auto grid = tsunami::geo::build_corridor_aligned_terrain_grid(corridor.corridor, corridor.record, policy).value();
    const auto coverage = tsunami::geo::calculate_corridor_coverage(corridor.corridor, corridor.record, grid, policy).value();
    auto preparation = tsunami::geo::TerrainConditioningPreparation{};
    preparation.grid = grid;
    preparation.coverage = coverage;
    preparation.identity = terrain_identity();
    preparation.policy.grid = policy;
    preparation.policy.merge = tsunami::geo::TerrainMergePolicy{"bathymetry-primary", "topography-primary", 20.0, tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning, "bathymetry preferred in actual producer readback fixture"};
    preparation.policy.gaps = tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::reject, 0.0, 0.0, 0U, 0U, 0.0, 0.0, "reject unresolved fixture gaps"};
    preparation.corridor_identity = corridor.record.identity;
    preparation.scenario_id = "terrain-scenario";
    preparation.target_site = "kamaishi";
    preparation.output_uncertainty = tsunami::data::DatasetUncertainty{tsunami::data::UncertaintyStatus::not_reported, {}, std::string{"not_reported"}};
    preparation.output_path = tsunami::geo::default_conditioned_terrain_path("conditioned-terrain");

    auto bathy = resampled(preparation, "bathymetry-primary", tsunami::geo::TerrainSourceRole::bathymetry, {-5.0, -4.0, -3.0, -2.0, -6.0, -5.0, -4.0, -3.0}, {1U, 1U, 1U, 0U, 1U, 1U, 0U, 0U});
    auto topo = resampled(preparation, "topography-primary", tsunami::geo::TerrainSourceRole::topography, {1.0, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0, 5.0}, {0U, 0U, 1U, 1U, 0U, 1U, 1U, 1U});
    const auto result = tsunami::geo::condition_terrain_from_resampled_sources(preparation, bathy, topo);
    REQUIRE(result.has_value());
    REQUIRE(tsunami::geo::validate_terrain_conditioning_record(result.value().record).has_value());

    const auto serialised = tsunami::geo::serialise_terrain_conditioning_record(result.value().record);
    REQUIRE(serialised.has_value());
    const auto parsed = tsunami::geo::parse_terrain_conditioning_record(serialised.value(), "actual-producer-terrain");
    REQUIRE(parsed.has_value());
    check_record_fields_equal(parsed.value(), result.value().record);
    const auto reserialised = tsunami::geo::serialise_terrain_conditioning_record(parsed.value());
    REQUIRE(reserialised.has_value());
    CHECK(reserialised.value() == serialised.value());
}

TEST_CASE("terrain public headers keep domain and adapter boundaries explicit", "[geo][terrain][architecture]")
{
    const auto domain_headers = std::vector<std::filesystem::path>{
        "src/geo/include/tsunami/geo/ConditionedTerrainRaster.hpp",
        "src/geo/include/tsunami/geo/TerrainConditioning.hpp",
        "src/geo/include/tsunami/geo/TerrainConditioningRecord.hpp",
        "src/geo/include/tsunami/geo/TerrainConditioningSerialisation.hpp",
        "src/geo/include/tsunami/geo/TerrainResampling.hpp",
        "src/geo/include/tsunami/geo/TerrainTargetGrid.hpp"};
    const auto forbidden = std::vector<std::string>{"GDAL", "OGR", "OSR", "CPL", "PJ_CONTEXT", "PROJ", "proj_", "H5", "Gmsh", "QObject", "QString", "QVariant", "nlohmann::"};
    for (const auto &header : domain_headers) {
        const auto text = read_text(header);
        for (const auto &token : forbidden) {
            CHECK(text.find(token) == std::string::npos);
        }
    }
    const auto adapter_headers = std::vector<std::filesystem::path>{
        "src/geo_gdal/include/tsunami/geo_gdal/GdalTerrainResampler.hpp",
        "src/geo_gdal/include/tsunami/geo_gdal/GdalConditionedTerrainArtifacts.hpp"};
    for (const auto &header : adapter_headers) {
        const auto text = read_text(header);
        for (const auto &token : {"GDALDataset", "GDALRasterBand", "OGR", "OSR", "CPL", "PJ_CONTEXT", "PROJ", "proj_", "H5", "OpenFOAM", "QObject", "QString", "QVariant"}) {
            CHECK(text.find(token) == std::string::npos);
        }
    }
}

#ifdef TSUNAMI_ENABLE_GEOSPATIAL
namespace
{
    [[nodiscard]] auto regional_mesh_fixture() -> tsunami::fvm::FiniteVolumeMesh
    {
        using namespace tsunami::fvm;
        auto made = make_finite_volume_mesh(
            MeshTopologyInput{
                MeshId{"terrain-artifact-readback-mesh"},
                2U,
                {
                    {{0U}, {0.0, -4.0, 0.0}},
                    {{1U}, {20.0, -4.0, 0.0}},
                    {{2U}, {20.0, 4.0, 0.0}},
                    {{3U}, {0.0, 4.0, 0.0}},
                },
                {
                    {{0U}, {{0U}, {1U}}, {0U}, std::nullopt, BoundaryPatchId{0U}},
                    {{1U}, {{1U}, {2U}}, {0U}, std::nullopt, BoundaryPatchId{1U}},
                    {{2U}, {{2U}, {3U}}, {1U}, std::nullopt, BoundaryPatchId{2U}},
                    {{3U}, {{3U}, {0U}}, {1U}, std::nullopt, BoundaryPatchId{3U}},
                    {{4U}, {{0U}, {2U}}, {0U}, CellId{1U}, std::nullopt},
                },
                {{{0U}, {{0U}, {1U}, {4U}}}, {{1U}, {{2U}, {3U}, {4U}}}},
                {
                    {BoundaryPatchId{0U}, "boundary.offshore", {FaceId{0U}}},
                    {BoundaryPatchId{1U}, "boundary.inland", {FaceId{1U}}},
                    {BoundaryPatchId{2U}, "boundary.left_side", {FaceId{2U}}},
                    {BoundaryPatchId{3U}, "boundary.right_side", {FaceId{3U}}},
                }});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto readback_case_root(std::string_view name) -> std::filesystem::path
    {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        auto root = std::filesystem::temp_directory_path() / ("tsunami-terrain-artifact-readback-" + std::string{name} + "-" + std::to_string(unique));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "outputs/terrain");
        std::filesystem::create_directories(root / "manifests/terrain");
        return root;
    }

#ifdef TSUNAMI_ENABLE_GEOSPATIAL
    class ScopedGdalConfig
    {
    public:
        ScopedGdalConfig(std::string key, const char *value)
            : key_{std::move(key)}
        {
            if (const auto *previous = CPLGetConfigOption(key_.c_str(), nullptr); previous != nullptr) {
                previous_ = std::string{previous};
            }
            CPLSetConfigOption(key_.c_str(), value);
        }

        ScopedGdalConfig(const ScopedGdalConfig &) = delete;
        auto operator=(const ScopedGdalConfig &) -> ScopedGdalConfig & = delete;

        ~ScopedGdalConfig()
        {
            CPLSetConfigOption(key_.c_str(), previous_ ? previous_->c_str() : nullptr);
        }

    private:
        std::string key_;
        std::optional<std::string> previous_;
    };

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }

    [[nodiscard]] auto conditioned_artifact_fixture(
        tsunami::data::CorridorRequest corridor = tsunami::data::CorridorRequest{
            "terrain-axis",
            tsunami::data::CorridorOrigin{0.0, 0.0},
            90.0,
            20.0,
            10.0,
            10.0,
            tsunami::data::CorridorNarrowingConfiguration{false, std::nullopt},
            tsunami::data::CorridorSpongeConfiguration{0.0, 0.0}},
        bool fill_gaps = false) -> tsunami::geo::TerrainConditioningResult
    {
        const auto corridor_geometry = corridor_result(corridor);
        const auto config = case_configuration(std::move(corridor));
        const auto data = manifest();
        auto bathy_raster = raster({-5.0, -4.0, -3.0, -2.0, -6.0, -5.0, -4.0, -3.0}, {1U, 1U, 1U, 0U, 1U, 1U, 0U, 0U});
        auto topo_raster = raster({1.0, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0, 5.0}, {0U, 0U, 1U, 1U, 0U, 1U, 1U, 1U});
        const auto bathy_import = import_record(bathy_raster, "bathymetry-import", "bathymetry-primary", "bathymetry-asset");
        const auto topo_import = import_record(topo_raster, "topography-import", "topography-primary", "topography-asset");
        const auto bathy_record = transformation_record(reference(), "bathymetry-transform", "bathymetry-import", "bathymetry-primary", "bathymetry-asset");
        const auto topo_record = transformation_record(reference(), "topography-transform", "topography-import", "topography-primary", "topography-asset");
        const auto bathy_plan = plan(bathy_raster);
        const auto topo_plan = plan(topo_raster);
        auto request = tsunami::geo::TerrainConditioningRequest{
            &config,
            &data,
            &corridor_geometry.corridor,
            &corridor_geometry.record,
            tsunami::geo::TerrainSourceRequest{tsunami::geo::TerrainSourceRole::bathymetry, &bathy_raster, &bathy_import, &bathy_plan, &bathy_record, "bathymetry-primary", "bathymetry-asset", tsunami::geo::RasterResamplingKernel::bilinear},
            tsunami::geo::TerrainSourceRequest{tsunami::geo::TerrainSourceRole::topography, &topo_raster, &topo_import, &topo_plan, &topo_record, "topography-primary", "topography-asset", tsunami::geo::RasterResamplingKernel::bilinear},
            terrain_identity(),
            tsunami::geo::TerrainConditioningPolicy{
                grid_policy(),
                tsunami::geo::TerrainMergePolicy{"bathymetry-primary", "topography-primary", 20.0, tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning, "bathymetry priority fixture"},
                fill_gaps
                    ? tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::bounded_inverse_distance, 15.0, 1.0, 1U, 8U, 2.0, 0.1, "fill rotated fixture gaps"}
                    : tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::reject, 0.0, 0.0, 0U, 0U, 0.0, 0.0, "reject unresolved fixture gaps"},
                tsunami::geo::TerrainUncertaintyPolicy{tsunami::geo::TerrainUncertaintyCombination::not_computed, std::nullopt, std::nullopt, std::nullopt, "not reported"}},
            std::filesystem::temp_directory_path()};
        auto result = tsunami::geo_gdal::condition_terrain_with_gdal(request);
        const auto message = result.has_value() ? std::string{"conditioned"} : result.error().code() + ": " + result.error().message();
        INFO(message);
        REQUIRE(result.has_value());
        return std::move(result).value();
    }

    auto write_bytes(const std::filesystem::path &path, std::string_view bytes) -> void
    {
        std::filesystem::create_directories(path.parent_path());
        auto file = std::ofstream{path, std::ios::binary};
        REQUIRE(file);
        file << bytes;
    }

    [[nodiscard]] auto read_bytes(const std::filesystem::path &path) -> std::vector<char>
    {
        auto file = std::ifstream{path, std::ios::binary};
        REQUIRE(file);
        return std::vector<char>{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

    [[nodiscard]] auto path_with_suffix(const std::filesystem::path &path, std::string_view suffix) -> std::filesystem::path
    {
        return std::filesystem::path{path.string() + std::string{suffix}};
    }

    [[nodiscard]] auto artifact_files(const tsunami::geo_gdal::ConditionedTerrainArtifactPaths &paths) -> std::vector<std::filesystem::path>
    {
        return {
            paths.terrain_path,
            path_with_suffix(paths.terrain_path, ".msk"),
            path_with_suffix(paths.terrain_path, ".aux.xml"),
            paths.coverage_path,
            path_with_suffix(paths.coverage_path, ".msk"),
            path_with_suffix(paths.coverage_path, ".aux.xml"),
            paths.lineage_path,
            path_with_suffix(paths.lineage_path, ".msk"),
            path_with_suffix(paths.lineage_path, ".aux.xml")};
    }

    using FileSnapshot = std::vector<std::pair<std::filesystem::path, std::optional<std::vector<char>>>>;

    [[nodiscard]] auto snapshot_files(const std::vector<std::filesystem::path> &paths) -> FileSnapshot
    {
        auto snapshot = FileSnapshot{};
        snapshot.reserve(paths.size());
        for (const auto &path : paths) {
            if (std::filesystem::exists(path)) {
                snapshot.push_back({path, read_bytes(path)});
            } else {
                snapshot.push_back({path, std::nullopt});
            }
        }
        return snapshot;
    }

    auto check_snapshot_restored(const FileSnapshot &snapshot) -> void
    {
        for (const auto &[path, bytes] : snapshot) {
            INFO(path.generic_string());
            CHECK(std::filesystem::exists(path) == bytes.has_value());
            if (bytes) {
                CHECK(read_bytes(path) == *bytes);
            }
        }
    }

    [[nodiscard]] auto contains_transaction_directory(const std::filesystem::path &root) -> bool
    {
        if (!std::filesystem::exists(root)) {
            return false;
        }
        for (const auto &entry : std::filesystem::recursive_directory_iterator{root}) {
            const auto name = entry.path().filename().generic_string();
            if (name.rfind(".tsunami-terrain-artifact-txn-", 0U) == 0U ||
                name.rfind(".tsunami-terrain-single-artifact-txn-", 0U) == 0U) {
                return true;
            }
        }
        return false;
    }
#endif

    auto set_dataset_metadata(
        const std::filesystem::path &path,
        std::string_view key,
        std::string_view value) -> void
    {
        auto *raw = static_cast<GDALDataset *>(GDALOpenEx(path.string().c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr));
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};
        REQUIRE(dataset->SetMetadataItem(std::string{key}.c_str(), std::string{value}.c_str()) == CE_None);
    }

    auto overwrite_lineage_code(const std::filesystem::path &path, std::uint16_t value) -> void
    {
        auto *raw = static_cast<GDALDataset *>(GDALOpenEx(path.string().c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr));
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};
        auto *band = dataset->GetRasterBand(1);
        REQUIRE(band != nullptr);
        REQUIRE(band->RasterIO(GF_Write, 0, 0, 1, 1, &value, 1, 1, GDT_UInt16, 0, 0) == CE_None);
    }

    auto overwrite_coverage_value(const std::filesystem::path &path, double value) -> void
    {
        auto *raw = static_cast<GDALDataset *>(GDALOpenEx(path.string().c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr));
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};
        auto *band = dataset->GetRasterBand(1);
        REQUIRE(band != nullptr);
        REQUIRE(band->RasterIO(GF_Write, 0, 0, 1, 1, &value, 1, 1, GDT_Float64, 0, 0) == CE_None);
    }

    auto overwrite_terrain_value(const std::filesystem::path &path, double value) -> void
    {
        auto *raw = static_cast<GDALDataset *>(GDALOpenEx(path.string().c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr));
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};
        auto *band = dataset->GetRasterBand(1);
        REQUIRE(band != nullptr);
        REQUIRE(band->RasterIO(GF_Write, 0, 0, 1, 1, &value, 1, 1, GDT_Float64, 0, 0) == CE_None);
    }

    auto overwrite_mask_value(const std::filesystem::path &path, std::uint8_t value) -> void
    {
        auto *raw = static_cast<GDALDataset *>(GDALOpenEx(path.string().c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr));
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};
        auto *band = dataset->GetRasterBand(1);
        REQUIRE(band != nullptr);
        auto *mask = band->GetMaskBand();
        REQUIRE(mask != nullptr);
        REQUIRE(mask->RasterIO(GF_Write, 0, 0, 1, 1, &value, 1, 1, GDT_Byte, 0, 0) == CE_None);
    }

    auto set_band_contract_metadata(
        const std::filesystem::path &path,
        std::optional<std::string_view> description,
        std::optional<std::string_view> unit,
        std::optional<double> scale,
        std::optional<double> offset,
        std::optional<double> nodata) -> void
    {
        auto *raw = static_cast<GDALDataset *>(GDALOpenEx(path.string().c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr));
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};
        auto *band = dataset->GetRasterBand(1);
        REQUIRE(band != nullptr);
        if (description) {
            band->SetDescription(std::string{*description}.c_str());
        }
        if (unit) {
            band->SetUnitType(std::string{*unit}.c_str());
        }
        if (scale) {
            REQUIRE(band->SetScale(*scale) == CE_None);
        }
        if (offset) {
            REQUIRE(band->SetOffset(*offset) == CE_None);
        }
        if (nodata) {
            REQUIRE(band->SetNoDataValue(*nodata) == CE_None);
        }
    }

    auto shift_geotransform(const std::filesystem::path &path) -> void
    {
        auto *raw = static_cast<GDALDataset *>(GDALOpenEx(path.string().c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr));
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};
        auto transform = std::array<double, 6U>{};
        REQUIRE(dataset->GetGeoTransform(transform.data()) == CE_None);
        transform[0] += 0.25;
        REQUIRE(dataset->SetGeoTransform(transform.data()) == CE_None);
    }

    auto set_dataset_crs(const std::filesystem::path &path, const char *crs) -> void
    {
        auto *raw = static_cast<GDALDataset *>(GDALOpenEx(path.string().c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr));
        REQUIRE(raw != nullptr);
        auto dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>{raw, GDALClose};
        auto srs = OGRSpatialReference{};
        REQUIRE(srs.SetFromUserInput(crs) == OGRERR_NONE);
        REQUIRE(dataset->SetSpatialRef(&srs) == CE_None);
    }
}

TEST_CASE("GDAL terrain adapter resamples sources and writes inspection GeoTIFFs", "[geo][terrain][resampling][warp][GeoTIFF][lineage]")
{
    REQUIRE(tsunami::geo_gdal::gdal_driver_available("MEM"));
    REQUIRE(tsunami::geo_gdal::gdal_driver_available("GTiff"));
    const auto corridor = corridor_result();
    const auto config = case_configuration();
    const auto data = manifest();
    auto bathy_raster = raster({-5.0, -4.0, -3.0, -2.0, -6.0, -5.0, -4.0, -3.0}, {1U, 1U, 1U, 0U, 1U, 1U, 0U, 0U});
    auto topo_raster = raster({1.0, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0, 5.0}, {0U, 0U, 1U, 1U, 0U, 1U, 1U, 1U});
    const auto bathy_import = import_record(bathy_raster, "bathymetry-import", "bathymetry-primary", "bathymetry-asset");
    const auto topo_import = import_record(topo_raster, "topography-import", "topography-primary", "topography-asset");
    const auto bathy_record = transformation_record(reference(), "bathymetry-transform", "bathymetry-import", "bathymetry-primary", "bathymetry-asset");
    const auto topo_record = transformation_record(reference(), "topography-transform", "topography-import", "topography-primary", "topography-asset");
    const auto bathy_plan = plan(bathy_raster);
    const auto topo_plan = plan(topo_raster);
    auto request = tsunami::geo::TerrainConditioningRequest{
        &config,
        &data,
        &corridor.corridor,
        &corridor.record,
        tsunami::geo::TerrainSourceRequest{tsunami::geo::TerrainSourceRole::bathymetry, &bathy_raster, &bathy_import, &bathy_plan, &bathy_record, "bathymetry-primary", "bathymetry-asset", tsunami::geo::RasterResamplingKernel::bilinear},
        tsunami::geo::TerrainSourceRequest{tsunami::geo::TerrainSourceRole::topography, &topo_raster, &topo_import, &topo_plan, &topo_record, "topography-primary", "topography-asset", tsunami::geo::RasterResamplingKernel::bilinear},
        terrain_identity(),
        tsunami::geo::TerrainConditioningPolicy{
            grid_policy(),
            tsunami::geo::TerrainMergePolicy{"bathymetry-primary", "topography-primary", 20.0, tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning, "bathymetry priority fixture"},
            tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::reject, 0.0, 0.0, 0U, 0U, 0.0, 0.0, "reject unresolved fixture gaps"},
            tsunami::geo::TerrainUncertaintyPolicy{tsunami::geo::TerrainUncertaintyCombination::not_computed, std::nullopt, std::nullopt, std::nullopt, "not reported"}},
        std::filesystem::temp_directory_path()};
    const auto result = tsunami::geo_gdal::condition_terrain_with_gdal(request).value();
    CHECK(result.diagnostics.unresolved_cell_count == 0U);
    CHECK(result.diagnostics.active_cell_count == 8U);
    CHECK(result.terrain.valid_mask()[0U] == 1U);
    CHECK(result.terrain.minimum_elevation_m() == -6.0);
    CHECK(result.terrain.maximum_elevation_m() == 5.0);
    const auto out_dir = std::filesystem::temp_directory_path() / "tsunami-terrain-geotiff-test";
    const auto terrain_path = out_dir / "illustrative_conditioned_terrain.tif";
    const auto coverage_path = out_dir / "illustrative_corridor_coverage.tif";
    const auto lineage_path = out_dir / "illustrative_cell_lineage.tif";
    REQUIRE(tsunami::geo_gdal::write_terrain_inspection_geotiffs_with_gdal(terrain_path, coverage_path, lineage_path, result.terrain, result.record).has_value());
    CHECK(std::filesystem::exists(terrain_path));
    CHECK(std::filesystem::exists(coverage_path));
    CHECK(std::filesystem::exists(lineage_path));

    auto point_registered = raster(std::vector<double>(8U, 0.0), std::vector<std::uint8_t>(8U, 1U), tsunami::geo::RasterCellRegistration::pixel_is_point);
    const auto bad_import = import_record(point_registered, "bathymetry-import", "bathymetry-primary", "bathymetry-asset");
    auto bad_request = request;
    bad_request.bathymetry.raster = &point_registered;
    bad_request.bathymetry.import_record = &bad_import;
    CHECK_FALSE(tsunami::geo::prepare_terrain_conditioning(bad_request).has_value());
}

TEST_CASE("conditioned terrain artefact bundle reads back into Regional2D preflight and transfer", "[geo][terrain][gdal][terrain-artifact-readback]")
{
    REQUIRE(tsunami::geo_gdal::gdal_driver_available("GTiff"));
    const auto corridor = corridor_result();
    const auto config = case_configuration();
    const auto data = manifest();
    auto bathy_raster = raster({-5.0, -4.0, -3.0, -2.0, -6.0, -5.0, -4.0, -3.0}, {1U, 1U, 1U, 0U, 1U, 1U, 0U, 0U});
    auto topo_raster = raster({1.0, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0, 5.0}, {0U, 0U, 1U, 1U, 0U, 1U, 1U, 1U});
    const auto bathy_import = import_record(bathy_raster, "bathymetry-import", "bathymetry-primary", "bathymetry-asset");
    const auto topo_import = import_record(topo_raster, "topography-import", "topography-primary", "topography-asset");
    const auto bathy_record = transformation_record(reference(), "bathymetry-transform", "bathymetry-import", "bathymetry-primary", "bathymetry-asset");
    const auto topo_record = transformation_record(reference(), "topography-transform", "topography-import", "topography-primary", "topography-asset");
    const auto bathy_plan = plan(bathy_raster);
    const auto topo_plan = plan(topo_raster);
    auto request = tsunami::geo::TerrainConditioningRequest{
        &config,
        &data,
        &corridor.corridor,
        &corridor.record,
        tsunami::geo::TerrainSourceRequest{tsunami::geo::TerrainSourceRole::bathymetry, &bathy_raster, &bathy_import, &bathy_plan, &bathy_record, "bathymetry-primary", "bathymetry-asset", tsunami::geo::RasterResamplingKernel::bilinear},
        tsunami::geo::TerrainSourceRequest{tsunami::geo::TerrainSourceRole::topography, &topo_raster, &topo_import, &topo_plan, &topo_record, "topography-primary", "topography-asset", tsunami::geo::RasterResamplingKernel::bilinear},
        terrain_identity(),
        tsunami::geo::TerrainConditioningPolicy{
            grid_policy(),
            tsunami::geo::TerrainMergePolicy{"bathymetry-primary", "topography-primary", 20.0, tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning, "bathymetry priority fixture"},
            tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::reject, 0.0, 0.0, 0U, 0U, 0.0, 0.0, "reject unresolved fixture gaps"},
            tsunami::geo::TerrainUncertaintyPolicy{tsunami::geo::TerrainUncertaintyCombination::not_computed, std::nullopt, std::nullopt, std::nullopt, "not reported"}},
        std::filesystem::temp_directory_path()};
    const auto produced = tsunami::geo_gdal::condition_terrain_with_gdal(request).value();
    const auto original_terrain = produced.terrain;
    const auto original_record = produced.record;
    REQUIRE(tsunami::geo::validate_terrain_conditioning_record(produced.record).has_value());

    const auto case_root = readback_case_root("success");
    const auto record_path = case_root / tsunami::geo::default_terrain_conditioning_record_path(produced.record.identity.output_dataset_id);
    REQUIRE(tsunami::geo::write_terrain_conditioning_record(record_path, produced.record).has_value());
    auto parsed_record = tsunami::geo::read_terrain_conditioning_record(record_path);
    REQUIRE(parsed_record.has_value());
    check_record_fields_equal(parsed_record.value(), produced.record);

    auto paths = tsunami::geo_gdal::make_conditioned_terrain_artifact_paths(case_root, parsed_record.value());
    REQUIRE(paths.has_value());
    CHECK(paths.value().terrain_path.filename() == "conditioned-terrain.tif");
    CHECK(paths.value().coverage_path.filename() == "conditioned-terrain.coverage.tif");
    CHECK(paths.value().lineage_path.filename() == "conditioned-terrain.lineage.tif");
    REQUIRE(tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths.value(), produced.terrain, parsed_record.value()).has_value());
    auto readback = tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(
        paths.value(),
        parsed_record.value(),
        tsunami::geo_gdal::ConditionedTerrainArtifactReadPolicy{parsed_record.value().grid_policy.maximum_output_cells});
    REQUIRE(readback.has_value());
    CHECK(readback.value().terrain == produced.terrain);
    CHECK(readback.value().diagnostics.artefact_contract_version == tsunami::geo_gdal::conditioned_terrain_artifact_contract_version);
    CHECK(readback.value().diagnostics.valid_terrain_cell_count == produced.record.diagnostics.active_cell_count);
    CHECK(readback.value().diagnostics.minimum_bed_elevation_m == Catch::Approx(produced.terrain.minimum_elevation_m()));
    CHECK(readback.value().diagnostics.maximum_bed_elevation_m == Catch::Approx(produced.terrain.maximum_elevation_m()));

    const auto mesh = regional_mesh_fixture();
    auto preflight = tsunami::r2d::validate_regional2d_geometry_preflight(
        tsunami::r2d::RegionalGeometryPreflightRequest{
            &corridor.corridor,
            &corridor.record,
            &readback.value().terrain,
            &parsed_record.value(),
            &mesh,
            tsunami::r2d::RegionalMeshImportPhysicalGroups{}});
    const auto preflight_message = preflight.has_value() ? std::string{"accepted"} : preflight.error().code() + ": " + preflight.error().message();
    INFO(preflight_message);
    REQUIRE(preflight.has_value());
    CHECK(preflight.value().validation_status == "accepted");
    auto stencil = tsunami::r2d::make_regional_raster_cell_transfer_stencil(
        mesh,
        readback.value().terrain.grid(),
        tsunami::r2d::RegionalRasterCellTransferPolicy{1.0e-9, 1.0e-12, 16U});
    REQUIRE(stencil.has_value());
    auto transferred = tsunami::r2d::transfer_conditioned_terrain_to_regional_bathymetry(
        mesh,
        readback.value().terrain,
        parsed_record.value(),
        preflight.value(),
        stencil.value(),
        tsunami::fvm::FieldId{"terrain-artifact-bed"},
        "conditioned terrain bed elevation");
    REQUIRE(transferred.has_value());
    CHECK(transferred.value().bathymetry.is_bound_to(mesh));
    CHECK(transferred.value().diagnostics.minimum_bed_elevation_m <= transferred.value().diagnostics.maximum_bed_elevation_m);
    CHECK(produced.terrain == original_terrain);
    check_record_fields_equal(produced.record, original_record);
}

TEST_CASE("conditioned terrain artefact reader rejects stale metadata, unsafe paths and corrupted rasters", "[geo][terrain][gdal][terrain-artifact-readback]")
{
    REQUIRE(tsunami::geo_gdal::gdal_driver_available("GTiff"));
    const auto produced = [&] {
        const auto corridor = corridor_result();
        const auto config = case_configuration();
        const auto data = manifest();
        auto bathy_raster = raster({-5.0, -4.0, -3.0, -2.0, -6.0, -5.0, -4.0, -3.0}, {1U, 1U, 1U, 0U, 1U, 1U, 0U, 0U});
        auto topo_raster = raster({1.0, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0, 5.0}, {0U, 0U, 1U, 1U, 0U, 1U, 1U, 1U});
        const auto bathy_import = import_record(bathy_raster, "bathymetry-import", "bathymetry-primary", "bathymetry-asset");
        const auto topo_import = import_record(topo_raster, "topography-import", "topography-primary", "topography-asset");
        const auto bathy_record = transformation_record(reference(), "bathymetry-transform", "bathymetry-import", "bathymetry-primary", "bathymetry-asset");
        const auto topo_record = transformation_record(reference(), "topography-transform", "topography-import", "topography-primary", "topography-asset");
        const auto bathy_plan = plan(bathy_raster);
        const auto topo_plan = plan(topo_raster);
        auto request = tsunami::geo::TerrainConditioningRequest{
            &config,
            &data,
            &corridor.corridor,
            &corridor.record,
            tsunami::geo::TerrainSourceRequest{tsunami::geo::TerrainSourceRole::bathymetry, &bathy_raster, &bathy_import, &bathy_plan, &bathy_record, "bathymetry-primary", "bathymetry-asset", tsunami::geo::RasterResamplingKernel::bilinear},
            tsunami::geo::TerrainSourceRequest{tsunami::geo::TerrainSourceRole::topography, &topo_raster, &topo_import, &topo_plan, &topo_record, "topography-primary", "topography-asset", tsunami::geo::RasterResamplingKernel::bilinear},
            terrain_identity(),
            tsunami::geo::TerrainConditioningPolicy{
                grid_policy(),
                tsunami::geo::TerrainMergePolicy{"bathymetry-primary", "topography-primary", 20.0, tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning, "bathymetry priority fixture"},
                tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::reject, 0.0, 0.0, 0U, 0U, 0.0, 0.0, "reject unresolved fixture gaps"},
                tsunami::geo::TerrainUncertaintyPolicy{tsunami::geo::TerrainUncertaintyCombination::not_computed, std::nullopt, std::nullopt, std::nullopt, "not reported"}},
            std::filesystem::temp_directory_path()};
        return tsunami::geo_gdal::condition_terrain_with_gdal(request).value();
    }();

    const auto write_bundle = [&](std::string_view name) {
        const auto root = readback_case_root(name);
        auto paths = tsunami::geo_gdal::make_conditioned_terrain_artifact_paths(root, produced.record);
        REQUIRE(paths.has_value());
        auto written = tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths.value(), produced.terrain, produced.record);
        const auto written_message = written.has_value() ? std::string{"written"} : written.error().code() + ": " + written.error().message();
        INFO(written_message);
        REQUIRE(written.has_value());
        return paths.value();
    };
    const auto policy = tsunami::geo_gdal::ConditionedTerrainArtifactReadPolicy{produced.record.grid_policy.maximum_output_cells};

    SECTION("metadata identity and role mismatches are rejected")
    {
        auto paths = write_bundle("metadata");
        set_dataset_metadata(paths.coverage_path, "TSUNAMI_ARTIFACT_ROLE", "conditioned_terrain");
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.role_mismatch");

        paths = write_bundle("lineage-version");
        set_dataset_metadata(paths.lineage_path, "TSUNAMI_LINEAGE_ENCODING_VERSION", "terrain-cell-lineage-code-v9");
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.band_metadata_mismatch");

        paths = write_bundle("terrain-revision");
        set_dataset_metadata(paths.terrain_path, "TSUNAMI_TERRAIN_REVISION", "99");
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.identity_mismatch");

        paths = write_bundle("case-revision");
        set_dataset_metadata(paths.terrain_path, "TSUNAMI_CASE_REVISION", "99");
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.identity_mismatch");

        paths = write_bundle("manifest-revision");
        set_dataset_metadata(paths.coverage_path, "TSUNAMI_MANIFEST_REVISION", "99");
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.identity_mismatch");

        paths = write_bundle("formula-version");
        set_dataset_metadata(paths.lineage_path, "TSUNAMI_FORMULA_VERSION", "terrain-conditioning-formula-v9");
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.band_metadata_mismatch");
    }

    SECTION("resource and path safety is enforced before read")
    {
        auto paths = write_bundle("paths");
        std::filesystem::remove(paths.coverage_path);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.file_missing");

        paths = write_bundle("directory-path");
        std::filesystem::remove(paths.coverage_path);
        std::filesystem::create_directory(paths.coverage_path);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.file_missing");

        paths.coverage_path = paths.terrain_path;
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.path_invalid");
        auto unsafe = produced.record;
        unsafe.output_path = std::filesystem::path{"outputs/terrain/../escape.tif"};
        CHECK_FALSE(tsunami::geo_gdal::make_conditioned_terrain_artifact_paths(readback_case_root("unsafe"), unsafe).has_value());

        paths = write_bundle("policy-limit");
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, tsunami::geo_gdal::ConditionedTerrainArtifactReadPolicy{1U}).error().code() == "geo.terrain.artifact_read.grid_mismatch");
    }

    SECTION("grid and CRS evidence must match semantically")
    {
        auto paths = write_bundle("pixel-point");
        set_dataset_metadata(paths.terrain_path, "AREA_OR_POINT", "Point");
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.grid_mismatch");

        paths = write_bundle("affine");
        shift_geotransform(paths.coverage_path);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.grid_mismatch");

        paths = write_bundle("crs");
        set_dataset_crs(paths.lineage_path, "EPSG:4326");
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.crs_mismatch");

        paths = write_bundle("same-crs-wkt");
        set_dataset_crs(paths.terrain_path, "LOCAL_CS[\"Synthetic metric\"]");
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).has_value());
    }

    SECTION("band contract metadata is strict")
    {
        auto paths = write_bundle("description");
        set_band_contract_metadata(paths.terrain_path, std::string_view{"wrong_description"}, std::nullopt, std::nullopt, std::nullopt, std::nullopt);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.band_metadata_mismatch");

        paths = write_bundle("unit");
        set_band_contract_metadata(paths.coverage_path, std::nullopt, std::string_view{"m"}, std::nullopt, std::nullopt, std::nullopt);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.band_metadata_mismatch");

        paths = write_bundle("scale");
        set_band_contract_metadata(paths.lineage_path, std::nullopt, std::nullopt, 2.0, std::nullopt, std::nullopt);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.band_metadata_mismatch");

        paths = write_bundle("offset");
        set_band_contract_metadata(paths.coverage_path, std::nullopt, std::nullopt, std::nullopt, 1.0, std::nullopt);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.band_metadata_mismatch");

        paths = write_bundle("nodata");
        set_band_contract_metadata(paths.terrain_path, std::nullopt, std::nullopt, std::nullopt, std::nullopt, -9999.0);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.band_metadata_mismatch");
    }

    SECTION("coverage and lineage corruptions are rejected")
    {
        auto paths = write_bundle("coverage-range");
        overwrite_coverage_value(paths.coverage_path, 1.25);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.coverage_invalid");

        paths = write_bundle("coverage-negative");
        overwrite_coverage_value(paths.coverage_path, -0.25);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.coverage_invalid");

        paths = write_bundle("lineage-code");
        overwrite_lineage_code(paths.lineage_path, 0U);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.lineage_code_invalid");

        paths = write_bundle("active-invalid-mask");
        overwrite_mask_value(paths.terrain_path, 0U);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.bundle_inconsistent");

        paths = write_bundle("nonfinite-bed");
        overwrite_terrain_value(paths.terrain_path, std::numeric_limits<double>::quiet_NaN());
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.bundle_inconsistent");

        paths = write_bundle("coverage-class");
        overwrite_coverage_value(paths.coverage_path, 0.0);
        CHECK(tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy).error().code() == "geo.terrain.artifact_read.bundle_inconsistent");
    }
}

TEST_CASE("conditioned terrain artefact writer replaces bundles transactionally", "[geo][terrain][gdal][terrain-artifact-readback][transaction]")
{
    REQUIRE(tsunami::geo_gdal::gdal_driver_available("GTiff"));
    const auto produced = conditioned_artifact_fixture();
    const auto policy = tsunami::geo_gdal::ConditionedTerrainArtifactReadPolicy{produced.record.grid_policy.maximum_output_cells};

    const auto make_paths = [&](std::string_view name) {
        const auto root = readback_case_root(name);
        auto paths = tsunami::geo_gdal::make_conditioned_terrain_artifact_paths(root, produced.record);
        REQUIRE(paths.has_value());
        return std::pair{root, paths.value()};
    };

    const auto write_bundle = [&](const tsunami::geo_gdal::ConditionedTerrainArtifactPaths &paths) {
        auto written = tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths, produced.terrain, produced.record);
        const auto message = written.has_value() ? std::string{"written"} : written.error().code() + ": " + written.error().message();
        INFO(message);
        REQUIRE(written.has_value());
    };

    SECTION("lineage encoding v1 is fixed and invalid values are rejected before persistence")
    {
        const auto expected = std::array<std::pair<tsunami::geo::TerrainCellLineage, std::uint16_t>, 10U>{{
            {tsunami::geo::TerrainCellLineage::outside_corridor, 1U},
            {tsunami::geo::TerrainCellLineage::excluded_boundary_fraction, 2U},
            {tsunami::geo::TerrainCellLineage::bathymetry_selected, 3U},
            {tsunami::geo::TerrainCellLineage::topography_selected, 4U},
            {tsunami::geo::TerrainCellLineage::overlap_bathymetry_selected, 5U},
            {tsunami::geo::TerrainCellLineage::overlap_topography_selected, 6U},
            {tsunami::geo::TerrainCellLineage::overlap_bathymetry_selected_with_conflict, 7U},
            {tsunami::geo::TerrainCellLineage::overlap_topography_selected_with_conflict, 8U},
            {tsunami::geo::TerrainCellLineage::filled_from_bathymetry_neighbourhood, 9U},
            {tsunami::geo::TerrainCellLineage::filled_from_topography_neighbourhood, 10U},
        }};
        for (const auto &[lineage, code] : expected) {
            CHECK(tsunami::geo::terrain_lineage_code(lineage) == code);
            auto decoded = tsunami::geo::terrain_cell_lineage_from_code(code);
            REQUIRE(decoded.has_value());
            CHECK(decoded.value() == lineage);
        }
        CHECK(tsunami::geo::terrain_lineage_code(static_cast<tsunami::geo::TerrainCellLineage>(999U)) == 0U);
        CHECK_FALSE(tsunami::geo::terrain_cell_lineage_from_code(0U).has_value());
        CHECK_FALSE(tsunami::geo::terrain_cell_lineage_from_code(11U).has_value());

        auto bad_lineage = produced.terrain.cell_lineage();
        bad_lineage[0U] = static_cast<tsunami::geo::TerrainCellLineage>(999U);
        const auto invalid = tsunami::geo::ConditionedTerrainRaster{
            produced.terrain.grid(),
            produced.terrain.values(),
            produced.terrain.valid_mask(),
            produced.terrain.corridor_coverage_fraction(),
            bad_lineage,
            produced.terrain.minimum_elevation_m(),
            produced.terrain.maximum_elevation_m()};
        const auto [root, paths] = make_paths("invalid-lineage");
        auto written = tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths, invalid, produced.record);
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == "geo.terrain.artifact_write.lineage_code_invalid");
        CHECK_FALSE(contains_transaction_directory(root));
    }

    SECTION("current, relative and absolute roots create canonical sibling paths")
    {
        auto dot_paths = tsunami::geo_gdal::make_conditioned_terrain_artifact_paths(".", produced.record);
        REQUIRE(dot_paths.has_value());
        CHECK(dot_paths.value().terrain_path.generic_string() == "outputs/terrain/conditioned-terrain.tif");
        CHECK(dot_paths.value().coverage_path.generic_string() == "outputs/terrain/conditioned-terrain.coverage.tif");
        CHECK(dot_paths.value().lineage_path.generic_string() == "outputs/terrain/conditioned-terrain.lineage.tif");

        auto relative_paths = tsunami::geo_gdal::make_conditioned_terrain_artifact_paths("relative-case-root", produced.record);
        REQUIRE(relative_paths.has_value());
        CHECK(relative_paths.value().terrain_path.generic_string() == "relative-case-root/outputs/terrain/conditioned-terrain.tif");

        const auto absolute_root = readback_case_root("absolute-root");
        auto absolute_paths = tsunami::geo_gdal::make_conditioned_terrain_artifact_paths(absolute_root, produced.record);
        REQUIRE(absolute_paths.has_value());
        CHECK(absolute_paths.value().terrain_path.is_absolute());
        CHECK(absolute_paths.value().coverage_path.parent_path() == absolute_paths.value().terrain_path.parent_path());
        CHECK(absolute_paths.value().lineage_path.parent_path() == absolute_paths.value().terrain_path.parent_path());
    }

    SECTION("malformed raster storage cardinality is rejected before filesystem mutation")
    {
        struct Case
        {
            std::string name;
            std::string field;
            tsunami::geo::ConditionedTerrainRaster terrain;
            std::string actual;
        };

        const auto make_raster = [&](std::vector<double> values,
                                     std::vector<std::uint8_t> mask,
                                     std::vector<double> coverage,
                                     std::vector<tsunami::geo::TerrainCellLineage> lineage) {
            return tsunami::geo::ConditionedTerrainRaster{
                produced.terrain.grid(),
                std::move(values),
                std::move(mask),
                std::move(coverage),
                std::move(lineage),
                produced.terrain.minimum_elevation_m(),
                produced.terrain.maximum_elevation_m()};
        };

        auto long_values = produced.terrain.values();
        long_values.push_back(123.0);
        auto cases = std::vector<Case>{};
        cases.push_back(Case{
            "short-values",
            "values",
            make_raster(
                std::vector<double>{produced.terrain.values().begin(), produced.terrain.values().end() - 1},
                produced.terrain.valid_mask(),
                produced.terrain.corridor_coverage_fraction(),
                produced.terrain.cell_lineage()),
            std::to_string(produced.terrain.values().size() - 1U)});
        cases.push_back(Case{
            "short-mask",
            "valid_mask",
            make_raster(
                produced.terrain.values(),
                std::vector<std::uint8_t>{produced.terrain.valid_mask().begin(), produced.terrain.valid_mask().end() - 1},
                produced.terrain.corridor_coverage_fraction(),
                produced.terrain.cell_lineage()),
            std::to_string(produced.terrain.valid_mask().size() - 1U)});
        cases.push_back(Case{
            "short-coverage",
            "corridor_coverage_fraction",
            make_raster(
                produced.terrain.values(),
                produced.terrain.valid_mask(),
                std::vector<double>{produced.terrain.corridor_coverage_fraction().begin(), produced.terrain.corridor_coverage_fraction().end() - 1},
                produced.terrain.cell_lineage()),
            std::to_string(produced.terrain.corridor_coverage_fraction().size() - 1U)});
        cases.push_back(Case{
            "short-lineage",
            "cell_lineage",
            make_raster(
                produced.terrain.values(),
                produced.terrain.valid_mask(),
                produced.terrain.corridor_coverage_fraction(),
                std::vector<tsunami::geo::TerrainCellLineage>{produced.terrain.cell_lineage().begin(), produced.terrain.cell_lineage().end() - 1}),
            std::to_string(produced.terrain.cell_lineage().size() - 1U)});
        cases.push_back(Case{
            "long-values",
            "values",
            make_raster(
                std::move(long_values),
                produced.terrain.valid_mask(),
                produced.terrain.corridor_coverage_fraction(),
                produced.terrain.cell_lineage()),
            std::to_string(produced.terrain.values().size() + 1U)});

        for (const auto &test_case : cases) {
            const auto [root, paths] = make_paths(test_case.name);
            write_bytes(paths.terrain_path, "existing terrain");
            write_bytes(paths.coverage_path, "existing coverage");
            write_bytes(paths.lineage_path, "existing lineage");
            write_bytes(path_with_suffix(paths.terrain_path, ".aux.xml"), "existing sidecar");
            const auto snapshot = snapshot_files(artifact_files(paths));

            auto written = tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths, test_case.terrain, produced.record);
            REQUIRE_FALSE(written.has_value());
            CHECK(written.error().code() == "geo.terrain.artifact_write.request_invalid");
            CHECK(context_value(written.error(), "state_changed") == "false");
            CHECK(context_value(written.error(), "field") == test_case.field);
            CHECK(context_value(written.error(), "expected") == std::to_string(produced.terrain.cell_count()));
            CHECK(context_value(written.error(), "actual") == test_case.actual);
            check_snapshot_restored(snapshot);
            CHECK_FALSE(contains_transaction_directory(root));
        }
    }

    SECTION("backup preparation failure restores all prior targets and sidecars byte-for-byte")
    {
        const auto [root, paths] = make_paths("backup-rollback");
        write_bundle(paths);
        write_bytes(path_with_suffix(paths.terrain_path, ".aux.xml"), "old terrain aux");
        write_bytes(path_with_suffix(paths.coverage_path, ".msk"), "old coverage mask");
        write_bytes(path_with_suffix(paths.lineage_path, ".aux.xml"), "old lineage aux");
        const auto snapshot = snapshot_files(artifact_files(paths));

        const auto fail_backup = ScopedGdalConfig{"TSUNAMI_TEST_TERRAIN_ARTIFACT_FAIL_BACKUP_AFTER_MOVED", "1"};
        auto replaced = tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths, produced.terrain, produced.record);
        REQUIRE_FALSE(replaced.has_value());
        CHECK(replaced.error().code() == "geo.terrain.artifact_write.replacement_failed");
        check_snapshot_restored(snapshot);
        CHECK_FALSE(contains_transaction_directory(root));
    }

    SECTION("final target validation failure rolls back the complete previous bundle")
    {
        const auto [root, paths] = make_paths("final-readback-rollback");
        write_bundle(paths);
        write_bytes(path_with_suffix(paths.terrain_path, ".aux.xml"), "old terrain aux");
        write_bytes(path_with_suffix(paths.coverage_path, ".aux.xml"), "old coverage aux");
        write_bytes(path_with_suffix(paths.lineage_path, ".msk"), "old lineage mask");
        const auto snapshot = snapshot_files(artifact_files(paths));

        const auto force_final = ScopedGdalConfig{"TSUNAMI_TEST_TERRAIN_ARTIFACT_FORCE_FINAL_READBACK_FAILURE", "YES"};
        auto replaced = tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths, produced.terrain, produced.record);
        REQUIRE_FALSE(replaced.has_value());
        CHECK(replaced.error().code() == "geo.terrain.artifact_write.replacement_failed");
        check_snapshot_restored(snapshot);
        auto readback = tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy);
        REQUIRE(readback.has_value());
        CHECK(readback.value().terrain == produced.terrain);
        CHECK_FALSE(contains_transaction_directory(root));
    }

    SECTION("post-commit transaction backup cleanup failure reports committed state")
    {
        const auto [root, paths] = make_paths("post-commit-cleanup");
        const auto unrelated = root / "outputs/terrain/unrelated.keep";
        write_bytes(unrelated, "preserve me");
        const auto unrelated_snapshot = read_bytes(unrelated);

        const auto fail_cleanup = ScopedGdalConfig{"TSUNAMI_TEST_TERRAIN_ARTIFACT_FAIL_TRANSACTION_CLEANUP", "YES"};
        auto written = tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths, produced.terrain, produced.record);
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == "geo.terrain.artifact_write.cleanup_failed");
        CHECK(context_value(written.error(), "state_changed") == "true");
        CHECK(context_value(written.error(), "recovery_directory_count") != "0");
        CHECK(written.error().context_value("recovery_directory_0").has_value());
        auto readback = tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy);
        REQUIRE(readback.has_value());
        CHECK(readback.value().terrain == produced.terrain);
        CHECK(read_bytes(unrelated) == unrelated_snapshot);
    }

    SECTION("partial multi-parent transaction allocation cleans already-owned directories")
    {
        const auto root = readback_case_root("partial-allocation");
        const auto paths = tsunami::geo_gdal::ConditionedTerrainArtifactPaths{
            root / "outputs/terrain/terrain/conditioned.tif",
            root / "outputs/terrain/coverage/conditioned.coverage.tif",
            root / "outputs/terrain/lineage/conditioned.lineage.tif"};
        write_bytes(paths.terrain_path, "existing terrain");
        write_bytes(paths.coverage_path, "existing coverage");
        write_bytes(paths.lineage_path, "existing lineage");
        write_bytes(path_with_suffix(paths.coverage_path, ".aux.xml"), "existing coverage aux");
        const auto snapshot = snapshot_files(artifact_files(paths));

        const auto failed_parent = paths.coverage_path.parent_path().generic_string();
        const auto fail_coverage_parent = ScopedGdalConfig{
            "TSUNAMI_TEST_TERRAIN_ARTIFACT_FAIL_TRANSACTION_PARENT",
            failed_parent.c_str()};
        auto written = tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths, produced.terrain, produced.record);
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code() == "geo.terrain.artifact_write.replacement_failed");
        CHECK(context_value(written.error(), "state_changed") == "false");
        check_snapshot_restored(snapshot);
        CHECK_FALSE(contains_transaction_directory(root));
    }

    SECTION("fixed legacy temp and backup sentinels are preserved on success and staging failure")
    {
        const auto [root, paths] = make_paths("sentinels");
        const auto sentinels = std::vector<std::filesystem::path>{
            std::filesystem::path{paths.terrain_path.string() + ".tmp.tif"},
            std::filesystem::path{paths.coverage_path.string() + ".tmp.tif"},
            std::filesystem::path{paths.lineage_path.string() + ".tmp.tif"},
            std::filesystem::path{paths.terrain_path.string() + ".bak"},
            std::filesystem::path{paths.coverage_path.string() + ".bak"},
            std::filesystem::path{paths.lineage_path.string() + ".bak"}};
        for (std::size_t i = 0U; i < sentinels.size(); ++i) {
            write_bytes(sentinels[i], "sentinel " + std::to_string(i));
        }
        const auto snapshot = snapshot_files(sentinels);
        write_bundle(paths);
        check_snapshot_restored(snapshot);

        const auto fail_after_terrain = ScopedGdalConfig{"TSUNAMI_TEST_TERRAIN_ARTIFACT_FAIL_AFTER_TERRAIN_STAGING", "YES"};
        auto failed = tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths, produced.terrain, produced.record);
        REQUIRE_FALSE(failed.has_value());
        CHECK(failed.error().code() == "geo.terrain.artifact_write.temporary_validation_failed");
        check_snapshot_restored(snapshot);
        CHECK_FALSE(contains_transaction_directory(root));
    }

    SECTION("stale original-name sidecars are removed from the accepted replacement state")
    {
        const auto [root, paths] = make_paths("stale-sidecars");
        write_bytes(path_with_suffix(paths.terrain_path, ".msk"), "stale terrain mask");
        write_bytes(path_with_suffix(paths.terrain_path, ".aux.xml"), "stale terrain aux");
        write_bytes(path_with_suffix(paths.coverage_path, ".aux.xml"), "stale coverage aux");
        write_bytes(path_with_suffix(paths.lineage_path, ".msk"), "stale lineage mask");
        write_bundle(paths);
        auto readback = tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(paths, produced.record, policy);
        REQUIRE(readback.has_value());
        CHECK(readback.value().terrain == produced.terrain);
        CHECK_FALSE(std::filesystem::exists(path_with_suffix(paths.terrain_path, ".msk")));
        CHECK_FALSE(std::filesystem::exists(path_with_suffix(paths.terrain_path, ".aux.xml")));
        CHECK_FALSE(std::filesystem::exists(path_with_suffix(paths.coverage_path, ".aux.xml")));
        CHECK_FALSE(std::filesystem::exists(path_with_suffix(paths.lineage_path, ".msk")));
        CHECK_FALSE(contains_transaction_directory(root));
    }

    SECTION("single terrain compatibility writer preserves fixed companion sentinels on success and failure")
    {
        const auto root = readback_case_root("single-writer");
        const auto terrain_path = root / "outputs/terrain/single.tif";
        const auto sentinels = std::vector<std::filesystem::path>{
            std::filesystem::path{terrain_path.string() + ".coverage.tmp.tif"},
            std::filesystem::path{terrain_path.string() + ".lineage.tmp.tif"}};
        write_bytes(sentinels[0U], "do not delete coverage");
        write_bytes(sentinels[1U], "do not delete lineage");
        const auto snapshot = snapshot_files(sentinels);

        auto written = tsunami::geo_gdal::write_conditioned_terrain_geotiff_with_gdal(terrain_path, produced.terrain, produced.record);
        REQUIRE(written.has_value());
        check_snapshot_restored(snapshot);

        const auto fail_after_terrain = ScopedGdalConfig{"TSUNAMI_TEST_TERRAIN_ARTIFACT_FAIL_AFTER_TERRAIN_STAGING", "YES"};
        auto failed = tsunami::geo_gdal::write_conditioned_terrain_geotiff_with_gdal(terrain_path, produced.terrain, produced.record);
        REQUIRE_FALSE(failed.has_value());
        CHECK(failed.error().code() == "geo.terrain.artifact_write.temporary_validation_failed");
        check_snapshot_restored(snapshot);
        CHECK_FALSE(contains_transaction_directory(root));
    }
}

TEST_CASE("rotated conditioned terrain artefact bundle reads back end-to-end", "[geo][terrain][gdal][terrain-artifact-readback][rotated]")
{
    REQUIRE(tsunami::geo_gdal::gdal_driver_available("GTiff"));
    auto produced = conditioned_artifact_fixture();
    constexpr auto rotated_component = 7.0710678118654755;
    const auto transform = tsunami::geo::RasterAffineTransform{
        produced.record.grid.transform().origin_x,
        rotated_component,
        -rotated_component,
        produced.record.grid.transform().origin_y,
        rotated_component,
        rotated_component};
    auto extent = tsunami::geo::raster_extent_from_corners(produced.record.grid.width(), produced.record.grid.height(), transform);
    REQUIRE(extent.has_value());
    auto rotated_grid = tsunami::geo::TerrainTargetGrid{
        produced.record.grid.width(),
        produced.record.grid.height(),
        produced.record.grid.spacing_m(),
        transform,
        extent.value(),
        produced.record.grid.target_reference(),
        produced.record.grid.xi_min_m(),
        produced.record.grid.xi_max_m(),
        produced.record.grid.eta_bottom_m(),
        produced.record.grid.eta_top_m(),
        produced.record.grid.longitudinal_padding_m(),
        produced.record.grid.transverse_padding_m()};
    produced.record.grid = rotated_grid;
    produced.terrain = tsunami::geo::ConditionedTerrainRaster{
        rotated_grid,
        produced.terrain.values(),
        produced.terrain.valid_mask(),
        produced.terrain.corridor_coverage_fraction(),
        produced.terrain.cell_lineage(),
        produced.terrain.minimum_elevation_m(),
        produced.terrain.maximum_elevation_m()};
    REQUIRE(tsunami::geo::validate_terrain_conditioning_record(produced.record).has_value());
    CHECK(std::abs(produced.record.grid.transform().row_rotation) > 1.0e-12);
    CHECK(std::abs(produced.record.grid.transform().column_rotation) > 1.0e-12);
    const auto root = readback_case_root("rotated");
    auto paths = tsunami::geo_gdal::make_conditioned_terrain_artifact_paths(root, produced.record);
    REQUIRE(paths.has_value());
    REQUIRE(tsunami::geo_gdal::write_conditioned_terrain_artifacts_with_gdal(paths.value(), produced.terrain, produced.record).has_value());
    auto readback = tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(
        paths.value(),
        produced.record,
        tsunami::geo_gdal::ConditionedTerrainArtifactReadPolicy{produced.record.grid_policy.maximum_output_cells});
    REQUIRE(readback.has_value());
    CHECK(readback.value().terrain == produced.terrain);
    CHECK_FALSE(contains_transaction_directory(root));
}
#endif
