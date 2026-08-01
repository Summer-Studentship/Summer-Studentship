#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/geo/CorridorConstructionParsing.hpp>
#include <tsunami/geo/CorridorConstructionSerialisation.hpp>
#include <tsunami/geo/TerrainConditioningParsing.hpp>
#include <tsunami/geo/TerrainConditioningSerialisation.hpp>

namespace
{
    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        auto value = error.context_value(key);
        return value ? *value : std::string{};
    }

    [[nodiscard]] auto case_ref(std::string id = "record-case") -> tsunami::data::CaseRevisionRef
    {
        return tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string(std::move(id)).value(), 1U};
    }

    [[nodiscard]] auto reference(std::string code = "EN-METRIC-1") -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"TEST"},
            std::move(code),
            "Synthetic east north metric reference",
            std::string{"LOCAL_CS[\"Synthetic metric\"]"},
            std::nullopt,
            std::string{"Synthetic datum"},
            std::string{"Synthetic realisation"},
            -0.0,
            {"Northing", "Easting"},
            {"north", "east"},
            {"metre-north", "metre-east"}};
    }

    [[nodiscard]] auto target_reference() -> tsunami::geo::ComputationalTargetReference
    {
        return tsunami::geo::ComputationalTargetReference{
            reference(),
            std::nullopt,
            tsunami::geo::ComputationalAxisConvention::east_north,
            "m",
            std::nullopt,
            std::nullopt};
    }

    [[nodiscard]] auto import_identity(std::string import_id, std::string dataset_id, std::string asset_id) -> tsunami::geo::GeospatialImportIdentity
    {
        return tsunami::geo::GeospatialImportIdentity{
            std::move(import_id),
            1U,
            case_ref(),
            "record-manifest",
            1U,
            std::move(dataset_id),
            std::move(asset_id),
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard]] auto transformation_identity(std::string id, std::string dataset_id) -> tsunami::geo::CoordinateTransformationIdentity
    {
        auto asset_id = "asset-" + dataset_id;
        return tsunami::geo::CoordinateTransformationIdentity{
            std::move(id),
            1U,
            case_ref(),
            "record-manifest",
            1U,
            dataset_id + "-import",
            1U,
            dataset_id,
            asset_id,
            std::move(dataset_id),
            "transform-process",
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard]] auto evidence(
        tsunami::geo::CorridorReferencePointRole role,
        std::string point_id,
        tsunami::geo::Coordinate3D coordinate) -> tsunami::geo::CorridorReferencePointEvidence
    {
        const auto dataset = role == tsunami::geo::CorridorReferencePointRole::epicentre ? "epicentre-dataset" : "target-dataset";
        return tsunami::geo::CorridorReferencePointEvidence{
            role,
            std::move(point_id),
            role == tsunami::geo::CorridorReferencePointRole::epicentre ? "reported earthquake epicentre" : "reported target point",
            coordinate,
            0U,
            std::nullopt,
            transformation_identity(role == tsunami::geo::CorridorReferencePointRole::epicentre ? "epicentre-transform" : "target-transform", dataset),
            reference("SOURCE"),
            target_reference(),
            "Synthetic record fixture",
            "https://example.test/source",
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard]] auto corridor_record() -> tsunami::geo::CorridorConstructionRecord
    {
        auto record = tsunami::geo::CorridorConstructionRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{tsunami::geo::corridor_construction_record_schema_name}, tsunami::geo::supported_corridor_construction_record_version};
        record.policy_version = tsunami::geo::supported_corridor_construction_record_policy_version;
        record.identity = tsunami::geo::CorridorConstructionIdentity{"corridor-axis", 1U, case_ref(), "axis", "corridor-dataset", "corridor-process", "2026-07-31T00:00:00Z"};
        record.scenario_id = "record-scenario";
        record.target_site = "synthetic-site";
        record.epicentre = evidence(tsunami::geo::CorridorReferencePointRole::epicentre, "epicentre-point", {0.0, 0.0, -5.0});
        record.target = evidence(tsunami::geo::CorridorReferencePointRole::target, "target-point", {100.0, 0.0, 2.0});
        record.target_reference = target_reference();
        record.policy = tsunami::geo::CorridorConstructionPolicy{1.0, 0.001, 0.001, 1.0e-12, 1.0e-7, 1.0e-12, "fixture tolerance"};
        record.configured_origin = {0.0, 0.0};
        record.configured_bearing_degrees = 90.0;
        record.derived_bearing_degrees = 90.0;
        record.offshore_extent_m = 20.0;
        record.epicentre_target_distance_m = 100.0;
        record.inland_extent_m = 30.0;
        record.total_length_m = 150.0;
        record.offshore_width_m = 40.0;
        record.inland_width_m = 40.0;
        record.narrowing_enabled = false;
        record.narrowing_rule = "constant_width";
        record.local_basis = tsunami::geo::CorridorLocalBasis{{1.0, 0.0}, {-0.0, 1.0}, 100.0, 90.0};
        record.stations = tsunami::geo::CorridorLongitudinalStations{-20.0, 0.0, 100.0, 130.0};
        record.sponge_limits = tsunami::geo::CorridorSpongeLimits{-20.0, -20.0, 0.0, 40.0};
        record.polygon = tsunami::geo::Polygon2D{{{-20.0, 20.0}, {-20.0, -20.0}, {130.0, -20.0}, {130.0, 20.0}, {-20.0, 20.0}}, {}};
        record.vertex_order_convention = "counter_clockwise_closed_exterior_ring";
        record.extent = tsunami::geo::BoundingBox2D{-20.0, -20.0, 130.0, 20.0};
        record.area_m2 = 6000.0;
        record.perimeter_m = 380.0;
        record.diagnostics = tsunami::geo::CorridorConstructionDiagnostics{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 6000.0, 6000.0, 0.0, 380.0, 380.0, 0.0, {"alpha-warning", "zeta-warning"}};
        record.configured_field_paths = {"/regional_2d/corridor"};
        record.warnings = {"alpha-warning", "zeta-warning"};
        return record;
    }

    [[nodiscard]] auto resampling(std::string dataset_id, tsunami::geo::TerrainSourceRole role) -> tsunami::geo::RasterResamplingRecord
    {
        auto record = tsunami::geo::RasterResamplingRecord{};
        record.dataset_id = dataset_id;
        record.asset_id = dataset_id + "-asset";
        record.import_identity = import_identity(dataset_id + "-import", dataset_id, record.asset_id);
        record.transformation_identity = tsunami::geo::CoordinateTransformationIdentity{
            dataset_id + "-transform",
            1U,
            case_ref(),
            "record-manifest",
            1U,
            record.import_identity.import_id,
            record.import_identity.import_revision,
            dataset_id,
            record.asset_id,
            dataset_id + "-projected",
            dataset_id + "-transform-process",
            "2026-07-31T00:00:00Z"};
        record.role = role;
        record.kernel = tsunami::geo::RasterResamplingKernel::bilinear;
        record.source_registration = tsunami::geo::RasterCellRegistration::pixel_is_area;
        record.target_registration = tsunami::geo::RasterCellRegistration::pixel_is_area;
        record.source_scale = -0.0;
        record.source_offset = role == tsunami::geo::TerrainSourceRole::bathymetry ? std::optional<double>{-0.0} : std::nullopt;
        record.minimum_source_spacing_m = 10.0;
        record.maximum_source_spacing_m = 10.0;
        record.nominal_source_spacing_m = 10.0;
        record.target_spacing_m = 10.0;
        record.maximum_upsampling_factor = 4.0;
        record.source_valid_cell_count = 4U;
        record.output_valid_cell_count = role == tsunami::geo::TerrainSourceRole::bathymetry ? 3U : 1U;
        record.source_nodata_cell_count = role == tsunami::geo::TerrainSourceRole::topography ? 2U : 1U;
        record.outside_coverage_cell_count = role == tsunami::geo::TerrainSourceRole::bathymetry ? 5U : 6U;
        record.operation = tsunami::geo::CoordinateOperationRecord{
            dataset_id + " accepted operation",
            std::string{"TEST"},
            std::string{"1001"},
            std::string{"Synthetic method"},
            std::optional<double>{-0.0},
            std::string{"fixture operation scope"},
            tsunami::geo::GeographicAreaOfInterest{-1.0, -1.0, 1.0, 1.0},
            std::nullopt,
            std::string{"{\"type\":\"Conversion\"}"},
            std::string{"+proj=noop"},
            false,
            reference("SOURCE-" + dataset_id),
            target_reference().horizontal,
            {
                tsunami::geo::CoordinateOperationGrid{
                    "zeta-grid",
                    std::filesystem::path{"grids/zeta.gsb"},
                    std::string{"zeta-package"},
                    std::string{"https://example.test/zeta.gsb"},
                    true,
                    true,
                    tsunami::data::ContentDigest{tsunami::data::DigestAlgorithm::sha256, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", tsunami::data::DigestOrigin::provider_declared},
                    tsunami::geo::GeodeticResourceVerificationStatus::declared_not_verified},
                tsunami::geo::CoordinateOperationGrid{
                    "alpha-grid",
                    std::filesystem::path{"grids/alpha.gsb"},
                    std::string{"alpha-package"},
                    std::string{"https://example.test/alpha.gsb"},
                    true,
                    true,
                    std::nullopt,
                    tsunami::geo::GeodeticResourceVerificationStatus::declared_not_verified},
            },
            "fixture-engine",
            "1.0",
            std::string{"fixture-db"}};
        record.vertical_steps = tsunami::geo::VerticalTransformationSpecification{
            true,
            {
                tsunami::geo::VerticalTransformationStep{
                    tsunami::geo::VerticalTransformationStepKind::unit_scale,
                    1.0,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    "source-height",
                    "target-height"},
                tsunami::geo::VerticalTransformationStep{
                    tsunami::geo::VerticalTransformationStepKind::constant_offset,
                    std::nullopt,
                    -0.0,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    "target-height",
                    "target-height"},
            }};
        record.adapter_name = "fixture";
        record.adapter_version = "1.0";
        return record;
    }

    [[nodiscard]] auto terrain_record() -> tsunami::geo::TerrainConditioningRecord
    {
        auto record = tsunami::geo::TerrainConditioningRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{tsunami::geo::terrain_conditioning_record_schema_name}, tsunami::geo::supported_terrain_conditioning_record_version};
        record.policy_version = tsunami::geo::supported_terrain_conditioning_record_policy_version;
        record.formula_version = tsunami::geo::terrain_conditioning_formula_version;
        record.identity = tsunami::geo::TerrainConditioningIdentity{"terrain-fixture", 1U, case_ref(), "record-manifest", 1U, "conditioned-terrain", "terrain-process", "2026-07-31T00:00:00Z"};
        record.scenario_id = "record-scenario";
        record.target_site = "synthetic-site";
        record.bathymetry_dataset_id = "bathymetry-primary";
        record.bathymetry_asset_id = "bathymetry-primary-asset";
        record.topography_dataset_id = "topography-primary";
        record.topography_asset_id = "topography-primary-asset";
        record.corridor_identity = tsunami::geo::CorridorConstructionIdentity{"corridor-axis", 1U, case_ref(), "axis", "corridor-dataset", "corridor-process", "2026-07-31T00:00:00Z"};
        record.target_reference = target_reference();
        const auto transform = tsunami::geo::RasterAffineTransform{0.0, 10.0, -0.0, 20.0, 0.0, -10.0};
        record.grid = tsunami::geo::TerrainTargetGrid{2U, 2U, 10.0, transform, {0.0, 0.0, 20.0, 20.0}, record.target_reference, -20.0, 0.0, -10.0, 10.0, -0.0, 0.0};
        record.grid_policy = tsunami::geo::TerrainTargetGridPolicy{10.0, 0.5, 4.0, 16U, 1.0e-9, 1.0e-12, "fixture grid policy"};
        record.bathymetry_resampling = resampling("bathymetry-primary", tsunami::geo::TerrainSourceRole::bathymetry);
        record.topography_resampling = resampling("topography-primary", tsunami::geo::TerrainSourceRole::topography);
        record.bathymetry_import_identity = record.bathymetry_resampling.import_identity;
        record.topography_import_identity = record.topography_resampling.import_identity;
        record.bathymetry_transformation_identity = record.bathymetry_resampling.transformation_identity;
        record.topography_transformation_identity = record.topography_resampling.transformation_identity;
        record.merge_policy = tsunami::geo::TerrainMergePolicy{"bathymetry-primary", "topography-primary", 100.0, tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning, "bathymetry priority"};
        record.gap_policy = tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::reject, 0.0, 0.0, 0U, 0U, 0.0, 0.0, "reject gaps"};
        record.diagnostics.total_cell_count = 4U;
        record.diagnostics.active_cell_count = 4U;
        record.diagnostics.bathymetry_selected_cell_count = 3U;
        record.diagnostics.topography_selected_cell_count = 1U;
        record.diagnostics.overlap_cell_count = 1U;
        record.diagnostics.overlap.overlap_cell_count = 1U;
        record.diagnostics.minimum_elevation_m = -3.0;
        record.diagnostics.maximum_elevation_m = -1.0;
        record.diagnostics.warnings = {"zeta-diagnostic", "alpha-diagnostic"};
        record.output_uncertainty = tsunami::data::DatasetUncertainty{
            tsunami::data::UncertaintyStatus::estimated,
            {
                tsunami::data::UncertaintyMeasure{"vertical_rmse", -0.0, "m", 0.95, std::string{"fixture estimate"}},
                tsunami::data::UncertaintyMeasure{"horizontal_rmse", 0.25, "m", std::nullopt, std::nullopt},
            },
            std::string{"ordered uncertainty fixture"}};
        record.output_media_type = "image/tiff";
        record.output_path = std::filesystem::path{"outputs/terrain/conditioned-terrain.tif"};
        record.digest_status = "not_computed_by_terrain_conditioning";
        record.warnings = {"zeta-warning", "alpha-warning"};
        return record;
    }

    [[nodiscard]] auto replace_once(std::string text, std::string_view needle, std::string_view replacement) -> std::string
    {
        const auto position = text.find(needle);
        REQUIRE(position != std::string::npos);
        text.replace(position, needle.size(), replacement);
        return text;
    }

    [[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string
    {
        auto file = std::ifstream{path, std::ios::binary};
        REQUIRE(file);
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

    auto check_reference_equal(const tsunami::geo::CoordinateReferenceDescriptor &left, const tsunami::geo::CoordinateReferenceDescriptor &right) -> void
    {
        CHECK(left.authority_name == right.authority_name);
        CHECK(left.authority_code == right.authority_code);
        CHECK(left.name == right.name);
        CHECK(left.canonical_wkt2 == right.canonical_wkt2);
        CHECK(left.canonical_projjson == right.canonical_projjson);
        CHECK(left.datum_name == right.datum_name);
        CHECK(left.datum_realisation == right.datum_realisation);
        CHECK(left.coordinate_epoch_decimal_year == right.coordinate_epoch_decimal_year);
        CHECK(left.axis_names == right.axis_names);
        CHECK(left.axis_directions == right.axis_directions);
        CHECK(left.axis_units == right.axis_units);
    }

    auto check_target_equal(const tsunami::geo::ComputationalTargetReference &left, const tsunami::geo::ComputationalTargetReference &right) -> void
    {
        check_reference_equal(left.horizontal, right.horizontal);
        CHECK(left.vertical.has_value() == right.vertical.has_value());
        if (left.vertical && right.vertical) {
            check_reference_equal(*left.vertical, *right.vertical);
        }
        CHECK(left.storage_axes == right.storage_axes);
        CHECK(left.horizontal_unit == right.horizontal_unit);
        CHECK(left.vertical_unit == right.vertical_unit);
        CHECK(left.vertical_positive == right.vertical_positive);
    }

    auto check_transformation_identity_equal(const tsunami::geo::CoordinateTransformationIdentity &left, const tsunami::geo::CoordinateTransformationIdentity &right) -> void
    {
        CHECK(left.transformation_id == right.transformation_id);
        CHECK(left.transformation_revision == right.transformation_revision);
        CHECK(left.case_revision == right.case_revision);
        CHECK(left.manifest_id == right.manifest_id);
        CHECK(left.manifest_revision == right.manifest_revision);
        CHECK(left.source_import_id == right.source_import_id);
        CHECK(left.source_import_revision == right.source_import_revision);
        CHECK(left.source_dataset_id == right.source_dataset_id);
        CHECK(left.source_asset_id == right.source_asset_id);
        CHECK(left.output_dataset_id == right.output_dataset_id);
        CHECK(left.output_process_id == right.output_process_id);
        CHECK(left.executed_at_utc == right.executed_at_utc);
    }

    auto check_import_identity_equal(const tsunami::geo::GeospatialImportIdentity &left, const tsunami::geo::GeospatialImportIdentity &right) -> void
    {
        CHECK(left.import_id == right.import_id);
        CHECK(left.import_revision == right.import_revision);
        CHECK(left.case_revision == right.case_revision);
        CHECK(left.manifest_id == right.manifest_id);
        CHECK(left.manifest_revision == right.manifest_revision);
        CHECK(left.dataset_id == right.dataset_id);
        CHECK(left.asset_id == right.asset_id);
        CHECK(left.executed_at_utc == right.executed_at_utc);
    }

    auto check_corridor_identity_equal(const tsunami::geo::CorridorConstructionIdentity &left, const tsunami::geo::CorridorConstructionIdentity &right) -> void
    {
        CHECK(left.corridor_id == right.corridor_id);
        CHECK(left.corridor_revision == right.corridor_revision);
        CHECK(left.case_revision == right.case_revision);
        CHECK(left.trajectory_id == right.trajectory_id);
        CHECK(left.output_dataset_id == right.output_dataset_id);
        CHECK(left.output_process_id == right.output_process_id);
        CHECK(left.executed_at_utc == right.executed_at_utc);
    }

    auto check_operation_equal(const tsunami::geo::CoordinateOperationRecord &left, const tsunami::geo::CoordinateOperationRecord &right) -> void
    {
        CHECK(left.operation_name == right.operation_name);
        CHECK(left.operation_authority == right.operation_authority);
        CHECK(left.operation_code == right.operation_code);
        CHECK(left.operation_method == right.operation_method);
        CHECK(left.operation_accuracy_m == right.operation_accuracy_m);
        CHECK(left.scope == right.scope);
        CHECK(left.area_of_use == right.area_of_use);
        CHECK(left.canonical_wkt2 == right.canonical_wkt2);
        CHECK(left.canonical_projjson == right.canonical_projjson);
        CHECK(left.canonical_pipeline == right.canonical_pipeline);
        CHECK(left.ballpark == right.ballpark);
        check_reference_equal(left.source_crs, right.source_crs);
        check_reference_equal(left.target_crs, right.target_crs);
        REQUIRE(left.grids.size() == right.grids.size());
        for (std::size_t i = 0U; i < left.grids.size(); ++i) {
            CHECK(left.grids[i].short_name == right.grids[i].short_name);
            CHECK(left.grids[i].full_path == right.grids[i].full_path);
            CHECK(left.grids[i].package_name == right.grids[i].package_name);
            CHECK(left.grids[i].source_uri == right.grids[i].source_uri);
            CHECK(left.grids[i].available == right.grids[i].available);
            CHECK(left.grids[i].open_licence == right.grids[i].open_licence);
            CHECK(left.grids[i].declared_digest == right.grids[i].declared_digest);
            CHECK(left.grids[i].verification_status == right.grids[i].verification_status);
        }
        CHECK(left.engine_name == right.engine_name);
        CHECK(left.engine_version == right.engine_version);
        CHECK(left.database_version == right.database_version);
    }

    auto check_vertical_equal(const tsunami::geo::VerticalTransformationSpecification &left, const tsunami::geo::VerticalTransformationSpecification &right) -> void
    {
        CHECK(left.enabled == right.enabled);
        REQUIRE(left.steps.size() == right.steps.size());
        for (std::size_t i = 0U; i < left.steps.size(); ++i) {
            CHECK(left.steps[i].kind == right.steps[i].kind);
            CHECK(left.steps[i].scale_factor == right.steps[i].scale_factor);
            CHECK(left.steps[i].offset_m == right.steps[i].offset_m);
            CHECK(left.steps[i].operation_authority == right.steps[i].operation_authority);
            CHECK(left.steps[i].operation_code == right.steps[i].operation_code);
            CHECK(left.steps[i].required_resource_name == right.steps[i].required_resource_name);
            CHECK(left.steps[i].source_reference == right.steps[i].source_reference);
            CHECK(left.steps[i].target_reference == right.steps[i].target_reference);
        }
    }

    auto check_resampling_equal(const tsunami::geo::RasterResamplingRecord &left, const tsunami::geo::RasterResamplingRecord &right) -> void
    {
        CHECK(left.dataset_id == right.dataset_id);
        CHECK(left.asset_id == right.asset_id);
        check_import_identity_equal(left.import_identity, right.import_identity);
        check_transformation_identity_equal(left.transformation_identity, right.transformation_identity);
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
        check_operation_equal(left.operation, right.operation);
        check_vertical_equal(left.vertical_steps, right.vertical_steps);
        CHECK(left.adapter_name == right.adapter_name);
        CHECK(left.adapter_version == right.adapter_version);
    }

    auto check_terrain_equal(const tsunami::geo::TerrainConditioningRecord &left, const tsunami::geo::TerrainConditioningRecord &right) -> void
    {
        CHECK(left.schema == right.schema);
        CHECK(left.policy_version == right.policy_version);
        CHECK(left.formula_version == right.formula_version);
        CHECK(left.identity == right.identity);
        CHECK(left.scenario_id == right.scenario_id);
        CHECK(left.target_site == right.target_site);
        CHECK(left.bathymetry_dataset_id == right.bathymetry_dataset_id);
        CHECK(left.bathymetry_asset_id == right.bathymetry_asset_id);
        check_import_identity_equal(left.bathymetry_import_identity, right.bathymetry_import_identity);
        check_transformation_identity_equal(left.bathymetry_transformation_identity, right.bathymetry_transformation_identity);
        CHECK(left.topography_dataset_id == right.topography_dataset_id);
        CHECK(left.topography_asset_id == right.topography_asset_id);
        check_import_identity_equal(left.topography_import_identity, right.topography_import_identity);
        check_transformation_identity_equal(left.topography_transformation_identity, right.topography_transformation_identity);
        check_corridor_identity_equal(left.corridor_identity, right.corridor_identity);
        check_target_equal(left.target_reference, right.target_reference);
        CHECK(left.grid == right.grid);
        CHECK(left.grid.extent() == right.grid.extent());
        CHECK(left.grid_policy == right.grid_policy);
        check_resampling_equal(left.bathymetry_resampling, right.bathymetry_resampling);
        check_resampling_equal(left.topography_resampling, right.topography_resampling);
        CHECK(left.merge_policy == right.merge_policy);
        CHECK(left.gap_policy == right.gap_policy);
        CHECK(left.diagnostics == right.diagnostics);
        CHECK(left.diagnostics.warnings == right.diagnostics.warnings);
        CHECK(left.output_uncertainty == right.output_uncertainty);
        CHECK(left.output_media_type == right.output_media_type);
        CHECK(left.output_path == right.output_path);
        CHECK(left.digest_status == right.digest_status);
        CHECK(left.warnings == right.warnings);
    }

    auto check_corridor_equal(const tsunami::geo::CorridorConstructionRecord &left, const tsunami::geo::CorridorConstructionRecord &right) -> void
    {
        CHECK(left.schema == right.schema);
        CHECK(left.policy_version == right.policy_version);
        CHECK(left.identity == right.identity);
        CHECK(left.scenario_id == right.scenario_id);
        CHECK(left.target_site == right.target_site);
        CHECK(left.epicentre == right.epicentre);
        CHECK(left.target == right.target);
        check_target_equal(left.target_reference, right.target_reference);
        CHECK(left.policy == right.policy);
        CHECK(left.configured_origin == right.configured_origin);
        CHECK(left.configured_bearing_degrees == right.configured_bearing_degrees);
        CHECK(left.derived_bearing_degrees == right.derived_bearing_degrees);
        CHECK(left.origin_residual_m == right.origin_residual_m);
        CHECK(left.bearing_residual_degrees == right.bearing_residual_degrees);
        CHECK(left.offshore_extent_m == right.offshore_extent_m);
        CHECK(left.epicentre_target_distance_m == right.epicentre_target_distance_m);
        CHECK(left.inland_extent_m == right.inland_extent_m);
        CHECK(left.total_length_m == right.total_length_m);
        CHECK(left.offshore_width_m == right.offshore_width_m);
        CHECK(left.inland_width_m == right.inland_width_m);
        CHECK(left.narrowing_enabled == right.narrowing_enabled);
        CHECK(left.narrowing_rule == right.narrowing_rule);
        CHECK(left.local_basis == right.local_basis);
        CHECK(left.stations == right.stations);
        CHECK(left.sponge_limits == right.sponge_limits);
        CHECK(left.polygon == right.polygon);
        CHECK(left.vertex_order_convention == right.vertex_order_convention);
        CHECK(left.extent == right.extent);
        CHECK(left.area_m2 == right.area_m2);
        CHECK(left.perimeter_m == right.perimeter_m);
        CHECK(left.diagnostics == right.diagnostics);
        CHECK(left.configured_field_paths == right.configured_field_paths);
        CHECK(left.warnings == right.warnings);
    }
}

TEST_CASE("canonical corridor records parse and reserialise byte-identically", "[geo-record-parsing]")
{
    const auto original = corridor_record();
    const auto first = tsunami::geo::serialise_corridor_construction_record(original).value();
    const auto parsed = tsunami::geo::parse_corridor_construction_record(first, "corridor-memory");
    REQUIRE(parsed.has_value());
    const auto second = tsunami::geo::serialise_corridor_construction_record(parsed.value()).value();
    CHECK(second == first);
    check_corridor_equal(parsed.value(), original);
    CHECK(second.ends_with('\n'));
}

TEST_CASE("canonical terrain records parse and reserialise byte-identically", "[geo-record-parsing]")
{
    const auto original = terrain_record();
    const auto first = tsunami::geo::serialise_terrain_conditioning_record(original).value();
    const auto parsed = tsunami::geo::parse_terrain_conditioning_record(first, "terrain-memory");
    REQUIRE(parsed.has_value());
    const auto second = tsunami::geo::serialise_terrain_conditioning_record(parsed.value()).value();
    CHECK(second == first);
    check_terrain_equal(parsed.value(), original);
    CHECK(first.find("\"major\": 2") != std::string::npos);
    CHECK(second.ends_with('\n'));
}

TEST_CASE("terrain v1 records require explicit migration for lossless read-back", "[geo-record-parsing]")
{
    const auto legacy_v1 = std::string{
        "{\n"
        "  \"schema\": {\n"
        "    \"schema_name\": \"tsunami.terrain_conditioning_record\",\n"
        "    \"version\": {\n"
        "      \"major\": 1,\n"
        "      \"minor\": 0,\n"
        "      \"patch\": 0\n"
        "    }\n"
        "  },\n"
        "  \"policy_version\": \"0.1\",\n"
        "  \"formula_version\": \"corridor-grid-priority-merge-v1\",\n"
        "  \"identity\": {},\n"
        "  \"grid\": {},\n"
        "  \"grid_policy\": {},\n"
        "  \"bathymetry_resampling\": {},\n"
        "  \"topography_resampling\": {},\n"
        "  \"merge_policy\": {},\n"
        "  \"gap_policy\": {},\n"
        "  \"diagnostics\": {},\n"
        "  \"output_media_type\": \"image/tiff\",\n"
        "  \"output_path\": \"outputs/terrain/legacy.tif\",\n"
        "  \"digest_status\": \"not_computed_by_terrain_conditioning\"\n"
        "}\n"};
    auto parsed = tsunami::geo::parse_terrain_conditioning_record(legacy_v1, "legacy-v1");
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().code() == "geo.terrain.record_parse.migration_required");
    CHECK(context_value(parsed.error(), "json_pointer") == "/schema/version");
    CHECK(parsed.error().message().find("lacks sufficient provenance") != std::string::npos);
}

TEST_CASE("geospatial record filesystem readers round-trip regular files", "[geo-record-parsing]")
{
    const auto directory = std::filesystem::temp_directory_path() / "tsunami-geospatial-record-parsing-tests";
    std::filesystem::create_directories(directory);
    const auto corridor_path = directory / "corridor.json";
    const auto terrain_path = directory / "terrain.json";

    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    REQUIRE(tsunami::geo::write_corridor_construction_record(corridor_path, corridor_record()).has_value());
    auto read_corridor = tsunami::geo::read_corridor_construction_record(corridor_path);
    REQUIRE(read_corridor.has_value());
    CHECK(tsunami::geo::serialise_corridor_construction_record(read_corridor.value()).value() == corridor_json);

    const auto terrain_json = tsunami::geo::serialise_terrain_conditioning_record(terrain_record()).value();
    REQUIRE(tsunami::geo::write_terrain_conditioning_record(terrain_path, terrain_record()).has_value());
    auto read_terrain = tsunami::geo::read_terrain_conditioning_record(terrain_path);
    REQUIRE(read_terrain.has_value());
    CHECK(tsunami::geo::serialise_terrain_conditioning_record(read_terrain.value()).value() == terrain_json);
}

TEST_CASE("explicit null optionals survive geospatial record round-trips", "[geo-record-parsing]")
{
    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    REQUIRE(corridor_json.find("\"source_feature_id\": null") != std::string::npos);
    auto corridor = tsunami::geo::parse_corridor_construction_record(corridor_json);
    REQUIRE(corridor.has_value());
    CHECK_FALSE(corridor.value().epicentre.source_feature_id.has_value());
    CHECK_FALSE(corridor.value().target_reference.vertical.has_value());

    const auto terrain_json = tsunami::geo::serialise_terrain_conditioning_record(terrain_record()).value();
    REQUIRE(terrain_json.find("\"source_offset\": null") != std::string::npos);
    auto terrain = tsunami::geo::parse_terrain_conditioning_record(terrain_json);
    REQUIRE(terrain.has_value());
    CHECK(terrain.value().bathymetry_resampling.source_scale.has_value());
    CHECK_FALSE(terrain.value().topography_resampling.source_offset.has_value());
}

TEST_CASE("ordered geospatial arrays retain canonical order", "[geo-record-parsing]")
{
    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    auto corridor = tsunami::geo::parse_corridor_construction_record(corridor_json).value();
    REQUIRE(corridor.polygon.exterior_ring.size() == 5U);
    CHECK(corridor.polygon.exterior_ring[0] == tsunami::geo::Point2D{-20.0, 20.0});
    CHECK(corridor.polygon.exterior_ring[1] == tsunami::geo::Point2D{-20.0, -20.0});
    CHECK(corridor.epicentre.source_reference.axis_names == std::vector<std::string>{"Northing", "Easting"});
    CHECK(corridor.diagnostics.warnings == std::vector<std::string>{"alpha-warning", "zeta-warning"});

    const auto terrain_json = tsunami::geo::serialise_terrain_conditioning_record(terrain_record()).value();
    auto terrain = tsunami::geo::parse_terrain_conditioning_record(terrain_json).value();
    CHECK(terrain.bathymetry_resampling.operation.grids[0].short_name == "zeta-grid");
    CHECK(terrain.bathymetry_resampling.operation.grids[1].short_name == "alpha-grid");
    CHECK(terrain.bathymetry_resampling.vertical_steps.steps[0].kind == tsunami::geo::VerticalTransformationStepKind::unit_scale);
    CHECK(terrain.bathymetry_resampling.vertical_steps.steps[1].kind == tsunami::geo::VerticalTransformationStepKind::constant_offset);
    CHECK(terrain.diagnostics.warnings == std::vector<std::string>{"zeta-diagnostic", "alpha-diagnostic"});
    CHECK(terrain.output_uncertainty.measures[0].quantity == "vertical_rmse");
    CHECK(terrain.output_uncertainty.measures[1].quantity == "horizontal_rmse");
    CHECK(terrain.warnings == std::vector<std::string>{"zeta-warning", "alpha-warning"});
}

TEST_CASE("geospatial record serializers normalise negative zero consistently", "[geo-record-parsing]")
{
    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    CHECK(corridor_json.find(": -0") == std::string::npos);

    const auto terrain_json = tsunami::geo::serialise_terrain_conditioning_record(terrain_record()).value();
    CHECK(terrain_json.find(": -0") == std::string::npos);
    CHECK(terrain_json.find("\"operation_accuracy_m\": 0") != std::string::npos);
    CHECK(terrain_json.find("\"offset_m\": 0") != std::string::npos);
    CHECK(terrain_json.find("\"coordinate_epoch_decimal_year\": 0") != std::string::npos);
}

TEST_CASE("geospatial record parsers reject malformed and non-object JSON", "[geo-record-parsing]")
{
    auto malformed = tsunami::geo::parse_corridor_construction_record("{\"schema\":", "bad-json");
    REQUIRE_FALSE(malformed.has_value());
    CHECK(malformed.error().code() == "geo.corridor.record_parse.malformed");
    CHECK(context_value(malformed.error(), "source_name") == "bad-json");

    auto non_object = tsunami::geo::parse_terrain_conditioning_record("[]", "array-json");
    REQUIRE_FALSE(non_object.has_value());
    CHECK(non_object.error().code() == "geo.terrain.record_parse.type_invalid");
    CHECK(context_value(non_object.error(), "json_pointer") == "/");
    CHECK(context_value(non_object.error(), "expected_type") == "object");
    CHECK(context_value(non_object.error(), "actual_type") == "array");
}

TEST_CASE("geospatial record parsers report missing wrong unknown and duplicate fields deterministically", "[geo-record-parsing]")
{
    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    auto missing = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "  \"policy_version\": \"0.1\",\n", ""), "missing-field");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code() == "geo.corridor.record_parse.field_missing");
    CHECK(context_value(missing.error(), "json_pointer") == "/policy_version");
    CHECK(context_value(missing.error(), "field") == "policy_version");

    auto wrong_type = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "\"policy_version\": \"0.1\"", "\"policy_version\": 1"), "wrong-type");
    REQUIRE_FALSE(wrong_type.has_value());
    CHECK(wrong_type.error().code() == "geo.corridor.record_parse.type_invalid");
    CHECK(context_value(wrong_type.error(), "expected_type") == "string");
    CHECK(context_value(wrong_type.error(), "actual_type") == "unsigned integer");

    auto unknown_top = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "{\n", "{\n  \"unexpected\": true,\n"), "unknown-top");
    REQUIRE_FALSE(unknown_top.has_value());
    CHECK(unknown_top.error().code() == "geo.corridor.record_parse.field_unknown");
    CHECK(context_value(unknown_top.error(), "json_pointer") == "/unexpected");

    auto unknown_nested = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "    \"schema_name\":", "    \"extra\": 0,\n    \"schema_name\":"), "unknown-nested");
    REQUIRE_FALSE(unknown_nested.has_value());
    CHECK(unknown_nested.error().code() == "geo.corridor.record_parse.field_unknown");
    CHECK(context_value(unknown_nested.error(), "json_pointer") == "/schema/extra");

    auto duplicate_top = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "  \"policy_version\": \"0.1\",\n", "  \"policy_version\": \"0.1\",\n  \"policy_version\": \"0.1\",\n"), "duplicate-top");
    REQUIRE_FALSE(duplicate_top.has_value());
    CHECK(duplicate_top.error().code() == "geo.corridor.record_parse.duplicate_key");
    CHECK(context_value(duplicate_top.error(), "json_pointer") == "/policy_version");

    auto duplicate_nested = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "      \"minor\": 0,\n", "      \"minor\": 0,\n      \"minor\": 0,\n"), "duplicate-nested");
    REQUIRE_FALSE(duplicate_nested.has_value());
    CHECK(duplicate_nested.error().code() == "geo.corridor.record_parse.duplicate_key");
    CHECK(context_value(duplicate_nested.error(), "json_pointer") == "/schema/version/minor");

    auto duplicate_array_object = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "        \"x\": -20,\n        \"y\": -20", "        \"x\": -20,\n        \"x\": -20,\n        \"y\": -20"), "duplicate-array-object");
    REQUIRE_FALSE(duplicate_array_object.has_value());
    CHECK(duplicate_array_object.error().code() == "geo.corridor.record_parse.duplicate_key");
    CHECK(context_value(duplicate_array_object.error(), "json_pointer") == "/polygon/exterior_ring/1/x");
}

