#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/geo/TerrainTargetGrid.hpp>

namespace tsunami::geo
{
    enum class TerrainCellLineage
    {
        outside_corridor,
        excluded_boundary_fraction,
        bathymetry_selected,
        topography_selected,
        overlap_bathymetry_selected,
        overlap_topography_selected,
        overlap_bathymetry_selected_with_conflict,
        overlap_topography_selected_with_conflict,
        filled_from_bathymetry_neighbourhood,
        filled_from_topography_neighbourhood
    };

    inline constexpr std::string_view terrain_cell_lineage_encoding_version{"terrain-cell-lineage-code-v1"};

    [[nodiscard]] auto to_string(TerrainCellLineage value) noexcept -> std::string_view;
    [[nodiscard]] auto terrain_lineage_code(TerrainCellLineage value) noexcept -> std::uint16_t;
    [[nodiscard]] auto terrain_cell_lineage_from_code(std::uint16_t code)
        -> tsunami::core::Result<TerrainCellLineage>;

    class ConditionedTerrainRaster
    {
    public:
        ConditionedTerrainRaster() = default;
        ConditionedTerrainRaster(
            TerrainTargetGrid grid,
            std::vector<double> values,
            std::vector<std::uint8_t> valid_mask,
            std::vector<double> corridor_coverage_fraction,
            std::vector<TerrainCellLineage> cell_lineage,
            double minimum_elevation_m,
            double maximum_elevation_m);

        [[nodiscard]] auto width() const noexcept -> std::uint64_t { return grid_.width(); }
        [[nodiscard]] auto height() const noexcept -> std::uint64_t { return grid_.height(); }
        [[nodiscard]] auto cell_count() const noexcept -> std::uint64_t { return grid_.cell_count(); }
        [[nodiscard]] auto grid() const noexcept -> const TerrainTargetGrid & { return grid_; }
        [[nodiscard]] auto values() const noexcept -> const std::vector<double> & { return values_; }
        [[nodiscard]] auto valid_mask() const noexcept -> const std::vector<std::uint8_t> & { return valid_mask_; }
        [[nodiscard]] auto corridor_coverage_fraction() const noexcept -> const std::vector<double> & { return corridor_coverage_fraction_; }
        [[nodiscard]] auto cell_lineage() const noexcept -> const std::vector<TerrainCellLineage> & { return cell_lineage_; }
        [[nodiscard]] auto minimum_elevation_m() const noexcept -> double { return minimum_elevation_m_; }
        [[nodiscard]] auto maximum_elevation_m() const noexcept -> double { return maximum_elevation_m_; }

        [[nodiscard]] auto operator==(const ConditionedTerrainRaster &) const -> bool = default;

    private:
        TerrainTargetGrid grid_;
        std::vector<double> values_;
        std::vector<std::uint8_t> valid_mask_;
        std::vector<double> corridor_coverage_fraction_;
        std::vector<TerrainCellLineage> cell_lineage_;
        double minimum_elevation_m_{};
        double maximum_elevation_m_{};
    };

    [[nodiscard]] auto make_conditioned_terrain_raster(
        TerrainTargetGrid grid,
        std::vector<double> values,
        std::vector<std::uint8_t> valid_mask,
        std::vector<double> corridor_coverage_fraction,
        std::vector<TerrainCellLineage> cell_lineage) -> tsunami::core::Result<ConditionedTerrainRaster>;

} // namespace tsunami::geo
