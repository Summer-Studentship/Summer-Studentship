#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <tsunami/geo/CoordinateTransformationPlan.hpp>
#include <tsunami/geo/GeospatialImportRecord.hpp>
#include <tsunami/geo/TerrainTargetGrid.hpp>

namespace tsunami::geo
{
    enum class TerrainSourceRole
    {
        bathymetry,
        topography
    };

    enum class RasterResamplingKernel
    {
        bilinear,
        area_average
    };

    enum class ResampledTerrainCellStatus
    {
        valid_resampled,
        source_nodata,
        outside_source_coverage
    };

    [[nodiscard]] auto to_string(TerrainSourceRole value) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(RasterResamplingKernel value) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(ResampledTerrainCellStatus value) noexcept -> std::string_view;

    struct TerrainSourceRequest
    {
        TerrainSourceRole role{TerrainSourceRole::bathymetry};
        const ImportedRaster *raster{};
        const GeospatialImportRecord *import_record{};
        const RasterTransformationPlan *transformation_plan{};
        const CoordinateTransformationRecord *transformation_record{};
        std::string expected_dataset_id;
        std::string expected_asset_id;
        RasterResamplingKernel resampling_kernel{RasterResamplingKernel::bilinear};
    };

    struct TerrainSourceResamplingRequest
    {
        const ImportedRaster *source_raster{};
        const GeospatialImportRecord *import_record{};
        const RasterTransformationPlan *transformation_plan{};
        const CoordinateTransformationRecord *transformation_record{};
        TerrainTargetGrid target_grid;
        TerrainSourceRole role{TerrainSourceRole::bathymetry};
        RasterResamplingKernel kernel{RasterResamplingKernel::bilinear};
        double maximum_upsampling_factor{};
        std::filesystem::path resource_root;
    };

    struct RasterResamplingRecord
    {
        std::string dataset_id;
        std::string asset_id;
        GeospatialImportIdentity import_identity;
        CoordinateTransformationIdentity transformation_identity;
        TerrainSourceRole role{TerrainSourceRole::bathymetry};
        RasterResamplingKernel kernel{RasterResamplingKernel::bilinear};
        RasterCellRegistration source_registration{RasterCellRegistration::unknown};
        RasterCellRegistration target_registration{RasterCellRegistration::pixel_is_area};
        std::optional<double> source_scale;
        std::optional<double> source_offset;
        double minimum_source_spacing_m{};
        double maximum_source_spacing_m{};
        double nominal_source_spacing_m{};
        double target_spacing_m{};
        double maximum_upsampling_factor{};
        std::uint64_t source_valid_cell_count{};
        std::uint64_t output_valid_cell_count{};
        std::uint64_t source_nodata_cell_count{};
        std::uint64_t outside_coverage_cell_count{};
        CoordinateOperationRecord operation;
        VerticalTransformationSpecification vertical_steps;
        std::string adapter_name;
        std::string adapter_version;
    };

    struct ResampledTerrainSource
    {
        std::string dataset_id;
        TerrainSourceRole role{TerrainSourceRole::bathymetry};
        TerrainTargetGrid grid;
        std::vector<double> values;
        std::vector<std::uint8_t> valid_mask;
        std::vector<ResampledTerrainCellStatus> cell_status;
        RasterResamplingRecord resampling;
    };

    [[nodiscard]] auto validate_terrain_source_resampling_request(
        const TerrainSourceResamplingRequest &request) -> tsunami::core::Result<void>;

} // namespace tsunami::geo
