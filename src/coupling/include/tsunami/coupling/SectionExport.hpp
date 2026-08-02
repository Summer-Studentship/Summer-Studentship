#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace tsunami::coupling
{
    inline constexpr auto regional_section_export_contract_version = 1U;

    struct RegionalCouplingSectionRequest
    {
        std::string section_id;
        std::string boundary_patch_name;

        [[nodiscard]] auto operator==(const RegionalCouplingSectionRequest &) const -> bool = default;
    };

    struct RegionalCouplingSectionSample
    {
        std::size_t local_index{};
        std::size_t cell_index{};
        std::size_t face_index{};
        double x_m{};
        double y_m{};

        [[nodiscard]] auto operator==(const RegionalCouplingSectionSample &) const -> bool = default;
    };

    struct RegionalCouplingSectionExportPaths
    {
        std::filesystem::path metadata_json;
        std::filesystem::path samples_csv;
        std::filesystem::path history_csv;

        [[nodiscard]] auto operator==(const RegionalCouplingSectionExportPaths &) const -> bool = default;
    };

    struct RegionalCouplingSectionExportMetadata
    {
        unsigned contract_version{regional_section_export_contract_version};
        std::string section_id;
        std::string boundary_patch_name;
        std::string mesh_id;
        std::size_t sample_count{};
        std::vector<RegionalCouplingSectionSample> samples;

        [[nodiscard]] auto operator==(const RegionalCouplingSectionExportMetadata &) const -> bool = default;
    };

} // namespace tsunami::coupling
