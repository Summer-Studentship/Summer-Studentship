#pragma once

#include <filesystem>

#include <tsunami/r2d/RegionalEarthquakeInitialisation.hpp>
#include <tsunami/r2d/RegionalSolveLoop.hpp>

namespace tsunami::r2d_io
{
    class RegionalCsvOutputWriter
    {
    public:
        RegionalCsvOutputWriter(std::filesystem::path output_directory, bool overwrite_existing);

        [[nodiscard]] auto output_directory() const noexcept -> const std::filesystem::path & { return output_directory_; }

        auto prepare() -> tsunami::core::Result<void>;
        auto write_diagnostics(const tsunami::r2d::RegionalStepDiagnostics &diagnostics) -> tsunami::core::Result<void>;
        auto write_snapshot(const tsunami::r2d::RegionalSnapshot &snapshot) -> tsunami::core::Result<void>;
        auto write_earthquake_initialisation(
            const tsunami::r2d::RegionalEarthquakeInitialisationDiagnostics &diagnostics) -> tsunami::core::Result<void>;

    private:
        std::filesystem::path output_directory_;
        bool overwrite_existing_{};
        bool diagnostics_header_written_{};
        bool snapshot_index_header_written_{};
    };

    auto write_regional_earthquake_initialisation_csv(
        const std::filesystem::path &output_path,
        const tsunami::r2d::RegionalEarthquakeInitialisationDiagnostics &diagnostics) -> tsunami::core::Result<void>;

} // namespace tsunami::r2d_io
