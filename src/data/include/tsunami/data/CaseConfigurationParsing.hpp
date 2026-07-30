#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <tsunami/data/CaseConfiguration.hpp>

namespace tsunami::data
{
    inline constexpr std::size_t max_case_configuration_bytes{1024U * 1024U};

    [[nodiscard]] auto parse_case_configuration(
        std::string_view document,
        std::string source_name = "<memory>") -> tsunami::core::Result<CaseConfiguration>;

    [[nodiscard]] auto read_case_configuration(const std::filesystem::path &path)
        -> tsunami::core::Result<CaseConfiguration>;

} // namespace tsunami::data
