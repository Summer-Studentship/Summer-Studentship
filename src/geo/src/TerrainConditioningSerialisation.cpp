#include <tsunami/geo/TerrainConditioningSerialisation.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

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

        auto bool_line(std::ostringstream &out, int indent, std::string_view key, bool value, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": " << (value ? "true" : "false");
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto number_line(std::ostringstream &out, int indent, std::string_view key, double value, bool comma = true) -> void
        {
            const auto canonical = value == 0.0 ? 0.0 : value;
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": " << std::setprecision(17) << canonical;
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto optional_number_line(std::ostringstream &out, int indent, std::string_view key, const std::optional<double> &value, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": ";
            if (value) {
                const auto canonical = *value == 0.0 ? 0.0 : *value;
                out << std::setprecision(17) << canonical;
            } else {
                out << "null";
            }
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto optional_string_line(std::ostringstream &out, int indent, std::string_view key, const std::optional<std::string> &value, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": ";
            if (value) {
                out << '"' << escape(*value) << '"';
            } else {
                out << "null";
            }
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto optional_path_line(std::ostringstream &out, int indent, std::string_view key, const std::optional<std::filesystem::path> &value, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": ";
            if (value) {
                out << '"' << escape(value->generic_string()) << '"';
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

        auto write_string_array(std::ostringstream &out, int indent, std::string_view key, const std::vector<std::string> &values, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": [";
            for (std::size_t i = 0U; i < values.size(); ++i) {
                out << (i == 0U ? "" : ", ") << '"' << escape(values[i]) << '"';
            }
            out << ']';
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

        auto write_case_revision_fields(std::ostringstream &out, int indent, const tsunami::data::CaseRevisionRef &case_revision) -> void
        {
            line(out, indent, "case_id", case_revision.case_id.str());
            uint_line(out, indent, "case_revision", case_revision.revision);
        }

        auto write_import_identity(std::ostringstream &out, int indent, std::string_view key, const GeospatialImportIdentity &identity) -> void
        {
            open_object(out, indent, key);
            line(out, indent + 2, "import_id", identity.import_id);
            uint_line(out, indent + 2, "import_revision", identity.import_revision);
            write_case_revision_fields(out, indent + 2, identity.case_revision);
            line(out, indent + 2, "manifest_id", identity.manifest_id);
            uint_line(out, indent + 2, "manifest_revision", identity.manifest_revision);
            line(out, indent + 2, "dataset_id", identity.dataset_id);
            line(out, indent + 2, "asset_id", identity.asset_id);
            line(out, indent + 2, "executed_at_utc", identity.executed_at_utc, false);
            close_object(out, indent);
        }

        auto write_transformation_identity(std::ostringstream &out, int indent, std::string_view key, const CoordinateTransformationIdentity &identity) -> void
        {
            open_object(out, indent, key);
            line(out, indent + 2, "transformation_id", identity.transformation_id);
            uint_line(out, indent + 2, "transformation_revision", identity.transformation_revision);
            write_case_revision_fields(out, indent + 2, identity.case_revision);
            line(out, indent + 2, "manifest_id", identity.manifest_id);
            uint_line(out, indent + 2, "manifest_revision", identity.manifest_revision);
            line(out, indent + 2, "source_import_id", identity.source_import_id);
            uint_line(out, indent + 2, "source_import_revision", identity.source_import_revision);
            line(out, indent + 2, "source_dataset_id", identity.source_dataset_id);
            line(out, indent + 2, "source_asset_id", identity.source_asset_id);
            line(out, indent + 2, "output_dataset_id", identity.output_dataset_id);
            line(out, indent + 2, "output_process_id", identity.output_process_id);
            line(out, indent + 2, "executed_at_utc", identity.executed_at_utc, false);
            close_object(out, indent);
        }

        auto write_corridor_identity(std::ostringstream &out, int indent, const CorridorConstructionIdentity &identity) -> void
        {
            open_object(out, indent, "corridor_identity");
            line(out, indent + 2, "corridor_id", identity.corridor_id);
            uint_line(out, indent + 2, "corridor_revision", identity.corridor_revision);
            write_case_revision_fields(out, indent + 2, identity.case_revision);
            line(out, indent + 2, "trajectory_id", identity.trajectory_id);
            line(out, indent + 2, "output_dataset_id", identity.output_dataset_id);
            line(out, indent + 2, "output_process_id", identity.output_process_id);
            line(out, indent + 2, "executed_at_utc", identity.executed_at_utc, false);
            close_object(out, indent);
        }

        auto write_reference(std::ostringstream &out, int indent, std::string_view key, const CoordinateReferenceDescriptor &reference, bool comma = true) -> void
        {
            open_object(out, indent, key);
            optional_string_line(out, indent + 2, "authority_name", reference.authority_name);
            optional_string_line(out, indent + 2, "authority_code", reference.authority_code);
            line(out, indent + 2, "name", reference.name);
            optional_string_line(out, indent + 2, "canonical_wkt2", reference.canonical_wkt2);
            optional_string_line(out, indent + 2, "canonical_projjson", reference.canonical_projjson);
            optional_string_line(out, indent + 2, "datum_name", reference.datum_name);
            optional_string_line(out, indent + 2, "datum_realisation", reference.datum_realisation);
            optional_number_line(out, indent + 2, "coordinate_epoch_decimal_year", reference.coordinate_epoch_decimal_year);
            write_string_array(out, indent + 2, "axis_names", reference.axis_names);
            write_string_array(out, indent + 2, "axis_directions", reference.axis_directions);
            write_string_array(out, indent + 2, "axis_units", reference.axis_units, false);
            close_object(out, indent, comma);
        }

        auto write_target(std::ostringstream &out, int indent, std::string_view key, const ComputationalTargetReference &target, bool comma = true) -> void
        {
            open_object(out, indent, key);
            write_reference(out, indent + 2, "horizontal", target.horizontal);
            if (target.vertical) {
                write_reference(out, indent + 2, "vertical", *target.vertical);
            } else {
                out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"vertical\": null,\n";
            }
            line(out, indent + 2, "storage_axes", to_string(target.storage_axes));
            line(out, indent + 2, "horizontal_unit", target.horizontal_unit);
            optional_string_line(out, indent + 2, "vertical_unit", target.vertical_unit);
            optional_string_line(out, indent + 2, "vertical_positive", target.vertical_positive, false);
            close_object(out, indent, comma);
        }

        auto write_box(std::ostringstream &out, int indent, std::string_view key, const BoundingBox2D &box, bool comma = true) -> void
        {
            open_object(out, indent, key);
            number_line(out, indent + 2, "minimum_x", box.minimum_x);
            number_line(out, indent + 2, "minimum_y", box.minimum_y);
            number_line(out, indent + 2, "maximum_x", box.maximum_x);
            number_line(out, indent + 2, "maximum_y", box.maximum_y, false);
            close_object(out, indent, comma);
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
            close_object(out, 4);
            write_box(out, 4, "extent", grid.extent(), false);
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

        auto write_area(std::ostringstream &out, int indent, std::string_view key, const GeographicAreaOfInterest &area, bool comma = true) -> void
        {
            open_object(out, indent, key);
            number_line(out, indent + 2, "west_longitude_degrees", area.west_longitude_degrees);
            number_line(out, indent + 2, "south_latitude_degrees", area.south_latitude_degrees);
            number_line(out, indent + 2, "east_longitude_degrees", area.east_longitude_degrees);
            number_line(out, indent + 2, "north_latitude_degrees", area.north_latitude_degrees, false);
            close_object(out, indent, comma);
        }

        auto write_digest(std::ostringstream &out, int indent, const std::optional<tsunami::data::ContentDigest> &digest, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << "\"declared_digest\": ";
            if (!digest) {
                out << "null";
                if (comma) {
                    out << ',';
                }
                out << '\n';
                return;
            }
            out << "{\n";
            line(out, indent + 2, "algorithm", tsunami::data::to_string(digest->algorithm));
            line(out, indent + 2, "value", digest->value);
            line(out, indent + 2, "origin", tsunami::data::to_string(digest->origin), false);
            close_object(out, indent, comma);
        }

        auto write_grids(std::ostringstream &out, int indent, std::string_view key, const std::vector<CoordinateOperationGrid> &grids, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": [";
            if (grids.empty()) {
                out << ']';
                if (comma) {
                    out << ',';
                }
                out << '\n';
                return;
            }
            out << '\n';
            for (std::size_t i = 0U; i < grids.size(); ++i) {
                out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "{\n";
                line(out, indent + 4, "short_name", grids[i].short_name);
                optional_path_line(out, indent + 4, "full_path", grids[i].full_path);
                optional_string_line(out, indent + 4, "package_name", grids[i].package_name);
                optional_string_line(out, indent + 4, "source_uri", grids[i].source_uri);
                bool_line(out, indent + 4, "available", grids[i].available);
                bool_line(out, indent + 4, "open_licence", grids[i].open_licence);
                write_digest(out, indent + 4, grids[i].declared_digest);
                line(out, indent + 4, "verification_status", to_string(grids[i].verification_status), false);
                out << std::string(static_cast<std::size_t>(indent + 2), ' ') << '}';
                if (i + 1U != grids.size()) {
                    out << ',';
                }
                out << '\n';
            }
            out << std::string(static_cast<std::size_t>(indent), ' ') << ']';
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto write_operation(std::ostringstream &out, int indent, const CoordinateOperationRecord &operation) -> void
        {
            open_object(out, indent, "operation");
            line(out, indent + 2, "operation_name", operation.operation_name);
            optional_string_line(out, indent + 2, "operation_authority", operation.operation_authority);
            optional_string_line(out, indent + 2, "operation_code", operation.operation_code);
            optional_string_line(out, indent + 2, "operation_method", operation.operation_method);
            optional_number_line(out, indent + 2, "operation_accuracy_m", operation.operation_accuracy_m);
            optional_string_line(out, indent + 2, "scope", operation.scope);
            if (operation.area_of_use) {
                write_area(out, indent + 2, "area_of_use", *operation.area_of_use);
            } else {
                out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"area_of_use\": null,\n";
            }
            optional_string_line(out, indent + 2, "canonical_wkt2", operation.canonical_wkt2);
            optional_string_line(out, indent + 2, "canonical_projjson", operation.canonical_projjson);
            optional_string_line(out, indent + 2, "canonical_pipeline", operation.canonical_pipeline);
            bool_line(out, indent + 2, "ballpark", operation.ballpark);
            write_reference(out, indent + 2, "source_crs", operation.source_crs);
            write_reference(out, indent + 2, "target_crs", operation.target_crs);
            write_grids(out, indent + 2, "grids", operation.grids);
            line(out, indent + 2, "engine_name", operation.engine_name);
            line(out, indent + 2, "engine_version", operation.engine_version);
            optional_string_line(out, indent + 2, "database_version", operation.database_version, false);
            close_object(out, indent);
        }

        auto write_vertical(std::ostringstream &out, int indent, const VerticalTransformationSpecification &vertical) -> void
        {
            open_object(out, indent, "vertical_operation");
            bool_line(out, indent + 2, "enabled", vertical.enabled);
            out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"steps\": [";
            if (vertical.steps.empty()) {
                out << "]\n";
                close_object(out, indent);
                return;
            }
            out << '\n';
            for (std::size_t i = 0U; i < vertical.steps.size(); ++i) {
                const auto &step = vertical.steps[i];
                out << std::string(static_cast<std::size_t>(indent + 4), ' ') << "{\n";
                line(out, indent + 6, "kind", to_string(step.kind));
                optional_number_line(out, indent + 6, "scale_factor", step.scale_factor);
                optional_number_line(out, indent + 6, "offset_m", step.offset_m);
                optional_string_line(out, indent + 6, "operation_authority", step.operation_authority);
                optional_string_line(out, indent + 6, "operation_code", step.operation_code);
                optional_string_line(out, indent + 6, "required_resource_name", step.required_resource_name);
                line(out, indent + 6, "source_reference", step.source_reference);
                line(out, indent + 6, "target_reference", step.target_reference, false);
                out << std::string(static_cast<std::size_t>(indent + 4), ' ') << '}';
                if (i + 1U != vertical.steps.size()) {
                    out << ',';
                }
                out << '\n';
            }
            out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "]\n";
            close_object(out, indent);
        }

        auto write_resampling(std::ostringstream &out, std::string_view key, const RasterResamplingRecord &record) -> void
        {
            open_object(out, 2, key);
            line(out, 4, "dataset_id", record.dataset_id);
            line(out, 4, "asset_id", record.asset_id);
            line(out, 4, "import_id", record.import_identity.import_id);
            write_import_identity(out, 4, "import_identity", record.import_identity);
            line(out, 4, "transformation_id", record.transformation_identity.transformation_id);
            write_transformation_identity(out, 4, "transformation_identity", record.transformation_identity);
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
            write_operation(out, 4, record.operation);
            write_vertical(out, 4, record.vertical_steps);
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
            number_line(out, 4, "maximum_elevation_m", diagnostics.maximum_elevation_m);
            write_string_array(out, 4, "warnings", diagnostics.warnings, false);
            close_object(out, 2);
        }

        auto write_uncertainty(std::ostringstream &out, const tsunami::data::DatasetUncertainty &uncertainty) -> void
        {
            open_object(out, 2, "output_uncertainty");
            line(out, 4, "status", tsunami::data::to_string(uncertainty.status));
            out << "    \"measures\": [";
            if (uncertainty.measures.empty()) {
                out << "],\n";
            } else {
                out << '\n';
                for (std::size_t i = 0U; i < uncertainty.measures.size(); ++i) {
                    const auto &measure = uncertainty.measures[i];
                    out << "      {\n";
                    line(out, 8, "quantity", measure.quantity);
                    number_line(out, 8, "value", measure.value);
                    line(out, 8, "unit", measure.unit);
                    optional_number_line(out, 8, "confidence_level", measure.confidence_level);
                    optional_string_line(out, 8, "method", measure.method, false);
                    out << "      }";
                    if (i + 1U != uncertainty.measures.size()) {
                        out << ',';
                    }
                    out << '\n';
                }
                out << "    ],\n";
            }
            optional_string_line(out, 4, "description", uncertainty.description, false);
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
        write_import_identity(out, 2, "bathymetry_import_identity", record.bathymetry_import_identity);
        write_transformation_identity(out, 2, "bathymetry_transformation_identity", record.bathymetry_transformation_identity);
        line(out, 2, "topography_dataset_id", record.topography_dataset_id);
        line(out, 2, "topography_asset_id", record.topography_asset_id);
        write_import_identity(out, 2, "topography_import_identity", record.topography_import_identity);
        write_transformation_identity(out, 2, "topography_transformation_identity", record.topography_transformation_identity);
        line(out, 2, "corridor_id", record.corridor_identity.corridor_id);
        write_corridor_identity(out, 2, record.corridor_identity);
        write_target(out, 2, "target_reference", record.target_reference);
        write_grid(out, record.grid);
        write_grid_policy(out, record.grid_policy);
        write_resampling(out, "bathymetry_resampling", record.bathymetry_resampling);
        write_resampling(out, "topography_resampling", record.topography_resampling);
        write_merge_policy(out, record.merge_policy);
        write_gap_policy(out, record.gap_policy);
        write_diagnostics(out, record.diagnostics);
        line(out, 2, "output_uncertainty_status", tsunami::data::to_string(record.output_uncertainty.status));
        write_uncertainty(out, record.output_uncertainty);
        line(out, 2, "output_media_type", record.output_media_type);
        line(out, 2, "output_path", record.output_path.generic_string());
        line(out, 2, "digest_status", record.digest_status);
        out << "  \"warnings\": [";
        for (std::size_t i = 0U; i < record.warnings.size(); ++i) {
            out << (i == 0U ? "" : ", ") << '"' << escape(record.warnings[i]) << '"';
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
