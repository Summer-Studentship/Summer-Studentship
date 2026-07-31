#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/geo/ConstructedCorridor.hpp>
#include <tsunami/geo/CorridorConstructionRecord.hpp>

namespace tsunami::geo
{
    struct TerrainTargetGridPolicy
    {
        double target_spacing_m{};
        double active_coverage_threshold{0.5};
        double maximum_upsampling_factor{};
        std::uint64_t maximum_output_cells{};
        double numerical_absolute_tolerance{};
        double numerical_relative_tolerance{};
        std::string policy_basis;

        [[nodiscard]] auto operator==(const TerrainTargetGridPolicy &) const -> bool = default;
    };

    class TerrainTargetGrid
    {
    public:
        TerrainTargetGrid() = default;
        TerrainTargetGrid(
            std::uint64_t width,
            std::uint64_t height,
            double spacing_m,
            RasterAffineTransform transform,
            BoundingBox2D extent,
            ComputationalTargetReference target_reference,
            double xi_min_m,
            double xi_max_m,
            double eta_bottom_m,
            double eta_top_m,
            double longitudinal_padding_m,
            double transverse_padding_m);

        [[nodiscard]] auto width() const noexcept -> std::uint64_t { return width_; }
        [[nodiscard]] auto height() const noexcept -> std::uint64_t { return height_; }
        [[nodiscard]] auto cell_count() const noexcept -> std::uint64_t { return width_ * height_; }
        [[nodiscard]] auto spacing_m() const noexcept -> double { return spacing_m_; }
        [[nodiscard]] auto transform() const noexcept -> const RasterAffineTransform & { return transform_; }
        [[nodiscard]] auto extent() const noexcept -> const BoundingBox2D & { return extent_; }
        [[nodiscard]] auto target_reference() const noexcept -> const ComputationalTargetReference & { return target_reference_; }
        [[nodiscard]] auto registration() const noexcept -> RasterCellRegistration { return RasterCellRegistration::pixel_is_area; }
        [[nodiscard]] auto xi_min_m() const noexcept -> double { return xi_min_m_; }
        [[nodiscard]] auto xi_max_m() const noexcept -> double { return xi_max_m_; }
        [[nodiscard]] auto eta_bottom_m() const noexcept -> double { return eta_bottom_m_; }
        [[nodiscard]] auto eta_top_m() const noexcept -> double { return eta_top_m_; }
        [[nodiscard]] auto longitudinal_padding_m() const noexcept -> double { return longitudinal_padding_m_; }
        [[nodiscard]] auto transverse_padding_m() const noexcept -> double { return transverse_padding_m_; }

        [[nodiscard]] auto operator==(const TerrainTargetGrid &) const -> bool = default;

    private:
        std::uint64_t width_{};
        std::uint64_t height_{};
        double spacing_m_{};
        RasterAffineTransform transform_;
        BoundingBox2D extent_;
        ComputationalTargetReference target_reference_;
        double xi_min_m_{};
        double xi_max_m_{};
        double eta_bottom_m_{};
        double eta_top_m_{};
        double longitudinal_padding_m_{};
        double transverse_padding_m_{};
    };

    enum class TerrainCorridorCellClass
    {
        outside_corridor,
        excluded_boundary_fraction,
        active
    };

    [[nodiscard]] auto to_string(TerrainCorridorCellClass value) noexcept -> std::string_view;

    struct TerrainCorridorCoverage
    {
        TerrainTargetGrid grid;
        std::vector<double> fractions;
        std::vector<TerrainCorridorCellClass> cell_classes;
        std::uint64_t active_cell_count{};
        std::uint64_t outside_cell_count{};
        std::uint64_t excluded_boundary_cell_count{};
    };

    [[nodiscard]] auto build_corridor_aligned_terrain_grid(
        const ConstructedCorridor &corridor,
        const CorridorConstructionRecord &corridor_record,
        const TerrainTargetGridPolicy &policy) -> tsunami::core::Result<TerrainTargetGrid>;

    [[nodiscard]] auto calculate_corridor_coverage(
        const ConstructedCorridor &corridor,
        const CorridorConstructionRecord &corridor_record,
        const TerrainTargetGrid &grid,
        const TerrainTargetGridPolicy &policy) -> tsunami::core::Result<TerrainCorridorCoverage>;

    [[nodiscard]] auto terrain_grid_cell_centre(
        const TerrainTargetGrid &grid,
        std::uint64_t column,
        std::uint64_t row) -> Point2D;

} // namespace tsunami::geo
