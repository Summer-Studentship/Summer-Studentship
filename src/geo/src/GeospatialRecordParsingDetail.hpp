#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <tsunami/core/Error.hpp>
#include <tsunami/core/Identity.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/CoordinateTransformation.hpp>
#include <tsunami/geo/ImportedRaster.hpp>
#include <tsunami/geo/ImportedVector.hpp>

namespace tsunami::geo::detail
{
    using Json = nlohmann::ordered_json;

    struct ParseFailure
    {
        tsunami::core::Error error;
    };

    enum class RecordKind
    {
        corridor,
        terrain
    };

    [[nodiscard]] inline auto parse_prefix(RecordKind kind) -> std::string_view
    {
        return kind == RecordKind::corridor ? "geo.corridor.record_parse" : "geo.terrain.record_parse";
    }

    [[nodiscard]] inline auto read_prefix(RecordKind kind) -> std::string_view
    {
        return kind == RecordKind::corridor ? "geo.corridor.record_read" : "geo.terrain.record_read";
    }

    [[nodiscard]] inline auto parse_operation(RecordKind kind) -> std::string_view
    {
        return kind == RecordKind::corridor ? "parse_corridor_construction_record" : "parse_terrain_conditioning_record";
    }

    [[nodiscard]] inline auto read_operation(RecordKind kind) -> std::string_view
    {
        return kind == RecordKind::corridor ? "read_corridor_construction_record" : "read_terrain_conditioning_record";
    }

    [[nodiscard]] inline auto actual_type(const Json &value) -> std::string
    {
        if (value.is_object()) {
            return "object";
        }
        if (value.is_array()) {
            return "array";
        }
        if (value.is_string()) {
            return "string";
        }
        if (value.is_boolean()) {
            return "boolean";
        }
        if (value.is_number_unsigned()) {
            return "unsigned integer";
        }
        if (value.is_number_integer()) {
            return "integer";
        }
        if (value.is_number()) {
            return "number";
        }
        if (value.is_null()) {
            return "null";
        }
        return "unknown";
    }

    [[nodiscard]] inline auto pointer_for(std::string_view parent, std::string_view field) -> std::string
    {
        auto escaped = std::string{};
        for (const auto ch : field) {
            if (ch == '~') {
                escaped += "~0";
            } else if (ch == '/') {
                escaped += "~1";
            } else {
                escaped += ch;
            }
        }
        if (parent == "/") {
            return "/" + escaped;
        }
        return std::string{parent} + "/" + escaped;
    }

    [[nodiscard]] inline auto parse_error(
        RecordKind kind,
        std::string_view suffix,
        std::string message,
        const std::string &source_name,
        std::string pointer,
        std::string expected_type = {},
        std::string actual = {},
        std::string field = {}) -> tsunami::core::Error
    {
        auto error = tsunami::core::Error{
            std::string{parse_prefix(kind)} + "." + std::string{suffix},
            std::move(message),
            tsunami::core::DiagnosticCategory::input_data,
            tsunami::core::Severity::error};
        error.add_context("operation", std::string{parse_operation(kind)})
            .add_context("source_name", source_name)
            .add_context("json_pointer", std::move(pointer))
            .add_context("state_changed", "false");
        if (!expected_type.empty()) {
            error.add_context("expected_type", std::move(expected_type));
        }
        if (!actual.empty()) {
            error.add_context("actual_type", std::move(actual));
        }
        if (!field.empty()) {
            error.add_context("field", std::move(field));
        }
        return error;
    }

    [[nodiscard]] inline auto read_error(
        RecordKind kind,
        std::string_view suffix,
        std::string message,
        const std::filesystem::path &path) -> tsunami::core::Error
    {
        auto error = tsunami::core::Error{
            std::string{read_prefix(kind)} + "." + std::string{suffix},
            std::move(message),
            tsunami::core::DiagnosticCategory::input_data,
            tsunami::core::Severity::error};
        error.add_context("operation", std::string{read_operation(kind)})
            .add_context("path", path.generic_string())
            .add_context("state_changed", "false");
        return error;
    }

