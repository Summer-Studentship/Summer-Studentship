#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <tsunami/geo/CorridorConstruction.hpp>
#include <tsunami/geo/TerrainConditioning.hpp>
#include <tsunami/geo/TerrainConditioningParsing.hpp>
#include <tsunami/geo/TerrainConditioningSerialisation.hpp>

#ifdef TSUNAMI_ENABLE_GEOSPATIAL
#include <tsunami/geo_gdal/GdalGeospatialImporter.hpp>
#include <tsunami/geo_gdal/GdalTerrainResampler.hpp>
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
    const auto adapter = read_text("src/geo_gdal/include/tsunami/geo_gdal/GdalTerrainResampler.hpp");
    CHECK(adapter.find("GDALDataset") == std::string::npos);
    CHECK(adapter.find("GDALRasterBand") == std::string::npos);
}

#ifdef TSUNAMI_ENABLE_GEOSPATIAL
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
#endif
