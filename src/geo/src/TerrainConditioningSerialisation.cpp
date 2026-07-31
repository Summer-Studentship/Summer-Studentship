#include <tsunami/geo/TerrainConditioningSerialisation.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto io_error(std::string code, std::string message, const std::filesystem::path &path)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::input_data,
                tsunami::core::Severity::error};
            error.add_context("operation", "write_terrain_conditioning_record")
                .add_context("path", path.generic_string())
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto escape(std::string_view text) -> std::string
        {
            auto out = std::ostringstream{};
            for (const auto ch : text) {
                switch (ch) {
                case '"':
                    out << "\\\"";
                    break;
                case '\\':
                    out << "\\\\";
                    break;
                case '\b':
                    out << "\\b";
                    break;
                case '\f':
                    out << "\\f";
                    break;
                case '\n':
                    out << "\\n";
                    break;
                case '\r':
                    out << "\\r";
                    break;
                case '\t':
                    out << "\\t";
                    break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20U) {
                        out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(ch));
                    } else {
                        out << ch;
                    }
                }
            }
            return out.str();
        }

        auto line(std::ostringstream &out, int indent, std::string_view key, std::string_view value, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": \"" << escape(value) << '"';
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto uint_line(std::ostringstream &out, int indent, std::string_view key, std::uint64_t value, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": " << value;
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto number_line(std::ostringstream &out, int indent, std::string_view key, double value, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": " << std::setprecision(17) << value;
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto optional_number_line(std::ostringstream &out, int indent, std::string_view key, const std::optional<double> &value, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": ";
            if (value) {
                out << std::setprecision(17) << *value;
            } else {
                out << "null";
            }
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto open_object(std::ostringstream &out, int indent, std::string_view key) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": {\n";
        }

        auto close_object(std::ostringstream &out, int indent, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '}';
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto write_schema(std::ostringstream &out, const tsunami::data::SchemaIdentity &schema) -> void
        {
            open_object(out, 2, "schema");
            line(out, 4, "schema_name", schema.schema_name);
            open_object(out, 4, "version");
            uint_line(out, 6, "major", schema.version.major);
            uint_line(out, 6, "minor", schema.version.minor);
            uint_line(out, 6, "patch", schema.version.patch, false);
            close_object(out, 4, false);
            close_object(out, 2);
        }

        auto write_identity(std::ostringstream &out, const TerrainConditioningIdentity &identity) -> void
        {
            open_object(out, 2, "identity");
            line(out, 4, "terrain_id", identity.terrain_id);
            uint_line(out, 4, "terrain_revision", identity.terrain_revision);
            line(out, 4, "case_id", identity.case_revision.case_id.str());
            uint_line(out, 4, "case_revision", identity.case_revision.revision);
            line(out, 4, "manifest_id", identity.manifest_id);
            uint_line(out, 4, "manifest_revision", identity.manifest_revision);
            line(out, 4, "output_dataset_id", identity.output_dataset_id);
            line(out, 4, "output_process_id", identity.output_process_id);
            line(out, 4, "executed_at_utc", identity.executed_at_utc, false);
            close_object(out, 2);
        }

        auto write_grid(std::ostringstream &out, const TerrainTargetGrid &grid) -> void
        {
            open_object(out, 2, "grid");
            uint_line(out, 4, "width", grid.width());
            uint_line(out, 4, "height", grid.height());
            number_line(out, 4, "spacing_m", grid.spacing_m());
            line(out, 4, "registration", to_string(grid.registration()));
            number_line(out, 4, "xi_min_m", grid.xi_min_m());
            number_line(out, 4, "xi_max_m", grid.xi_max_m());
            number_line(out, 4, "eta_bottom_m", grid.eta_bottom_m());
            number_line(out, 4, "eta_top_m", grid.eta_top_m());
            number_line(out, 4, "longitudinal_padding_m", grid.longitudinal_padding_m());
            number_line(out, 4, "transverse_padding_m", grid.transverse_padding_m());
            open_object(out, 4, "affine");
            number_line(out, 6, "origin_x", grid.transform().origin_x);
            number_line(out, 6, "pixel_width", grid.transform().pixel_width);
            number_line(out, 6, "row_rotation", grid.transform().row_rotation);
            number_line(out, 6, "origin_y", grid.transform().origin_y);
            number_line(out, 6, "column_rotation", grid.transform().column_rotation);
            number_line(out, 6, "pixel_height", grid.transform().pixel_height, false);
            close_object(out, 4, false);
            close_object(out, 2);
        }

        auto write_grid_policy(std::ostringstream &out, const TerrainTargetGridPolicy &policy) -> void
        {
            open_object(out, 2, "grid_policy");
            number_line(out, 4, "target_spacing_m", policy.target_spacing_m);
            number_line(out, 4, "active_coverage_threshold", policy.active_coverage_threshold);
            number_line(out, 4, "maximum_upsampling_factor", policy.maximum_upsampling_factor);
            uint_line(out, 4, "maximum_output_cells", policy.maximum_output_cells);
            number_line(out, 4, "numerical_absolute_tolerance", policy.numerical_absolute_tolerance);
            number_line(out, 4, "numerical_relative_tolerance", policy.numerical_relative_tolerance);
            line(out, 4, "policy_basis", policy.policy_basis, false);
            close_object(out, 2);
        }

        auto write_resampling(std::ostringstream &out, std::string_view key, const RasterResamplingRecord &record) -> void
        {
            open_object(out, 2, key);
            line(out, 4, "dataset_id", record.dataset_id);
            line(out, 4, "asset_id", record.asset_id);
            line(out, 4, "import_id", record.import_identity.import_id);
            line(out, 4, "transformation_id", record.transformation_identity.transformation_id);
            line(out, 4, "role", to_string(record.role));
            line(out, 4, "kernel", to_string(record.kernel));
            line(out, 4, "source_registration", to_string(record.source_registration));
            line(out, 4, "target_registration", to_string(record.target_registration));
            optional_number_line(out, 4, "source_scale", record.source_scale);
            optional_number_line(out, 4, "source_offset", record.source_offset);
            number_line(out, 4, "minimum_source_spacing_m", record.minimum_source_spacing_m);
            number_line(out, 4, "maximum_source_spacing_m", record.maximum_source_spacing_m);
            number_line(out, 4, "nominal_source_spacing_m", record.nominal_source_spacing_m);
            number_line(out, 4, "target_spacing_m", record.target_spacing_m);
            number_line(out, 4, "maximum_upsampling_factor", record.maximum_upsampling_factor);
            uint_line(out, 4, "source_valid_cell_count", record.source_valid_cell_count);
            uint_line(out, 4, "output_valid_cell_count", record.output_valid_cell_count);
            uint_line(out, 4, "source_nodata_cell_count", record.source_nodata_cell_count);
            uint_line(out, 4, "outside_coverage_cell_count", record.outside_coverage_cell_count);
            line(out, 4, "operation_name", record.operation.operation_name);
            line(out, 4, "adapter_name", record.adapter_name);
            line(out, 4, "adapter_version", record.adapter_version, false);
            close_object(out, 2);
        }

        auto write_merge_policy(std::ostringstream &out, const TerrainMergePolicy &policy) -> void
        {
            open_object(out, 2, "merge_policy");
            line(out, 4, "first_priority_dataset_id", policy.first_priority_dataset_id);
            line(out, 4, "second_priority_dataset_id", policy.second_priority_dataset_id);
            number_line(out, 4, "maximum_overlap_disagreement_m", policy.maximum_overlap_disagreement_m);
            line(out, 4, "conflict_policy", to_string(policy.conflict_policy));
            line(out, 4, "priority_basis", policy.priority_basis, false);
            close_object(out, 2);
        }

        auto write_gap_policy(std::ostringstream &out, const TerrainGapResolutionPolicy &policy) -> void
        {
            open_object(out, 2, "gap_policy");
            line(out, 4, "kind", to_string(policy.kind));
            number_line(out, 4, "maximum_fill_distance_m", policy.maximum_fill_distance_m);
            number_line(out, 4, "maximum_component_diameter_m", policy.maximum_component_diameter_m);
            uint_line(out, 4, "maximum_component_cells", policy.maximum_component_cells);
            uint_line(out, 4, "minimum_donor_count", policy.minimum_donor_count);
            number_line(out, 4, "distance_exponent", policy.distance_exponent);
            number_line(out, 4, "maximum_filled_fraction", policy.maximum_filled_fraction);
            line(out, 4, "policy_basis", policy.policy_basis, false);
            close_object(out, 2);
        }

        auto write_diagnostics(std::ostringstream &out, const TerrainConditioningDiagnostics &diagnostics) -> void
        {
            open_object(out, 2, "diagnostics");
            uint_line(out, 4, "total_cell_count", diagnostics.total_cell_count);
            uint_line(out, 4, "active_cell_count", diagnostics.active_cell_count);
            uint_line(out, 4, "outside_corridor_cell_count", diagnostics.outside_corridor_cell_count);
            uint_line(out, 4, "excluded_boundary_cell_count", diagnostics.excluded_boundary_cell_count);
            uint_line(out, 4, "bathymetry_selected_cell_count", diagnostics.bathymetry_selected_cell_count);
            uint_line(out, 4, "topography_selected_cell_count", diagnostics.topography_selected_cell_count);
            uint_line(out, 4, "overlap_cell_count", diagnostics.overlap_cell_count);
            uint_line(out, 4, "overlap_conflict_cell_count", diagnostics.overlap_conflict_cell_count);
            uint_line(out, 4, "initially_unresolved_cell_count", diagnostics.initially_unresolved_cell_count);
            uint_line(out, 4, "filled_cell_count", diagnostics.filled_cell_count);
            uint_line(out, 4, "unresolved_cell_count", diagnostics.unresolved_cell_count);
            open_object(out, 4, "overlap");
            uint_line(out, 6, "overlap_cell_count", diagnostics.overlap.overlap_cell_count);
            uint_line(out, 6, "disagreement_exceedance_count", diagnostics.overlap.disagreement_exceedance_count);
            number_line(out, 6, "mean_signed_difference_m", diagnostics.overlap.mean_signed_difference_m);
            number_line(out, 6, "root_mean_square_difference_m", diagnostics.overlap.root_mean_square_difference_m);
            number_line(out, 6, "maximum_absolute_difference_m", diagnostics.overlap.maximum_absolute_difference_m, false);
            close_object(out, 4);
            number_line(out, 4, "minimum_elevation_m", diagnostics.minimum_elevation_m);
            number_line(out, 4, "maximum_elevation_m", diagnostics.maximum_elevation_m, false);
            close_object(out, 2);
        }
    }

    auto serialise_terrain_conditioning_record(
        const TerrainConditioningRecord &record) -> tsunami::core::Result<std::string>
    {
        if (auto valid = validate_terrain_conditioning_record(record); !valid) {
            return tsunami::core::failure<std::string>(valid.error());
        }
        auto out = std::ostringstream{};
        out << "{\n";
        write_schema(out, record.schema);
        line(out, 2, "policy_version", record.policy_version);
        line(out, 2, "formula_version", record.formula_version);
        write_identity(out, record.identity);
        line(out, 2, "scenario_id", record.scenario_id);
        line(out, 2, "target_site", record.target_site);
        line(out, 2, "bathymetry_dataset_id", record.bathymetry_dataset_id);
        line(out, 2, "bathymetry_asset_id", record.bathymetry_asset_id);
        line(out, 2, "topography_dataset_id", record.topography_dataset_id);
        line(out, 2, "topography_asset_id", record.topography_asset_id);
        line(out, 2, "corridor_id", record.corridor_identity.corridor_id);
        write_grid(out, record.grid);
        write_grid_policy(out, record.grid_policy);
        write_resampling(out, "bathymetry_resampling", record.bathymetry_resampling);
        write_resampling(out, "topography_resampling", record.topography_resampling);
        write_merge_policy(out, record.merge_policy);
        write_gap_policy(out, record.gap_policy);
        write_diagnostics(out, record.diagnostics);
        line(out, 2, "output_uncertainty_status", tsunami::data::to_string(record.output_uncertainty.status));
        line(out, 2, "output_media_type", record.output_media_type);
        line(out, 2, "output_path", record.output_path.generic_string());
        line(out, 2, "digest_status", record.digest_status);
        auto warnings = record.warnings;
        std::sort(warnings.begin(), warnings.end());
        out << "  \"warnings\": [";
        for (std::size_t i = 0U; i < warnings.size(); ++i) {
            out << (i == 0U ? "" : ", ") << '"' << escape(warnings[i]) << '"';
        }
        out << "]\n";
        out << "}\n";
        return tsunami::core::success(out.str());
    }

    auto write_terrain_conditioning_record(
        const std::filesystem::path &path,
        const TerrainConditioningRecord &record) -> tsunami::core::Result<void>
    {
        auto serialised = serialise_terrain_conditioning_record(record);
        if (!serialised) {
            return tsunami::core::failure(serialised.error());
        }
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return tsunami::core::failure(io_error("geo.terrain.record_write_failed", "failed to create terrain record parent directory", path));
            }
        }
        const auto temporary = path.string() + ".tmp";
        {
            auto file = std::ofstream{temporary, std::ios::binary | std::ios::trunc};
            if (!file) {
                return tsunami::core::failure(io_error("geo.terrain.record_write_failed", "failed to open temporary terrain record", path));
            }
            file << serialised.value();
            if (!file) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return tsunami::core::failure(io_error("geo.terrain.record_write_failed", "failed to write temporary terrain record", path));
            }
        }
        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return tsunami::core::failure(io_error("geo.terrain.record_write_failed", "failed to replace terrain record", path));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
