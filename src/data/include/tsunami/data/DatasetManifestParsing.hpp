#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <tsunami/data/DatasetManifest.hpp>

namespace tsunami::data
{
    inline constexpr std::size_t max_dataset_manifest_bytes{8U * 1024U * 1024U};

    [[nodiscard]] auto parse_dataset_manifest(
        std::string_view document,
        std::string source_name = "<memory>") -> tsunami::core::Result<DatasetManifest>;

    [[nodiscard]] auto read_dataset_manifest(const std::filesystem::path &path)
        -> tsunami::core::Result<DatasetManifest>;

} // namespace tsunami::data
