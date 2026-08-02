#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/fvm/FiniteVolumeMesh.hpp>
#include <tsunami/fvm/MeshBinding.hpp>
#include <tsunami/geo/ConditionedTerrainRaster.hpp>
#include <tsunami/geo/TerrainConditioningRecord.hpp>
#include <tsunami/r2d/RegionalBathymetry.hpp>
#include <tsunami/r2d/RegionalGeometryPreflight.hpp>

namespace tsunami::r2d
{
    inline constexpr std::string_view regional_terrain_transfer_method_id{
        "area_weighted_piecewise_constant_v1"};

    struct RegionalRasterCellTransferPolicy
    {
        double absolute_area_tolerance_m2{};
        double relative_area_tolerance{};
        std::size_t maximum_contributors_per_cell{};

        [[nodiscard]] auto operator==(const RegionalRasterCellTransferPolicy &) const -> bool = default;
    };

    struct RegionalRasterCellContribution
    {
        std::uint64_t raster_cell_index{};
        double overlap_area_m2{};
        double weight{};

        [[nodiscard]] auto operator==(const RegionalRasterCellContribution &) const -> bool = default;
    };

    struct RegionalRasterCellContributionRange
    {
        std::size_t begin{};
        std::size_t count{};

        [[nodiscard]] auto operator==(const RegionalRasterCellContributionRange &) const -> bool = default;
    };

    class RegionalRasterCellTransferStencil
    {
    public:
        RegionalRasterCellTransferStencil(const RegionalRasterCellTransferStencil &) = default;
        RegionalRasterCellTransferStencil(RegionalRasterCellTransferStencil &&) noexcept = default;
        auto operator=(const RegionalRasterCellTransferStencil &) -> RegionalRasterCellTransferStencil & = default;
        auto operator=(RegionalRasterCellTransferStencil &&) noexcept -> RegionalRasterCellTransferStencil & = default;
        ~RegionalRasterCellTransferStencil() = default;

        [[nodiscard]] auto mesh_binding() const noexcept -> const tsunami::fvm::MeshBinding & { return mesh_binding_; }
        [[nodiscard]] auto grid() const noexcept -> const tsunami::geo::TerrainTargetGrid & { return grid_; }
        [[nodiscard]] auto policy() const noexcept -> const RegionalRasterCellTransferPolicy & { return policy_; }
        [[nodiscard]] auto cell_ranges() const noexcept -> std::span<const RegionalRasterCellContributionRange>
        {
            return cell_ranges_;
        }
        [[nodiscard]] auto mapped_area_m2() const noexcept -> std::span<const double> { return mapped_area_m2_; }
        [[nodiscard]] auto contributions() const noexcept -> std::span<const RegionalRasterCellContribution>
        {
            return contributions_;
        }

        [[nodiscard]] auto operator==(const RegionalRasterCellTransferStencil &) const -> bool = default;

    private:
        friend auto make_regional_raster_cell_transfer_stencil(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::geo::TerrainTargetGrid &grid,
            const RegionalRasterCellTransferPolicy &policy)
            -> tsunami::core::Result<RegionalRasterCellTransferStencil>;

        RegionalRasterCellTransferStencil(
            tsunami::fvm::MeshBinding mesh_binding,
            tsunami::geo::TerrainTargetGrid grid,
            RegionalRasterCellTransferPolicy policy,
            std::vector<RegionalRasterCellContributionRange> cell_ranges,
            std::vector<double> mapped_area_m2,
            std::vector<RegionalRasterCellContribution> contributions);

        tsunami::fvm::MeshBinding mesh_binding_;
        tsunami::geo::TerrainTargetGrid grid_;
        RegionalRasterCellTransferPolicy policy_;
        std::vector<RegionalRasterCellContributionRange> cell_ranges_;
        std::vector<double> mapped_area_m2_;
        std::vector<RegionalRasterCellContribution> contributions_;
    };

    struct RegionalTerrainTransferDiagnostics
    {
        std::string method_id;
        std::string mesh_id;
        std::string terrain_id;
        std::size_t cell_count{};
        std::size_t total_contributor_count{};
        std::size_t minimum_contributors_per_cell{};
        std::size_t maximum_contributors_per_cell{};
        double total_mesh_area_m2{};
        double total_mapped_terrain_area_m2{};
        double maximum_cell_area_residual_m2{};
        double minimum_bed_elevation_m{};
        double maximum_bed_elevation_m{};
        std::map<std::string, std::size_t> contributor_lineage_counts;
    };

    struct RegionalTerrainTransferResult
    {
        RegionalBathymetry bathymetry;
        RegionalTerrainTransferDiagnostics diagnostics;
    };

    struct RegionalScalarRasterTransferResult
    {
        std::vector<tsunami::core::Real> values;
        std::size_t cell_count{};
        std::size_t total_contributor_count{};
        double minimum_value{};
        double maximum_value{};
    };

    [[nodiscard]] auto make_regional_raster_cell_transfer_stencil(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::geo::TerrainTargetGrid &grid,
        const RegionalRasterCellTransferPolicy &policy)
        -> tsunami::core::Result<RegionalRasterCellTransferStencil>;

    [[nodiscard]] auto transfer_conditioned_terrain_to_regional_bathymetry(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::geo::ConditionedTerrainRaster &terrain,
        const tsunami::geo::TerrainConditioningRecord &record,
        const RegionalGeometryPreflightReport &preflight,
        const RegionalRasterCellTransferStencil &stencil,
        tsunami::fvm::FieldId field_id,
        std::string field_name)
        -> tsunami::core::Result<RegionalTerrainTransferResult>;

    [[nodiscard]] auto transfer_scalar_raster_to_regional_cells(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::geo::TerrainTargetGrid &grid,
        std::span<const double> raster_values,
        std::span<const std::uint8_t> valid_mask,
        const RegionalRasterCellTransferStencil &stencil,
        std::string source_id)
        -> tsunami::core::Result<RegionalScalarRasterTransferResult>;

} // namespace tsunami::r2d
