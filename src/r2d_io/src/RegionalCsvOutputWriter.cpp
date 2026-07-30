#include <tsunami/r2d_io/RegionalCsvOutputWriter.hpp>

#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

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

        [[nodiscard]] auto restriction_name(tsunami::r2d::TimestepRestrictionKind restriction) -> std::string_view
        {
            switch (restriction) {
            case tsunami::r2d::TimestepRestrictionKind::none:
                return "none";
            case tsunami::r2d::TimestepRestrictionKind::cfl:
                return "cfl";
            case tsunami::r2d::TimestepRestrictionKind::positivity:
                return "positivity";
            case tsunami::r2d::TimestepRestrictionKind::relaxation:
                return "relaxation";
            case tsunami::r2d::TimestepRestrictionKind::source:
                return "source";
            case tsunami::r2d::TimestepRestrictionKind::multiple:
                return "multiple";
            case tsunami::r2d::TimestepRestrictionKind::equal:
                return "equal";
            }
            return "unknown";
        }

        [[nodiscard]] auto source_restriction_name(const tsunami::r2d::RegionalStepDiagnostics &diagnostics) -> std::string_view
        {
            const auto active = diagnostics.sources.maximum_manning_rate > 0.0 ||
                                diagnostics.sources.maximum_coriolis_rate > 0.0;
            if (!active) {
                return "none";
            }
            if (diagnostics.stable_timestep.restriction == tsunami::r2d::TimestepRestrictionKind::source ||
                diagnostics.stable_timestep.restriction == tsunami::r2d::TimestepRestrictionKind::multiple) {
                return restriction_name(diagnostics.stable_timestep.restriction);
            }
            return "present";
        }

        [[nodiscard]] auto cell_value(std::optional<tsunami::fvm::CellId> cell_id) -> std::string
        {
            return cell_id ? std::to_string(cell_id->value) : std::string{};
        }

        [[nodiscard]] auto csv_escape(std::string_view value) -> std::string
        {
            const auto needs_quotes = value.find_first_of(",\"\n\r") != std::string_view::npos;
            if (!needs_quotes) {
                return std::string{value};
            }
            std::string escaped{"\""};
            for (const auto ch : value) {
                if (ch == '"') {
                    escaped += "\"\"";
                } else {
                    escaped += ch;
                }
            }
            escaped += '"';
            return escaped;
        }

        [[nodiscard]] auto real_string(tsunami::core::Real value) -> std::string
        {
            std::ostringstream stream;
            stream << std::setprecision(17) << value;
            return stream.str();
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
            std::filesystem::remove(output_directory_ / "earthquake_initialisation.csv", ec);
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
                            "maximum_signal_speed,water_volume,momentum_x,momentum_y,wet_cells,dry_cells,minimum_depth,maximum_depth,"
                            "relaxation_zones,relaxation_active_cells,relaxation_maximum_rate,"
                            "relaxation_mass_source_rate,relaxation_outgoing_mass_rate,"
                            "source_restriction,manning_active_cells,coriolis_active_cells,"
                            "maximum_manning_coefficient,maximum_coriolis_magnitude,"
                            "maximum_manning_rate,maximum_coriolis_rate,"
                            "manning_limiting_cell,coriolis_limiting_cell,"
                            "source_momentum_x_change,source_momentum_y_change,"
                            "source_initial_kinetic_energy,source_final_kinetic_energy,"
                            "friction_kinetic_energy_removed,coriolis_kinetic_energy_error\n";
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
            << diagnostics.integrals.maximum_depth << ','
            << diagnostics.relaxation.zone_count << ','
            << diagnostics.relaxation.active_cell_count << ','
            << diagnostics.relaxation.maximum_rate << ','
            << diagnostics.relaxation.integrated_mass_source_rate << ','
            << diagnostics.relaxation.outgoing_mass_rate << ','
            << source_restriction_name(diagnostics) << ','
            << diagnostics.sources.manning_active_cell_count << ','
            << diagnostics.sources.coriolis_active_cell_count << ','
            << diagnostics.sources.maximum_manning_coefficient << ','
            << diagnostics.sources.maximum_coriolis_magnitude << ','
            << diagnostics.sources.maximum_manning_rate << ','
            << diagnostics.sources.maximum_coriolis_rate << ','
            << cell_value(diagnostics.sources.manning_limiting_cell) << ','
            << cell_value(diagnostics.sources.coriolis_limiting_cell) << ','
            << diagnostics.sources.momentum_x_change << ','
            << diagnostics.sources.momentum_y_change << ','
            << diagnostics.sources.initial_kinetic_energy << ','
            << diagnostics.sources.final_kinetic_energy << ','
            << diagnostics.sources.friction_kinetic_energy_removed << ','
            << diagnostics.sources.coriolis_kinetic_energy_error << '\n';
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

    auto RegionalCsvOutputWriter::write_earthquake_initialisation(
        const tsunami::r2d::RegionalEarthquakeInitialisationDiagnostics &diagnostics) -> tsunami::core::Result<void>
    {
        return write_regional_earthquake_initialisation_csv(output_directory_ / "earthquake_initialisation.csv", diagnostics);
    }

    auto write_regional_earthquake_initialisation_csv(
        const std::filesystem::path &output_path,
        const tsunami::r2d::RegionalEarthquakeInitialisationDiagnostics &diagnostics) -> tsunami::core::Result<void>
    {
        std::ofstream file(output_path, std::ios::trunc);
        if (!file) {
            return tsunami::core::failure(io_error(
                "r2d.io.earthquake_csv_open_failed",
                "could not open earthquake initialisation CSV output"));
        }
        file << "source_kind,event_id,model_id,source_format,coordinate_reference,subfault_count,"
                "bed_mapping,surface_transfer,cell_count,"
                "minimum_eastward_displacement,maximum_eastward_displacement,"
                "minimum_northward_displacement,maximum_northward_displacement,"
                "minimum_upward_displacement,maximum_upward_displacement,"
                "minimum_effective_bed_displacement,maximum_effective_bed_displacement,"
                "minimum_surface_perturbation,maximum_surface_perturbation,"
                "integrated_upward_displacement,integrated_effective_bed_displacement,integrated_surface_perturbation,"
                "pre_event_water_volume,post_event_water_volume,water_volume_change,"
                "maximum_absolute_bathymetry_change,maximum_absolute_surface_perturbation,maximum_absolute_depth_change,"
                "newly_wet_cell_count,newly_dry_cell_count,pre_event_maximum_momentum,post_event_maximum_momentum\n";
        file << tsunami::r2d::to_string(diagnostics.metadata.source_kind) << ','
             << csv_escape(diagnostics.metadata.event_id) << ','
             << csv_escape(diagnostics.metadata.model_id) << ','
             << csv_escape(diagnostics.metadata.source_format) << ','
             << csv_escape(diagnostics.metadata.coordinate_reference) << ','
             << diagnostics.metadata.subfault_count << ','
             << tsunami::r2d::to_string(diagnostics.bed_mapping) << ','
             << tsunami::r2d::to_string(diagnostics.surface_transfer) << ','
             << diagnostics.cell_count << ','
             << real_string(diagnostics.minimum_eastward_displacement) << ','
             << real_string(diagnostics.maximum_eastward_displacement) << ','
             << real_string(diagnostics.minimum_northward_displacement) << ','
             << real_string(diagnostics.maximum_northward_displacement) << ','
             << real_string(diagnostics.minimum_upward_displacement) << ','
             << real_string(diagnostics.maximum_upward_displacement) << ','
             << real_string(diagnostics.minimum_effective_bed_displacement) << ','
             << real_string(diagnostics.maximum_effective_bed_displacement) << ','
             << real_string(diagnostics.minimum_surface_perturbation) << ','
             << real_string(diagnostics.maximum_surface_perturbation) << ','
             << real_string(diagnostics.integrated_upward_displacement) << ','
             << real_string(diagnostics.integrated_effective_bed_displacement) << ','
             << real_string(diagnostics.integrated_surface_perturbation) << ','
             << real_string(diagnostics.pre_event_water_volume) << ','
             << real_string(diagnostics.post_event_water_volume) << ','
             << real_string(diagnostics.water_volume_change) << ','
             << real_string(diagnostics.maximum_absolute_bathymetry_change) << ','
             << real_string(diagnostics.maximum_absolute_surface_perturbation) << ','
             << real_string(diagnostics.maximum_absolute_depth_change) << ','
             << diagnostics.newly_wet_cell_count << ','
             << diagnostics.newly_dry_cell_count << ','
             << real_string(diagnostics.pre_event_maximum_momentum) << ','
             << real_string(diagnostics.post_event_maximum_momentum) << '\n';
        if (!file) {
            return tsunami::core::failure(io_error(
                "r2d.io.earthquake_csv_write_failed",
                "could not write earthquake initialisation CSV output"));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::r2d_io