TEST_CASE("schema version integers are checked before uint32 conversion", "[geo-record-parsing]")
{
    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    auto overflow = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "\"major\": 1", "\"major\": 4294967297"), "overflow-schema");
    REQUIRE_FALSE(overflow.has_value());
    CHECK(overflow.error().code() == "geo.corridor.record_parse.type_invalid");
    CHECK(context_value(overflow.error(), "json_pointer") == "/schema/version/major");
    CHECK(context_value(overflow.error(), "expected_type") == "uint32");
}

TEST_CASE("geospatial record parsers compose semantic validation failures", "[geo-record-parsing]")
{
    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    auto bad_schema = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "\"major\": 1", "\"major\": 2"), "bad-schema");
    REQUIRE_FALSE(bad_schema.has_value());
    CHECK(bad_schema.error().code() == "geo.corridor.record_parse.validation_failed");
    CHECK(bad_schema.error().cause_code() == std::optional<std::string>{"geo.corridor.record_invalid"});

    auto bad_formula = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "flat-ended-epicentre-target-v1", "unsupported-formula"), "bad-formula");
    REQUIRE_FALSE(bad_formula.has_value());
    CHECK(bad_formula.error().code() == "geo.corridor.record_parse.validation_failed");
    CHECK(bad_formula.error().cause_code().has_value());

    auto bad_nested_evidence = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "\"point_id\": \"epicentre-point\"", "\"point_id\": \"BAD ID\""), "bad-evidence");
    REQUIRE_FALSE(bad_nested_evidence.has_value());
    CHECK(bad_nested_evidence.error().code() == "geo.corridor.record_parse.validation_failed");
    CHECK(bad_nested_evidence.error().cause_code() == std::optional<std::string>{"geo.corridor.record_invalid"});

    const auto terrain_json = tsunami::geo::serialise_terrain_conditioning_record(terrain_record()).value();
    auto bad_policy = tsunami::geo::parse_terrain_conditioning_record(replace_once(terrain_json, "\"policy_version\": \"0.1\"", "\"policy_version\": \"9.9\""), "bad-policy");
    REQUIRE_FALSE(bad_policy.has_value());
    CHECK(bad_policy.error().code() == "geo.terrain.record_parse.validation_failed");
    CHECK(bad_policy.error().cause_code() == std::optional<std::string>{"geo.terrain.record_invalid"});

    auto bad_terrain_formula = tsunami::geo::parse_terrain_conditioning_record(replace_once(terrain_json, "corridor-grid-priority-merge-v1", "unsupported-formula"), "bad-terrain-formula");
    REQUIRE_FALSE(bad_terrain_formula.has_value());
    CHECK(bad_terrain_formula.error().code() == "geo.terrain.record_parse.validation_failed");
    CHECK(bad_terrain_formula.error().cause_code() == std::optional<std::string>{"geo.terrain.record_invalid"});
}

