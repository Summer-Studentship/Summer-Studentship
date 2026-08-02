#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/geo/TerrainConditioningRecord.hpp>

namespace tsunami::geo_gdal
{
    inline constexpr std::uint32_t earthquake_displacement_artifact_contract_version{1U};
    inline constexpr std::string_view earthquake_vertical_displacement_role{"vertical_seabed_displacement"};

    struct EarthquakeDisplacementArtifactPaths
    {
        std::filesystem::path displacement_path;
        std::filesystem::path metadata_path;

        [[nodiscard]] auto operator==(const EarthquakeDisplacementArtifactPaths &) const -> bool = default;
    };

    struct EarthquakeDisplacementArtifactMetadata
    {
        std::uint32_t artifact_contract_version{earthquake_displacement_artifact_contract_version};
        std::string event_id;
        std::string model_id;
        std::string source_format;
        std::string coordinate_reference;
        std::uint64_t subfault_count{};
        std::string vertical_unit{"m"};
        std::string source_uri;
        std::string source_sha256;
        std::string generated_at_utc;
        std::string producer;

        [[nodiscard]] auto operator==(const EarthquakeDisplacementArtifactMetadata &) const -> bool = default;
    };

    struct EarthquakeDisplacementArtifactReadPolicy
    {
        std::uint64_t maximum_cells{};

        [[nodiscard]] auto operator==(const EarthquakeDisplacementArtifactReadPolicy &) const -> bool = default;
    };

    struct EarthquakeDisplacementArtifactReadDiagnostics
    {
        std::uint32_t artifact_contract_version{};
        std::string event_id;
        std::string model_id;
        std::string source_format;
        std::string coordinate_reference;
        std::uint64_t subfault_count{};
        std::uint64_t width{};
        std::uint64_t height{};
        std::uint64_t cell_count{};
        std::uint64_t valid_cell_count{};
        double minimum_vertical_displacement_m{};
        double maximum_vertical_displacement_m{};
        EarthquakeDisplacementArtifactPaths paths;
        std::string validation_status;

        [[nodiscard]] auto operator==(const EarthquakeDisplacementArtifactReadDiagnostics &) const -> bool = default;
    };

    struct EarthquakeDisplacementArtifactReadResult
    {
        tsunami::geo::TerrainTargetGrid grid;
        std::vector<double> vertical_displacement_m;
        std::vector<std::uint8_t> valid_mask;
        EarthquakeDisplacementArtifactMetadata metadata;
        EarthquakeDisplacementArtifactReadDiagnostics diagnostics;
    };

    [[nodiscard]] auto read_earthquake_displacement_artifact_with_gdal(
        const EarthquakeDisplacementArtifactPaths &paths,
        const tsunami::geo::TerrainConditioningRecord &terrain_record,
        const EarthquakeDisplacementArtifactReadPolicy &policy)
        -> tsunami::core::Result<EarthquakeDisplacementArtifactReadResult>;

    [[nodiscard]] auto write_earthquake_displacement_artifact_with_gdal(
        const EarthquakeDisplacementArtifactPaths &paths,
        const tsunami::geo::TerrainTargetGrid &grid,
        const std::vector<double> &vertical_displacement_m,
        const std::vector<std::uint8_t> &valid_mask,
        const EarthquakeDisplacementArtifactMetadata &metadata)
        -> tsunami::core::Result<void>;

} // namespace tsunami::geo_gdal
