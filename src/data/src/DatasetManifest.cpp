#include <tsunami/data/DatasetManifestValidation.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <tuple>

#include <nlohmann/json.hpp>

namespace tsunami::data
{
    namespace
    {
        constexpr auto max_logical_id_length = std::size_t{128U};

        [[nodiscard]] auto manifest_error(
            std::string code,
            std::string message,
            std::string rule_id,
            std::string pointer = {},
            std::string field = {}) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_dataset_manifest")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            if (!pointer.empty()) {
                error.add_context("json_pointer", std::move(pointer));
            }
            if (!field.empty()) {
                error.add_context("field", std::move(field));
            }
            return error;
        }

        [[nodiscard]] auto binding_error(
            std::string code,
            std::string message,
            std::string rule_id,
            std::string dataset_id = {},
            std::string expected = {},
            std::string actual = {}) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_dataset_manifest_for_case")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            if (!dataset_id.empty()) {
                error.add_context("dataset_id", std::move(dataset_id));
            }
            if (!expected.empty()) {
                error.add_context("expected", std::move(expected));
            }
            if (!actual.empty()) {
                error.add_context("actual", std::move(actual));
            }
            return error;
        }

        [[nodiscard]] auto version_error(std::string message) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                "data.dataset_manifest.schema_version_invalid",
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "parse_dataset_manifest_version")
                .add_context("rule_id", "manifest.schema_version.required")
                .add_context("json_pointer", "/schema_version")
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto has_embedded_null(std::string_view text) -> bool
        {
            return text.find('\0') != std::string_view::npos;
        }

        [[nodiscard]] auto has_control(std::string_view text) -> bool
        {
            return std::any_of(text.begin(), text.end(), [](unsigned char ch) { return ch < 0x20U; });
        }

        [[nodiscard]] auto matches(std::string_view text, const char *pattern) -> bool
        {
            return std::regex_match(text.begin(), text.end(), std::regex{pattern});
        }

        [[nodiscard]] auto logical_id_valid(std::string_view text) -> bool
        {
            return !text.empty() && text.size() <= max_logical_id_length && !has_embedded_null(text) &&
                   matches(text, R"([a-z0-9]+(?:[._-][a-z0-9]+)*)");
        }

        [[nodiscard]] auto text_present(const std::string &text) -> bool
        {
            return !text.empty() && !has_embedded_null(text);
        }

        [[nodiscard]] auto optional_text_present(const std::optional<std::string> &text) -> bool
        {
            return !text || text_present(*text);
        }

        [[nodiscard]] auto finite(double value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto leap_year(int year) -> bool
        {
            return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        }

        [[nodiscard]] auto date_valid(std::string_view text) -> bool
        {
            static const auto re = std::regex{R"(^(\d{4})-(\d{2})-(\d{2})$)"};
            std::cmatch match;
            if (!std::regex_match(text.begin(), text.end(), match, re)) {
                return false;
            }
            const auto year = std::stoi(match[1].str());
            const auto month = std::stoi(match[2].str());
            const auto day = std::stoi(match[3].str());
            if (month < 1 || month > 12) {
                return false;
            }
            const auto days = std::array<int, 12>{31, leap_year(year) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return day >= 1 && day <= days[static_cast<std::size_t>(month - 1)];
        }

        [[nodiscard]] auto timestamp_valid(std::string_view text) -> bool
        {
            static const auto re = std::regex{R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})Z$)"};
            std::cmatch match;
            if (!std::regex_match(text.begin(), text.end(), match, re)) {
                return false;
            }
            if (!date_valid(match[1].str() + "-" + match[2].str() + "-" + match[3].str())) {
                return false;
            }
            const auto hour = std::stoi(match[4].str());
            const auto minute = std::stoi(match[5].str());
            const auto second = std::stoi(match[6].str());
            return hour <= 23 && minute <= 59 && second <= 59;
        }

        [[nodiscard]] auto uri_safe(std::string_view text) -> bool
        {
            if (text.empty() || has_control(text)) {
                return false;
            }
            const auto value = std::string{text};
            const auto colon = value.find(':');
            if (colon == std::string::npos || colon == 0U || !matches(std::string_view{value}.substr(0U, colon), R"([A-Za-z][A-Za-z0-9+.-]*)")) {
                return false;
            }
            const auto authority = value.find("//", colon + 1U);
            if (authority == colon + 1U) {
                const auto start = authority + 2U;
                const auto end = value.find_first_of("/?#", start);
                const auto host = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (host.empty() || host.find('@') != std::string::npos) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] auto managed_path_safe(const std::filesystem::path &path) -> bool
        {
            const auto generic = path.generic_string();
            if (generic.empty() || has_embedded_null(generic) || path.is_absolute() || path.has_root_name() ||
                path.has_root_directory() || path.filename().empty()) {
                return false;
            }
            if (generic.find('\\') != std::string::npos || generic.rfind("inputs/data/", 0U) != 0U) {
                return false;
            }
            for (const auto &part : path) {
                const auto piece = part.generic_string();
                if (piece.empty() || piece == "." || piece == "..") {
                    return false;
                }
            }
            return path.lexically_normal().generic_string() == generic;
        }

        [[nodiscard]] auto sha256_valid(std::string_view text) -> bool
        {
            return matches(text, R"([0-9a-f]{64})");
        }

        [[nodiscard]] auto media_type_valid(std::string_view text) -> bool
        {
            return matches(text, R"([A-Za-z0-9!#$&^_.+-]+/[A-Za-z0-9!#$&^_.+-]+)");
        }

        [[nodiscard]] auto contains_role(const DatasetRecord &dataset, DatasetRole role) -> bool
        {
            return std::find(dataset.roles.begin(), dataset.roles.end(), role) != dataset.roles.end();
        }

        [[nodiscard]] auto requires_vertical(DatasetRole role) -> bool
        {
            return role == DatasetRole::bathymetry || role == DatasetRole::topography ||
                   role == DatasetRole::earthquake_displacement || role == DatasetRole::prescribed_surface;
        }

        [[nodiscard]] auto requires_spatial(DatasetRole role) -> bool
        {
            return role != DatasetRole::auxiliary;
        }

        [[nodiscard]] auto extension_json_valid(std::string_view text) -> bool
        {
            try {
                const auto parsed = nlohmann::json::parse(text.begin(), text.end());
                static_cast<void>(parsed);
            } catch (const nlohmann::json::exception &) {
                return false;
            }
            return true;
        }

        auto validate_extensions(const DatasetManifestExtensions &extensions, std::string pointer) -> tsunami::core::Result<void>
        {
            auto names = std::set<std::string>{};
            for (const auto &extension : extensions.values) {
                if (extension.name.empty() || has_embedded_null(extension.name)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.dataset_invalid", "extension name is invalid", "manifest.extensions.unique", pointer));
                }
                if (!names.insert(extension.name).second) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.dataset_invalid", "extension names must be unique", "manifest.extensions.unique", pointer));
                }
                if (!extension_json_valid(extension.canonical_json)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.dataset_invalid", "extension JSON is invalid", "manifest.extensions.canonical", pointer + "/" + extension.name));
                }
            }
            return tsunami::core::success();
        }

        auto validate_spatial_reference(const DatasetRecord &dataset) -> tsunami::core::Result<void>
        {
            const auto &spatial = dataset.spatial_reference;
            const auto any_spatial_role = std::any_of(dataset.roles.begin(), dataset.roles.end(), requires_spatial);
            const auto any_vertical_role = std::any_of(dataset.roles.begin(), dataset.roles.end(), requires_vertical);
            if (any_spatial_role && spatial.applicability != SpatialApplicability::spatial) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.spatial_reference_invalid", "dataset role requires spatial metadata", "manifest.spatial.applicability.consistent", "/datasets"));
            }
            if (spatial.applicability == SpatialApplicability::not_applicable) {
                if (spatial.horizontal_crs || spatial.vertical_datum || spatial.horizontal_unit || spatial.vertical_unit || spatial.axis_order || spatial.vertical_positive) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.spatial_reference_invalid", "not-applicable spatial metadata must be null", "manifest.spatial.applicability.consistent", "/datasets"));
                }
                return tsunami::core::success();
            }
            if (!optional_text_present(spatial.horizontal_crs) || !optional_text_present(spatial.horizontal_unit) ||
                !optional_text_present(spatial.axis_order) || !spatial.horizontal_crs || !spatial.horizontal_unit || !spatial.axis_order) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.spatial_reference_invalid", "spatial dataset requires horizontal CRS, unit and axis order", "manifest.spatial.horizontal.required", "/datasets"));
            }
            if (any_vertical_role && (!optional_text_present(spatial.vertical_datum) || !optional_text_present(spatial.vertical_unit) ||
                                     !optional_text_present(spatial.vertical_positive) || !spatial.vertical_datum || !spatial.vertical_unit || !spatial.vertical_positive)) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.spatial_reference_invalid", "dataset role requires vertical metadata", "manifest.spatial.vertical.required", "/datasets"));
            }
            return tsunami::core::success();
        }

        auto validate_resolution(const DatasetRecord &dataset) -> tsunami::core::Result<void>
        {
            const auto &spatial = dataset.resolution.spatial;
            if (dataset.representation == DatasetRepresentationKind::raster && spatial.kind == SpatialResolutionKind::not_applicable) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.resolution_invalid", "raster datasets require applicable spatial resolution", "manifest.resolution.spatial.consistent", "/datasets"));
            }
            if (spatial.kind == SpatialResolutionKind::grid_spacing) {
                if (!spatial.x || !spatial.y || !finite(*spatial.x) || !finite(*spatial.y) || *spatial.x <= 0.0 || *spatial.y <= 0.0 ||
                    !spatial.unit || !text_present(*spatial.unit)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.resolution_invalid", "grid spacing requires positive x/y and unit", "manifest.resolution.spatial.consistent", "/datasets"));
                }
            } else if (spatial.kind == SpatialResolutionKind::nominal) {
                if ((!spatial.x || !finite(*spatial.x) || *spatial.x <= 0.0) && (!spatial.y || !finite(*spatial.y) || *spatial.y <= 0.0)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.resolution_invalid", "nominal spatial resolution requires a positive value", "manifest.resolution.spatial.consistent", "/datasets"));
                }
                if (!spatial.unit || !text_present(*spatial.unit)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.resolution_invalid", "nominal spatial resolution requires a unit", "manifest.resolution.spatial.consistent", "/datasets"));
                }
            } else if (spatial.kind == SpatialResolutionKind::irregular) {
                if (spatial.x || spatial.y || spatial.unit || !spatial.description || !text_present(*spatial.description)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.resolution_invalid", "irregular spatial resolution requires description only", "manifest.resolution.spatial.consistent", "/datasets"));
                }
            } else if (spatial.x || spatial.y || spatial.unit) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.resolution_invalid", "not-reported spatial resolution cannot carry numeric values or unit", "manifest.resolution.spatial.consistent", "/datasets"));
            }

            const auto &temporal = dataset.resolution.temporal;
            if (temporal.kind == TemporalResolutionKind::interval) {
                if (!temporal.value || !finite(*temporal.value) || *temporal.value <= 0.0 || !temporal.unit || !text_present(*temporal.unit)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.resolution_invalid", "temporal interval requires positive value and unit", "manifest.resolution.temporal.consistent", "/datasets"));
                }
            } else if (temporal.kind == TemporalResolutionKind::irregular) {
                if (temporal.value || temporal.unit || !temporal.description || !text_present(*temporal.description)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.resolution_invalid", "irregular temporal resolution requires description only", "manifest.resolution.temporal.consistent", "/datasets"));
                }
            } else if (temporal.value || temporal.unit) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.resolution_invalid", "temporal resolution status cannot carry value or unit", "manifest.resolution.temporal.consistent", "/datasets"));
            }
            return tsunami::core::success();
        }

        auto validate_uncertainty(const DatasetRecord &dataset) -> tsunami::core::Result<void>
        {
            const auto &uncertainty = dataset.uncertainty;
            if (uncertainty.status == UncertaintyStatus::reported || uncertainty.status == UncertaintyStatus::estimated) {
                if (uncertainty.measures.empty() && (!uncertainty.description || !text_present(*uncertainty.description))) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.uncertainty_invalid", "reported or estimated uncertainty requires measures or description", "manifest.uncertainty.consistent", "/datasets"));
                }
            } else if (!uncertainty.measures.empty()) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.uncertainty_invalid", "not-reported uncertainty must not carry measures", "manifest.uncertainty.consistent", "/datasets"));
            }
            for (const auto &measure : uncertainty.measures) {
                if (!text_present(measure.quantity) || !finite(measure.value) || !text_present(measure.unit)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.uncertainty_invalid", "uncertainty measure requires quantity, finite value and unit", "manifest.uncertainty.consistent", "/datasets"));
                }
                if (measure.confidence_level && (!finite(*measure.confidence_level) || *measure.confidence_level <= 0.0 || *measure.confidence_level > 1.0)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.uncertainty_invalid", "confidence level is out of range", "manifest.uncertainty.consistent", "/datasets"));
                }
            }
            return tsunami::core::success();
        }

        auto validate_asset(const DatasetRecord &dataset, const DatasetAsset &asset, std::set<std::string> &asset_ids, std::uint64_t &primary_count)
            -> tsunami::core::Result<void>
        {
            if (!logical_id_valid(asset.asset_id) || !asset_ids.insert(asset.asset_id).second) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.asset_invalid", "asset id is invalid or duplicate", "manifest.asset.id.unique", "/datasets/assets"));
            }
            if (asset.role == DatasetAssetRole::primary) {
                ++primary_count;
            }
            if (!media_type_valid(asset.media_type) || (asset.byte_size && *asset.byte_size == 0U)) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.asset_invalid", "asset media type or byte size is invalid", "manifest.asset.location.consistent", "/datasets/assets"));
            }
            if (asset.location.kind == DatasetLocationKind::managed_path) {
                if (!asset.location.managed_path || asset.location.external_uri || !managed_path_safe(*asset.location.managed_path)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.path_invalid", "managed asset path is unsafe", "manifest.asset.managed_path.safe", "/datasets/assets/location"));
                }
            } else if (!asset.location.external_uri || asset.location.managed_path || !uri_safe(*asset.location.external_uri)) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.uri_invalid", "external asset URI is invalid", "manifest.asset.location.consistent", "/datasets/assets/location"));
            }
            if (asset.digest.algorithm != DigestAlgorithm::sha256 || !sha256_valid(asset.digest.value)) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.digest_invalid", "asset digest must be lower-case SHA-256", "manifest.asset.digest.sha256", "/datasets/assets/digest"));
            }
            if (dataset.origin_kind == DatasetOriginKind::generated) {
                if (asset.digest.origin != DigestOrigin::project_computed) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.digest_invalid", "generated assets require project-computed digest", "manifest.asset.generated.project_digest", "/datasets/assets/digest"));
                }
                if (asset.location.kind != DatasetLocationKind::managed_path) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.asset_invalid", "generated assets require managed paths", "manifest.asset.location.consistent", "/datasets/assets/location"));
                }
            }
            return tsunami::core::success();
        }

        auto validate_dataset_record(const DatasetRecord &dataset) -> tsunami::core::Result<void>
        {
            if (!logical_id_valid(dataset.dataset_id)) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.dataset_invalid", "dataset id is invalid", "manifest.dataset.id.unique", "/datasets/dataset_id"));
            }
            if (dataset.roles.empty()) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.role_invalid", "dataset roles must be nonempty", "manifest.dataset.roles.nonempty", "/datasets/roles"));
            }
            auto roles = std::set<DatasetRole>{};
            for (const auto role : dataset.roles) {
                if (!roles.insert(role).second) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.role_invalid", "dataset roles must be unique", "manifest.dataset.roles.nonempty", "/datasets/roles"));
                }
            }
            if (!text_present(dataset.title) || !logical_id_valid(dataset.provider_id) || !logical_id_valid(dataset.licence_id) ||
                !optional_text_present(dataset.description) || !optional_text_present(dataset.citation)) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.dataset_invalid", "dataset title or references are invalid", "manifest.dataset.provider.exists", "/datasets"));
            }
            if (dataset.origin_kind == DatasetOriginKind::source) {
                if (!dataset.source || dataset.generated_by_process_id) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.dataset_invalid", "source dataset origin fields are inconsistent", "manifest.dataset.origin.consistent", "/datasets"));
                }
                if (!uri_safe(dataset.source->source_uri) || !timestamp_valid(dataset.source->accessed_at_utc) ||
                    !optional_text_present(dataset.source->source_version) ||
                    (dataset.source->publication_date && !date_valid(*dataset.source->publication_date))) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.source_invalid", "source acquisition metadata is invalid", "manifest.dataset.origin.consistent", "/datasets/source"));
                }
            } else if (dataset.source || !dataset.generated_by_process_id || !logical_id_valid(*dataset.generated_by_process_id)) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.dataset_invalid", "generated dataset origin fields are inconsistent", "manifest.dataset.origin.consistent", "/datasets"));
            }
            auto asset_ids = std::set<std::string>{};
            auto primary_count = std::uint64_t{};
            for (const auto &asset : dataset.assets) {
                auto valid = validate_asset(dataset, asset, asset_ids, primary_count);
                if (!valid) {
                    return valid;
                }
            }
            if (primary_count != 1U) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.asset_invalid", "dataset requires exactly one primary asset", "manifest.asset.primary.exactly_one", "/datasets/assets"));
            }
            if (auto spatial = validate_spatial_reference(dataset); !spatial) {
                return spatial;
            }
            if (auto resolution = validate_resolution(dataset); !resolution) {
                return resolution;
            }
            if (auto uncertainty = validate_uncertainty(dataset); !uncertainty) {
                return uncertainty;
            }
            return validate_extensions(dataset.extensions, "/datasets/extensions");
        }

        auto validate_process_record(const ProcessingRecord &process) -> tsunami::core::Result<void>
        {
            if (!logical_id_valid(process.process_id) || !logical_id_valid(process.operation) || !timestamp_valid(process.executed_at_utc)) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.process_invalid", "process identity is invalid", "manifest.process.id.unique", "/processes"));
            }
            if (!text_present(process.software.name) || !text_present(process.software.version) ||
                (process.software.repository_uri && !uri_safe(*process.software.repository_uri)) ||
                (process.software.commit_sha && !matches(*process.software.commit_sha, R"([0-9a-f]{7,64})"))) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.process_invalid", "processing software metadata is invalid", "manifest.process.id.unique", "/processes/software"));
            }
            try {
                const auto parameters = nlohmann::json::parse(process.canonical_parameters_json.begin(), process.canonical_parameters_json.end());
                if (!parameters.is_object()) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.parameters_invalid", "processing parameters must be a JSON object", "manifest.process.parameters.object", "/processes/parameters"));
                }
            } catch (const nlohmann::json::exception &) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.parameters_invalid", "processing parameters are invalid JSON", "manifest.process.parameters.object", "/processes/parameters"));
            }
            if (process.input_dataset_ids.empty()) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.process_invalid", "process inputs must be nonempty", "manifest.process.inputs.nonempty", "/processes/input_dataset_ids"));
            }
            if (process.output_dataset_ids.empty()) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.process_invalid", "process outputs must be nonempty", "manifest.process.outputs.nonempty", "/processes/output_dataset_ids"));
            }
            auto inputs = std::set<std::string>{};
            for (const auto &id : process.input_dataset_ids) {
                if (!logical_id_valid(id) || !inputs.insert(id).second) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.process_invalid", "process input ids must be valid and unique", "manifest.process.references.exist", "/processes/input_dataset_ids"));
                }
            }
            auto outputs = std::set<std::string>{};
            for (const auto &id : process.output_dataset_ids) {
                if (!logical_id_valid(id) || !outputs.insert(id).second) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.process_invalid", "process output ids must be valid and unique", "manifest.process.references.exist", "/processes/output_dataset_ids"));
                }
                if (inputs.contains(id)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.process_invalid", "process input and output ids must be disjoint", "manifest.process.input_output.disjoint", "/processes"));
                }
            }
            return validate_extensions(process.extensions, "/processes/extensions");
        }

        template <class T, class Id>
        [[nodiscard]] auto duplicate_id(const std::vector<T> &records, Id id) -> std::optional<std::string>
        {
            auto seen = std::set<std::string>{};
            for (const auto &record : records) {
                const auto &value = id(record);
                if (!seen.insert(value).second) {
                    return value;
                }
            }
            return std::nullopt;
        }

        auto validate_references_and_lineage(const DatasetManifest &manifest) -> tsunami::core::Result<void>
        {
            if (manifest.providers().empty() || manifest.licences().empty() || manifest.datasets().empty()) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.required_field_missing", "providers, licences and datasets must be nonempty", "manifest.dataset.provider.exists", "/"));
            }
            if (auto duplicate = duplicate_id(manifest.providers(), [](const DatasetProvider &p) -> const std::string & { return p.provider_id; })) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.provider_invalid", "provider ids must be unique", "manifest.provider.id.unique", "/providers", "provider_id").add_context("provider_id", *duplicate));
            }
            if (auto duplicate = duplicate_id(manifest.licences(), [](const DatasetLicence &l) -> const std::string & { return l.licence_id; })) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.licence_invalid", "licence ids must be unique", "manifest.licence.id.unique", "/licences", "licence_id").add_context("licence_id", *duplicate));
            }
            if (auto duplicate = duplicate_id(manifest.datasets(), [](const DatasetRecord &d) -> const std::string & { return d.dataset_id; })) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.dataset_invalid", "dataset ids must be unique", "manifest.dataset.id.unique", "/datasets", "dataset_id").add_context("dataset_id", *duplicate));
            }
            if (auto duplicate = duplicate_id(manifest.processes(), [](const ProcessingRecord &p) -> const std::string & { return p.process_id; })) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.process_invalid", "process ids must be unique", "manifest.process.id.unique", "/processes", "process_id").add_context("process_id", *duplicate));
            }

            auto providers = std::set<std::string>{};
            for (const auto &provider : manifest.providers()) {
                providers.insert(provider.provider_id);
            }
            auto licences = std::set<std::string>{};
            for (const auto &licence : manifest.licences()) {
                licences.insert(licence.licence_id);
            }
            auto datasets = std::map<std::string, const DatasetRecord *>{};
            for (const auto &dataset : manifest.datasets()) {
                datasets.emplace(dataset.dataset_id, &dataset);
                if (!providers.contains(dataset.provider_id)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.reference_missing", "dataset provider reference is missing", "manifest.dataset.provider.exists", "/datasets/provider_id").add_context("dataset_id", dataset.dataset_id).add_context("provider_id", dataset.provider_id));
                }
                if (!licences.contains(dataset.licence_id)) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.reference_missing", "dataset licence reference is missing", "manifest.dataset.licence.exists", "/datasets/licence_id").add_context("dataset_id", dataset.dataset_id).add_context("licence_id", dataset.licence_id));
                }
            }

            auto processes = std::map<std::string, const ProcessingRecord *>{};
            auto producers = std::map<std::string, std::string>{};
            for (const auto &process : manifest.processes()) {
                processes.emplace(process.process_id, &process);
                for (const auto &id : process.input_dataset_ids) {
                    if (!datasets.contains(id)) {
                        return tsunami::core::failure(manifest_error("data.dataset_manifest.reference_missing", "process input dataset is missing", "manifest.process.references.exist", "/processes/input_dataset_ids").add_context("process_id", process.process_id).add_context("dataset_id", id));
                    }
                }
                for (const auto &id : process.output_dataset_ids) {
                    auto found = datasets.find(id);
                    if (found == datasets.end()) {
                        return tsunami::core::failure(manifest_error("data.dataset_manifest.reference_missing", "process output dataset is missing", "manifest.process.references.exist", "/processes/output_dataset_ids").add_context("process_id", process.process_id).add_context("dataset_id", id));
                    }
                    if (found->second->origin_kind != DatasetOriginKind::generated) {
                        return tsunami::core::failure(manifest_error("data.dataset_manifest.dataset_invalid", "process output must be a generated dataset", "manifest.lineage.source.not_output", "/processes/output_dataset_ids").add_context("process_id", process.process_id).add_context("dataset_id", id));
                    }
                    if (!producers.emplace(id, process.process_id).second) {
                        return tsunami::core::failure(manifest_error("data.dataset_manifest.multiple_producers", "generated dataset has multiple producers", "manifest.lineage.generated.single_producer", "/processes/output_dataset_ids").add_context("dataset_id", id));
                    }
                }
            }

            for (const auto &dataset : manifest.datasets()) {
                if (dataset.origin_kind == DatasetOriginKind::source) {
                    if (producers.contains(dataset.dataset_id)) {
                        return tsunami::core::failure(manifest_error("data.dataset_manifest.dataset_invalid", "source dataset cannot be a process output", "manifest.lineage.source.not_output", "/datasets"));
                    }
                    continue;
                }
                auto producer = producers.find(dataset.dataset_id);
                if (producer == producers.end()) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.lineage_incomplete", "generated dataset has no producer", "manifest.lineage.generated.single_producer", "/datasets/generated_by_process_id").add_context("dataset_id", dataset.dataset_id));
                }
                if (!dataset.generated_by_process_id || *dataset.generated_by_process_id != producer->second) {
                    return tsunami::core::failure(manifest_error("data.dataset_manifest.lineage_incomplete", "generated dataset producer does not agree with process output", "manifest.lineage.generated.producer_agrees", "/datasets/generated_by_process_id").add_context("dataset_id", dataset.dataset_id).add_context("process_id", producer->second));
                }
            }

            auto lineage_state = std::map<std::string, int>{};
            auto source_rooted = std::map<std::string, bool>{};
            auto lineage_failure = std::optional<tsunami::core::Error>{};
            auto reaches_source = std::function<bool(const std::string &, std::size_t)>{};
            reaches_source = [&](const std::string &dataset_id, std::size_t depth) -> bool {
                if (lineage_failure.has_value()) {
                    return false;
                }
                if (depth > datasets.size()) {
                    lineage_failure = manifest_error("data.dataset_manifest.lineage_cycle", "lineage traversal exceeded graph bounds", "manifest.lineage.acyclic", "/processes").add_context("dataset_id", dataset_id);
                    return false;
                }
                const auto current_dataset = datasets.at(dataset_id);
                if (current_dataset->origin_kind == DatasetOriginKind::source) {
                    source_rooted[dataset_id] = true;
                    return true;
                }
                const auto state = lineage_state[dataset_id];
                if (state == 1) {
                    lineage_failure = manifest_error("data.dataset_manifest.lineage_cycle", "generated lineage contains a cycle", "manifest.lineage.acyclic", "/processes").add_context("dataset_id", dataset_id);
                    return false;
                }
                if (state == 2) {
                    return source_rooted[dataset_id];
                }

                lineage_state[dataset_id] = 1;
                const auto producer = current_dataset->generated_by_process_id ? processes.find(*current_dataset->generated_by_process_id) : processes.end();
                if (producer == processes.end()) {
                    lineage_failure = manifest_error("data.dataset_manifest.lineage_incomplete", "generated lineage producer is missing", "manifest.lineage.source_rooted", "/datasets/generated_by_process_id").add_context("dataset_id", dataset_id);
                    return false;
                }

                auto has_source = false;
                for (const auto &input : producer->second->input_dataset_ids) {
                    has_source = reaches_source(input, depth + 1U) || has_source;
                }
                lineage_state[dataset_id] = 2;
                source_rooted[dataset_id] = has_source;
                if (!has_source && !lineage_failure.has_value()) {
                    lineage_failure = manifest_error("data.dataset_manifest.lineage_incomplete", "generated lineage is not source-rooted", "manifest.lineage.source_rooted", "/datasets").add_context("dataset_id", dataset_id);
                }
                return has_source;
            };

            for (const auto &dataset : manifest.datasets()) {
                if (dataset.origin_kind == DatasetOriginKind::generated) {
                    static_cast<void>(reaches_source(dataset.dataset_id, 0U));
                    if (lineage_failure.has_value()) {
                        return tsunami::core::failure(*lineage_failure);
                    }
                }
            }
            return tsunami::core::success();
        }

        auto sort_extensions(DatasetManifestExtensions &extensions) -> void
        {
            std::sort(extensions.values.begin(), extensions.values.end(), [](const auto &left, const auto &right) { return left.name < right.name; });
        }

        auto normalise(DatasetManifest &manifest) -> void
        {
            static_cast<void>(manifest);
        }

        auto role_rank(DatasetRole role) -> std::string_view
        {
            return to_string(role);
        }
    } // namespace

    auto to_string(DatasetManifestCompatibility compatibility) noexcept -> std::string_view
    {
        switch (compatibility) {
        case DatasetManifestCompatibility::exact:
            return "exact";
        case DatasetManifestCompatibility::patch_equivalent:
            return "patch_equivalent";
        case DatasetManifestCompatibility::forward_compatible_minor:
            return "forward_compatible_minor";
        case DatasetManifestCompatibility::migration_required:
            return "migration_required";
        case DatasetManifestCompatibility::unsupported_major:
            return "unsupported_major";
        }
        return "unsupported_major";
    }

    auto to_string(DatasetRole role) noexcept -> std::string_view
    {
        switch (role) {
        case DatasetRole::bathymetry:
            return "bathymetry";
        case DatasetRole::topography:
            return "topography";
        case DatasetRole::earthquake_displacement:
            return "earthquake_displacement";
        case DatasetRole::prescribed_surface:
            return "prescribed_surface";
        case DatasetRole::manning:
            return "manning";
        case DatasetRole::coriolis:
            return "coriolis";
        case DatasetRole::observation:
            return "observation";
        case DatasetRole::auxiliary:
            return "auxiliary";
        }
        return "auxiliary";
    }

    auto to_string(DatasetOriginKind kind) noexcept -> std::string_view { return kind == DatasetOriginKind::source ? "source" : "generated"; }
    auto to_string(DatasetLocationKind kind) noexcept -> std::string_view { return kind == DatasetLocationKind::managed_path ? "managed_path" : "external_uri"; }
    auto to_string(DigestAlgorithm) noexcept -> std::string_view { return "sha256"; }

    auto to_string(DatasetRepresentationKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case DatasetRepresentationKind::raster:
            return "raster";
        case DatasetRepresentationKind::vector:
            return "vector";
        case DatasetRepresentationKind::point_series:
            return "point_series";
        case DatasetRepresentationKind::table:
            return "table";
        case DatasetRepresentationKind::multidimensional:
            return "multidimensional";
        case DatasetRepresentationKind::other:
            return "other";
        }
        return "other";
    }

    auto to_string(DatasetAssetRole role) noexcept -> std::string_view
    {
        switch (role) {
        case DatasetAssetRole::primary:
            return "primary";
        case DatasetAssetRole::metadata:
            return "metadata";
        case DatasetAssetRole::auxiliary:
            return "auxiliary";
        }
        return "auxiliary";
    }

    auto to_string(DigestOrigin origin) noexcept -> std::string_view
    {
        return origin == DigestOrigin::provider_declared ? "provider_declared" : "project_computed";
    }

    auto to_string(SpatialApplicability applicability) noexcept -> std::string_view
    {
        return applicability == SpatialApplicability::spatial ? "spatial" : "not_applicable";
    }

    auto to_string(SpatialResolutionKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case SpatialResolutionKind::grid_spacing:
            return "grid_spacing";
        case SpatialResolutionKind::nominal:
            return "nominal";
        case SpatialResolutionKind::irregular:
            return "irregular";
        case SpatialResolutionKind::not_reported:
            return "not_reported";
        case SpatialResolutionKind::not_applicable:
            return "not_applicable";
        }
        return "not_reported";
    }

    auto to_string(TemporalResolutionKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case TemporalResolutionKind::static_dataset:
            return "static_dataset";
        case TemporalResolutionKind::interval:
            return "interval";
        case TemporalResolutionKind::irregular:
            return "irregular";
        case TemporalResolutionKind::not_reported:
            return "not_reported";
        case TemporalResolutionKind::not_applicable:
            return "not_applicable";
        }
        return "not_applicable";
    }

    auto to_string(UncertaintyStatus status) noexcept -> std::string_view
    {
        switch (status) {
        case UncertaintyStatus::reported:
            return "reported";
        case UncertaintyStatus::estimated:
            return "estimated";
        case UncertaintyStatus::not_reported:
            return "not_reported";
        case UncertaintyStatus::not_applicable:
            return "not_applicable";
        }
        return "not_reported";
    }

    DatasetManifest::DatasetManifest(
        SchemaIdentity schema,
        DatasetManifestCompatibility compatibility,
        std::string policy_version,
        DatasetManifestIdentity identity,
        std::vector<DatasetProvider> providers,
        std::vector<DatasetLicence> licences,
        std::vector<DatasetRecord> datasets,
        std::vector<ProcessingRecord> processes,
        DatasetManifestExtensions extensions)
        : schema_{std::move(schema)}
        , compatibility_{compatibility}
        , policy_version_{std::move(policy_version)}
        , identity_{std::move(identity)}
        , providers_{std::move(providers)}
        , licences_{std::move(licences)}
        , datasets_{std::move(datasets)}
        , processes_{std::move(processes)}
        , extensions_{std::move(extensions)}
    {
    }

    auto DatasetManifest::find_provider(std::string_view provider_id) const noexcept -> const DatasetProvider *
    {
        const auto found = std::find_if(providers_.begin(), providers_.end(), [&](const auto &provider) { return provider.provider_id == provider_id; });
        return found == providers_.end() ? nullptr : std::addressof(*found);
    }

    auto DatasetManifest::find_licence(std::string_view licence_id) const noexcept -> const DatasetLicence *
    {
        const auto found = std::find_if(licences_.begin(), licences_.end(), [&](const auto &licence) { return licence.licence_id == licence_id; });
        return found == licences_.end() ? nullptr : std::addressof(*found);
    }

    auto DatasetManifest::find_dataset(std::string_view dataset_id) const noexcept -> const DatasetRecord *
    {
        const auto found = std::find_if(datasets_.begin(), datasets_.end(), [&](const auto &dataset) { return dataset.dataset_id == dataset_id; });
        return found == datasets_.end() ? nullptr : std::addressof(*found);
    }

    auto DatasetManifest::find_process(std::string_view process_id) const noexcept -> const ProcessingRecord *
    {
        const auto found = std::find_if(processes_.begin(), processes_.end(), [&](const auto &process) { return process.process_id == process_id; });
        return found == processes_.end() ? nullptr : std::addressof(*found);
    }

    auto parse_dataset_manifest_version(std::string_view text) -> tsunami::core::Result<tsunami::core::SemanticVersion>
    {
        auto values = std::array<std::uint32_t, 3>{};
        auto begin = text.data();
        const auto *const end = text.data() + text.size();
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (begin == end || *begin < '0' || *begin > '9') {
                return tsunami::core::failure<tsunami::core::SemanticVersion>(version_error("semantic version component is missing"));
            }
            std::uint64_t parsed{};
            const auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec != std::errc{} || parsed > std::numeric_limits<std::uint32_t>::max()) {
                return tsunami::core::failure<tsunami::core::SemanticVersion>(version_error("semantic version component is invalid or overflows"));
            }
            values[index] = static_cast<std::uint32_t>(parsed);
            begin = ptr;
            if (index + 1U < values.size()) {
                if (begin == end || *begin != '.') {
                    return tsunami::core::failure<tsunami::core::SemanticVersion>(version_error("semantic version requires three dot-separated components"));
                }
                ++begin;
            }
        }
        if (begin != end) {
            return tsunami::core::failure<tsunami::core::SemanticVersion>(version_error("semantic version contains trailing characters"));
        }
        return tsunami::core::success(tsunami::core::SemanticVersion{values[0], values[1], values[2]});
    }

    auto classify_dataset_manifest_version(const tsunami::core::SemanticVersion &version) noexcept -> DatasetManifestCompatibility
    {
        if (version.major == supported_dataset_manifest_version.major &&
            version.minor == supported_dataset_manifest_version.minor &&
            version.patch == supported_dataset_manifest_version.patch) {
            return DatasetManifestCompatibility::exact;
        }
        if (version.major == 0U) {
            return DatasetManifestCompatibility::migration_required;
        }
        if (version.major > supported_dataset_manifest_version.major) {
            return DatasetManifestCompatibility::unsupported_major;
        }
        if (version.minor == supported_dataset_manifest_version.minor) {
            return DatasetManifestCompatibility::patch_equivalent;
        }
        return DatasetManifestCompatibility::forward_compatible_minor;
    }

    auto validate_dataset_manifest(const DatasetManifest &manifest) -> tsunami::core::Result<void>
    {
        if (manifest.schema_identity().schema_name != dataset_manifest_schema_name) {
            return tsunami::core::failure(manifest_error("data.dataset_manifest.schema_version_invalid", "schema name is unsupported", "manifest.schema_version.required", "/schema_version"));
        }
        const auto compatibility = classify_dataset_manifest_version(manifest.schema_identity().version);
        if (compatibility == DatasetManifestCompatibility::migration_required) {
            return tsunami::core::failure(manifest_error("data.dataset_manifest.migration_required", "legacy manifest schema requires migration", "manifest.schema_version.compatible", "/schema_version"));
        }
        if (compatibility == DatasetManifestCompatibility::unsupported_major) {
            return tsunami::core::failure(manifest_error("data.dataset_manifest.schema_major_unsupported", "manifest schema major version is unsupported", "manifest.schema_version.compatible", "/schema_version"));
        }
        if (manifest.compatibility() != compatibility) {
            return tsunami::core::failure(manifest_error("data.dataset_manifest.schema_version_invalid", "stored compatibility does not match schema version", "manifest.schema_version.compatible", "/schema_version"));
        }
        if (manifest.policy_version() != supported_dataset_manifest_policy_version) {
            return tsunami::core::failure(manifest_error("data.dataset_manifest.policy_version_invalid", "manifest policy version is unsupported", "manifest.policy_version.supported", "/policy_version"));
        }
        const auto &identity = manifest.identity();
        if (!logical_id_valid(identity.manifest_id)) {
            return tsunami::core::failure(manifest_error("data.dataset_manifest.identity_invalid", "manifest id is invalid", "manifest.identity.manifest_id.valid", "/manifest/manifest_id"));
        }
        if (identity.manifest_revision == 0U || !identity.case_revision.case_id || identity.case_revision.revision == 0U ||
            !timestamp_valid(identity.created_at_utc) || !text_present(identity.created_by)) {
            return tsunami::core::failure(manifest_error("data.dataset_manifest.identity_invalid", "manifest identity is invalid", "manifest.identity.revision.positive", "/manifest"));
        }
        for (const auto &provider : manifest.providers()) {
            if (!logical_id_valid(provider.provider_id) || !text_present(provider.name) || !optional_text_present(provider.organisation) ||
                (provider.homepage_uri && !uri_safe(*provider.homepage_uri))) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.provider_invalid", "provider record is invalid", "manifest.provider.name.required", "/providers").add_context("provider_id", provider.provider_id));
            }
            if (auto valid = validate_extensions(provider.extensions, "/providers/extensions"); !valid) {
                return valid;
            }
        }
        for (const auto &licence : manifest.licences()) {
            if (!logical_id_valid(licence.licence_id) || !text_present(licence.name) || !text_present(licence.expression) ||
                !optional_text_present(licence.attribution) || (licence.licence_uri && !uri_safe(*licence.licence_uri))) {
                return tsunami::core::failure(manifest_error("data.dataset_manifest.licence_invalid", "licence record is invalid", "manifest.licence.expression.required", "/licences").add_context("licence_id", licence.licence_id));
            }
            if (auto valid = validate_extensions(licence.extensions, "/licences/extensions"); !valid) {
                return valid;
            }
        }
        for (const auto &dataset : manifest.datasets()) {
            if (auto valid = validate_dataset_record(dataset); !valid) {
                return valid;
            }
        }
        for (const auto &process : manifest.processes()) {
            if (auto valid = validate_process_record(process); !valid) {
                return valid;
            }
        }
        if (auto valid = validate_extensions(manifest.extensions(), "/extensions"); !valid) {
            return valid;
        }
        return validate_references_and_lineage(manifest);
    }

    auto validate_dataset_manifest_for_case(const DatasetManifest &manifest, const CaseConfiguration &configuration)
        -> tsunami::core::Result<void>
    {
        if (manifest.identity().case_revision.case_id != configuration.identity().case_id) {
            return tsunami::core::failure(binding_error(
                "data.dataset_manifest.case_identity_mismatch",
                "manifest case id does not match case configuration",
                "manifest.case.identity.matches",
                {},
                configuration.identity().case_id.str(),
                manifest.identity().case_revision.case_id.str()));
        }
        if (manifest.identity().case_revision.revision != configuration.identity().revision) {
            return tsunami::core::failure(binding_error(
                "data.dataset_manifest.case_revision_mismatch",
                "manifest case revision does not match case configuration",
                "manifest.case.revision.matches",
                {},
                std::to_string(configuration.identity().revision),
                std::to_string(manifest.identity().case_revision.revision)));
        }
        const auto require_binding = [&](std::string_view id, DatasetRole role) -> tsunami::core::Result<void> {
            const auto *dataset = manifest.find_dataset(id);
            if (dataset == nullptr) {
                return tsunami::core::failure(binding_error("data.dataset_manifest.binding_missing", "case binding is missing from manifest", "manifest.case.binding.exists", std::string{id}, std::string{to_string(role)}));
            }
            if (!contains_role(*dataset, role)) {
                return tsunami::core::failure(binding_error("data.dataset_manifest.binding_role_mismatch", "case binding dataset does not carry the required role", "manifest.case.binding.role_matches", std::string{id}, std::string{to_string(role)}));
            }
            return tsunami::core::success();
        };
        const auto &bindings = configuration.datasets();
        if (auto valid = require_binding(bindings.bathymetry, DatasetRole::bathymetry); !valid) {
            return valid;
        }
        if (auto valid = require_binding(bindings.topography, DatasetRole::topography); !valid) {
            return valid;
        }
        if (bindings.earthquake_displacement) {
            if (auto valid = require_binding(*bindings.earthquake_displacement, DatasetRole::earthquake_displacement); !valid) {
                return valid;
            }
        }
        if (bindings.prescribed_surface) {
            if (auto valid = require_binding(*bindings.prescribed_surface, DatasetRole::prescribed_surface); !valid) {
                return valid;
            }
        }
        if (bindings.manning) {
            if (auto valid = require_binding(*bindings.manning, DatasetRole::manning); !valid) {
                return valid;
            }
        }
        if (bindings.coriolis) {
            if (auto valid = require_binding(*bindings.coriolis, DatasetRole::coriolis); !valid) {
                return valid;
            }
        }
        for (const auto &observation : bindings.observations) {
            if (auto valid = require_binding(observation, DatasetRole::observation); !valid) {
                return valid;
            }
        }
        return tsunami::core::success();
    }

    auto make_dataset_manifest(
        SchemaIdentity schema,
        DatasetManifestCompatibility compatibility,
        std::string policy_version,
        DatasetManifestIdentity identity,
        std::vector<DatasetProvider> providers,
        std::vector<DatasetLicence> licences,
        std::vector<DatasetRecord> datasets,
        std::vector<ProcessingRecord> processes,
        DatasetManifestExtensions extensions) -> tsunami::core::Result<DatasetManifest>
    {
        sort_extensions(extensions);
        for (auto &provider : providers) {
            sort_extensions(provider.extensions);
        }
        for (auto &licence : licences) {
            sort_extensions(licence.extensions);
        }
        for (auto &dataset : datasets) {
            std::sort(dataset.roles.begin(), dataset.roles.end(), [](auto left, auto right) { return role_rank(left) < role_rank(right); });
            std::sort(dataset.assets.begin(), dataset.assets.end(), [](const auto &left, const auto &right) { return left.asset_id < right.asset_id; });
            std::sort(dataset.uncertainty.measures.begin(), dataset.uncertainty.measures.end(), [](const auto &left, const auto &right) {
                return std::tie(left.quantity, left.unit, left.method, left.confidence_level, left.value) <
                       std::tie(right.quantity, right.unit, right.method, right.confidence_level, right.value);
            });
            sort_extensions(dataset.extensions);
        }
        for (auto &process : processes) {
            std::sort(process.input_dataset_ids.begin(), process.input_dataset_ids.end());
            std::sort(process.output_dataset_ids.begin(), process.output_dataset_ids.end());
            sort_extensions(process.extensions);
        }
        std::sort(providers.begin(), providers.end(), [](const auto &left, const auto &right) { return left.provider_id < right.provider_id; });
        std::sort(licences.begin(), licences.end(), [](const auto &left, const auto &right) { return left.licence_id < right.licence_id; });
        std::sort(datasets.begin(), datasets.end(), [](const auto &left, const auto &right) { return left.dataset_id < right.dataset_id; });
        std::sort(processes.begin(), processes.end(), [](const auto &left, const auto &right) { return left.process_id < right.process_id; });

        auto manifest = DatasetManifest{
            std::move(schema),
            compatibility,
            std::move(policy_version),
            std::move(identity),
            std::move(providers),
            std::move(licences),
            std::move(datasets),
            std::move(processes),
            std::move(extensions)};
        normalise(manifest);
        auto validation = validate_dataset_manifest(manifest);
        if (!validation) {
            return tsunami::core::failure<DatasetManifest>(validation.error());
        }
        return tsunami::core::success(std::move(manifest));
    }

} // namespace tsunami::data