TEST_CASE("geospatial record file readers reject empty missing and oversized files deterministically", "[geo-record-parsing]")
{
    const auto directory = std::filesystem::temp_directory_path() / "tsunami-geospatial-record-read-failures";
    std::filesystem::create_directories(directory);
    const auto missing_path = directory / "missing.json";
    auto missing = tsunami::geo::read_corridor_construction_record(missing_path);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code() == "geo.corridor.record_read.failed");
    CHECK(context_value(missing.error(), "path") == missing_path.generic_string());

    const auto empty_path = directory / "empty.json";
    { auto out = std::ofstream{empty_path, std::ios::binary | std::ios::trunc}; }
    auto empty = tsunami::geo::read_terrain_conditioning_record(empty_path);
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().code() == "geo.terrain.record_parse.malformed");
    CHECK(read_text(empty_path).empty());

    const auto oversized_path = directory / "oversized.json";
    {
        auto out = std::ofstream{oversized_path, std::ios::binary | std::ios::trunc};
        const auto bytes = std::string(tsunami::geo::maximum_corridor_construction_record_document_bytes + 1U, ' ');
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    auto oversized = tsunami::geo::read_corridor_construction_record(oversized_path);
    REQUIRE_FALSE(oversized.has_value());
    CHECK(oversized.error().code() == "geo.corridor.record_read.too_large");
}

