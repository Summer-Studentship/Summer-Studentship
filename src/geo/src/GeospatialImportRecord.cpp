#include <tsunami/geo/GeospatialImportRecord.hpp>

#include <algorithm>
#include <cmath>
#include <regex>
#include <string>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto geo_error(std::string code, std::string message, std::string rule_id)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_geospatial_import_record")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto logical_id_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^[a-z0-9]+(?:[._-][a-z0-9]+)*$"};
            return !text.empty() && text.size() <= 128U && std::regex_match(text, pattern);
        }

        [[nodiscard]] auto finite_box(const BoundingBox2D &box) noexcept -> bool
        {
            return std::isfinite(box.minimum_x) && std::isfinite(box.maximum_x) &&
                std::isfinite(box.minimum_y) && std::isfinite(box.maximum_y) &&
                box.minimum_x <= box.maximum_x && box.minimum_y <= box.maximum_y;
        }
    }

    auto to_string(GeospatialImportKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case GeospatialImportKind::raster:
            return "raster";
        case GeospatialImportKind::vector:
            return "vector";
        }
        return "unknown";
    }

    auto default_geospatial_import_record_path(std::string_view dataset_id, std::string_view asset_id)
        -> std::filesystem::path
    {
        auto path = std::filesystem::path{"manifests"} / "imports" / std::string{dataset_id};
        path /= std::string{asset_id} + ".json";
        return path;
    }

    auto validate_geospatial_import_record(const GeospatialImportRecord &record) -> tsunami::core::Result<void>
    {
        if (record.schema.schema_name != geospatial_import_record_schema_name ||
            record.schema.version != supported_geospatial_import_record_version) {
            return tsunami::core::failure(geo_error("geo.import.record_invalid", "geospatial import record schema is unsupported", "geo.import.record.schema"));
        }
        if (record.policy_version != supported_geospatial_import_record_policy_version) {
            return tsunami::core::failure(geo_error("geo.import.record_invalid", "geospatial import record policy is unsupported", "geo.import.record.policy"));
        }
        if (!logical_id_valid(record.identity.import_id) || record.identity.import_revision == 0U ||
            !logical_id_valid(record.identity.manifest_id) || record.identity.manifest_revision == 0U ||
            !logical_id_valid(record.identity.dataset_id) || !logical_id_valid(record.identity.asset_id)) {
            return tsunami::core::failure(geo_error("geo.import.record_invalid", "geospatial import record identity is invalid", "geo.import.record.identity"));
        }
        if (record.adapter_name.empty() || record.adapter_version.empty() || record.driver_short_name.empty() ||
            record.driver_long_name.empty() || record.media_type.empty() || record.managed_path.empty()) {
            return tsunami::core::failure(geo_error("geo.import.record_invalid", "geospatial import record metadata is incomplete", "geo.import.record.metadata"));
        }
        if (record.digest_verification_status != "not_verified") {
            return tsunami::core::failure(geo_error("geo.import.record_invalid", "digest verification status must remain not_verified in G1", "geo.import.record.digest.not_verified"));
        }
        if (!finite_box(record.extent)) {
            return tsunami::core::failure(geo_error("geo.import.record_invalid", "geospatial import record extent is invalid", "geo.import.record.extent"));
        }
        if (record.import_kind == GeospatialImportKind::raster) {
            if (!record.raster || record.vector) {
                return tsunami::core::failure(geo_error("geo.import.record_invalid", "raster import record must contain only raster summary", "geo.import.record.kind.raster"));
            }
        } else if (!record.vector || record.raster) {
            return tsunami::core::failure(geo_error("geo.import.record_invalid", "vector import record must contain only vector summary", "geo.import.record.kind.vector"));
        }
        auto codes = std::vector<std::string>{};
        for (const auto &warning : record.warnings) {
            if (warning.code.empty() || warning.message.empty()) {
                return tsunami::core::failure(geo_error("geo.import.record_invalid", "import warnings must be complete", "geo.import.record.warning"));
            }
            codes.push_back(warning.code);
        }
        if (!std::is_sorted(codes.begin(), codes.end())) {
            return tsunami::core::failure(geo_error("geo.import.record_invalid", "import warnings must be sorted deterministically", "geo.import.record.warning.order"));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
