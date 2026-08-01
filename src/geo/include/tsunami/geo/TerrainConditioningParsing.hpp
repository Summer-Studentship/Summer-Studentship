#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <tsunami/core/Result.hpp>
#include <tsunami/geo/TerrainConditioningRecord.hpp>

namespace tsunami::geo
{
    inline constexpr std::size_t maximum_terrain_conditioning_record_document_bytes = 8U * 1024U * 1024U;

    [[nodiscard]] auto parse_terrain_conditioning_record(
        std::string_view document,
        std::string source_name = "<memory>") -> tsunami::core::Result<TerrainConditioningRecord>;

    [[nodiscard]] auto read_terrain_conditioning_record(
        const std::filesystem::path &path) -> tsunami::core::Result<TerrainConditioningRecord>;

} // namespace tsunami::geo
