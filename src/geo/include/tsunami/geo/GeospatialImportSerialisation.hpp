#pragma once

#include <filesystem>
#include <string>

#include <tsunami/geo/GeospatialImportRecord.hpp>

namespace tsunami::geo
{
    [[nodiscard]] auto serialise_geospatial_import_record(const GeospatialImportRecord &record)
        -> tsunami::core::Result<std::string>;

    [[nodiscard]] auto write_geospatial_import_record(
        const std::filesystem::path &path,
        const GeospatialImportRecord &record) -> tsunami::core::Result<void>;

} // namespace tsunami::geo
