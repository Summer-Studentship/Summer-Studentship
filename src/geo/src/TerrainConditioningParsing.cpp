#include <tsunami/geo/TerrainConditioningParsing.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

#include "GeospatialRecordParsingDetail.hpp"

namespace tsunami::geo
{
    namespace
    {
        constexpr auto kind = detail::RecordKind::terrain;

        [[nodiscard]] auto parse_identity(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> TerrainConditioningIdentity
        {
            detail::reject_unknown(
                json,
                {"terrain_id", "terrain_revision", "case_id", "case_revision", "manifest_id", "manifest_revision", "output_dataset_id", "output_process_id", "executed_at_utc"},
                pointer,
                source,
                kind);
            return TerrainConditioningIdentity{
                detail::string_value(json, "terrain_id", pointer, source, kind),
                detail::uint_value(json, "terrain_revision", pointer, source, kind),
                detail::parse_case_revision(json, pointer, source, kind),
                detail::string_value(json, "manifest_id", pointer, source, kind),
                detail::uint_value(json, "manifest_revision", pointer, source, kind),
                detail::string_value(json, "output_dataset_id", pointer, source, kind),
                detail::string_value(json, "output_process_id", pointer, source, kind),
                detail::string_value(json, "executed_at_utc", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_grid(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source,
            const ComputationalTargetReference &target_reference) -> TerrainTargetGrid
        {
            detail::reject_unknown(
                json,
                {"width", "height", "spacing_m", "registration", "xi_min_m", "xi_max_m", "eta_bottom_m", "eta_top_m", "longitudinal_padding_m", "transverse_padding_m", "affine", "extent"},
                pointer,
                source,
                kind);
            const auto registration = detail::enum_value<RasterCellRegistration>(
                json,
                "registration",
                pointer,
                source,
                kind,
                {{"pixel_is_area", RasterCellRegistration::pixel_is_area}, {"pixel_is_point", RasterCellRegistration::pixel_is_point}, {"unknown", RasterCellRegistration::unknown}});
            if (registration != RasterCellRegistration::pixel_is_area) {
                detail::fail(kind, "type_invalid", "terrain grid registration must be pixel_is_area", source, detail::pointer_for(pointer, "registration"), "pixel_is_area", "string", "registration");
            }
            const auto &affine_json = detail::child(json, "affine", pointer, source, kind);
            const auto affine_pointer = detail::pointer_for(pointer, "affine");
            detail::reject_unknown(affine_json, {"origin_x", "pixel_width", "row_rotation", "origin_y", "column_rotation", "pixel_height"}, affine_pointer, source, kind);
            const auto transform = RasterAffineTransform{
                detail::number_value(affine_json, "origin_x", affine_pointer, source, kind),
                detail::number_value(affine_json, "pixel_width", affine_pointer, source, kind),
                detail::number_value(affine_json, "row_rotation", affine_pointer, source, kind),
                detail::number_value(affine_json, "origin_y", affine_pointer, source, kind),
                detail::number_value(affine_json, "column_rotation", affine_pointer, source, kind),
                detail::number_value(affine_json, "pixel_height", affine_pointer, source, kind)};
            const auto width = detail::uint_value(json, "width", pointer, source, kind);
            const auto height = detail::uint_value(json, "height", pointer, source, kind);
            return TerrainTargetGrid{
                width,
                height,
                detail::number_value(json, "spacing_m", pointer, source, kind),
                transform,
                detail::parse_box(detail::child(json, "extent", pointer, source, kind), detail::pointer_for(pointer, "extent"), source, kind),
                target_reference,
                detail::number_value(json, "xi_min_m", pointer, source, kind),
                detail::number_value(json, "xi_max_m", pointer, source, kind),
                detail::number_value(json, "eta_bottom_m", pointer, source, kind),
                detail::number_value(json, "eta_top_m", pointer, source, kind),
                detail::number_value(json, "longitudinal_padding_m", pointer, source, kind),
                detail::number_value(json, "transverse_padding_m", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_grid_policy(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> TerrainTargetGridPolicy
        {
            detail::reject_unknown(
                json,
                {"target_spacing_m", "active_coverage_threshold", "maximum_upsampling_factor", "maximum_output_cells", "numerical_absolute_tolerance", "numerical_relative_tolerance", "policy_basis"},
                pointer,
                source,
                kind);
            return TerrainTargetGridPolicy{
                detail::number_value(json, "target_spacing_m", pointer, source, kind),
                detail::number_value(json, "active_coverage_threshold", pointer, source, kind),
                detail::number_value(json, "maximum_upsampling_factor", pointer, source, kind),
                detail::uint_value(json, "maximum_output_cells", pointer, source, kind),
                detail::number_value(json, "numerical_absolute_tolerance", pointer, source, kind),
                detail::number_value(json, "numerical_relative_tolerance", pointer, source, kind),
                detail::string_value(json, "policy_basis", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_resampling(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> RasterResamplingRecord
        {
            detail::reject_unknown(
                json,
                {"dataset_id", "asset_id", "import_id", "import_identity", "transformation_id", "transformation_identity", "role", "kernel", "source_registration", "target_registration", "source_scale", "source_offset", "minimum_source_spacing_m", "maximum_source_spacing_m", "nominal_source_spacing_m", "target_spacing_m", "maximum_upsampling_factor", "source_valid_cell_count", "output_valid_cell_count", "source_nodata_cell_count", "outside_coverage_cell_count", "operation_name", "operation", "vertical_operation", "adapter_name", "adapter_version"},
                pointer,
                source,
                kind);
            auto record = RasterResamplingRecord{};
            record.dataset_id = detail::string_value(json, "dataset_id", pointer, source, kind);
            record.asset_id = detail::string_value(json, "asset_id", pointer, source, kind);
            const auto import_id = detail::string_value(json, "import_id", pointer, source, kind);
            record.import_identity = detail::parse_import_identity(detail::child(json, "import_identity", pointer, source, kind), detail::pointer_for(pointer, "import_identity"), source, kind);
            if (record.import_identity.import_id != import_id) {
                detail::fail(kind, "validation_failed", "resampling import_id disagrees with import_identity", source, detail::pointer_for(pointer, "import_id"), "matching import identity", "string", "import_id");
            }
            const auto transformation_id = detail::string_value(json, "transformation_id", pointer, source, kind);
            record.transformation_identity = detail::parse_transformation_identity(detail::child(json, "transformation_identity", pointer, source, kind), detail::pointer_for(pointer, "transformation_identity"), source, kind);
            if (record.transformation_identity.transformation_id != transformation_id) {
                detail::fail(kind, "validation_failed", "resampling transformation_id disagrees with transformation_identity", source, detail::pointer_for(pointer, "transformation_id"), "matching transformation identity", "string", "transformation_id");
            }
            record.role = detail::enum_value<TerrainSourceRole>(
                json,
                "role",
                pointer,
                source,
                kind,
                {{"bathymetry", TerrainSourceRole::bathymetry}, {"topography", TerrainSourceRole::topography}});
            record.kernel = detail::enum_value<RasterResamplingKernel>(
                json,
                "kernel",
                pointer,
                source,
                kind,
                {{"bilinear", RasterResamplingKernel::bilinear}, {"area_average", RasterResamplingKernel::area_average}});
            record.source_registration = detail::enum_value<RasterCellRegistration>(
                json,
                "source_registration",
                pointer,
                source,
                kind,
                {{"pixel_is_area", RasterCellRegistration::pixel_is_area}, {"pixel_is_point", RasterCellRegistration::pixel_is_point}, {"unknown", RasterCellRegistration::unknown}});
            record.target_registration = detail::enum_value<RasterCellRegistration>(
                json,
                "target_registration",
                pointer,
                source,
                kind,
                {{"pixel_is_area", RasterCellRegistration::pixel_is_area}, {"pixel_is_point", RasterCellRegistration::pixel_is_point}, {"unknown", RasterCellRegistration::unknown}});
            record.source_scale = detail::nullable_number(json, "source_scale", pointer, source, kind);
            record.source_offset = detail::nullable_number(json, "source_offset", pointer, source, kind);
            record.minimum_source_spacing_m = detail::number_value(json, "minimum_source_spacing_m", pointer, source, kind);
            record.maximum_source_spacing_m = detail::number_value(json, "maximum_source_spacing_m", pointer, source, kind);
            record.nominal_source_spacing_m = detail::number_value(json, "nominal_source_spacing_m", pointer, source, kind);
            record.target_spacing_m = detail::number_value(json, "target_spacing_m", pointer, source, kind);
            record.maximum_upsampling_factor = detail::number_value(json, "maximum_upsampling_factor", pointer, source, kind);
            record.source_valid_cell_count = detail::uint_value(json, "source_valid_cell_count", pointer, source, kind);
            record.output_valid_cell_count = detail::uint_value(json, "output_valid_cell_count", pointer, source, kind);
            record.source_nodata_cell_count = detail::uint_value(json, "source_nodata_cell_count", pointer, source, kind);
            record.outside_coverage_cell_count = detail::uint_value(json, "outside_coverage_cell_count", pointer, source, kind);
            const auto operation_name = detail::string_value(json, "operation_name", pointer, source, kind);
            record.operation = detail::parse_coordinate_operation(detail::child(json, "operation", pointer, source, kind), detail::pointer_for(pointer, "operation"), source, kind);
            if (record.operation.operation_name != operation_name) {
                detail::fail(kind, "validation_failed", "resampling operation_name disagrees with operation", source, detail::pointer_for(pointer, "operation_name"), "matching operation name", "string", "operation_name");
            }
            record.vertical_steps = detail::parse_vertical_operation(detail::child(json, "vertical_operation", pointer, source, kind), detail::pointer_for(pointer, "vertical_operation"), source, kind);
            record.adapter_name = detail::string_value(json, "adapter_name", pointer, source, kind);
            record.adapter_version = detail::string_value(json, "adapter_version", pointer, source, kind);
            return record;
        }

        [[nodiscard]] auto parse_merge_policy(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> TerrainMergePolicy
        {
            detail::reject_unknown(json, {"first_priority_dataset_id", "second_priority_dataset_id", "maximum_overlap_disagreement_m", "conflict_policy", "priority_basis"}, pointer, source, kind);
            return TerrainMergePolicy{
                detail::string_value(json, "first_priority_dataset_id", pointer, source, kind),
                detail::string_value(json, "second_priority_dataset_id", pointer, source, kind),
                detail::number_value(json, "maximum_overlap_disagreement_m", pointer, source, kind),
                detail::enum_value<TerrainOverlapConflictPolicy>(
                    json,
                    "conflict_policy",
                    pointer,
                    source,
                    kind,
                    {{"reject", TerrainOverlapConflictPolicy::reject}, {"accept_priority_with_warning", TerrainOverlapConflictPolicy::accept_priority_with_warning}}),
                detail::string_value(json, "priority_basis", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_gap_policy(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> TerrainGapResolutionPolicy
        {
            detail::reject_unknown(json, {"kind", "maximum_fill_distance_m", "maximum_component_diameter_m", "maximum_component_cells", "minimum_donor_count", "distance_exponent", "maximum_filled_fraction", "policy_basis"}, pointer, source, kind);
            return TerrainGapResolutionPolicy{
                detail::enum_value<TerrainGapResolutionKind>(
                    json,
                    "kind",
                    pointer,
                    source,
                    kind,
                    {{"reject", TerrainGapResolutionKind::reject}, {"bounded_inverse_distance", TerrainGapResolutionKind::bounded_inverse_distance}}),
                detail::number_value(json, "maximum_fill_distance_m", pointer, source, kind),
                detail::number_value(json, "maximum_component_diameter_m", pointer, source, kind),
                detail::uint_value(json, "maximum_component_cells", pointer, source, kind),
                detail::uint_value(json, "minimum_donor_count", pointer, source, kind),
                detail::number_value(json, "distance_exponent", pointer, source, kind),
                detail::number_value(json, "maximum_filled_fraction", pointer, source, kind),
                detail::string_value(json, "policy_basis", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_diagnostics(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> TerrainConditioningDiagnostics
        {
            detail::reject_unknown(
                json,
                {"total_cell_count", "active_cell_count", "outside_corridor_cell_count", "excluded_boundary_cell_count", "bathymetry_selected_cell_count", "topography_selected_cell_count", "overlap_cell_count", "overlap_conflict_cell_count", "initially_unresolved_cell_count", "filled_cell_count", "unresolved_cell_count", "overlap", "minimum_elevation_m", "maximum_elevation_m", "warnings"},
                pointer,
                source,
                kind);
            const auto &overlap_json = detail::child(json, "overlap", pointer, source, kind);
            const auto overlap_pointer = detail::pointer_for(pointer, "overlap");
            detail::reject_unknown(overlap_json, {"overlap_cell_count", "disagreement_exceedance_count", "mean_signed_difference_m", "root_mean_square_difference_m", "maximum_absolute_difference_m"}, overlap_pointer, source, kind);
            auto diagnostics = TerrainConditioningDiagnostics{};
            diagnostics.total_cell_count = detail::uint_value(json, "total_cell_count", pointer, source, kind);
            diagnostics.active_cell_count = detail::uint_value(json, "active_cell_count", pointer, source, kind);
            diagnostics.outside_corridor_cell_count = detail::uint_value(json, "outside_corridor_cell_count", pointer, source, kind);
            diagnostics.excluded_boundary_cell_count = detail::uint_value(json, "excluded_boundary_cell_count", pointer, source, kind);
            diagnostics.bathymetry_selected_cell_count = detail::uint_value(json, "bathymetry_selected_cell_count", pointer, source, kind);
            diagnostics.topography_selected_cell_count = detail::uint_value(json, "topography_selected_cell_count", pointer, source, kind);
            diagnostics.overlap_cell_count = detail::uint_value(json, "overlap_cell_count", pointer, source, kind);
            diagnostics.overlap_conflict_cell_count = detail::uint_value(json, "overlap_conflict_cell_count", pointer, source, kind);
            diagnostics.initially_unresolved_cell_count = detail::uint_value(json, "initially_unresolved_cell_count", pointer, source, kind);
            diagnostics.filled_cell_count = detail::uint_value(json, "filled_cell_count", pointer, source, kind);
            diagnostics.unresolved_cell_count = detail::uint_value(json, "unresolved_cell_count", pointer, source, kind);
            diagnostics.overlap = TerrainOverlapDiagnostics{
                detail::uint_value(overlap_json, "overlap_cell_count", overlap_pointer, source, kind),
                detail::uint_value(overlap_json, "disagreement_exceedance_count", overlap_pointer, source, kind),
                detail::number_value(overlap_json, "mean_signed_difference_m", overlap_pointer, source, kind),
                detail::number_value(overlap_json, "root_mean_square_difference_m", overlap_pointer, source, kind),
                detail::number_value(overlap_json, "maximum_absolute_difference_m", overlap_pointer, source, kind)};
            diagnostics.minimum_elevation_m = detail::number_value(json, "minimum_elevation_m", pointer, source, kind);
            diagnostics.maximum_elevation_m = detail::number_value(json, "maximum_elevation_m", pointer, source, kind);
            diagnostics.warnings = detail::string_array(json, "warnings", pointer, source, kind);
            return diagnostics;
        }

        [[nodiscard]] auto migration_required_error(const std::string &source) -> tsunami::core::Error
        {
            return detail::parse_error(
                kind,
                "migration_required",
                "terrain conditioning record v1 lacks sufficient provenance for lossless reconstruction; v2 read-back is required",
                source,
                "/schema/version",
                "tsunami.terrain_conditioning_record 2.0.0",
                "tsunami.terrain_conditioning_record 1.0.0");
        }

        [[nodiscard]] auto validation_error(
            const tsunami::core::Error &cause,
            const std::string &source) -> tsunami::core::Error
        {
            auto error = detail::parse_error(kind, "validation_failed", "terrain conditioning record failed semantic validation", source, "/", "valid terrain conditioning record", "object");
            error.with_cause_code(cause.code());
            return error;
        }
    }

    auto parse_terrain_conditioning_record(
        std::string_view document,
        std::string source_name) -> tsunami::core::Result<TerrainConditioningRecord>
    {
        try {
            auto root = detail::parse_json_document(document, source_name, kind);
            detail::require_object(root, "/", source_name, kind);
            const auto schema = detail::parse_schema(detail::child(root, "schema", "/", source_name, kind), "/schema", source_name, kind);
            if (schema.schema_name == terrain_conditioning_record_schema_name &&
                schema.version == tsunami::core::SemanticVersion{1U, 0U, 0U}) {
                return tsunami::core::failure<TerrainConditioningRecord>(migration_required_error(source_name));
            }
            detail::reject_unknown(
                root,
                {"schema", "policy_version", "formula_version", "identity", "scenario_id", "target_site", "bathymetry_dataset_id", "bathymetry_asset_id", "bathymetry_import_identity", "bathymetry_transformation_identity", "topography_dataset_id", "topography_asset_id", "topography_import_identity", "topography_transformation_identity", "corridor_id", "corridor_identity", "target_reference", "grid", "grid_policy", "bathymetry_resampling", "topography_resampling", "merge_policy", "gap_policy", "diagnostics", "output_uncertainty_status", "output_uncertainty", "output_media_type", "output_path", "digest_status", "warnings"},
                "/",
                source_name,
                kind);
            auto record = TerrainConditioningRecord{};
            record.schema = schema;
            record.policy_version = detail::string_value(root, "policy_version", "/", source_name, kind);
            record.formula_version = detail::string_value(root, "formula_version", "/", source_name, kind);
            record.identity = parse_identity(detail::child(root, "identity", "/", source_name, kind), "/identity", source_name);
            record.scenario_id = detail::string_value(root, "scenario_id", "/", source_name, kind);
            record.target_site = detail::string_value(root, "target_site", "/", source_name, kind);
            record.bathymetry_dataset_id = detail::string_value(root, "bathymetry_dataset_id", "/", source_name, kind);
            record.bathymetry_asset_id = detail::string_value(root, "bathymetry_asset_id", "/", source_name, kind);
            record.bathymetry_import_identity = detail::parse_import_identity(detail::child(root, "bathymetry_import_identity", "/", source_name, kind), "/bathymetry_import_identity", source_name, kind);
            record.bathymetry_transformation_identity = detail::parse_transformation_identity(detail::child(root, "bathymetry_transformation_identity", "/", source_name, kind), "/bathymetry_transformation_identity", source_name, kind);
            record.topography_dataset_id = detail::string_value(root, "topography_dataset_id", "/", source_name, kind);
            record.topography_asset_id = detail::string_value(root, "topography_asset_id", "/", source_name, kind);
            record.topography_import_identity = detail::parse_import_identity(detail::child(root, "topography_import_identity", "/", source_name, kind), "/topography_import_identity", source_name, kind);
            record.topography_transformation_identity = detail::parse_transformation_identity(detail::child(root, "topography_transformation_identity", "/", source_name, kind), "/topography_transformation_identity", source_name, kind);
            const auto corridor_id = detail::string_value(root, "corridor_id", "/", source_name, kind);
            record.corridor_identity = detail::parse_corridor_identity(detail::child(root, "corridor_identity", "/", source_name, kind), "/corridor_identity", source_name, kind);
            if (record.corridor_identity.corridor_id != corridor_id) {
                return tsunami::core::failure<TerrainConditioningRecord>(
                    detail::parse_error(kind, "validation_failed", "corridor_id disagrees with corridor_identity", source_name, "/corridor_id", "matching corridor identity", "string")
                        .with_cause_code("geo.terrain.record_invalid"));
            }
            record.target_reference = detail::parse_target(detail::child(root, "target_reference", "/", source_name, kind), "/target_reference", source_name, kind);
            record.grid = parse_grid(detail::child(root, "grid", "/", source_name, kind), "/grid", source_name, record.target_reference);
            record.grid_policy = parse_grid_policy(detail::child(root, "grid_policy", "/", source_name, kind), "/grid_policy", source_name);
            record.bathymetry_resampling = parse_resampling(detail::child(root, "bathymetry_resampling", "/", source_name, kind), "/bathymetry_resampling", source_name);
            record.topography_resampling = parse_resampling(detail::child(root, "topography_resampling", "/", source_name, kind), "/topography_resampling", source_name);
            record.merge_policy = parse_merge_policy(detail::child(root, "merge_policy", "/", source_name, kind), "/merge_policy", source_name);
            record.gap_policy = parse_gap_policy(detail::child(root, "gap_policy", "/", source_name, kind), "/gap_policy", source_name);
            record.diagnostics = parse_diagnostics(detail::child(root, "diagnostics", "/", source_name, kind), "/diagnostics", source_name);
            const auto status = detail::parse_uncertainty_status(root, "output_uncertainty_status", "/", source_name, kind);
            record.output_uncertainty = detail::parse_uncertainty(detail::child(root, "output_uncertainty", "/", source_name, kind), "/output_uncertainty", source_name, kind);
            if (record.output_uncertainty.status != status) {
                return tsunami::core::failure<TerrainConditioningRecord>(
                    detail::parse_error(kind, "validation_failed", "output uncertainty status disagrees with output_uncertainty", source_name, "/output_uncertainty_status", "matching uncertainty status", "string")
                        .with_cause_code("geo.terrain.record_invalid"));
            }
            record.output_media_type = detail::string_value(root, "output_media_type", "/", source_name, kind);
            record.output_path = std::filesystem::path{detail::string_value(root, "output_path", "/", source_name, kind)};
            record.digest_status = detail::string_value(root, "digest_status", "/", source_name, kind);
            record.warnings = detail::string_array(root, "warnings", "/", source_name, kind);

            if (auto valid = validate_terrain_conditioning_record(record); !valid) {
                return tsunami::core::failure<TerrainConditioningRecord>(validation_error(valid.error(), source_name));
            }
            return tsunami::core::success(std::move(record));
        } catch (const detail::ParseFailure &failure) {
            return tsunami::core::failure<TerrainConditioningRecord>(failure.error);
        } catch (const std::exception &) {
            return tsunami::core::failure<TerrainConditioningRecord>(
                detail::parse_error(kind, "malformed", "parser exception was translated", source_name, "/", "canonical terrain conditioning JSON", "exception"));
        }
    }

    auto read_terrain_conditioning_record(
        const std::filesystem::path &path) -> tsunami::core::Result<TerrainConditioningRecord>
    {
        auto bytes = detail::read_bounded_file(path, maximum_terrain_conditioning_record_document_bytes, kind);
        if (!bytes) {
            return tsunami::core::failure<TerrainConditioningRecord>(bytes.error());
        }
        return parse_terrain_conditioning_record(bytes.value(), path.generic_string());
    }

} // namespace tsunami::geo