TEST_CASE("geospatial record parse failures do not mutate source strings or files", "[geo-record-parsing]")
{
    auto source = std::string{"{\"schema\": 1}"};
    const auto before = source;
    auto parsed = tsunami::geo::parse_corridor_construction_record(source, "stable-source");
    REQUIRE_FALSE(parsed.has_value());
    CHECK(source == before);

    const auto directory = std::filesystem::temp_directory_path() / "tsunami-geospatial-record-no-mutation";
    std::filesystem::create_directories(directory);
    const auto path = directory / "invalid.json";
    {
        auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
        out << source;
    }
    auto read = tsunami::geo::read_corridor_construction_record(path);
    REQUIRE_FALSE(read.has_value());
    CHECK(read_text(path) == before);
}

TEST_CASE("public geospatial parsing headers expose no JSON or adapter implementation types", "[geo-record-parsing]")
{
    const auto root = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path();
    const auto headers = std::vector<std::filesystem::path>{
        root / "src/geo/include/tsunami/geo/CorridorConstructionParsing.hpp",
        root / "src/geo/include/tsunami/geo/TerrainConditioningParsing.hpp"};
    const auto forbidden = std::vector<std::string>{"nlohmann", "QObject", "QString", "QVariant", "GDAL", "OGR", "PROJ", "H5", "OpenFOAM"};
    for (const auto &header : headers) {
        const auto text = read_text(header);
        for (const auto &token : forbidden) {
            CHECK(text.find(token) == std::string::npos);
        }
    }
}
