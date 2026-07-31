#pragma once

#include <filesystem>
#include <string>

#include <tsunami/geo/TerrainConditioningRecord.hpp>

namespace tsunami::geo
{
    [[nodiscard]] auto serialise_terrain_conditioning_record(
        const TerrainConditioningRecord &record) -> tsunami::core::Result<std::string>;

    [[nodiscard]] auto write_terrain_conditioning_record(
        const std::filesystem::path &path,
        const TerrainConditioningRecord &record) -> tsunami::core::Result<void>;

} // namespace tsunami::geo
