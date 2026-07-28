#include <tsunami/r2d_io/RegionalCsvOutputWriter.hpp>

#include <fstream>
#include <iomanip>

namespace tsunami::r2d_io
{
    namespace
    {
        [[nodiscard]] auto io_error(std::string code, std::string message) -> tsunami::core::Error
        {
            return tsunami::core::Error{std::move(code), std::move(message)};
        }

        [[nodiscard]] auto open_append(const std::filesystem::path &path) -> tsunami::core::Result<std::ofstream>
        {
            std::ofstream file(path, std::ios::app);
            if (!file) {
                return tsunami::core::failure<std::ofstream>(io_error(
                    "r2d.io.csv.open_failed",
                    "could not open regional CSV output for append"));
            }
            file << std::setprecision(17);
            return tsunami::core::success(std::move(file));
        }
    } // namespace

    RegionalCsvOutputWriter::RegionalCsvOutputWriter(std::filesystem::path output_directory, bool overwrite_existing)
        : output_directory_{std::move(output_directory)}
        , overwrite_existing_{overwrite_existing}
    {
    }

    auto RegionalCsvOutputWriter::prepare() -> tsunami::core::Result<void>
    {
        std::error_code ec;
        if (std::filesystem::exists(output_directory_, ec) && !overwrite_existing_ &&
            (!std::filesystem::is_empty(output_directory_, ec))) {
            return tsunami::core::failure(io_error(
                "r2d.io.csv.output_exists",
                "regional CSV output directory already exists and is not empty"));
        }
        std::filesystem::create_directories(output_directory_, ec);
        if (ec) {
            return tsunami::core::failure(io_error(
                "r2d.io.csv.create_failed",
                "could not create regional CSV output directory"));
        }
        if (overwrite_existing_) {
            std::filesystem::remove(output_directory_ / "diagnostics.csv", ec);
            std::filesystem::remove(output_directory_ / "snapshots.csv", ec);
        }
        diagnostics_header_written_ = std::filesystem::exists(output_directory_ / "diagnostics.csv");
        snapshot_index_header_written_ = std::filesystem::exists(output_directory_ / "snapshots.csv");
        return tsunami::core::success();
    }

    auto RegionalCsvOutputWriter::write_diagnostics(const tsunami::r2d::RegionalStepDiagnostics &diagnostics)
        -> tsunami::core::Result<void>
    {
        auto file = open_append(output_directory_ / "diagnostics.csv");
        if (!file) {
            return tsunami::core::failure(file.error());
        }
        if (!diagnostics_header_written_) {
            file.value() << "step,start_time,end_time,timestep,scheme,attempted_stages,accepted_stages,rejected_attempts,"
                            "maximum_signal_speed,water_volume,momentum_x,momentum_y,wet_cells,dry_cells,minimum_depth,maximum_depth\n";
            diagnostics_header_written_ = true;
        }
        file.value()
            << diagnostics.step_index << ','
            << diagnostics.start_time << ','
            << diagnostics.end_time << ','
            << diagnostics.timestep << ','
            << tsunami::r2d::to_string(diagnostics.scheme) << ','
            << diagnostics.attempted_stages << ','
            << diagnostics.accepted_stages << ','
            << diagnostics.rejected_attempts << ','
            << diagnostics.maximum_signal_speed << ','
            << diagnostics.integrals.water_volume << ','
            << diagnostics.integrals.momentum_x << ','
            << diagnostics.integrals.momentum_y << ','
            << diagnostics.integrals.wet_cell_count << ','
            << diagnostics.integrals.dry_cell_count << ','
            << diagnostics.integrals.minimum_depth << ','
            << diagnostics.integrals.maximum_depth << '\n';
        return tsunami::core::success();
    }

    auto RegionalCsvOutputWriter::write_snapshot(const tsunami::r2d::RegionalSnapshot &snapshot)
        -> tsunami::core::Result<void>
    {
        auto file = open_append(output_directory_ / "snapshots.csv");
        if (!file) {
            return tsunami::core::failure(file.error());
        }
        if (!snapshot_index_header_written_) {
            file.value() << "step,time,cell,depth,momentum_x,momentum_y,bed_elevation,free_surface_elevation\n";
            snapshot_index_header_written_ = true;
        }
        for (std::size_t index = 0; index < snapshot.depth.size(); ++index) {
            file.value()
                << snapshot.step_index << ','
                << snapshot.time << ','
                << index << ','
                << snapshot.depth[index] << ','
                << snapshot.momentum_x[index] << ','
                << snapshot.momentum_y[index] << ','
                << snapshot.bed_elevation[index] << ','
                << snapshot.free_surface_elevation[index] << '\n';
        }
        return tsunami::core::success();
    }

} // namespace tsunami::r2d_io
