#pragma once

#include <filesystem>
#include <string>

#include <tsunami/geo/CorridorConstructionRecord.hpp>

namespace tsunami::geo
{
    [[nodiscard]] auto serialise_corridor_construction_record(
        const CorridorConstructionRecord &record) -> tsunami::core::Result<std::string>;

    [[nodiscard]] auto write_corridor_construction_record(
        const std::filesystem::path &path,
        const CorridorConstructionRecord &record) -> tsunami::core::Result<void>;

} // namespace tsunami::geo
