#pragma once

#include <filesystem>
#include <string>

#include <tsunami/data/DatasetManifest.hpp>

namespace tsunami::data
{
    [[nodiscard]] auto serialise_dataset_manifest(const DatasetManifest &manifest)
        -> tsunami::core::Result<std::string>;

    [[nodiscard]] auto write_dataset_manifest(
        const std::filesystem::path &path,
        const DatasetManifest &manifest) -> tsunami::core::Result<void>;

} // namespace tsunami::data
