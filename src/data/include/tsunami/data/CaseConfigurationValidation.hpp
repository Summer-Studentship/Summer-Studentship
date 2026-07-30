#pragma once

#include <string_view>

#include <tsunami/data/CaseConfiguration.hpp>

namespace tsunami::data
{
    [[nodiscard]] auto parse_semantic_version(std::string_view text)
        -> tsunami::core::Result<tsunami::core::SemanticVersion>;

    [[nodiscard]] auto classify_case_schema_version(const tsunami::core::SemanticVersion &version) noexcept
        -> CaseSchemaCompatibility;

    [[nodiscard]] auto validate_case_configuration(const CaseConfiguration &configuration)
        -> tsunami::core::Result<void>;

} // namespace tsunami::data
