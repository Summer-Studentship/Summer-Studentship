#include <tsunami/geo/ConditionedTerrainRaster.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto terrain_error(std::string message, std::string rule_id) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                "geo.terrain.output_invalid",
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "make_conditioned_terrain_raster")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }
    }

    ConditionedTerrainRaster::ConditionedTerrainRaster(
        TerrainTargetGrid grid,
        std::vector<double> values,
        std::vector<std::uint8_t> valid_mask,
        std::vector<double> corridor_coverage_fraction,
        std::vector<TerrainCellLineage> cell_lineage,
        double minimum_elevation_m,
        double maximum_elevation_m)
        : grid_{std::move(grid)},
          values_{std::move(values)},
          valid_mask_{std::move(valid_mask)},
          corridor_coverage_fraction_{std::move(corridor_coverage_fraction)},
          cell_lineage_{std::move(cell_lineage)},
          minimum_elevation_m_{minimum_elevation_m},
          maximum_elevation_m_{maximum_elevation_m}
    {
    }

    auto to_string(TerrainCellLineage value) noexcept -> std::string_view
    {
        switch (value) {
        case TerrainCellLineage::outside_corridor:
            return "outside_corridor";
        case TerrainCellLineage::excluded_boundary_fraction:
            return "excluded_boundary_fraction";
        case TerrainCellLineage::bathymetry_selected:
            return "bathymetry_selected";
        case TerrainCellLineage::topography_selected:
            return "topography_selected";
        case TerrainCellLineage::overlap_bathymetry_selected:
            return "overlap_bathymetry_selected";
        case TerrainCellLineage::overlap_topography_selected:
            return "overlap_topography_selected";
        case TerrainCellLineage::overlap_bathymetry_selected_with_conflict:
            return "overlap_bathymetry_selected_with_conflict";
        case TerrainCellLineage::overlap_topography_selected_with_conflict:
            return "overlap_topography_selected_with_conflict";
        case TerrainCellLineage::filled_from_bathymetry_neighbourhood:
            return "filled_from_bathymetry_neighbourhood";
        case TerrainCellLineage::filled_from_topography_neighbourhood:
            return "filled_from_topography_neighbourhood";
        }
        return "outside_corridor";
    }

    auto terrain_lineage_code(TerrainCellLineage value) noexcept -> std::uint16_t
    {
        switch (value) {
        case TerrainCellLineage::outside_corridor:
            return 1U;
        case TerrainCellLineage::excluded_boundary_fraction:
            return 2U;
        case TerrainCellLineage::bathymetry_selected:
            return 3U;
        case TerrainCellLineage::topography_selected:
            return 4U;
        case TerrainCellLineage::overlap_bathymetry_selected:
            return 5U;
        case TerrainCellLineage::overlap_topography_selected:
            return 6U;
        case TerrainCellLineage::overlap_bathymetry_selected_with_conflict:
            return 7U;
        case TerrainCellLineage::overlap_topography_selected_with_conflict:
            return 8U;
        case TerrainCellLineage::filled_from_bathymetry_neighbourhood:
            return 9U;
        case TerrainCellLineage::filled_from_topography_neighbourhood:
            return 10U;
        }
        return 0U;
    }

    auto terrain_cell_lineage_from_code(std::uint16_t code)
        -> tsunami::core::Result<TerrainCellLineage>
    {
        switch (code) {
        case 1U:
            return tsunami::core::success(TerrainCellLineage::outside_corridor);
        case 2U:
            return tsunami::core::success(TerrainCellLineage::excluded_boundary_fraction);
        case 3U:
            return tsunami::core::success(TerrainCellLineage::bathymetry_selected);
        case 4U:
            return tsunami::core::success(TerrainCellLineage::topography_selected);
        case 5U:
            return tsunami::core::success(TerrainCellLineage::overlap_bathymetry_selected);
        case 6U:
            return tsunami::core::success(TerrainCellLineage::overlap_topography_selected);
        case 7U:
            return tsunami::core::success(TerrainCellLineage::overlap_bathymetry_selected_with_conflict);
        case 8U:
            return tsunami::core::success(TerrainCellLineage::overlap_topography_selected_with_conflict);
        case 9U:
            return tsunami::core::success(TerrainCellLineage::filled_from_bathymetry_neighbourhood);
        case 10U:
            return tsunami::core::success(TerrainCellLineage::filled_from_topography_neighbourhood);
        default:
            return tsunami::core::failure<TerrainCellLineage>(
                terrain_error("terrain lineage code is not recognised", "geo.terrain.output.lineage_code_known")
                    .add_context("lineage_encoding_version", std::string{terrain_cell_lineage_encoding_version})
                    .add_context("actual", std::to_string(code)));
        }
    }

    auto make_conditioned_terrain_raster(
        TerrainTargetGrid grid,
        std::vector<double> values,
        std::vector<std::uint8_t> valid_mask,
        std::vector<double> corridor_coverage_fraction,
        std::vector<TerrainCellLineage> cell_lineage) -> tsunami::core::Result<ConditionedTerrainRaster>
    {
        const auto cells = static_cast<std::size_t>(grid.cell_count());
        if (values.size() != cells || valid_mask.size() != cells ||
            corridor_coverage_fraction.size() != cells || cell_lineage.size() != cells) {
            return tsunami::core::failure<ConditionedTerrainRaster>(terrain_error("terrain vectors do not match target-grid cell count", "geo.terrain.output.lineage_complete"));
        }
        auto min_value = 0.0;
        auto max_value = 0.0;
        auto have_valid = false;
        for (std::size_t i = 0U; i < cells; ++i) {
            if (valid_mask[i] != 0U) {
                if (!std::isfinite(values[i])) {
                    return tsunami::core::failure<ConditionedTerrainRaster>(terrain_error("active terrain value is nonfinite", "geo.terrain.output.no_active_nodata"));
                }
                min_value = have_valid ? std::min(min_value, values[i]) : values[i];
                max_value = have_valid ? std::max(max_value, values[i]) : values[i];
                have_valid = true;
            }
        }
        if (!have_valid) {
            return tsunami::core::failure<ConditionedTerrainRaster>(terrain_error("conditioned terrain contains no valid active cells", "geo.terrain.output.no_active_nodata"));
        }
        return tsunami::core::success(ConditionedTerrainRaster{
            std::move(grid),
            std::move(values),
            std::move(valid_mask),
            std::move(corridor_coverage_fraction),
            std::move(cell_lineage),
            min_value,
            max_value});
    }

} // namespace tsunami::geo
