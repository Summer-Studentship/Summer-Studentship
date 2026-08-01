#include <tsunami/geo/CorridorConstructionSerialisation.hpp>

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
            error.add_context("operation", "write_corridor_construction_record")
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
            const auto canonical = value == 0.0 ? 0.0 : value;
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": " << std::setprecision(17) << canonical;
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

        auto nullable_string_line(std::ostringstream &out, int indent, std::string_view key, const std::optional<std::string> &value, bool comma = true) -> void
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

        auto write_schema(std::ostringstream &out, int indent, const tsunami::data::SchemaIdentity &schema) -> void
        {
            open_object(out, indent, "schema");
            line(out, indent + 2, "schema_name", schema.schema_name);
            open_object(out, indent + 2, "version");
            uint_line(out, indent + 4, "major", schema.version.major);
            uint_line(out, indent + 4, "minor", schema.version.minor);
            uint_line(out, indent + 4, "patch", schema.version.patch, false);
            close_object(out, indent + 2, false);
            close_object(out, indent);
        }

        auto write_identity(std::ostringstream &out, int indent, const CorridorConstructionIdentity &identity) -> void
        {
            open_object(out, indent, "identity");
            line(out, indent + 2, "corridor_id", identity.corridor_id);
            uint_line(out, indent + 2, "corridor_revision", identity.corridor_revision);
            line(out, indent + 2, "case_id", identity.case_revision.case_id.str());
            uint_line(out, indent + 2, "case_revision", identity.case_revision.revision);
            line(out, indent + 2, "trajectory_id", identity.trajectory_id);
            line(out, indent + 2, "output_dataset_id", identity.output_dataset_id);
            line(out, indent + 2, "output_process_id", identity.output_process_id);
            line(out, indent + 2, "executed_at_utc", identity.executed_at_utc, false);
            close_object(out, indent);
        }

        auto write_transformation_identity(std::ostringstream &out, int indent, const CoordinateTransformationIdentity &identity) -> void
        {
            open_object(out, indent, "transformation_identity");
            line(out, indent + 2, "transformation_id", identity.transformation_id);
            uint_line(out, indent + 2, "transformation_revision", identity.transformation_revision);
            line(out, indent + 2, "case_id", identity.case_revision.case_id.str());
            uint_line(out, indent + 2, "case_revision", identity.case_revision.revision);
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

        auto write_string_array(std::ostringstream &out, int indent, std::string_view key, const std::vector<std::string> &values, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": [";
            for (std::size_t i = 0; i < values.size(); ++i) {
                out << (i == 0U ? "" : ", ") << '"' << escape(values[i]) << '"';
            }
            out << ']';
            if (comma) {
                out << ',';
            }
            out << '\n';
        }

        auto write_reference(std::ostringstream &out, int indent, std::string_view key, const CoordinateReferenceDescriptor &reference, bool comma = true) -> void
        {
            open_object(out, indent, key);
            nullable_string_line(out, indent + 2, "authority_name", reference.authority_name);
            nullable_string_line(out, indent + 2, "authority_code", reference.authority_code);
            line(out, indent + 2, "name", reference.name);
            nullable_string_line(out, indent + 2, "canonical_wkt2", reference.canonical_wkt2);
            nullable_string_line(out, indent + 2, "canonical_projjson", reference.canonical_projjson);
            nullable_string_line(out, indent + 2, "datum_name", reference.datum_name);
            nullable_string_line(out, indent + 2, "datum_realisation", reference.datum_realisation);
            out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"coordinate_epoch_decimal_year\": ";
            if (reference.coordinate_epoch_decimal_year) {
                const auto canonical = *reference.coordinate_epoch_decimal_year == 0.0 ? 0.0 : *reference.coordinate_epoch_decimal_year;
                out << std::setprecision(17) << canonical;
            } else {
                out << "null";
            }
            out << ",\n";
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
            nullable_string_line(out, indent + 2, "vertical_unit", target.vertical_unit);
            nullable_string_line(out, indent + 2, "vertical_positive", target.vertical_positive, false);
            close_object(out, indent, comma);
        }

        auto write_point(std::ostringstream &out, int indent, std::string_view key, Point2D point, bool comma = true) -> void
        {
            open_object(out, indent, key);
            number_line(out, indent + 2, "x", point.x);
            number_line(out, indent + 2, "y", point.y, false);
            close_object(out, indent, comma);
        }

        auto write_coordinate(std::ostringstream &out, int indent, std::string_view key, Coordinate3D point) -> void
        {
            open_object(out, indent, key);
            number_line(out, indent + 2, "x", point.x);
            number_line(out, indent + 2, "y", point.y);
            number_line(out, indent + 2, "z", point.z, false);
            close_object(out, indent);
        }

        auto write_evidence(std::ostringstream &out, int indent, std::string_view key, const CorridorReferencePointEvidence &evidence, bool comma = true) -> void
        {
            open_object(out, indent, key);
            line(out, indent + 2, "role", to_string(evidence.role));
            line(out, indent + 2, "point_id", evidence.point_id);
            line(out, indent + 2, "definition", evidence.definition);
            write_coordinate(out, indent + 2, "coordinate", evidence.coordinate);
            uint_line(out, indent + 2, "coordinate_index", static_cast<std::uint64_t>(evidence.coordinate_index));
            nullable_string_line(out, indent + 2, "source_feature_id", evidence.source_feature_id);
            write_transformation_identity(out, indent + 2, evidence.transformation_identity);
            write_reference(out, indent + 2, "source_reference", evidence.source_reference);
            write_target(out, indent + 2, "target_reference", evidence.target_reference);
            line(out, indent + 2, "source_document_title", evidence.source_document_title);
            line(out, indent + 2, "source_document_uri", evidence.source_document_uri);
            line(out, indent + 2, "accessed_at_utc", evidence.accessed_at_utc, false);
            close_object(out, indent, comma);
        }

        auto write_policy(std::ostringstream &out, int indent, const CorridorConstructionPolicy &policy) -> void
        {
            open_object(out, indent, "policy");
            number_line(out, indent + 2, "minimum_reference_separation_m", policy.minimum_reference_separation_m);
            number_line(out, indent + 2, "origin_tolerance_m", policy.origin_tolerance_m);
            number_line(out, indent + 2, "bearing_tolerance_degrees", policy.bearing_tolerance_degrees);
            number_line(out, indent + 2, "basis_orthonormal_tolerance", policy.basis_orthonormal_tolerance);
            number_line(out, indent + 2, "geometry_absolute_tolerance_m", policy.geometry_absolute_tolerance_m);
            number_line(out, indent + 2, "geometry_relative_tolerance", policy.geometry_relative_tolerance);
            line(out, indent + 2, "tolerance_basis", policy.tolerance_basis, false);
            close_object(out, indent);
        }

        auto write_basis(std::ostringstream &out, int indent, const CorridorLocalBasis &basis) -> void
        {
            open_object(out, indent, "local_basis");
            write_point(out, indent + 2, "tangent", basis.tangent);
            write_point(out, indent + 2, "left_normal", basis.left_normal);
            number_line(out, indent + 2, "epicentre_target_distance_m", basis.epicentre_target_distance_m);
            number_line(out, indent + 2, "derived_bearing_degrees_clockwise_from_north", basis.derived_bearing_degrees_clockwise_from_north, false);
            close_object(out, indent);
        }

        auto write_stations(std::ostringstream &out, int indent, const CorridorLongitudinalStations &stations) -> void
        {
            open_object(out, indent, "stations");
            number_line(out, indent + 2, "offshore_xi_m", stations.offshore_xi_m);
            number_line(out, indent + 2, "epicentre_xi_m", stations.epicentre_xi_m);
            number_line(out, indent + 2, "target_xi_m", stations.target_xi_m);
            number_line(out, indent + 2, "inland_xi_m", stations.inland_xi_m, false);
            close_object(out, indent);
        }

        auto write_sponge(std::ostringstream &out, int indent, const CorridorSpongeLimits &sponge) -> void
        {
            open_object(out, indent, "sponge_limits");
            number_line(out, indent + 2, "offshore_start_xi_m", sponge.offshore_start_xi_m);
            number_line(out, indent + 2, "offshore_end_xi_m", sponge.offshore_end_xi_m);
            number_line(out, indent + 2, "side_width_m", sponge.side_width_m);
            number_line(out, indent + 2, "minimum_unsponge_width_m", sponge.minimum_unsponge_width_m, false);
            close_object(out, indent);
        }

        auto write_polygon(std::ostringstream &out, int indent, const Polygon2D &polygon) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << "\"polygon\": {\n";
            out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"exterior_ring\": [\n";
            for (std::size_t i = 0; i < polygon.exterior_ring.size(); ++i) {
                out << std::string(static_cast<std::size_t>(indent + 4), ' ') << "{\n";
                number_line(out, indent + 6, "x", polygon.exterior_ring[i].x);
                number_line(out, indent + 6, "y", polygon.exterior_ring[i].y, false);
                out << std::string(static_cast<std::size_t>(indent + 4), ' ') << '}';
                if (i + 1U != polygon.exterior_ring.size()) {
                    out << ',';
                }
                out << '\n';
            }
            out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "],\n";
            out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"interior_rings\": []\n";
            out << std::string(static_cast<std::size_t>(indent), ' ') << "},\n";
        }

        auto write_box(std::ostringstream &out, int indent, std::string_view key, BoundingBox2D box) -> void
        {
            open_object(out, indent, key);
            number_line(out, indent + 2, "minimum_x", box.minimum_x);
            number_line(out, indent + 2, "minimum_y", box.minimum_y);
            number_line(out, indent + 2, "maximum_x", box.maximum_x);
            number_line(out, indent + 2, "maximum_y", box.maximum_y, false);
            close_object(out, indent);
        }

        auto write_diagnostics(std::ostringstream &out, int indent, const CorridorConstructionDiagnostics &diagnostics) -> void
        {
            open_object(out, indent, "diagnostics");
            number_line(out, indent + 2, "origin_residual_m", diagnostics.origin_residual_m);
            number_line(out, indent + 2, "bearing_residual_degrees", diagnostics.bearing_residual_degrees);
            number_line(out, indent + 2, "basis_tangent_norm_residual", diagnostics.basis_tangent_norm_residual);
            number_line(out, indent + 2, "basis_normal_norm_residual", diagnostics.basis_normal_norm_residual);
            number_line(out, indent + 2, "basis_orthogonality_residual", diagnostics.basis_orthogonality_residual);
            number_line(out, indent + 2, "basis_determinant_residual", diagnostics.basis_determinant_residual);
            number_line(out, indent + 2, "analytic_area_m2", diagnostics.analytic_area_m2);
            number_line(out, indent + 2, "polygon_area_m2", diagnostics.polygon_area_m2);
            number_line(out, indent + 2, "area_residual_m2", diagnostics.area_residual_m2);
            number_line(out, indent + 2, "analytic_perimeter_m", diagnostics.analytic_perimeter_m);
            number_line(out, indent + 2, "polygon_perimeter_m", diagnostics.polygon_perimeter_m);
            number_line(out, indent + 2, "perimeter_residual_m", diagnostics.perimeter_residual_m);
            write_string_array(out, indent + 2, "warnings", diagnostics.warnings, false);
            close_object(out, indent);
        }
    }

    auto serialise_corridor_construction_record(const CorridorConstructionRecord &record)
        -> tsunami::core::Result<std::string>
    {
        if (auto valid = validate_corridor_construction_record(record); !valid) {
            return tsunami::core::failure<std::string>(valid.error());
        }
        auto out = std::ostringstream{};
        out << "{\n";
        write_schema(out, 2, record.schema);
        line(out, 2, "policy_version", record.policy_version);
        line(out, 2, "formula_version", corridor_construction_formula_version);
        write_identity(out, 2, record.identity);
        line(out, 2, "scenario_id", record.scenario_id);
        line(out, 2, "target_site", record.target_site);
        write_evidence(out, 2, "epicentre", record.epicentre);
        write_evidence(out, 2, "target", record.target);
        write_target(out, 2, "target_reference", record.target_reference);
        write_policy(out, 2, record.policy);
        write_point(out, 2, "configured_origin", record.configured_origin);
        number_line(out, 2, "configured_bearing_degrees", record.configured_bearing_degrees);
        number_line(out, 2, "derived_bearing_degrees", record.derived_bearing_degrees);
        number_line(out, 2, "origin_residual_m", record.origin_residual_m);
        number_line(out, 2, "bearing_residual_degrees", record.bearing_residual_degrees);
        number_line(out, 2, "offshore_extent_m", record.offshore_extent_m);
        number_line(out, 2, "epicentre_target_distance_m", record.epicentre_target_distance_m);
        number_line(out, 2, "inland_extent_m", record.inland_extent_m);
        number_line(out, 2, "total_length_m", record.total_length_m);
        number_line(out, 2, "offshore_width_m", record.offshore_width_m);
        number_line(out, 2, "inland_width_m", record.inland_width_m);
        bool_line(out, 2, "narrowing_enabled", record.narrowing_enabled);
        line(out, 2, "narrowing_rule", record.narrowing_rule);
        write_basis(out, 2, record.local_basis);
        write_stations(out, 2, record.stations);
        write_sponge(out, 2, record.sponge_limits);
        write_polygon(out, 2, record.polygon);
        line(out, 2, "vertex_order_convention", record.vertex_order_convention);
        write_box(out, 2, "extent", record.extent);
        number_line(out, 2, "area_m2", record.area_m2);
        number_line(out, 2, "perimeter_m", record.perimeter_m);
        write_diagnostics(out, 2, record.diagnostics);
        write_string_array(out, 2, "configured_field_paths", record.configured_field_paths);
        write_string_array(out, 2, "warnings", record.warnings, false);
        out << "}\n";
        return tsunami::core::success(out.str());
    }

    auto write_corridor_construction_record(
        const std::filesystem::path &path,
        const CorridorConstructionRecord &record) -> tsunami::core::Result<void>
    {
        auto bytes = serialise_corridor_construction_record(record);
        if (!bytes) {
            return tsunami::core::failure(bytes.error());
        }
        auto temporary = path;
        temporary += ".tmp";
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        {
            auto file = std::ofstream{temporary, std::ios::binary | std::ios::trunc};
            if (!file) {
                return tsunami::core::failure(io_error("geo.corridor.record_write_failed", "could not open temporary corridor record", temporary));
            }
            file << bytes.value();
            file.flush();
            if (!file) {
                std::filesystem::remove(temporary, ec);
                return tsunami::core::failure(io_error("geo.corridor.record_write_failed", "could not write temporary corridor record", temporary));
            }
        }
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return tsunami::core::failure(io_error("geo.corridor.record_write_failed", "could not commit corridor record", path));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
