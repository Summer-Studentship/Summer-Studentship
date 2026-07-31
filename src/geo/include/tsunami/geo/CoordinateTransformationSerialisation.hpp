#pragma once

#include <filesystem>
#include <string>

#include <tsunami/core/Result.hpp>
#include <tsunami/geo/CoordinateTransformationRecord.hpp>

namespace tsunami::geo
{
    [[nodiscard]] auto serialise_coordinate_transformation_record(
        const CoordinateTransformationRecord &record) -> tsunami::core::Result<std::string>;

    [[nodiscard]] auto write_coordinate_transformation_record(
        const std::filesystem::path &path,
        const CoordinateTransformationRecord &record) -> tsunami::core::Result<void>;

} // namespace tsunami::geo
