#include <tsunami/r2d/RegionalEarthquakeDiagnostics.hpp>

#include <algorithm>

namespace tsunami::r2d
{
    namespace
    {
        [[nodiscard]] auto contains_embedded_null(std::string_view text) -> bool
        {
            return text.find('\0') != std::string_view::npos;
        }

        [[nodiscard]] auto metadata_error(
            const RegionalEarthquakeSourceMetadata &metadata,
            std::string message) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                "r2d.earthquake.metadata_invalid",
                std::move(message),
                tsunami::core::DiagnosticCategory::numerical,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_regional_earthquake_source_metadata")
                .add_context("rule_id", "SWE-R2D-EQK")
                .add_context("source_kind", std::string{to_string(metadata.source_kind)})
                .add_context("event_id", metadata.event_id)
                .add_context("model_id", metadata.model_id)
                .add_context("state_changed", "false");
            return error;
        }
    } // namespace

    auto to_string(RegionalEarthquakeSourceKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case RegionalEarthquakeSourceKind::synthetic:
            return "synthetic";
        case RegionalEarthquakeSourceKind::gridded_displacement:
            return "gridded_displacement";
        case RegionalEarthquakeSourceKind::finite_fault:
            return "finite_fault";
        }
        return "unknown";
    }

    auto to_string(RegionalBedDeformationMappingKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case RegionalBedDeformationMappingKind::vertical_only:
            return "vertical_only";
        case RegionalBedDeformationMappingKind::horizontal_slope_corrected:
            return "horizontal_slope_corrected";
        }
        return "unknown";
    }

    auto to_string(RegionalSurfaceTransferKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case RegionalSurfaceTransferKind::passive_equal_to_effective_bed:
            return "passive_equal_to_effective_bed";
        case RegionalSurfaceTransferKind::prescribed:
            return "prescribed";
        }
        return "unknown";
    }

    auto validate_regional_earthquake_source_metadata(
        const RegionalEarthquakeSourceMetadata &metadata) -> tsunami::core::Result<void>
    {
        const auto invalid_text = [](const std::string &text) {
            return text.empty() || contains_embedded_null(text);
        };
        if (invalid_text(metadata.event_id)) {
            return tsunami::core::failure(metadata_error(metadata, "earthquake event id is required"));
        }
        if (invalid_text(metadata.model_id)) {
            return tsunami::core::failure(metadata_error(metadata, "earthquake model id is required"));
        }
        if (invalid_text(metadata.source_format)) {
            return tsunami::core::failure(metadata_error(metadata, "earthquake source format is required"));
        }
        if (invalid_text(metadata.coordinate_reference)) {
            return tsunami::core::failure(metadata_error(metadata, "earthquake coordinate reference is required"));
        }
        if (metadata.source_kind == RegionalEarthquakeSourceKind::finite_fault && metadata.subfault_count == 0U) {
            return tsunami::core::failure(metadata_error(metadata, "finite-fault metadata requires at least one subfault"));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::r2d
