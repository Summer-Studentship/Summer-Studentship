#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
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

    struct RegionalRasterCellTransferStencil
    {
        tsunami::fvm::MeshBinding mesh_binding;
        tsunami::geo::TerrainTargetGrid grid;
        RegionalRasterCellTransferPolicy policy;
        std::vector<RegionalRasterCellContributionRange> cell_ranges;
        std::vector<double> mapped_area_m2;
        std::vector<RegionalRasterCellContribution> contributions;

        [[nodiscard]] auto operator==(const RegionalRasterCellTransferStencil &) const -> bool = default;
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

} // namespace tsunami::r2d
