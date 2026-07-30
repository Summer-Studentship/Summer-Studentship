#pragma once

#include <filesystem>
#include <string>

#include <tsunami/data/CaseConfiguration.hpp>

namespace tsunami::data
{
    [[nodiscard]] auto serialise_case_configuration(const CaseConfiguration &configuration)
        -> tsunami::core::Result<std::string>;

    [[nodiscard]] auto write_case_configuration(
        const std::filesystem::path &path,
        const CaseConfiguration &configuration) -> tsunami::core::Result<void>;

} // namespace tsunami::data
