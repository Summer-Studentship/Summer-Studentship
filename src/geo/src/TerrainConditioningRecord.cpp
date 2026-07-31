#include <tsunami/geo/TerrainConditioningRecord.hpp>

#include <cmath>
#include <regex>
#include <string>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto record_error(std::string message, std::string rule_id) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                "geo.terrain.record_invalid",
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_terrain_conditioning_record")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto logical_id_valid(std::string_view text) -> bool
        {
            static const auto pattern = std::regex{"^[a-z0-9]+(?:[._-][a-z0-9]+)*$"};
            const auto copy = std::string{text};
            return !copy.empty() && copy.size() <= 128U && std::regex_match(copy, pattern);
        }

        [[nodiscard]] auto timestamp_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z$"};
            return std::regex_match(text, pattern);
        }

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }
    }

    auto to_string(TerrainOverlapConflictPolicy value) noexcept -> std::string_view
    {
        switch (value) {
        case TerrainOverlapConflictPolicy::reject:
            return "reject";
        case TerrainOverlapConflictPolicy::accept_priority_with_warning:
            return "accept_priority_with_warning";
        }
        return "reject";
    }

    auto to_string(TerrainGapResolutionKind value) noexcept -> std::string_view
    {
        switch (value) {
        case TerrainGapResolutionKind::reject:
            return "reject";
        case TerrainGapResolutionKind::bounded_inverse_distance:
            return "bounded_inverse_distance";
        }
        return "reject";
    }

    auto to_string(TerrainUncertaintyCombination value) noexcept -> std::string_view
    {
        switch (value) {
        case TerrainUncertaintyCombination::not_computed:
            return "not_computed";
        case TerrainUncertaintyCombination::root_sum_square:
            return "root_sum_square";
        case TerrainUncertaintyCombination::conservative_sum:
            return "conservative_sum";
        }
        return "not_computed";
    }

    auto default_terrain_conditioning_record_path(
        std::string_view output_dataset_id) -> std::filesystem::path
    {
        if (!logical_id_valid(output_dataset_id)) {
            return {};
        }
        return std::filesystem::path{"manifests"} / "terrain" / (std::string{output_dataset_id} + ".json");
    }

    auto default_conditioned_terrain_path(
        std::string_view output_dataset_id) -> std::filesystem::path
    {
        if (!logical_id_valid(output_dataset_id)) {
            return {};
        }
        return std::filesystem::path{"outputs"} / "terrain" / (std::string{output_dataset_id} + ".tif");
    }

    auto validate_terrain_conditioning_record(
        const TerrainConditioningRecord &record) -> tsunami::core::Result<void>
    {
        if (record.schema.schema_name != terrain_conditioning_record_schema_name ||
            record.schema.version != supported_terrain_conditioning_record_version ||
            record.policy_version != supported_terrain_conditioning_record_policy_version ||
            record.formula_version != terrain_conditioning_formula_version) {
            return tsunami::core::failure(record_error("terrain conditioning record schema identity is unsupported", "geo.terrain.record.schema"));
        }
        if (!logical_id_valid(record.identity.terrain_id) || record.identity.terrain_revision == 0U ||
            !logical_id_valid(record.identity.manifest_id) || record.identity.manifest_revision == 0U ||
            !logical_id_valid(record.identity.output_dataset_id) ||
            !logical_id_valid(record.identity.output_process_id) ||
            !timestamp_valid(record.identity.executed_at_utc)) {
            return tsunami::core::failure(record_error("terrain conditioning identity is invalid", "geo.terrain.request.identities_match"));
        }
        if (record.diagnostics.unresolved_cell_count != 0U || record.diagnostics.active_cell_count == 0U ||
            !finite(record.diagnostics.minimum_elevation_m) || !finite(record.diagnostics.maximum_elevation_m) ||
            record.diagnostics.minimum_elevation_m > record.diagnostics.maximum_elevation_m) {
            return tsunami::core::failure(record_error("terrain diagnostics contain unresolved active cells or invalid elevation bounds", "geo.terrain.output.no_active_nodata"));
        }
        if (record.digest_status != "not_computed_by_terrain_conditioning" ||
            record.output_media_type != "image/tiff" || record.output_path.empty()) {
            return tsunami::core::failure(record_error("terrain output metadata is incomplete", "geo.terrain.record.output"));
        }
        if (record.grid.cell_count() != record.diagnostics.total_cell_count ||
            record.grid.registration() != RasterCellRegistration::pixel_is_area ||
            record.target_reference != record.grid.target_reference()) {
            return tsunami::core::failure(record_error("terrain target grid metadata is inconsistent", "geo.terrain.grid.pixel_is_area"));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
