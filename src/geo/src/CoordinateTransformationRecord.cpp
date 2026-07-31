#include <tsunami/geo/CoordinateTransformationRecord.hpp>

#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <string>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto crs_error(std::string code, std::string message, std::string rule_id)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_coordinate_transformation_record")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto text_present(const std::string &text) -> bool
        {
            return !text.empty() && text.find('\0') == std::string::npos;
        }

        [[nodiscard]] auto optional_text_present(const std::optional<std::string> &text) -> bool
        {
            return !text || text_present(*text);
        }

        [[nodiscard]] auto logical_id_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^[a-z0-9]+(?:[._-][a-z0-9]+)*$"};
            return !text.empty() && text.size() <= 128U && std::regex_match(text, pattern);
        }

        [[nodiscard]] auto finite_extent(const BoundingBox2D &extent) noexcept -> bool
        {
            return std::isfinite(extent.minimum_x) && std::isfinite(extent.minimum_y) &&
                std::isfinite(extent.maximum_x) && std::isfinite(extent.maximum_y) &&
                extent.minimum_x <= extent.maximum_x && extent.minimum_y <= extent.maximum_y;
        }

        [[nodiscard]] auto finite_nonnegative(double value) noexcept -> bool
        {
            return std::isfinite(value) && value >= 0.0;
        }

        [[nodiscard]] auto timestamp_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z$"};
            return std::regex_match(text, pattern);
        }
    }

    auto default_coordinate_transformation_record_path(
        std::string_view source_dataset_id,
        std::string_view transformation_id) -> std::filesystem::path
    {
        if (!logical_id_valid(std::string{source_dataset_id}) || !logical_id_valid(std::string{transformation_id})) {
            return {};
        }
        return std::filesystem::path{"manifests"} / "transformations" /
            std::string{source_dataset_id} / (std::string{transformation_id} + ".json");
    }

    auto validate_coordinate_operation_grid(const CoordinateOperationGrid &grid)
        -> tsunami::core::Result<void>
    {
        if (!text_present(grid.short_name) || !optional_text_present(grid.full_path) ||
            !optional_text_present(grid.package_name) || !optional_text_present(grid.source_uri)) {
            return tsunami::core::failure(crs_error("geo.crs.resource_unverified", "coordinate operation grid metadata is invalid", "geo.crs.operation.resources_verified"));
        }
        if (grid.available && grid.verification_status == GeodeticResourceVerificationStatus::unavailable) {
            return tsunami::core::failure(crs_error("geo.crs.resource_unverified", "available grid requires explicit verification status", "geo.crs.operation.resources_verified").add_context("grid_name", grid.short_name));
        }
        return tsunami::core::success();
    }

    auto validate_coordinate_operation_record(const CoordinateOperationRecord &record)
        -> tsunami::core::Result<void>
    {
        if (!text_present(record.operation_name) || !optional_text_present(record.operation_authority) ||
            !optional_text_present(record.operation_code) || !optional_text_present(record.operation_method) ||
            !optional_text_present(record.scope) || !optional_text_present(record.canonical_wkt2) ||
            !optional_text_present(record.canonical_projjson) || !optional_text_present(record.canonical_pipeline) ||
            !text_present(record.engine_name) || !text_present(record.engine_version) ||
            !optional_text_present(record.database_version)) {
            return tsunami::core::failure(crs_error("geo.crs.operation_not_found", "coordinate operation metadata is incomplete", "geo.crs.operation.metadata.valid"));
        }
        if (record.ballpark) {
            return tsunami::core::failure(crs_error("geo.crs.operation_ballpark", "ballpark coordinate operations are forbidden", "geo.crs.operation.ballpark_forbidden"));
        }
        if (record.operation_accuracy_m && !finite_nonnegative(*record.operation_accuracy_m)) {
            return tsunami::core::failure(crs_error("geo.crs.operation_accuracy_exceeded", "operation accuracy must be finite and nonnegative", "geo.crs.operation.accuracy_accepted"));
        }
        if (record.area_of_use) {
            if (auto valid = validate_geographic_area_of_interest(*record.area_of_use); !valid) {
                return valid;
            }
        }
        if (auto source = validate_coordinate_reference_descriptor(record.source_crs); !source) {
            return source;
        }
        if (auto target = validate_coordinate_reference_descriptor(record.target_crs); !target) {
            return target;
        }
        auto names = std::set<std::string>{};
        for (const auto &grid : record.grids) {
            if (!names.insert(grid.short_name).second) {
                return tsunami::core::failure(crs_error("geo.crs.resource_conflict", "duplicate coordinate-operation grid metadata", "geo.crs.operation.resources_verified").add_context("grid_name", grid.short_name));
            }
            if (auto valid = validate_coordinate_operation_grid(grid); !valid) {
                return valid;
            }
        }
        return tsunami::core::success();
    }

    auto validate_coordinate_transformation_diagnostics(
        const CoordinateTransformationDiagnostics &diagnostics) -> tsunami::core::Result<void>
    {
        if (diagnostics.transformed_coordinate_count > diagnostics.coordinate_count ||
            diagnostics.failed_coordinate_count != 0U ||
            !finite_nonnegative(diagnostics.maximum_forward_control_residual_m) ||
            !finite_nonnegative(diagnostics.maximum_inverse_round_trip_residual_m) ||
            (diagnostics.maximum_vertical_control_residual_m &&
             !finite_nonnegative(*diagnostics.maximum_vertical_control_residual_m)) ||
            !finite_extent(diagnostics.source_extent) || !finite_extent(diagnostics.target_extent)) {
            return tsunami::core::failure(crs_error("geo.crs.record_invalid", "coordinate transformation diagnostics are invalid", "geo.crs.record.diagnostics.valid"));
        }
        if (diagnostics.transformed_coordinate_count != diagnostics.coordinate_count) {
            return tsunami::core::failure(crs_error("geo.crs.record_invalid", "successful diagnostics must transform every coordinate", "geo.crs.record.diagnostics.valid"));
        }
        return tsunami::core::success();
    }

    auto validate_coordinate_transformation_record(const CoordinateTransformationRecord &record)
        -> tsunami::core::Result<void>
    {
        if (record.schema.schema_name != coordinate_transformation_record_schema_name ||
            record.schema.version != supported_coordinate_transformation_record_version ||
            record.policy_version != supported_coordinate_transformation_record_policy_version) {
            return tsunami::core::failure(crs_error("geo.crs.record_invalid", "coordinate transformation record schema identity is unsupported", "geo.crs.record.schema"));
        }
        const auto &id = record.identity;
        if (!logical_id_valid(id.transformation_id) || id.transformation_revision == 0U ||
            !logical_id_valid(id.manifest_id) || id.manifest_revision == 0U ||
            !logical_id_valid(id.source_import_id) || id.source_import_revision == 0U ||
            !logical_id_valid(id.source_dataset_id) || !logical_id_valid(id.source_asset_id) ||
            !logical_id_valid(id.output_dataset_id) || !logical_id_valid(id.output_process_id) ||
            !timestamp_valid(id.executed_at_utc)) {
            return tsunami::core::failure(crs_error("geo.crs.record_invalid", "coordinate transformation record identity is invalid", "geo.crs.record.identity"));
        }
        if (auto source = validate_coordinate_reference_descriptor(record.source_horizontal); !source) {
            return source;
        }
        if (record.source_vertical) {
            if (auto source_vertical = validate_coordinate_reference_descriptor(*record.source_vertical); !source_vertical) {
                return source_vertical;
            }
        }
        if (record.target.horizontal_unit != "m") {
            return tsunami::core::failure(crs_error("geo.crs.record_invalid", "record target must use metric horizontal storage", "geo.crs.unit.horizontal_metres"));
        }
        if (auto area = validate_geographic_area_of_interest(record.area_of_interest); !area) {
            return area;
        }
        if (auto operation = validate_coordinate_operation_record(record.horizontal_operation); !operation) {
            return operation;
        }
        if (auto vertical = validate_vertical_transformation(record.vertical_operation); !vertical) {
            return vertical;
        }
        auto names = std::set<std::string>{};
        for (const auto &grid : record.grids) {
            if (!names.insert(grid.short_name).second) {
                return tsunami::core::failure(crs_error("geo.crs.resource_conflict", "duplicate record grid metadata", "geo.crs.operation.resources_verified"));
            }
            if (auto valid = validate_coordinate_operation_grid(grid); !valid) {
                return valid;
            }
        }
        if (!finite_extent(record.source_extent) || !finite_extent(record.target_extent)) {
            return tsunami::core::failure(crs_error("geo.crs.record_invalid", "record extents are invalid", "geo.crs.record.extents"));
        }
        if (auto diagnostics = validate_coordinate_transformation_diagnostics(record.diagnostics); !diagnostics) {
            return diagnostics;
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
