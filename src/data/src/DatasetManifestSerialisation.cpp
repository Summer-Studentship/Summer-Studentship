#include <tsunami/data/DatasetManifestSerialisation.hpp>

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include <tsunami/data/DatasetManifestValidation.hpp>

namespace tsunami::data
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        [[nodiscard]] auto io_error(std::string code, std::string message, const std::filesystem::path &path) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::persistence,
                tsunami::core::Severity::error};
            error.add_context("operation", "write_dataset_manifest")
                .add_context("path", path.generic_string())
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto optional_string(const std::optional<std::string> &value) -> Json
        {
            return value ? Json(*value) : Json(nullptr);
        }

        [[nodiscard]] auto optional_double(const std::optional<double> &value) -> Json
        {
            return value ? Json(*value) : Json(nullptr);
        }

        [[nodiscard]] auto optional_uint64(const std::optional<std::uint64_t> &value) -> Json
        {
            return value ? Json(*value) : Json(nullptr);
        }

        [[nodiscard]] auto append_lf(std::string text) -> std::string
        {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                text.pop_back();
            }
            text.push_back('\n');
            return text;
        }

        [[nodiscard]] auto extensions_json(const DatasetManifestExtensions &extensions) -> Json
        {
            auto object = Json::object();
            for (const auto &extension : extensions.values) {
                object[extension.name] = Json::parse(extension.canonical_json);
            }
            return object;
        }

        [[nodiscard]] auto provider_json(const DatasetProvider &provider) -> Json
        {
            return Json::object({
                {"provider_id", provider.provider_id},
                {"name", provider.name},
                {"organisation", optional_string(provider.organisation)},
                {"homepage_uri", optional_string(provider.homepage_uri)},
                {"extensions", extensions_json(provider.extensions)}});
        }

        [[nodiscard]] auto licence_json(const DatasetLicence &licence) -> Json
        {
            return Json::object({
                {"licence_id", licence.licence_id},
                {"name", licence.name},
                {"expression", licence.expression},
                {"licence_uri", optional_string(licence.licence_uri)},
                {"attribution", optional_string(licence.attribution)},
                {"extensions", extensions_json(licence.extensions)}});
        }

        [[nodiscard]] auto location_json(const DatasetAssetLocation &location) -> Json
        {
            return Json::object({
                {"kind", std::string{to_string(location.kind)}},
                {"managed_path", location.managed_path ? Json(location.managed_path->generic_string()) : Json(nullptr)},
                {"external_uri", optional_string(location.external_uri)}});
        }

        [[nodiscard]] auto asset_json(const DatasetAsset &asset) -> Json
        {
            return Json::object({
                {"asset_id", asset.asset_id},
                {"role", std::string{to_string(asset.role)}},
                {"location", location_json(asset.location)},
                {"media_type", asset.media_type},
                {"byte_size", optional_uint64(asset.byte_size)},
                {"digest", Json::object({
                               {"algorithm", std::string{to_string(asset.digest.algorithm)}},
                               {"value", asset.digest.value},
                               {"origin", std::string{to_string(asset.digest.origin)}}})}});
        }

        [[nodiscard]] auto source_json(const std::optional<SourceAcquisitionRecord> &source) -> Json
        {
            if (!source) {
                return Json(nullptr);
            }
            return Json::object({
                {"source_uri", source->source_uri},
                {"accessed_at_utc", source->accessed_at_utc},
                {"source_version", optional_string(source->source_version)},
                {"publication_date", optional_string(source->publication_date)}});
        }

        [[nodiscard]] auto spatial_reference_json(const DatasetSpatialReference &spatial) -> Json
        {
            return Json::object({
                {"applicability", std::string{to_string(spatial.applicability)}},
                {"horizontal_crs", optional_string(spatial.horizontal_crs)},
                {"vertical_datum", optional_string(spatial.vertical_datum)},
                {"horizontal_unit", optional_string(spatial.horizontal_unit)},
                {"vertical_unit", optional_string(spatial.vertical_unit)},
                {"axis_order", optional_string(spatial.axis_order)},
                {"vertical_positive", optional_string(spatial.vertical_positive)}});
        }

        [[nodiscard]] auto resolution_json(const DatasetResolution &resolution) -> Json
        {
            return Json::object({
                {"spatial", Json::object({
                                {"kind", std::string{to_string(resolution.spatial.kind)}},
                                {"x", optional_double(resolution.spatial.x)},
                                {"y", optional_double(resolution.spatial.y)},
                                {"unit", optional_string(resolution.spatial.unit)},
                                {"description", optional_string(resolution.spatial.description)}})},
                {"temporal", Json::object({
                                 {"kind", std::string{to_string(resolution.temporal.kind)}},
                                 {"value", optional_double(resolution.temporal.value)},
                                 {"unit", optional_string(resolution.temporal.unit)},
                                 {"description", optional_string(resolution.temporal.description)}})}});
        }

        [[nodiscard]] auto uncertainty_json(const DatasetUncertainty &uncertainty) -> Json
        {
            auto measures = Json::array();
            for (const auto &measure : uncertainty.measures) {
                measures.push_back(Json::object({
                    {"quantity", measure.quantity},
                    {"value", measure.value},
                    {"unit", measure.unit},
                    {"confidence_level", optional_double(measure.confidence_level)},
                    {"method", optional_string(measure.method)}}));
            }
            return Json::object({
                {"status", std::string{to_string(uncertainty.status)}},
                {"measures", std::move(measures)},
                {"description", optional_string(uncertainty.description)}});
        }

        [[nodiscard]] auto dataset_json(const DatasetRecord &dataset) -> Json
        {
            auto roles = Json::array();
            for (const auto role : dataset.roles) {
                roles.push_back(std::string{to_string(role)});
            }
            auto assets = Json::array();
            for (const auto &asset : dataset.assets) {
                assets.push_back(asset_json(asset));
            }
            return Json::object({
                {"dataset_id", dataset.dataset_id},
                {"origin_kind", std::string{to_string(dataset.origin_kind)}},
                {"representation", std::string{to_string(dataset.representation)}},
                {"roles", std::move(roles)},
                {"title", dataset.title},
                {"description", optional_string(dataset.description)},
                {"provider_id", dataset.provider_id},
                {"licence_id", dataset.licence_id},
                {"source", source_json(dataset.source)},
                {"generated_by_process_id", optional_string(dataset.generated_by_process_id)},
                {"assets", std::move(assets)},
                {"spatial_reference", spatial_reference_json(dataset.spatial_reference)},
                {"resolution", resolution_json(dataset.resolution)},
                {"uncertainty", uncertainty_json(dataset.uncertainty)},
                {"citation", optional_string(dataset.citation)},
                {"extensions", extensions_json(dataset.extensions)}});
        }

        [[nodiscard]] auto process_json(const ProcessingRecord &process) -> Json
        {
            return Json::object({
                {"process_id", process.process_id},
                {"operation", process.operation},
                {"executed_at_utc", process.executed_at_utc},
                {"software", Json::object({
                                 {"name", process.software.name},
                                 {"version", process.software.version},
                                 {"repository_uri", optional_string(process.software.repository_uri)},
                                 {"commit_sha", optional_string(process.software.commit_sha)}})},
                {"parameters", Json::parse(process.canonical_parameters_json)},
                {"input_dataset_ids", process.input_dataset_ids},
                {"output_dataset_ids", process.output_dataset_ids},
                {"extensions", extensions_json(process.extensions)}});
        }

        [[nodiscard]] auto manifest_json(const DatasetManifest &manifest) -> Json
        {
            auto providers = Json::array();
            for (const auto &provider : manifest.providers()) {
                providers.push_back(provider_json(provider));
            }
            auto licences = Json::array();
            for (const auto &licence : manifest.licences()) {
                licences.push_back(licence_json(licence));
            }
            auto datasets = Json::array();
            for (const auto &dataset : manifest.datasets()) {
                datasets.push_back(dataset_json(dataset));
            }
            auto processes = Json::array();
            for (const auto &process : manifest.processes()) {
                processes.push_back(process_json(process));
            }
            const auto &identity = manifest.identity();
            return Json::object({
                {"schema_version", manifest.schema_identity().version.text()},
                {"policy_version", std::string{manifest.policy_version()}},
                {"manifest", Json::object({
                                 {"manifest_id", identity.manifest_id},
                                 {"manifest_revision", identity.manifest_revision},
                                 {"case_id", identity.case_revision.case_id.str()},
                                 {"case_revision", identity.case_revision.revision},
                                 {"created_at_utc", identity.created_at_utc},
                                 {"created_by", identity.created_by}})},
                {"providers", std::move(providers)},
                {"licences", std::move(licences)},
                {"datasets", std::move(datasets)},
                {"processes", std::move(processes)},
                {"extensions", extensions_json(manifest.extensions())}});
        }
    } // namespace

    auto serialise_dataset_manifest(const DatasetManifest &manifest) -> tsunami::core::Result<std::string>
    {
        auto validation = validate_dataset_manifest(manifest);
        if (!validation) {
            return tsunami::core::failure<std::string>(validation.error());
        }
        try {
            return tsunami::core::success(append_lf(manifest_json(manifest).dump(2)));
        } catch (const nlohmann::json::exception &error) {
            auto diagnostic = tsunami::core::Error{
                "data.dataset_manifest.serialisation_failed",
                "dataset manifest serialisation failed",
                tsunami::core::DiagnosticCategory::persistence,
                tsunami::core::Severity::error};
            diagnostic.add_context("operation", "serialise_dataset_manifest")
                .add_context("parser_detail", error.what())
                .add_context("state_changed", "false");
            return tsunami::core::failure<std::string>(std::move(diagnostic));
        }
    }

    auto write_dataset_manifest(const std::filesystem::path &path, const DatasetManifest &manifest) -> tsunami::core::Result<void>
    {
        auto bytes = serialise_dataset_manifest(manifest);
        if (!bytes) {
            return tsunami::core::failure(bytes.error());
        }
        const auto temporary = path.parent_path() / (path.filename().generic_string() + ".tmp");
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) {
                return tsunami::core::failure(io_error("data.dataset_manifest.write_open_failed", "could not open temporary dataset manifest file", temporary));
            }
            file.write(bytes.value().data(), static_cast<std::streamsize>(bytes.value().size()));
            file.flush();
            if (!file) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return tsunami::core::failure(io_error("data.dataset_manifest.write_failed", "could not write complete dataset manifest file", temporary));
            }
        }
        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return tsunami::core::failure(io_error("data.dataset_manifest.commit_failed", "could not commit dataset manifest file", path));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::data
