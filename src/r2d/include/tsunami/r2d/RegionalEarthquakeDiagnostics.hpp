#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <tsunami/r2d/ShallowWaterState.hpp>

namespace tsunami::r2d
{
    enum class RegionalEarthquakeSourceKind
    {
        synthetic,
        gridded_displacement,
        finite_fault
    };

    enum class RegionalBedDeformationMappingKind
    {
        vertical_only,
        horizontal_slope_corrected
    };

    enum class RegionalSurfaceTransferKind
    {
        passive_equal_to_effective_bed,
        prescribed
    };

    [[nodiscard]] auto to_string(RegionalEarthquakeSourceKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(RegionalBedDeformationMappingKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(RegionalSurfaceTransferKind kind) noexcept -> std::string_view;

    struct RegionalEarthquakeSourceMetadata
    {
        RegionalEarthquakeSourceKind source_kind{RegionalEarthquakeSourceKind::synthetic};
        std::string event_id;
        std::string model_id;
        std::string source_format;
        std::string coordinate_reference;
        std::size_t subfault_count{};
    };

    [[nodiscard]] auto validate_regional_earthquake_source_metadata(
        const RegionalEarthquakeSourceMetadata &metadata) -> tsunami::core::Result<void>;

    struct RegionalEarthquakeInitialisationDiagnostics
    {
        RegionalEarthquakeSourceMetadata metadata;
        RegionalBedDeformationMappingKind bed_mapping{RegionalBedDeformationMappingKind::vertical_only};
        RegionalSurfaceTransferKind surface_transfer{RegionalSurfaceTransferKind::passive_equal_to_effective_bed};
        std::size_t cell_count{};
        tsunami::core::Real minimum_eastward_displacement{};
        tsunami::core::Real maximum_eastward_displacement{};
        tsunami::core::Real minimum_northward_displacement{};
        tsunami::core::Real maximum_northward_displacement{};
        tsunami::core::Real minimum_upward_displacement{};
        tsunami::core::Real maximum_upward_displacement{};
        tsunami::core::Real minimum_effective_bed_displacement{};
        tsunami::core::Real maximum_effective_bed_displacement{};
        tsunami::core::Real minimum_surface_perturbation{};
        tsunami::core::Real maximum_surface_perturbation{};
        tsunami::core::Real integrated_upward_displacement{};
        tsunami::core::Real integrated_effective_bed_displacement{};
        tsunami::core::Real integrated_surface_perturbation{};
        tsunami::core::Real pre_event_water_volume{};
        tsunami::core::Real post_event_water_volume{};
        tsunami::core::Real water_volume_change{};
        tsunami::core::Real maximum_absolute_bathymetry_change{};
        tsunami::core::Real maximum_absolute_surface_perturbation{};
        tsunami::core::Real maximum_absolute_depth_change{};
        std::size_t newly_wet_cell_count{};
        std::size_t newly_dry_cell_count{};
        tsunami::core::Real pre_event_maximum_momentum{};
        tsunami::core::Real post_event_maximum_momentum{};
    };

} // namespace tsunami::r2d

