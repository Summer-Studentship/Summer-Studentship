#pragma once

#include <string_view>

#include <tsunami/data/DatasetManifest.hpp>

namespace tsunami::data
{
    [[nodiscard]] auto parse_dataset_manifest_version(std::string_view text)
        -> tsunami::core::Result<tsunami::core::SemanticVersion>;

    [[nodiscard]] auto classify_dataset_manifest_version(const tsunami::core::SemanticVersion &version) noexcept
        -> DatasetManifestCompatibility;

    [[nodiscard]] auto validate_dataset_manifest(const DatasetManifest &manifest)
        -> tsunami::core::Result<void>;

    [[nodiscard]] auto validate_dataset_manifest_for_case(
        const DatasetManifest &manifest,
        const CaseConfiguration &configuration) -> tsunami::core::Result<void>;

} // namespace tsunami::data
