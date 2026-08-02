#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

#include <tsunami/geo/TerrainConditioning.hpp>
#include <tsunami/geo/TerrainConditioningSerialisation.hpp>

namespace tsunami::geo_gdal
{
    inline constexpr std::uint32_t conditioned_terrain_artifact_contract_version{1U};
    inline constexpr std::string_view conditioned_terrain_role{"conditioned_terrain"};
    inline constexpr std::string_view corridor_coverage_fraction_role{"corridor_coverage_fraction"};
    inline constexpr std::string_view terrain_cell_lineage_role{"terrain_cell_lineage"};

    struct ConditionedTerrainArtifactPaths
    {
        std::filesystem::path terrain_path;
        std::filesystem::path coverage_path;
        std::filesystem::path lineage_path;

        [[nodiscard]] auto operator==(const ConditionedTerrainArtifactPaths &) const -> bool = default;
    };

    struct ConditionedTerrainArtifactReadPolicy
    {
        std::uint64_t maximum_cells{};

        [[nodiscard]] auto operator==(const ConditionedTerrainArtifactReadPolicy &) const -> bool = default;
    };

    struct ConditionedTerrainArtifactReadDiagnostics
    {
        std::uint32_t artefact_contract_version{};
        std::string gdal_runtime_version;
        std::string terrain_id;
        std::uint64_t terrain_revision{};
        std::uint64_t width{};
        std::uint64_t height{};
        std::uint64_t cell_count{};
        std::uint64_t valid_terrain_cell_count{};
        std::uint64_t invalid_terrain_cell_count{};
        double minimum_bed_elevation_m{};
        double maximum_bed_elevation_m{};
        double minimum_coverage_fraction{};
        double maximum_coverage_fraction{};
        std::map<std::string, std::uint64_t> lineage_counts;
        ConditionedTerrainArtifactPaths paths;
        std::string validation_status;

        [[nodiscard]] auto operator==(const ConditionedTerrainArtifactReadDiagnostics &) const -> bool = default;
    };

    struct ConditionedTerrainArtifactReadResult
    {
        tsunami::geo::ConditionedTerrainRaster terrain;
        ConditionedTerrainArtifactReadDiagnostics diagnostics;
    };

    [[nodiscard]] auto make_conditioned_terrain_artifact_paths(
        const std::filesystem::path &case_root,
        const tsunami::geo::TerrainConditioningRecord &record)
        -> tsunami::core::Result<ConditionedTerrainArtifactPaths>;

    [[nodiscard]] auto read_conditioned_terrain_artifacts_with_gdal(
        const ConditionedTerrainArtifactPaths &paths,
        const tsunami::geo::TerrainConditioningRecord &record,
        const ConditionedTerrainArtifactReadPolicy &policy)
        -> tsunami::core::Result<ConditionedTerrainArtifactReadResult>;

    [[nodiscard]] auto write_conditioned_terrain_artifacts_with_gdal(
        const ConditionedTerrainArtifactPaths &paths,
        const tsunami::geo::ConditionedTerrainRaster &terrain,
        const tsunami::geo::TerrainConditioningRecord &record)
        -> tsunami::core::Result<void>;

} // namespace tsunami::geo_gdal
