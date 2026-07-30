#include <tsunami/data/DatasetManifestParsing.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <nlohmann/json.hpp>

#include <tsunami/data/DatasetManifestValidation.hpp>

namespace tsunami::data
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        struct ParseFailure
        {
            tsunami::core::Error error;
        };

        [[nodiscard]] auto parse_error(
            std::string code,
            std::string message,
            std::string source_name,
            std::string pointer = {},
            std::string field = {},
            std::string rule = {}) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::input_data,
                tsunami::core::Severity::error};
            error.add_context("operation", "parse_dataset_manifest")
                .add_context("source_name", std::move(source_name))
                .add_context("state_changed", "false");
            if (!pointer.empty()) {
                error.add_context("json_pointer", std::move(pointer));
            }
            if (!field.empty()) {
                error.add_context("field", std::move(field));
            }
            if (!rule.empty()) {
                error.add_context("rule_id", std::move(rule));
            }
            return error;
        }

        [[nodiscard]] auto file_error(std::string code, std::string message, const std::filesystem::path &path) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::input_data,
                tsunami::core::Severity::error};
            error.add_context("operation", "read_dataset_manifest")
                .add_context("path", path.generic_string())
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto pointer_for(std::string_view parent, std::string_view field) -> std::string
        {
            if (parent == "/") {
                return "/" + std::string{field};
            }
            return std::string{parent} + "/" + std::string{field};
        }

        [[noreturn]] auto fail(
            std::string code,
            std::string message,
            const std::string &source,
            std::string pointer,
            std::string field = {},
            std::string rule = {}) -> void
        {
            throw ParseFailure{parse_error(std::move(code), std::move(message), source, std::move(pointer), std::move(field), std::move(rule))};
        }

        auto require_object(const Json &json, const std::string &pointer, const std::string &source) -> void
        {
            if (!json.is_object()) {
                fail("data.dataset_manifest.field_type_invalid", "JSON value must be an object", source, pointer);
            }
        }

        auto reject_unknown(const Json &json, const std::set<std::string> &allowed, const std::string &pointer, const std::string &source) -> void
        {
            require_object(json, pointer, source);
            for (const auto &[key, value] : json.items()) {
                static_cast<void>(value);
                if (!allowed.contains(key)) {
                    fail("data.dataset_manifest.unknown_field", "unknown field is not permitted in a core section", source, pointer_for(pointer, key), key, "manifest.unknown_field");
                }
            }
        }

        [[nodiscard]] auto child(const Json &json, std::string_view key, const std::string &pointer, const std::string &source) -> const Json &
        {
            const auto found = json.find(std::string{key});
            if (found == json.end()) {
                fail("data.dataset_manifest.required_field_missing", "required field is missing", source, pointer_for(pointer, key), std::string{key});
            }
            return *found;
        }

        template <class T>
        [[nodiscard]] auto required(const Json &json, std::string_view key, const std::string &pointer, const std::string &source, const char *type) -> T
        {
            const auto &value = child(json, key, pointer, source);
            try {
                if constexpr (std::is_same_v<T, std::string>) {
                    if (!value.is_string()) {
                        throw std::runtime_error{"type"};
                    }
                } else if constexpr (std::is_same_v<T, bool>) {
                    if (!value.is_boolean()) {
                        throw std::runtime_error{"type"};
                    }
                } else if constexpr (std::is_same_v<T, double>) {
                    if (!value.is_number()) {
                        throw std::runtime_error{"type"};
                    }
                } else if constexpr (std::is_same_v<T, std::uint64_t>) {
                    if (!value.is_number_unsigned()) {
                        throw std::runtime_error{"type"};
                    }
                }
                auto result = value.get<T>();
                if constexpr (std::is_same_v<T, double>) {
                    if (!std::isfinite(result)) {
                        throw std::runtime_error{"finite"};
                    }
                }
                return result;
            } catch (const std::exception &) {
                auto error = parse_error("data.dataset_manifest.field_type_invalid", "field has the wrong JSON type", source, pointer_for(pointer, key), std::string{key});
                error.add_context("expected", type);
                throw ParseFailure{std::move(error)};
            }
        }

        template <class T>
        [[nodiscard]] auto nullable(const Json &json, std::string_view key, const std::string &pointer, const std::string &source, const char *type)
            -> std::optional<T>
        {
            const auto &value = child(json, key, pointer, source);
            if (value.is_null()) {
                return std::nullopt;
            }
            return required<T>(json, key, pointer, source, type);
        }

        template <class Enum>
        [[nodiscard]] auto enum_value(
            const Json &json,
            std::string_view key,
            const std::string &pointer,
            const std::string &source,
            std::initializer_list<std::pair<std::string_view, Enum>> values) -> Enum
        {
            const auto text = required<std::string>(json, key, pointer, source, "string");
            for (const auto &[name, value] : values) {
                if (text == name) {
                    return value;
                }
            }
            fail("data.dataset_manifest.field_type_invalid", "field value is not a supported enum string", source, pointer_for(pointer, key), std::string{key});
        }

        [[nodiscard]] auto canonical_json(Json value) -> std::string
        {
            if (value.is_object()) {
                Json sorted = Json::object();
                auto keys = std::vector<std::string>{};
                for (const auto &[key, child_value] : value.items()) {
                    static_cast<void>(child_value);
                    keys.push_back(key);
                }
                std::sort(keys.begin(), keys.end());
                for (const auto &key : keys) {
                    sorted[key] = Json::parse(canonical_json(value[key]));
                }
                value = std::move(sorted);
            } else if (value.is_array()) {
                for (auto &child_value : value) {
                    child_value = Json::parse(canonical_json(child_value));
                }
            }
            return value.dump();
        }

        [[nodiscard]] auto extensions(const Json &json, const std::string &pointer, const std::string &source) -> DatasetManifestExtensions
        {
            require_object(json, pointer, source);
            auto result = DatasetManifestExtensions{};
            for (const auto &[key, value] : json.items()) {
                result.values.push_back(DatasetManifestExtension{key, canonical_json(value)});
            }
            return result;
        }

        [[nodiscard]] auto identity(const Json &json, const std::string &source) -> DatasetManifestIdentity
        {
            constexpr auto pointer = "/manifest";
            reject_unknown(json, {"manifest_id", "manifest_revision", "case_id", "case_revision", "created_at_utc", "created_by"}, pointer, source);
            const auto case_id_text = required<std::string>(json, "case_id", pointer, source, "string");
            auto case_id = tsunami::core::CaseId::from_string(case_id_text);
            if (!case_id) {
                fail("data.dataset_manifest.identity_invalid", "case_id is invalid", source, "/manifest/case_id", "case_id", "manifest.identity.case_revision.valid");
            }
            return DatasetManifestIdentity{
                required<std::string>(json, "manifest_id", pointer, source, "string"),
                required<std::uint64_t>(json, "manifest_revision", pointer, source, "unsigned integer"),
                CaseRevisionRef{std::move(*case_id), required<std::uint64_t>(json, "case_revision", pointer, source, "unsigned integer")},
                required<std::string>(json, "created_at_utc", pointer, source, "string"),
                required<std::string>(json, "created_by", pointer, source, "string")};
        }

        [[nodiscard]] auto parse_provider(const Json &json, const std::string &pointer, const std::string &source) -> DatasetProvider
        {
            reject_unknown(json, {"provider_id", "name", "organisation", "homepage_uri", "extensions"}, pointer, source);
            return DatasetProvider{
                required<std::string>(json, "provider_id", pointer, source, "string"),
                required<std::string>(json, "name", pointer, source, "string"),
                nullable<std::string>(json, "organisation", pointer, source, "string"),
                nullable<std::string>(json, "homepage_uri", pointer, source, "string"),
                extensions(child(json, "extensions", pointer, source), pointer_for(pointer, "extensions"), source)};
        }

        [[nodiscard]] auto parse_licence(const Json &json, const std::string &pointer, const std::string &source) -> DatasetLicence
        {
            reject_unknown(json, {"licence_id", "name", "expression", "licence_uri", "attribution", "extensions"}, pointer, source);
            return DatasetLicence{
                required<std::string>(json, "licence_id", pointer, source, "string"),
                required<std::string>(json, "name", pointer, source, "string"),
                required<std::string>(json, "expression", pointer, source, "string"),
                nullable<std::string>(json, "licence_uri", pointer, source, "string"),
                nullable<std::string>(json, "attribution", pointer, source, "string"),
                extensions(child(json, "extensions", pointer, source), pointer_for(pointer, "extensions"), source)};
        }

        [[nodiscard]] auto parse_source(const Json &json, const std::string &pointer, const std::string &source) -> SourceAcquisitionRecord
        {
            reject_unknown(json, {"source_uri", "accessed_at_utc", "source_version", "publication_date"}, pointer, source);
            return SourceAcquisitionRecord{
                required<std::string>(json, "source_uri", pointer, source, "string"),
                required<std::string>(json, "accessed_at_utc", pointer, source, "string"),
                nullable<std::string>(json, "source_version", pointer, source, "string"),
                nullable<std::string>(json, "publication_date", pointer, source, "string")};
        }

        [[nodiscard]] auto parse_location(const Json &json, const std::string &pointer, const std::string &source) -> DatasetAssetLocation
        {
            reject_unknown(json, {"kind", "managed_path", "external_uri"}, pointer, source);
            auto location = DatasetAssetLocation{};
            location.kind = enum_value<DatasetLocationKind>(json, "kind", pointer, source, {{"managed_path", DatasetLocationKind::managed_path}, {"external_uri", DatasetLocationKind::external_uri}});
            const auto managed = nullable<std::string>(json, "managed_path", pointer, source, "string");
            location.managed_path = managed ? std::optional<std::filesystem::path>{std::filesystem::path{*managed}} : std::nullopt;
            location.external_uri = nullable<std::string>(json, "external_uri", pointer, source, "string");
            return location;
        }

        [[nodiscard]] auto parse_digest(const Json &json, const std::string &pointer, const std::string &source) -> ContentDigest
        {
            reject_unknown(json, {"algorithm", "value", "origin"}, pointer, source);
            return ContentDigest{
                enum_value<DigestAlgorithm>(json, "algorithm", pointer, source, {{"sha256", DigestAlgorithm::sha256}}),
                required<std::string>(json, "value", pointer, source, "string"),
                enum_value<DigestOrigin>(json, "origin", pointer, source, {{"provider_declared", DigestOrigin::provider_declared}, {"project_computed", DigestOrigin::project_computed}})};
        }

        [[nodiscard]] auto parse_asset(const Json &json, const std::string &pointer, const std::string &source) -> DatasetAsset
        {
            reject_unknown(json, {"asset_id", "role", "location", "media_type", "byte_size", "digest"}, pointer, source);
            return DatasetAsset{
                required<std::string>(json, "asset_id", pointer, source, "string"),
                enum_value<DatasetAssetRole>(json, "role", pointer, source, {{"primary", DatasetAssetRole::primary}, {"metadata", DatasetAssetRole::metadata}, {"auxiliary", DatasetAssetRole::auxiliary}}),
                parse_location(child(json, "location", pointer, source), pointer_for(pointer, "location"), source),
                required<std::string>(json, "media_type", pointer, source, "string"),
                nullable<std::uint64_t>(json, "byte_size", pointer, source, "unsigned integer"),
                parse_digest(child(json, "digest", pointer, source), pointer_for(pointer, "digest"), source)};
        }

        [[nodiscard]] auto parse_spatial_reference(const Json &json, const std::string &pointer, const std::string &source) -> DatasetSpatialReference
        {
            reject_unknown(json, {"applicability", "horizontal_crs", "vertical_datum", "horizontal_unit", "vertical_unit", "axis_order", "vertical_positive"}, pointer, source);
            return DatasetSpatialReference{
                enum_value<SpatialApplicability>(json, "applicability", pointer, source, {{"spatial", SpatialApplicability::spatial}, {"not_applicable", SpatialApplicability::not_applicable}}),
                nullable<std::string>(json, "horizontal_crs", pointer, source, "string"),
                nullable<std::string>(json, "vertical_datum", pointer, source, "string"),
                nullable<std::string>(json, "horizontal_unit", pointer, source, "string"),
                nullable<std::string>(json, "vertical_unit", pointer, source, "string"),
                nullable<std::string>(json, "axis_order", pointer, source, "string"),
                nullable<std::string>(json, "vertical_positive", pointer, source, "string")};
        }

        [[nodiscard]] auto parse_spatial_resolution(const Json &json, const std::string &pointer, const std::string &source) -> SpatialResolution
        {
            reject_unknown(json, {"kind", "x", "y", "unit", "description"}, pointer, source);
            return SpatialResolution{
                enum_value<SpatialResolutionKind>(json, "kind", pointer, source, {{"grid_spacing", SpatialResolutionKind::grid_spacing}, {"nominal", SpatialResolutionKind::nominal}, {"irregular", SpatialResolutionKind::irregular}, {"not_reported", SpatialResolutionKind::not_reported}, {"not_applicable", SpatialResolutionKind::not_applicable}}),
                nullable<double>(json, "x", pointer, source, "number"),
                nullable<double>(json, "y", pointer, source, "number"),
                nullable<std::string>(json, "unit", pointer, source, "string"),
                nullable<std::string>(json, "description", pointer, source, "string")};
        }

        [[nodiscard]] auto parse_temporal_resolution(const Json &json, const std::string &pointer, const std::string &source) -> TemporalResolution
        {
            reject_unknown(json, {"kind", "value", "unit", "description"}, pointer, source);
            return TemporalResolution{
                enum_value<TemporalResolutionKind>(json, "kind", pointer, source, {{"static_dataset", TemporalResolutionKind::static_dataset}, {"interval", TemporalResolutionKind::interval}, {"irregular", TemporalResolutionKind::irregular}, {"not_reported", TemporalResolutionKind::not_reported}, {"not_applicable", TemporalResolutionKind::not_applicable}}),
                nullable<double>(json, "value", pointer, source, "number"),
                nullable<std::string>(json, "unit", pointer, source, "string"),
                nullable<std::string>(json, "description", pointer, source, "string")};
        }

        [[nodiscard]] auto parse_resolution(const Json &json, const std::string &pointer, const std::string &source) -> DatasetResolution
        {
            reject_unknown(json, {"spatial", "temporal"}, pointer, source);
            return DatasetResolution{
                parse_spatial_resolution(child(json, "spatial", pointer, source), pointer_for(pointer, "spatial"), source),
                parse_temporal_resolution(child(json, "temporal", pointer, source), pointer_for(pointer, "temporal"), source)};
        }

        [[nodiscard]] auto parse_measure(const Json &json, const std::string &pointer, const std::string &source) -> UncertaintyMeasure
        {
            reject_unknown(json, {"quantity", "value", "unit", "confidence_level", "method"}, pointer, source);
            return UncertaintyMeasure{
                required<std::string>(json, "quantity", pointer, source, "string"),
                required<double>(json, "value", pointer, source, "number"),
                required<std::string>(json, "unit", pointer, source, "string"),
                nullable<double>(json, "confidence_level", pointer, source, "number"),
                nullable<std::string>(json, "method", pointer, source, "string")};
        }

        [[nodiscard]] auto parse_uncertainty(const Json &json, const std::string &pointer, const std::string &source) -> DatasetUncertainty
        {
            reject_unknown(json, {"status", "measures", "description"}, pointer, source);
            const auto &items = child(json, "measures", pointer, source);
            if (!items.is_array()) {
                fail("data.dataset_manifest.field_type_invalid", "measures must be an array", source, pointer_for(pointer, "measures"), "measures");
            }
            auto measures = std::vector<UncertaintyMeasure>{};
            for (std::size_t index = 0; index < items.size(); ++index) {
                measures.push_back(parse_measure(items[index], pointer_for(pointer_for(pointer, "measures"), std::to_string(index)), source));
            }
            return DatasetUncertainty{
                enum_value<UncertaintyStatus>(json, "status", pointer, source, {{"reported", UncertaintyStatus::reported}, {"estimated", UncertaintyStatus::estimated}, {"not_reported", UncertaintyStatus::not_reported}, {"not_applicable", UncertaintyStatus::not_applicable}}),
                std::move(measures),
                nullable<std::string>(json, "description", pointer, source, "string")};
        }

        [[nodiscard]] auto parse_dataset(const Json &json, const std::string &pointer, const std::string &source) -> DatasetRecord
        {
            reject_unknown(json, {"dataset_id", "origin_kind", "representation", "roles", "title", "description", "provider_id", "licence_id", "source", "generated_by_process_id", "assets", "spatial_reference", "resolution", "uncertainty", "citation", "extensions"}, pointer, source);
            const auto &role_items = child(json, "roles", pointer, source);
            if (!role_items.is_array()) {
                fail("data.dataset_manifest.field_type_invalid", "roles must be an array", source, pointer_for(pointer, "roles"), "roles");
            }
            auto roles = std::vector<DatasetRole>{};
            for (std::size_t index = 0; index < role_items.size(); ++index) {
                if (!role_items[index].is_string()) {
                    fail("data.dataset_manifest.field_type_invalid", "role must be a string", source, pointer_for(pointer_for(pointer, "roles"), std::to_string(index)), "roles");
                }
                const auto text = role_items[index].get<std::string>();
                auto role_object = Json::object({{"role", text}});
                roles.push_back(enum_value<DatasetRole>(role_object, "role", pointer_for(pointer, "roles"), source, {{"bathymetry", DatasetRole::bathymetry}, {"topography", DatasetRole::topography}, {"earthquake_displacement", DatasetRole::earthquake_displacement}, {"prescribed_surface", DatasetRole::prescribed_surface}, {"manning", DatasetRole::manning}, {"coriolis", DatasetRole::coriolis}, {"observation", DatasetRole::observation}, {"auxiliary", DatasetRole::auxiliary}}));
            }
            const auto &asset_items = child(json, "assets", pointer, source);
            if (!asset_items.is_array()) {
                fail("data.dataset_manifest.field_type_invalid", "assets must be an array", source, pointer_for(pointer, "assets"), "assets");
            }
            auto assets = std::vector<DatasetAsset>{};
            for (std::size_t index = 0; index < asset_items.size(); ++index) {
                assets.push_back(parse_asset(asset_items[index], pointer_for(pointer_for(pointer, "assets"), std::to_string(index)), source));
            }
            const auto &source_json = child(json, "source", pointer, source);
            auto source_record = std::optional<SourceAcquisitionRecord>{};
            if (!source_json.is_null()) {
                source_record = parse_source(source_json, pointer_for(pointer, "source"), source);
            }
            return DatasetRecord{
                required<std::string>(json, "dataset_id", pointer, source, "string"),
                enum_value<DatasetOriginKind>(json, "origin_kind", pointer, source, {{"source", DatasetOriginKind::source}, {"generated", DatasetOriginKind::generated}}),
                enum_value<DatasetRepresentationKind>(json, "representation", pointer, source, {{"raster", DatasetRepresentationKind::raster}, {"vector", DatasetRepresentationKind::vector}, {"point_series", DatasetRepresentationKind::point_series}, {"table", DatasetRepresentationKind::table}, {"multidimensional", DatasetRepresentationKind::multidimensional}, {"other", DatasetRepresentationKind::other}}),
                std::move(roles),
                required<std::string>(json, "title", pointer, source, "string"),
                nullable<std::string>(json, "description", pointer, source, "string"),
                required<std::string>(json, "provider_id", pointer, source, "string"),
                required<std::string>(json, "licence_id", pointer, source, "string"),
                std::move(source_record),
                nullable<std::string>(json, "generated_by_process_id", pointer, source, "string"),
                std::move(assets),
                parse_spatial_reference(child(json, "spatial_reference", pointer, source), pointer_for(pointer, "spatial_reference"), source),
                parse_resolution(child(json, "resolution", pointer, source), pointer_for(pointer, "resolution"), source),
                parse_uncertainty(child(json, "uncertainty", pointer, source), pointer_for(pointer, "uncertainty"), source),
                nullable<std::string>(json, "citation", pointer, source, "string"),
                extensions(child(json, "extensions", pointer, source), pointer_for(pointer, "extensions"), source)};
        }

        [[nodiscard]] auto parse_software(const Json &json, const std::string &pointer, const std::string &source) -> ProcessingSoftware
        {
            reject_unknown(json, {"name", "version", "repository_uri", "commit_sha"}, pointer, source);
            return ProcessingSoftware{
                required<std::string>(json, "name", pointer, source, "string"),
                required<std::string>(json, "version", pointer, source, "string"),
                nullable<std::string>(json, "repository_uri", pointer, source, "string"),
                nullable<std::string>(json, "commit_sha", pointer, source, "string")};
        }

        [[nodiscard]] auto string_array(const Json &json, std::string_view key, const std::string &pointer, const std::string &source) -> std::vector<std::string>
        {
            const auto &items = child(json, key, pointer, source);
            if (!items.is_array()) {
                fail("data.dataset_manifest.field_type_invalid", "field must be an array", source, pointer_for(pointer, key), std::string{key});
            }
            auto values = std::vector<std::string>{};
            for (std::size_t index = 0; index < items.size(); ++index) {
                if (!items[index].is_string()) {
                    fail("data.dataset_manifest.field_type_invalid", "array item must be a string", source, pointer_for(pointer_for(pointer, key), std::to_string(index)), std::string{key});
                }
                values.push_back(items[index].get<std::string>());
            }
            return values;
        }

        [[nodiscard]] auto parse_process(const Json &json, const std::string &pointer, const std::string &source) -> ProcessingRecord
        {
            reject_unknown(json, {"process_id", "operation", "executed_at_utc", "software", "parameters", "input_dataset_ids", "output_dataset_ids", "extensions"}, pointer, source);
            const auto &parameters = child(json, "parameters", pointer, source);
            if (!parameters.is_object()) {
                fail("data.dataset_manifest.parameters_invalid", "processing parameters must be an object", source, pointer_for(pointer, "parameters"), "parameters");
            }
            return ProcessingRecord{
                required<std::string>(json, "process_id", pointer, source, "string"),
                required<std::string>(json, "operation", pointer, source, "string"),
                required<std::string>(json, "executed_at_utc", pointer, source, "string"),
                parse_software(child(json, "software", pointer, source), pointer_for(pointer, "software"), source),
                canonical_json(parameters),
                string_array(json, "input_dataset_ids", pointer, source),
                string_array(json, "output_dataset_ids", pointer, source),
                extensions(child(json, "extensions", pointer, source), pointer_for(pointer, "extensions"), source)};
        }

        template <class T, class Parser>
        [[nodiscard]] auto parse_array(const Json &root, std::string_view key, const std::string &source, Parser parser) -> std::vector<T>
        {
            const auto root_pointer = std::string{"/"};
            const auto &array = child(root, key, root_pointer, source);
            if (!array.is_array()) {
                fail("data.dataset_manifest.field_type_invalid", "field must be an array", source, pointer_for("/", key), std::string{key});
            }
            auto result = std::vector<T>{};
            for (std::size_t index = 0; index < array.size(); ++index) {
                result.push_back(parser(array[index], pointer_for(pointer_for("/", key), std::to_string(index)), source));
            }
            return result;
        }
    } // namespace

    auto parse_dataset_manifest(std::string_view document, std::string source_name) -> tsunami::core::Result<DatasetManifest>
    {
        if (document.empty()) {
            return tsunami::core::failure<DatasetManifest>(parse_error(
                "data.dataset_manifest.document_empty",
                "dataset manifest document is empty",
                source_name,
                "/"));
        }
        Json root;
        try {
            root = Json::parse(document.begin(), document.end());
        } catch (const nlohmann::json::parse_error &error) {
            auto diagnostic = parse_error(
                "data.dataset_manifest.json_invalid",
                "dataset manifest JSON is invalid",
                source_name);
            diagnostic.add_context("byte_offset", std::to_string(error.byte));
            return tsunami::core::failure<DatasetManifest>(std::move(diagnostic));
        }
        try {
            if (!root.is_object()) {
                fail("data.dataset_manifest.root_type_invalid", "dataset manifest root must be an object", source_name, "/");
            }
            reject_unknown(root, {"schema_version", "policy_version", "manifest", "providers", "licences", "datasets", "processes", "extensions"}, "/", source_name);
            const auto schema_text = required<std::string>(root, "schema_version", "/", source_name, "string");
            auto version = parse_dataset_manifest_version(schema_text);
            if (!version) {
                auto error = version.error();
                error.add_context("source_name", source_name);
                throw ParseFailure{std::move(error)};
            }
            const auto compatibility = classify_dataset_manifest_version(version.value());
            if (compatibility == DatasetManifestCompatibility::migration_required) {
                fail("data.dataset_manifest.migration_required", "legacy dataset manifest requires migration", source_name, "/schema_version", "schema_version", "manifest.schema_version.compatible");
            }
            if (compatibility == DatasetManifestCompatibility::unsupported_major) {
                fail("data.dataset_manifest.schema_major_unsupported", "dataset manifest major version is unsupported", source_name, "/schema_version", "schema_version", "manifest.schema_version.compatible");
            }
            auto manifest = make_dataset_manifest(
                SchemaIdentity{std::string{dataset_manifest_schema_name}, version.value()},
                compatibility,
                required<std::string>(root, "policy_version", "/", source_name, "string"),
                identity(child(root, "manifest", "/", source_name), source_name),
                parse_array<DatasetProvider>(root, "providers", source_name, parse_provider),
                parse_array<DatasetLicence>(root, "licences", source_name, parse_licence),
                parse_array<DatasetRecord>(root, "datasets", source_name, parse_dataset),
                parse_array<ProcessingRecord>(root, "processes", source_name, parse_process),
                extensions(child(root, "extensions", "/", source_name), "/extensions", source_name));
            if (!manifest) {
                auto error = manifest.error();
                error.add_context("source_name", source_name);
                return tsunami::core::failure<DatasetManifest>(std::move(error));
            }
            return manifest;
        } catch (const ParseFailure &failure) {
            return tsunami::core::failure<DatasetManifest>(failure.error);
        } catch (const std::exception &error) {
            auto diagnostic = parse_error(
                "data.dataset_manifest.field_type_invalid",
                "dataset manifest parsing failed before publication",
                source_name,
                "/");
            diagnostic.add_context("parser_detail", error.what());
            return tsunami::core::failure<DatasetManifest>(std::move(diagnostic));
        }
    }

    auto read_dataset_manifest(const std::filesystem::path &path) -> tsunami::core::Result<DatasetManifest>
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return tsunami::core::failure<DatasetManifest>(file_error("data.dataset_manifest.file_open_failed", "could not open dataset manifest", path));
        }
        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        if (size < 0) {
            return tsunami::core::failure<DatasetManifest>(file_error("data.dataset_manifest.file_read_failed", "could not determine dataset manifest size", path));
        }
        if (size == 0) {
            return tsunami::core::failure<DatasetManifest>(file_error("data.dataset_manifest.document_empty", "dataset manifest file is empty", path));
        }
        if (static_cast<std::uint64_t>(size) > max_dataset_manifest_bytes) {
            auto error = file_error("data.dataset_manifest.file_too_large", "dataset manifest exceeds the G1 size limit", path);
            error.add_context("expected", std::to_string(max_dataset_manifest_bytes))
                .add_context("actual", std::to_string(size));
            return tsunami::core::failure<DatasetManifest>(std::move(error));
        }
        file.seekg(0, std::ios::beg);
        std::string bytes(static_cast<std::size_t>(size), '\0');
        file.read(bytes.data(), size);
        if (!file && size > 0) {
            return tsunami::core::failure<DatasetManifest>(file_error("data.dataset_manifest.file_read_failed", "could not read complete dataset manifest", path));
        }
        return parse_dataset_manifest(bytes, path.generic_string());
    }

} // namespace tsunami::data
