#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include <tsunami/core/Identity.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/r2d/RegionalCasePreparation.hpp>
#include <tsunami/r2d/RegionalSolveLoop.hpp>

namespace tsunami::r2d_case
{
    struct RegionalFileCaseRunPolicy
    {
        tsunami::r2d::RegionalCasePreparationPolicy preparation;
        tsunami::r2d::RegionalRasterCellTransferPolicy transfer;

        [[nodiscard]] auto operator==(const RegionalFileCaseRunPolicy &) const -> bool = default;
    };

    struct RegionalFileCaseRunRequest
    {
        std::filesystem::path case_root;
        std::filesystem::path terrain_record_path;
        std::filesystem::path mesh_path;
        std::optional<std::filesystem::path> corridor_record_path;
        tsunami::core::RunId run_id;
        RegionalFileCaseRunPolicy policy;
        bool overwrite_existing_outputs{};
        std::stop_token stop_token{};
    };

    struct RegionalFileTerrainArtifactDiagnostics
    {
        std::uint32_t artifact_contract_version{};
        std::string terrain_id;
        std::uint64_t terrain_revision{};
        std::uint64_t width{};
        std::uint64_t height{};
        std::uint64_t cell_count{};
        std::uint64_t valid_cell_count{};
        std::uint64_t invalid_cell_count{};
        double minimum_bed_elevation_m{};
        double maximum_bed_elevation_m{};
        std::string validation_status;

        [[nodiscard]] auto operator==(const RegionalFileTerrainArtifactDiagnostics &) const -> bool = default;
    };

    struct RegionalFileCaseRunDiagnostics
    {
        std::string case_id;
        std::uint64_t case_revision{};
        std::string scenario_id;
        std::string target_site;
        std::string manifest_id;
        std::uint64_t manifest_revision{};
        std::string corridor_id;
        std::uint64_t corridor_revision{};
        std::string terrain_id;
        std::uint64_t terrain_revision{};
        std::string mesh_id;
        std::string run_id;
        RegionalFileTerrainArtifactDiagnostics terrain_artifacts;
        tsunami::r2d::RegionalGeometryPreflightReport preflight;
        tsunami::r2d::RegionalTerrainTransferDiagnostics terrain_transfer;
        tsunami::r2d::RegionalCasePreparationDiagnostics preparation;
        tsunami::r2d::RegionalSolveSummary solve;
        double maximum_final_depth_residual_m{};
        double maximum_final_momentum_m2_per_s{};
        double final_water_volume_residual_m3{};
        std::uint64_t limiting_final_depth_cell_id{};
        std::uint64_t limiting_final_momentum_cell_id{};
        std::vector<std::string> completed_steps;
    };

    struct RegionalFileCaseRunOutputArtifacts
    {
        std::filesystem::path diagnostics_csv;
        std::filesystem::path snapshots_csv;
        std::optional<std::filesystem::path> earthquake_initialisation_csv;

        [[nodiscard]] auto operator==(const RegionalFileCaseRunOutputArtifacts &) const -> bool = default;
    };

    struct RegionalFileCaseRunResult
    {
        std::filesystem::path case_root;
        std::filesystem::path output_directory;
        RegionalFileCaseRunDiagnostics diagnostics;
        RegionalFileCaseRunOutputArtifacts output_artifacts;
        tsunami::core::Time final_simulation_time{};
    };

    [[nodiscard]] auto run_regional_case_from_files(const RegionalFileCaseRunRequest &request)
        -> tsunami::core::Result<RegionalFileCaseRunResult>;

} // namespace tsunami::r2d_case
