#pragma once

#include <filesystem>

#include <tsunami/geo_gdal/GdalConditionedTerrainArtifacts.hpp>
#include <tsunami/geo/TerrainConditioning.hpp>
#include <tsunami/geo/TerrainConditioningSerialisation.hpp>

namespace tsunami::geo_gdal
{
    [[nodiscard]] auto resample_terrain_source_with_gdal(
        const tsunami::geo::TerrainSourceResamplingRequest &request)
        -> tsunami::core::Result<tsunami::geo::ResampledTerrainSource>;

    [[nodiscard]] auto condition_terrain_with_gdal(
        const tsunami::geo::TerrainConditioningRequest &request)
        -> tsunami::core::Result<tsunami::geo::TerrainConditioningResult>;

    [[nodiscard]] auto write_conditioned_terrain_geotiff_with_gdal(
        const std::filesystem::path &path,
        const tsunami::geo::ConditionedTerrainRaster &terrain,
        const tsunami::geo::TerrainConditioningRecord &record)
        -> tsunami::core::Result<void>;

    [[nodiscard]] auto write_terrain_inspection_geotiffs_with_gdal(
        const std::filesystem::path &terrain_path,
        const std::filesystem::path &coverage_path,
        const std::filesystem::path &lineage_path,
        const tsunami::geo::ConditionedTerrainRaster &terrain,
        const tsunami::geo::TerrainConditioningRecord &record)
        -> tsunami::core::Result<void>;

} // namespace tsunami::geo_gdal
