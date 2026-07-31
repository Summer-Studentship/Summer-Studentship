#include <tsunami/geo/CoordinateTransformationSerialisation.hpp>

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
            error.add_context("operation", "write_coordinate_transformation_record")
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

        auto nullable_number_line(std::ostringstream &out, int indent, std::string_view key, const std::optional<double> &value, bool comma = true) -> void
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

        auto write_area(std::ostringstream &out, int indent, std::string_view key, const GeographicAreaOfInterest &area, bool comma = true) -> void
        {
            open_object(out, indent, key);
            number_line(out, indent + 2, "west_longitude_degrees", area.west_longitude_degrees);
            number_line(out, indent + 2, "south_latitude_degrees", area.south_latitude_degrees);
            number_line(out, indent + 2, "east_longitude_degrees", area.east_longitude_degrees);
            number_line(out, indent + 2, "north_latitude_degrees", area.north_latitude_degrees, false);
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
            nullable_number_line(out, indent + 2, "coordinate_epoch_decimal_year", reference.coordinate_epoch_decimal_year);
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

        auto write_grids(std::ostringstream &out, int indent, std::string_view key, std::vector<CoordinateOperationGrid> grids, bool comma = true) -> void
        {
            std::sort(grids.begin(), grids.end(), [](const auto &left, const auto &right) {
                return left.short_name < right.short_name;
            });
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
            for (std::size_t i = 0; i < grids.size(); ++i) {
                out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "{\n";
                line(out, indent + 4, "short_name", grids[i].short_name);
                nullable_string_line(out, indent + 4, "full_path", grids[i].full_path);
                nullable_string_line(out, indent + 4, "package_name", grids[i].package_name);
                nullable_string_line(out, indent + 4, "source_uri", grids[i].source_uri);
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

        auto write_operation(std::ostringstream &out, int indent, std::string_view key, const CoordinateOperationRecord &operation, bool comma = true) -> void
        {
            open_object(out, indent, key);
            line(out, indent + 2, "operation_name", operation.operation_name);
            nullable_string_line(out, indent + 2, "operation_authority", operation.operation_authority);
            nullable_string_line(out, indent + 2, "operation_code", operation.operation_code);
            nullable_string_line(out, indent + 2, "operation_method", operation.operation_method);
            nullable_number_line(out, indent + 2, "operation_accuracy_m", operation.operation_accuracy_m);
            nullable_string_line(out, indent + 2, "scope", operation.scope);
            if (operation.area_of_use) {
                write_area(out, indent + 2, "area_of_use", *operation.area_of_use);
            } else {
                out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"area_of_use\": null,\n";
            }
            nullable_string_line(out, indent + 2, "canonical_wkt2", operation.canonical_wkt2);
            nullable_string_line(out, indent + 2, "canonical_projjson", operation.canonical_projjson);
            nullable_string_line(out, indent + 2, "canonical_pipeline", operation.canonical_pipeline);
            bool_line(out, indent + 2, "ballpark", operation.ballpark);
            write_reference(out, indent + 2, "source_crs", operation.source_crs);
            write_reference(out, indent + 2, "target_crs", operation.target_crs);
            write_grids(out, indent + 2, "grids", operation.grids);
            line(out, indent + 2, "engine_name", operation.engine_name);
            line(out, indent + 2, "engine_version", operation.engine_version);
            nullable_string_line(out, indent + 2, "database_version", operation.database_version, false);
            close_object(out, indent, comma);
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
            for (std::size_t i = 0; i < vertical.steps.size(); ++i) {
                const auto &step = vertical.steps[i];
                out << std::string(static_cast<std::size_t>(indent + 4), ' ') << "{\n";
                line(out, indent + 6, "kind", to_string(step.kind));
                nullable_number_line(out, indent + 6, "scale_factor", step.scale_factor);
                nullable_number_line(out, indent + 6, "offset_m", step.offset_m);
                nullable_string_line(out, indent + 6, "operation_authority", step.operation_authority);
                nullable_string_line(out, indent + 6, "operation_code", step.operation_code);
                nullable_string_line(out, indent + 6, "required_resource_name", step.required_resource_name);
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

        auto write_diagnostics(std::ostringstream &out, int indent, const CoordinateTransformationDiagnostics &diagnostics) -> void
        {
            open_object(out, indent, "diagnostics");
            uint_line(out, indent + 2, "coordinate_count", diagnostics.coordinate_count);
            uint_line(out, indent + 2, "transformed_coordinate_count", diagnostics.transformed_coordinate_count);
            uint_line(out, indent + 2, "failed_coordinate_count", diagnostics.failed_coordinate_count);
            number_line(out, indent + 2, "maximum_forward_control_residual_m", diagnostics.maximum_forward_control_residual_m);
            number_line(out, indent + 2, "maximum_inverse_round_trip_residual_m", diagnostics.maximum_inverse_round_trip_residual_m);
            nullable_number_line(out, indent + 2, "maximum_vertical_control_residual_m", diagnostics.maximum_vertical_control_residual_m);
            write_box(out, indent + 2, "source_extent", diagnostics.source_extent);
            write_box(out, indent + 2, "target_extent", diagnostics.target_extent);
            auto warnings = diagnostics.warnings;
            std::sort(warnings.begin(), warnings.end());
            write_string_array(out, indent + 2, "warnings", warnings, false);
            close_object(out, indent);
        }
    }

    auto serialise_coordinate_transformation_record(const CoordinateTransformationRecord &record)
        -> tsunami::core::Result<std::string>
    {
        if (auto valid = validate_coordinate_transformation_record(record); !valid) {
            return tsunami::core::failure<std::string>(valid.error());
        }
        auto out = std::ostringstream{};
        out << "{\n";
        open_object(out, 2, "schema");
        line(out, 4, "schema_name", record.schema.schema_name);
        open_object(out, 4, "version");
        uint_line(out, 6, "major", record.schema.version.major);
        uint_line(out, 6, "minor", record.schema.version.minor);
        uint_line(out, 6, "patch", record.schema.version.patch, false);
        close_object(out, 4, false);
        close_object(out, 2);
        line(out, 2, "policy_version", record.policy_version);
        open_object(out, 2, "identity");
        line(out, 4, "transformation_id", record.identity.transformation_id);
        uint_line(out, 4, "transformation_revision", record.identity.transformation_revision);
        line(out, 4, "case_id", record.identity.case_revision.case_id.str());
        uint_line(out, 4, "case_revision", record.identity.case_revision.revision);
        line(out, 4, "manifest_id", record.identity.manifest_id);
        uint_line(out, 4, "manifest_revision", record.identity.manifest_revision);
        line(out, 4, "source_import_id", record.identity.source_import_id);
        uint_line(out, 4, "source_import_revision", record.identity.source_import_revision);
        line(out, 4, "source_dataset_id", record.identity.source_dataset_id);
        line(out, 4, "source_asset_id", record.identity.source_asset_id);
        line(out, 4, "output_dataset_id", record.identity.output_dataset_id);
        line(out, 4, "output_process_id", record.identity.output_process_id);
        line(out, 4, "executed_at_utc", record.identity.executed_at_utc, false);
        close_object(out, 2);
        write_reference(out, 2, "source_horizontal", record.source_horizontal);
        if (record.source_vertical) {
            write_reference(out, 2, "source_vertical", *record.source_vertical);
        } else {
            out << "  \"source_vertical\": null,\n";
        }
        write_target(out, 2, "target", record.target);
        write_area(out, 2, "area_of_interest", record.area_of_interest);
        write_operation(out, 2, "horizontal_operation", record.horizontal_operation);
        write_vertical(out, 2, record.vertical_operation);
        line(out, 2, "storage_axes", to_string(record.storage_axes));
        write_grids(out, 2, "grids", record.grids);
        write_box(out, 2, "source_extent", record.source_extent);
        write_box(out, 2, "target_extent", record.target_extent);
        write_diagnostics(out, 2, record.diagnostics);
        auto warnings = record.warnings;
        std::sort(warnings.begin(), warnings.end());
        write_string_array(out, 2, "warnings", warnings, false);
        out << "}\n";
        return tsunami::core::success(out.str());
    }

    auto write_coordinate_transformation_record(
        const std::filesystem::path &path,
        const CoordinateTransformationRecord &record) -> tsunami::core::Result<void>
    {
        auto bytes = serialise_coordinate_transformation_record(record);
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
                return tsunami::core::failure(io_error("geo.crs.record_write_failed", "could not open temporary transformation record", temporary));
            }
            file << bytes.value();
            file.flush();
            if (!file) {
                std::filesystem::remove(temporary, ec);
                return tsunami::core::failure(io_error("geo.crs.record_write_failed", "could not write temporary transformation record", temporary));
            }
        }
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return tsunami::core::failure(io_error("geo.crs.record_write_failed", "could not commit transformation record", path));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
