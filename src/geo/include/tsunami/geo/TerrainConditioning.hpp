#pragma once

#include <filesystem>

#include <tsunami/data/CaseConfiguration.hpp>
#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/TerrainConditioningRecord.hpp>

namespace tsunami::geo
{
    struct TerrainConditioningPolicy
    {
        TerrainTargetGridPolicy grid;
        TerrainMergePolicy merge;
        TerrainGapResolutionPolicy gaps;
        TerrainUncertaintyPolicy uncertainty;

        [[nodiscard]] auto operator==(const TerrainConditioningPolicy &) const -> bool = default;
    };

    struct TerrainConditioningRequest
    {
        const tsunami::data::CaseConfiguration *configuration{};
        const tsunami::data::DatasetManifest *manifest{};
        const ConstructedCorridor *corridor{};
        const CorridorConstructionRecord *corridor_record{};
        TerrainSourceRequest bathymetry;
        TerrainSourceRequest topography;
        TerrainConditioningIdentity identity;
        TerrainConditioningPolicy policy;
        std::filesystem::path resource_root;
    };

    struct TerrainConditioningPreparation
    {
        TerrainTargetGrid grid;
        TerrainCorridorCoverage coverage;
        TerrainSourceResamplingRequest bathymetry_resampling;
        TerrainSourceResamplingRequest topography_resampling;
        TerrainConditioningIdentity identity;
        TerrainConditioningPolicy policy;
        CorridorConstructionIdentity corridor_identity;
        std::string scenario_id;
        std::string target_site;
        tsunami::data::DatasetUncertainty output_uncertainty;
        std::filesystem::path output_path;
    };

    struct TerrainConditioningResult
    {
        ConditionedTerrainRaster terrain;
        TerrainConditioningRecord record;
        TerrainConditioningDiagnostics diagnostics;
    };

    [[nodiscard]] auto prepare_terrain_conditioning(
        const TerrainConditioningRequest &request) -> tsunami::core::Result<TerrainConditioningPreparation>;

    [[nodiscard]] auto condition_terrain_from_resampled_sources(
        const TerrainConditioningPreparation &preparation,
        const ResampledTerrainSource &bathymetry,
        const ResampledTerrainSource &topography) -> tsunami::core::Result<TerrainConditioningResult>;

} // namespace tsunami::geo
