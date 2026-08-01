#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <tsunami/core/Result.hpp>
#include <tsunami/geo/CorridorConstructionRecord.hpp>

namespace tsunami::geo
{
    inline constexpr std::size_t maximum_corridor_construction_record_document_bytes = 4U * 1024U * 1024U;

    [[nodiscard]] auto parse_corridor_construction_record(
        std::string_view document,
        std::string source_name = "<memory>") -> tsunami::core::Result<CorridorConstructionRecord>;

    [[nodiscard]] auto read_corridor_construction_record(
        const std::filesystem::path &path) -> tsunami::core::Result<CorridorConstructionRecord>;

} // namespace tsunami::geo