    [[noreturn]] inline auto fail(
        RecordKind kind,
        std::string_view suffix,
        std::string message,
        const std::string &source_name,
        std::string pointer,
        std::string expected_type = {},
        std::string actual = {},
        std::string field = {}) -> void
    {
        throw ParseFailure{parse_error(kind, suffix, std::move(message), source_name, std::move(pointer), std::move(expected_type), std::move(actual), std::move(field))};
    }

    inline auto require_object(const Json &value, const std::string &pointer, const std::string &source, RecordKind kind) -> void
    {
        if (!value.is_object()) {
            fail(kind, "type_invalid", "JSON value must be an object", source, pointer, "object", actual_type(value));
        }
    }

    inline auto reject_unknown(
        const Json &value,
        const std::set<std::string> &allowed,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> void
    {
        require_object(value, pointer, source, kind);
        for (const auto &[key, child_value] : value.items()) {
            static_cast<void>(child_value);
            if (!allowed.contains(key)) {
                fail(kind, "field_unknown", "unknown field is not permitted", source, pointer_for(pointer, key), {}, {}, key);
            }
        }
    }

    [[nodiscard]] inline auto child(
        const Json &value,
        std::string_view key,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> const Json &
    {
        const auto found = value.find(std::string{key});
        if (found == value.end()) {
            fail(kind, "field_missing", "required field is missing", source, pointer_for(pointer, key), {}, {}, std::string{key});
        }
        return *found;
    }

    [[nodiscard]] inline auto string_value(
        const Json &value,
        std::string_view key,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> std::string
    {
        const auto &field = child(value, key, pointer, source, kind);
        if (!field.is_string()) {
            fail(kind, "type_invalid", "field has the wrong JSON type", source, pointer_for(pointer, key), "string", actual_type(field), std::string{key});
        }
        auto text = field.get<std::string>();
        if (text.find('\0') != std::string::npos) {
            fail(kind, "type_invalid", "string field contains embedded NUL", source, pointer_for(pointer, key), "string without embedded NUL", "string", std::string{key});
        }
        return text;
    }

    [[nodiscard]] inline auto nullable_string(
        const Json &value,
        std::string_view key,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> std::optional<std::string>
    {
        const auto &field = child(value, key, pointer, source, kind);
        if (field.is_null()) {
            return std::nullopt;
        }
        if (!field.is_string()) {
            fail(kind, "type_invalid", "nullable field has the wrong JSON type", source, pointer_for(pointer, key), "string or null", actual_type(field), std::string{key});
        }
        auto text = field.get<std::string>();
        if (text.find('\0') != std::string::npos) {
            fail(kind, "type_invalid", "string field contains embedded NUL", source, pointer_for(pointer, key), "string without embedded NUL or null", "string", std::string{key});
        }
        return text;
    }

    [[nodiscard]] inline auto number_value(
        const Json &value,
        std::string_view key,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> double
    {
        const auto &field = child(value, key, pointer, source, kind);
        if (!field.is_number()) {
            fail(kind, "type_invalid", "field has the wrong JSON type", source, pointer_for(pointer, key), "number", actual_type(field), std::string{key});
        }
        const auto out = field.get<double>();
        if (!std::isfinite(out)) {
            fail(kind, "type_invalid", "numeric field is not finite", source, pointer_for(pointer, key), "finite number", "number", std::string{key});
        }
        return out;
    }

    [[nodiscard]] inline auto nullable_number(
        const Json &value,
        std::string_view key,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> std::optional<double>
    {
        const auto &field = child(value, key, pointer, source, kind);
        if (field.is_null()) {
            return std::nullopt;
        }
        if (!field.is_number()) {
            fail(kind, "type_invalid", "nullable field has the wrong JSON type", source, pointer_for(pointer, key), "number or null", actual_type(field), std::string{key});
        }
        const auto out = field.get<double>();
        if (!std::isfinite(out)) {
            fail(kind, "type_invalid", "numeric field is not finite", source, pointer_for(pointer, key), "finite number or null", "number", std::string{key});
        }
        return out;
    }

    [[nodiscard]] inline auto bool_value(
        const Json &value,
        std::string_view key,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> bool
    {
        const auto &field = child(value, key, pointer, source, kind);
        if (!field.is_boolean()) {
            fail(kind, "type_invalid", "field has the wrong JSON type", source, pointer_for(pointer, key), "boolean", actual_type(field), std::string{key});
        }
        return field.get<bool>();
    }

    [[nodiscard]] inline auto uint_value(
        const Json &value,
        std::string_view key,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> std::uint64_t
    {
        const auto &field = child(value, key, pointer, source, kind);
        if (!field.is_number_unsigned()) {
            fail(kind, "type_invalid", "field has the wrong JSON type", source, pointer_for(pointer, key), "unsigned integer", actual_type(field), std::string{key});
        }
        return field.get<std::uint64_t>();
    }

    [[nodiscard]] inline auto size_value(
        const Json &value,
        std::string_view key,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> std::size_t
    {
        const auto out = uint_value(value, key, pointer, source, kind);
        if (out > std::numeric_limits<std::size_t>::max()) {
            fail(kind, "type_invalid", "unsigned integer exceeds size_t", source, pointer_for(pointer, key), "size_t", "unsigned integer", std::string{key});
        }
        return static_cast<std::size_t>(out);
    }

    template <class Enum>
    [[nodiscard]] auto enum_value(
        const Json &value,
        std::string_view key,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind,
        std::initializer_list<std::pair<std::string_view, Enum>> accepted) -> Enum
    {
        const auto text = string_value(value, key, pointer, source, kind);
        for (const auto &[name, result] : accepted) {
            if (text == name) {
                return result;
            }
        }
        fail(kind, "type_invalid", "field value is not a supported enum string", source, pointer_for(pointer, key), "supported enum string", "string", std::string{key});
    }

    [[nodiscard]] inline auto string_array(
        const Json &value,
        std::string_view key,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> std::vector<std::string>
    {
        const auto array_pointer = pointer_for(pointer, key);
        const auto &field = child(value, key, pointer, source, kind);
        if (!field.is_array()) {
            fail(kind, "type_invalid", "field has the wrong JSON type", source, array_pointer, "array", actual_type(field), std::string{key});
        }
        auto out = std::vector<std::string>{};
        out.reserve(field.size());
        for (std::size_t i = 0U; i < field.size(); ++i) {
            const auto &entry = field[i];
            const auto entry_pointer = array_pointer + "/" + std::to_string(i);
            if (!entry.is_string()) {
                fail(kind, "type_invalid", "array item has the wrong JSON type", source, entry_pointer, "string", actual_type(entry), std::string{key});
            }
            auto text = entry.get<std::string>();
            if (text.find('\0') != std::string::npos) {
                fail(kind, "type_invalid", "array string item contains embedded NUL", source, entry_pointer, "string without embedded NUL", "string", std::string{key});
            }
            out.push_back(std::move(text));
        }
        return out;
    }

    [[nodiscard]] inline auto parse_schema(
        const Json &value,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> tsunami::data::SchemaIdentity
    {
        reject_unknown(value, {"schema_name", "version"}, pointer, source, kind);
        const auto &version = child(value, "version", pointer, source, kind);
        const auto version_pointer = pointer_for(pointer, "version");
        reject_unknown(version, {"major", "minor", "patch"}, version_pointer, source, kind);
        return tsunami::data::SchemaIdentity{
            string_value(value, "schema_name", pointer, source, kind),
            tsunami::core::SemanticVersion{
                static_cast<std::uint32_t>(uint_value(version, "major", version_pointer, source, kind)),
                static_cast<std::uint32_t>(uint_value(version, "minor", version_pointer, source, kind)),
                static_cast<std::uint32_t>(uint_value(version, "patch", version_pointer, source, kind))}};
    }

    [[nodiscard]] inline auto parse_case_revision(
        const Json &value,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> tsunami::data::CaseRevisionRef
    {
        const auto case_id_text = string_value(value, "case_id", pointer, source, kind);
        auto case_id = tsunami::core::CaseId::from_string(case_id_text);
        if (!case_id) {
            fail(kind, "type_invalid", "case_id is not a valid project identifier", source, pointer_for(pointer, "case_id"), "valid case id", "string", "case_id");
        }
        return tsunami::data::CaseRevisionRef{std::move(*case_id), uint_value(value, "case_revision", pointer, source, kind)};
    }

    [[nodiscard]] inline auto parse_point2(
        const Json &value,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> Point2D
    {
        reject_unknown(value, {"x", "y"}, pointer, source, kind);
        return Point2D{number_value(value, "x", pointer, source, kind), number_value(value, "y", pointer, source, kind)};
    }

    [[nodiscard]] inline auto parse_coordinate3(
        const Json &value,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> Coordinate3D
    {
        reject_unknown(value, {"x", "y", "z"}, pointer, source, kind);
        return Coordinate3D{number_value(value, "x", pointer, source, kind), number_value(value, "y", pointer, source, kind), number_value(value, "z", pointer, source, kind)};
    }

    [[nodiscard]] inline auto parse_box(
        const Json &value,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> BoundingBox2D
    {
        reject_unknown(value, {"minimum_x", "minimum_y", "maximum_x", "maximum_y"}, pointer, source, kind);
        return BoundingBox2D{
            number_value(value, "minimum_x", pointer, source, kind),
            number_value(value, "minimum_y", pointer, source, kind),
            number_value(value, "maximum_x", pointer, source, kind),
            number_value(value, "maximum_y", pointer, source, kind)};
    }

    [[nodiscard]] inline auto parse_reference(
        const Json &value,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> CoordinateReferenceDescriptor
    {
        reject_unknown(
            value,
            {"authority_name", "authority_code", "name", "canonical_wkt2", "canonical_projjson", "datum_name", "datum_realisation", "coordinate_epoch_decimal_year", "axis_names", "axis_directions", "axis_units"},
            pointer,
            source,
            kind);
        return CoordinateReferenceDescriptor{
            nullable_string(value, "authority_name", pointer, source, kind),
            nullable_string(value, "authority_code", pointer, source, kind),
            string_value(value, "name", pointer, source, kind),
            nullable_string(value, "canonical_wkt2", pointer, source, kind),
            nullable_string(value, "canonical_projjson", pointer, source, kind),
            nullable_string(value, "datum_name", pointer, source, kind),
            nullable_string(value, "datum_realisation", pointer, source, kind),
            nullable_number(value, "coordinate_epoch_decimal_year", pointer, source, kind),
            string_array(value, "axis_names", pointer, source, kind),
            string_array(value, "axis_directions", pointer, source, kind),
            string_array(value, "axis_units", pointer, source, kind)};
    }

    [[nodiscard]] inline auto parse_target(
        const Json &value,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> ComputationalTargetReference
    {
        reject_unknown(value, {"horizontal", "vertical", "storage_axes", "horizontal_unit", "vertical_unit", "vertical_positive"}, pointer, source, kind);
        const auto &vertical = child(value, "vertical", pointer, source, kind);
        auto vertical_reference = std::optional<CoordinateReferenceDescriptor>{};
        if (vertical.is_null()) {
            vertical_reference = std::nullopt;
        } else {
            vertical_reference = parse_reference(vertical, pointer_for(pointer, "vertical"), source, kind);
        }
        return ComputationalTargetReference{
            parse_reference(child(value, "horizontal", pointer, source, kind), pointer_for(pointer, "horizontal"), source, kind),
            std::move(vertical_reference),
            enum_value<ComputationalAxisConvention>(
                value,
                "storage_axes",
                pointer,
                source,
                kind,
                {{"east_north", ComputationalAxisConvention::east_north}, {"east_north_up", ComputationalAxisConvention::east_north_up}}),
            string_value(value, "horizontal_unit", pointer, source, kind),
            nullable_string(value, "vertical_unit", pointer, source, kind),
            nullable_string(value, "vertical_positive", pointer, source, kind)};
    }

    [[nodiscard]] inline auto parse_transformation_identity(
        const Json &value,
        const std::string &pointer,
        const std::string &source,
        RecordKind kind) -> CoordinateTransformationIdentity
    {
        reject_unknown(
            value,
            {"transformation_id", "transformation_revision", "case_id", "case_revision", "manifest_id", "manifest_revision", "source_import_id", "source_import_revision", "source_dataset_id", "source_asset_id", "output_dataset_id", "output_process_id", "executed_at_utc"},
            pointer,
            source,
            kind);
        return CoordinateTransformationIdentity{
            string_value(value, "transformation_id", pointer, source, kind),
            uint_value(value, "transformation_revision", pointer, source, kind),
            parse_case_revision(value, pointer, source, kind),
            string_value(value, "manifest_id", pointer, source, kind),
            uint_value(value, "manifest_revision", pointer, source, kind),
            string_value(value, "source_import_id", pointer, source, kind),
            uint_value(value, "source_import_revision", pointer, source, kind),
            string_value(value, "source_dataset_id", pointer, source, kind),
            string_value(value, "source_asset_id", pointer, source, kind),
            string_value(value, "output_dataset_id", pointer, source, kind),
            string_value(value, "output_process_id", pointer, source, kind),
            string_value(value, "executed_at_utc", pointer, source, kind)};
    }

    [[nodiscard]] inline auto parse_json_document(
        std::string_view document,
        const std::string &source,
        RecordKind kind) -> Json
    {
        if (document.find('\0') != std::string_view::npos) {
            fail(kind, "malformed", "JSON document contains embedded NUL", source, "/", "UTF-8 JSON text", "embedded NUL");
        }
        struct Frame
        {
            std::set<std::string> keys;
            std::string pointer;
            std::string pending_child_pointer;
        };
        auto frames = std::vector<Frame>{};
        const auto callback = [&](int, nlohmann::json::parse_event_t event, Json &parsed) -> bool {
            if (event == nlohmann::json::parse_event_t::object_start) {
                auto pointer = std::string{"/"};
                if (!frames.empty() && !frames.back().pending_child_pointer.empty()) {
                    pointer = frames.back().pending_child_pointer;
                    frames.back().pending_child_pointer.clear();
                }
                frames.push_back(Frame{{}, std::move(pointer), {}});
            } else if (event == nlohmann::json::parse_event_t::object_end) {
                if (!frames.empty()) {
                    frames.pop_back();
                }
            } else if (event == nlohmann::json::parse_event_t::key) {
                auto key = parsed.get<std::string>();
                if (key.find('\0') != std::string::npos) {
                    throw ParseFailure{parse_error(kind, "malformed", "object key contains embedded NUL", source, frames.empty() ? "/" : pointer_for(frames.back().pointer, key), "object key without embedded NUL", "string", key)};
                }
                if (!frames.empty()) {
                    const auto inserted = frames.back().keys.insert(key).second;
                    const auto pointer = pointer_for(frames.back().pointer, key);
                    frames.back().pending_child_pointer = pointer;
                    if (!inserted) {
                        throw ParseFailure{parse_error(kind, "duplicate_key", "duplicate object key is not permitted", source, pointer, "unique object key", "duplicate key", key)};
                    }
                }
            }
            return true;
        };
        try {
            return Json::parse(document.begin(), document.end(), callback, true, false);
        } catch (const ParseFailure &) {
            throw;
        } catch (const nlohmann::json::exception &) {
            fail(kind, "malformed", "document is not well-formed UTF-8 JSON", source, "/", "UTF-8 JSON", "malformed");
        } catch (const std::exception &) {
            fail(kind, "malformed", "document could not be parsed", source, "/", "UTF-8 JSON", "malformed");
        }
    }

    inline auto read_bounded_file(
        const std::filesystem::path &path,
        std::size_t maximum_size,
        RecordKind kind) -> tsunami::core::Result<std::string>
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) {
            return tsunami::core::failure<std::string>(read_error(kind, "failed", "record file does not exist", path));
        }
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            return tsunami::core::failure<std::string>(read_error(kind, "failed", "record path is not a regular file", path));
        }
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) {
            return tsunami::core::failure<std::string>(read_error(kind, "failed", "could not determine record file size", path));
        }
        if (size > maximum_size) {
            return tsunami::core::failure<std::string>(read_error(kind, "too_large", "record file exceeds maximum supported size", path));
        }
        auto file = std::ifstream{path, std::ios::binary};
        if (!file) {
            return tsunami::core::failure<std::string>(read_error(kind, "failed", "could not open record file", path));
        }
        auto bytes = std::string{};
        bytes.reserve(static_cast<std::size_t>(size));
        bytes.assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
        if (file.bad() || bytes.size() != static_cast<std::size_t>(size)) {
            return tsunami::core::failure<std::string>(read_error(kind, "failed", "could not completely read record file", path));
        }
        return tsunami::core::success(std::move(bytes));
    }

} // namespace tsunami::geo::detail
