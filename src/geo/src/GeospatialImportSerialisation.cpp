#include <tsunami/geo/GeospatialImportSerialisation.hpp>

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
            error.add_context("operation", "write_geospatial_import_record")
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

        auto number_line(std::ostringstream &out, int indent, std::string_view key, double value, bool comma = true) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << '"' << key << "\": " << std::setprecision(17) << value;
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

        auto write_box(std::ostringstream &out, int indent, std::string_view key, const BoundingBox2D &box, bool comma = true) -> void
        {
            open_object(out, indent, key);
            number_line(out, indent + 2, "minimum_x", box.minimum_x);
            number_line(out, indent + 2, "minimum_y", box.minimum_y);
            number_line(out, indent + 2, "maximum_x", box.maximum_x);
            number_line(out, indent + 2, "maximum_y", box.maximum_y, false);
            close_object(out, indent, comma);
        }

        auto write_transform(std::ostringstream &out, int indent, std::string_view key, const RasterAffineTransform &transform, bool comma = true) -> void
        {
            open_object(out, indent, key);
            number_line(out, indent + 2, "origin_x", transform.origin_x);
            number_line(out, indent + 2, "pixel_width", transform.pixel_width);
            number_line(out, indent + 2, "row_rotation", transform.row_rotation);
            number_line(out, indent + 2, "origin_y", transform.origin_y);
            number_line(out, indent + 2, "column_rotation", transform.column_rotation);
            number_line(out, indent + 2, "pixel_height", transform.pixel_height, false);
            close_object(out, indent, comma);
        }

        auto write_digest(std::ostringstream &out, int indent, const tsunami::data::ContentDigest &digest) -> void
        {
            open_object(out, indent, "declared_digest");
            line(out, indent + 2, "algorithm", tsunami::data::to_string(digest.algorithm));
            line(out, indent + 2, "value", digest.value);
            line(out, indent + 2, "origin", tsunami::data::to_string(digest.origin), false);
            close_object(out, indent);
        }

        auto write_native_reference(std::ostringstream &out, int indent, const NativeSpatialReference &reference) -> void
        {
            open_object(out, indent, "native_spatial_reference");
            nullable_string_line(out, indent + 2, "authority_name", reference.authority_name);
            nullable_string_line(out, indent + 2, "authority_code", reference.authority_code);
            nullable_string_line(out, indent + 2, "crs_name", reference.crs_name);
            nullable_string_line(out, indent + 2, "datum_name", reference.datum_name);
            nullable_string_line(out, indent + 2, "canonical_wkt2", reference.canonical_wkt2);
            out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"axis_names\": [";
            for (std::size_t i = 0; i < reference.axis_names.size(); ++i) {
                out << (i == 0U ? "" : ", ") << '"' << escape(reference.axis_names[i]) << '"';
            }
            out << "],\n";
            out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"axis_directions\": [";
            for (std::size_t i = 0; i < reference.axis_directions.size(); ++i) {
                out << (i == 0U ? "" : ", ") << '"' << escape(reference.axis_directions[i]) << '"';
            }
            out << "],\n";
            out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"axis_units\": [";
            for (std::size_t i = 0; i < reference.axis_units.size(); ++i) {
                out << (i == 0U ? "" : ", ") << '"' << escape(reference.axis_units[i]) << '"';
            }
            out << "],\n";
            nullable_string_line(out, indent + 2, "coordinate_epoch", reference.coordinate_epoch, false);
            close_object(out, indent);
        }

        auto write_evidence(std::ostringstream &out, int indent, std::string_view key, const DatumSourceEvidence &evidence, bool comma = true) -> void
        {
            open_object(out, indent, key);
            line(out, indent + 2, "component", to_string(evidence.component));
            line(out, indent + 2, "reference_kind", to_string(evidence.reference_kind));
            line(out, indent + 2, "origin", to_string(evidence.origin));
            line(out, indent + 2, "status", to_string(evidence.status));
            line(out, indent + 2, "datum_name", evidence.datum_name);
            nullable_string_line(out, indent + 2, "datum_realisation", evidence.datum_realisation);
            nullable_string_line(out, indent + 2, "authority_name", evidence.authority_name);
            nullable_string_line(out, indent + 2, "authority_code", evidence.authority_code);
            nullable_string_line(out, indent + 2, "coordinate_epoch", evidence.coordinate_epoch);
            nullable_string_line(out, indent + 2, "effective_from", evidence.effective_from);
            nullable_string_line(out, indent + 2, "effective_to", evidence.effective_to);
            nullable_string_line(out, indent + 2, "station_id", evidence.station_id);
            line(out, indent + 2, "unit", evidence.unit);
            nullable_string_line(out, indent + 2, "positive_direction", evidence.positive_direction);
            nullable_string_line(out, indent + 2, "tide_system", evidence.tide_system);
            line(out, indent + 2, "source_document_title", evidence.source_document_title);
            line(out, indent + 2, "source_document_uri", evidence.source_document_uri);
            line(out, indent + 2, "accessed_at_utc", evidence.accessed_at_utc, false);
            close_object(out, indent, comma);
        }

        auto write_evidence_set(std::ostringstream &out, int indent, const DatumEvidenceSet &set) -> void
        {
            open_object(out, indent, "datum_evidence");
            write_evidence(out, indent + 2, "horizontal", set.horizontal);
            if (set.vertical) {
                write_evidence(out, indent + 2, "vertical", *set.vertical, false);
            } else {
                out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "\"vertical\": null\n";
            }
            close_object(out, indent);
        }

        auto write_warnings(std::ostringstream &out, int indent, const std::vector<ImportWarning> &warnings) -> void
        {
            out << std::string(static_cast<std::size_t>(indent), ' ') << "\"warnings\": [";
            if (warnings.empty()) {
                out << "]\n";
                return;
            }
            out << '\n';
            for (std::size_t i = 0; i < warnings.size(); ++i) {
                out << std::string(static_cast<std::size_t>(indent + 2), ' ') << "{\n";
                line(out, indent + 4, "code", warnings[i].code);
                line(out, indent + 4, "message", warnings[i].message, false);
                out << std::string(static_cast<std::size_t>(indent + 2), ' ') << '}';
                if (i + 1U != warnings.size()) {
                    out << ',';
                }
                out << '\n';
            }
            out << std::string(static_cast<std::size_t>(indent), ' ') << "]\n";
        }
    }

    auto serialise_geospatial_import_record(const GeospatialImportRecord &record)
        -> tsunami::core::Result<std::string>
    {
        if (auto valid = validate_geospatial_import_record(record); !valid) {
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
        line(out, 4, "import_id", record.identity.import_id);
        uint_line(out, 4, "import_revision", record.identity.import_revision);
        line(out, 4, "case_id", record.identity.case_revision.case_id.str());
        uint_line(out, 4, "case_revision", record.identity.case_revision.revision);
        line(out, 4, "manifest_id", record.identity.manifest_id);
        uint_line(out, 4, "manifest_revision", record.identity.manifest_revision);
        line(out, 4, "dataset_id", record.identity.dataset_id);
        line(out, 4, "asset_id", record.identity.asset_id);
        line(out, 4, "executed_at_utc", record.identity.executed_at_utc, false);
        close_object(out, 2);
        line(out, 2, "import_kind", to_string(record.import_kind));
        line(out, 2, "adapter_name", record.adapter_name);
        line(out, 2, "adapter_version", record.adapter_version);
        line(out, 2, "driver_short_name", record.driver_short_name);
        line(out, 2, "driver_long_name", record.driver_long_name);
        line(out, 2, "media_type", record.media_type);
        line(out, 2, "managed_path", record.managed_path.generic_string());
        write_digest(out, 2, record.declared_digest);
        line(out, 2, "digest_verification_status", record.digest_verification_status);
        write_native_reference(out, 2, record.native_spatial_reference);
        write_evidence_set(out, 2, record.datum_evidence);
        write_box(out, 2, "extent", record.extent);
        if (record.raster) {
            open_object(out, 2, "raster");
            uint_line(out, 4, "width", record.raster->width);
            uint_line(out, 4, "height", record.raster->height);
            uint_line(out, 4, "cell_count", record.raster->cell_count);
            uint_line(out, 4, "band_count", record.raster->band_count);
            line(out, 4, "native_data_type", to_string(record.raster->native_type));
            write_transform(out, 4, "affine_transform", record.raster->transform);
            line(out, 4, "cell_registration", to_string(record.raster->registration));
            bool_line(out, 4, "has_nodata", record.raster->has_nodata);
            nullable_number_line(out, 4, "nodata_value", record.raster->nodata_value);
            nullable_number_line(out, 4, "scale", record.raster->scale);
            nullable_number_line(out, 4, "offset", record.raster->offset);
            open_object(out, 4, "spatial_resolution");
            line(out, 6, "kind", tsunami::data::to_string(record.raster->spatial_resolution.kind));
            nullable_number_line(out, 6, "x", record.raster->spatial_resolution.x);
            nullable_number_line(out, 6, "y", record.raster->spatial_resolution.y);
            nullable_string_line(out, 6, "unit", record.raster->spatial_resolution.unit);
            nullable_string_line(out, 6, "description", record.raster->spatial_resolution.description, false);
            close_object(out, 4, false);
            close_object(out, 2);
            out << "  \"vector\": null,\n";
        } else {
            out << "  \"raster\": null,\n";
            open_object(out, 2, "vector");
            line(out, 4, "layer_name", record.vector->layer_name);
            uint_line(out, 4, "feature_count", record.vector->feature_count);
            line(out, 4, "geometry_kind", to_string(record.vector->geometry_kind));
            uint_line(out, 4, "field_count", record.vector->field_count);
            uint_line(out, 4, "coordinate_count", record.vector->coordinate_count);
            write_box(out, 4, "extent", record.vector->extent, false);
            close_object(out, 2);
        }
        write_warnings(out, 2, record.warnings);
        out << "}\n";
        return tsunami::core::success(out.str());
    }

    auto write_geospatial_import_record(const std::filesystem::path &path, const GeospatialImportRecord &record)
        -> tsunami::core::Result<void>
    {
        auto bytes = serialise_geospatial_import_record(record);
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
                return tsunami::core::failure(io_error("geo.import.record_write_failed", "could not open temporary import record", temporary));
            }
            file << bytes.value();
            file.flush();
            if (!file) {
                std::filesystem::remove(temporary, ec);
                return tsunami::core::failure(io_error("geo.import.record_write_failed", "could not write temporary import record", temporary));
            }
        }
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return tsunami::core::failure(io_error("geo.import.record_write_failed", "could not commit import record", path));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
