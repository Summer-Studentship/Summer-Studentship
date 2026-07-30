#include <tsunami/geo/GeospatialImport.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <regex>
#include <set>
#include <string>

#include <tsunami/data/DatasetManifestValidation.hpp>

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
            error.add_context("operation", "validate_geospatial_import_request")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto lower(std::string text) -> std::string
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        [[nodiscard]] auto logical_id_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^[a-z0-9]+(?:[._-][a-z0-9]+)*$"};
            return !text.empty() && text.size() <= 128U && std::regex_match(text, pattern);
        }

        [[nodiscard]] auto timestamp_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z$"};
            return std::regex_match(text, pattern);
        }

        [[nodiscard]] auto date_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^\\d{4}-\\d{2}-\\d{2}$"};
            return std::regex_match(text, pattern);
        }

        [[nodiscard]] auto uri_safe(const std::string &text) -> bool
        {
            if (text.empty() || text.find_first_of("\r\n\t") != std::string::npos) {
                return false;
            }
            const auto colon = text.find(':');
            if (colon == std::string::npos || colon == 0U) {
                return false;
            }
            const auto authority = text.find("//");
            if (authority != std::string::npos) {
                const auto start = authority + 2U;
                const auto end = text.find('/', start);
                if (text.substr(start, end == std::string::npos ? std::string::npos : end - start).find('@') != std::string::npos) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] auto text_present(const std::string &text) -> bool
        {
            return !text.empty() && text.find('\0') == std::string::npos;
        }

        [[nodiscard]] auto optional_text_present(const std::optional<std::string> &text) -> bool
        {
            return !text || text_present(*text);
        }

        [[nodiscard]] auto datum_status_error(DatumEvidenceStatus status) -> std::string
        {
            switch (status) {
            case DatumEvidenceStatus::inferred:
                return "geo.import.datum_inferred";
            case DatumEvidenceStatus::unknown:
                return "geo.import.datum_unknown";
            case DatumEvidenceStatus::conflicting:
                return "geo.import.datum_conflict";
            case DatumEvidenceStatus::authoritative_declared:
            case DatumEvidenceStatus::dataset_declared:
                return {};
            }
            return "geo.import.datum_evidence_invalid";
        }

        [[nodiscard]] auto validate_evidence_record(const DatumSourceEvidence &evidence, DatumReferenceComponent component)
            -> tsunami::core::Result<void>
        {
            if (evidence.component != component) {
                return tsunami::core::failure(geo_error("geo.import.datum_evidence_invalid", "datum evidence component is inconsistent", "geo.import.datum.component").add_context("datum_component", std::string{to_string(component)}));
            }
            if (auto code = datum_status_error(evidence.status); !code.empty()) {
                return tsunami::core::failure(geo_error(code, "datum evidence status is not accepted for production import", "geo.import.datum.status.accepted").add_context("datum_component", std::string{to_string(component)}).add_context("evidence_status", std::string{to_string(evidence.status)}));
            }
            if (!text_present(evidence.datum_name) || !text_present(evidence.unit) ||
                !text_present(evidence.source_document_title) || !uri_safe(evidence.source_document_uri) ||
                !timestamp_valid(evidence.accessed_at_utc) || !optional_text_present(evidence.datum_realisation) ||
                !optional_text_present(evidence.authority_name) || !optional_text_present(evidence.authority_code) ||
                !optional_text_present(evidence.coordinate_epoch) || !optional_text_present(evidence.station_id) ||
                !optional_text_present(evidence.positive_direction) || !optional_text_present(evidence.tide_system)) {
                return tsunami::core::failure(geo_error("geo.import.datum_evidence_invalid", "datum evidence has invalid required fields", "geo.import.datum.source_document.required").add_context("datum_component", std::string{to_string(component)}));
            }
            if ((evidence.effective_from && !date_valid(*evidence.effective_from)) ||
                (evidence.effective_to && !date_valid(*evidence.effective_to)) ||
                (evidence.effective_from && evidence.effective_to && *evidence.effective_from > *evidence.effective_to)) {
                return tsunami::core::failure(geo_error("geo.import.datum_evidence_invalid", "datum evidence effective period is invalid", "geo.import.datum.effective_period.contains_observation").add_context("datum_component", std::string{to_string(component)}));
            }
            if (evidence.status == DatumEvidenceStatus::authoritative_declared &&
                (!evidence.authority_name || !evidence.authority_code)) {
                return tsunami::core::failure(geo_error("geo.import.datum_evidence_invalid", "authoritative datum evidence requires authority fields", "geo.import.datum.authority.required").add_context("datum_component", std::string{to_string(component)}));
            }
            if (evidence.reference_kind == DatumReferenceKind::tide_gauge_reference && !evidence.station_id) {
                return tsunami::core::failure(geo_error("geo.import.datum_evidence_invalid", "tide-gauge datum evidence requires a station id", "geo.import.datum.station.consistent").add_context("datum_component", std::string{to_string(component)}));
            }
            if (component == DatumReferenceComponent::vertical && !evidence.positive_direction) {
                return tsunami::core::failure(geo_error("geo.import.datum_evidence_invalid", "vertical datum evidence requires a positive direction", "geo.import.datum.vertical_positive.consistent").add_context("datum_component", "vertical"));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto role_needs_vertical(const tsunami::data::DatasetRecord &dataset) -> bool
        {
            return std::any_of(dataset.roles.begin(), dataset.roles.end(), [](auto role) {
                return role == tsunami::data::DatasetRole::bathymetry ||
                    role == tsunami::data::DatasetRole::topography ||
                    role == tsunami::data::DatasetRole::earthquake_displacement ||
                    role == tsunami::data::DatasetRole::prescribed_surface;
            });
        }

        [[nodiscard]] auto extension_allowed(const std::filesystem::path &path, GeospatialImportKind kind) -> bool
        {
            const auto ext = lower(path.extension().generic_string());
            if (kind == GeospatialImportKind::raster) {
                return ext == ".tif" || ext == ".tiff";
            }
            return ext == ".gpkg";
        }

        [[nodiscard]] auto media_type_allowed(const std::string &media_type, GeospatialImportKind kind) -> bool
        {
            const auto value = lower(media_type);
            if (kind == GeospatialImportKind::raster) {
                return value == "image/tiff" || value == "image/geotiff" || value == "application/geotiff";
            }
            return value == "application/geopackage+sqlite3" || value == "application/geopackage" || value == "application/octet-stream";
        }

        [[nodiscard]] auto native_authority_text(const NativeSpatialReference &native_reference) -> std::optional<std::string>
        {
            if (native_reference.authority_name && native_reference.authority_code) {
                return *native_reference.authority_name + ":" + *native_reference.authority_code;
            }
            return std::nullopt;
        }

        [[nodiscard]] auto normalised_equals(std::string_view left, std::string_view right) -> bool
        {
            return lower(std::string{left}) == lower(std::string{right});
        }

        [[nodiscard]] auto path_has_parent_reference(const std::filesystem::path &path) -> bool
        {
            return std::any_of(path.begin(), path.end(), [](const auto &part) {
                return part == "..";
            });
        }

        [[nodiscard]] auto path_inside(const std::filesystem::path &child, const std::filesystem::path &parent) -> bool
        {
            auto child_it = child.begin();
            auto parent_it = parent.begin();
            for (; parent_it != parent.end(); ++parent_it, ++child_it) {
                if (child_it == child.end() || *child_it != *parent_it) {
                    return false;
                }
            }
            return true;
        }
    }

    auto validate_datum_evidence_set(
        const tsunami::data::DatasetRecord &dataset,
        const NativeSpatialReference &native_reference,
        const DatumEvidenceSet &evidence) -> tsunami::core::Result<void>
    {
        if (dataset.spatial_reference.applicability == tsunami::data::SpatialApplicability::spatial && !native_reference.canonical_wkt2 && !native_reference.authority_code) {
            return tsunami::core::failure(geo_error("geo.import.spatial_reference_missing", "spatial manifest dataset requires embedded spatial reference metadata", "geo.import.datum.manifest_asset.consistent").add_context("dataset_id", dataset.dataset_id));
        }
        if (auto valid = validate_evidence_record(evidence.horizontal, DatumReferenceComponent::horizontal); !valid) {
            return valid;
        }
        if (auto authority = native_authority_text(native_reference);
            authority && dataset.spatial_reference.horizontal_crs &&
            !normalised_equals(*authority, *dataset.spatial_reference.horizontal_crs)) {
            return tsunami::core::failure(geo_error("geo.import.datum_conflict", "manifest and embedded horizontal authority conflict", "geo.import.datum.manifest_asset.consistent")
                                             .add_context("dataset_id", dataset.dataset_id)
                                             .add_context("expected", *dataset.spatial_reference.horizontal_crs)
                                             .add_context("actual", *authority));
        }
        if (native_reference.authority_code && evidence.horizontal.authority_code &&
            !normalised_equals(*native_reference.authority_code, *evidence.horizontal.authority_code)) {
            return tsunami::core::failure(geo_error("geo.import.datum_conflict", "embedded and supplied horizontal authority codes conflict", "geo.import.datum.manifest_asset.consistent")
                                             .add_context("datum_component", "horizontal")
                                             .add_context("expected", *native_reference.authority_code)
                                             .add_context("actual", *evidence.horizontal.authority_code));
        }
        if (dataset.spatial_reference.horizontal_unit && !normalised_equals(*dataset.spatial_reference.horizontal_unit, evidence.horizontal.unit)) {
            return tsunami::core::failure(geo_error("geo.import.unit_conflict", "manifest and datum evidence horizontal units conflict", "geo.import.datum.unit.consistent")
                                             .add_context("datum_component", "horizontal")
                                             .add_context("expected", *dataset.spatial_reference.horizontal_unit)
                                             .add_context("actual", evidence.horizontal.unit));
        }
        if (role_needs_vertical(dataset)) {
            if (!evidence.vertical) {
                return tsunami::core::failure(geo_error("geo.import.datum_evidence_missing", "dataset role requires vertical datum evidence", "geo.import.datum.vertical.required").add_context("dataset_id", dataset.dataset_id).add_context("datum_component", "vertical"));
            }
            if (auto valid = validate_evidence_record(*evidence.vertical, DatumReferenceComponent::vertical); !valid) {
                return valid;
            }
            if (dataset.spatial_reference.vertical_datum && !normalised_equals(*dataset.spatial_reference.vertical_datum, evidence.vertical->datum_name)) {
                return tsunami::core::failure(geo_error("geo.import.datum_conflict", "manifest and supplied vertical datum names conflict", "geo.import.datum.manifest_asset.consistent")
                                                 .add_context("datum_component", "vertical")
                                                 .add_context("expected", *dataset.spatial_reference.vertical_datum)
                                                 .add_context("actual", evidence.vertical->datum_name));
            }
            if (dataset.spatial_reference.vertical_unit && !normalised_equals(*dataset.spatial_reference.vertical_unit, evidence.vertical->unit)) {
                return tsunami::core::failure(geo_error("geo.import.unit_conflict", "manifest and datum evidence vertical units conflict", "geo.import.datum.unit.consistent")
                                                 .add_context("datum_component", "vertical")
                                                 .add_context("expected", *dataset.spatial_reference.vertical_unit)
                                                 .add_context("actual", evidence.vertical->unit));
            }
            if (dataset.spatial_reference.vertical_positive && evidence.vertical->positive_direction &&
                !normalised_equals(*dataset.spatial_reference.vertical_positive, *evidence.vertical->positive_direction)) {
                return tsunami::core::failure(geo_error("geo.import.vertical_sign_conflict", "manifest and datum evidence vertical positive directions conflict", "geo.import.datum.vertical_positive.consistent")
                                                 .add_context("datum_component", "vertical")
                                                 .add_context("expected", *dataset.spatial_reference.vertical_positive)
                                                 .add_context("actual", *evidence.vertical->positive_direction));
            }
        }
        return tsunami::core::success();
    }

    auto resolve_geospatial_import_asset(
        const GeospatialImportRequest &request,
        GeospatialImportKind import_kind,
        std::string_view) -> tsunami::core::Result<ResolvedGeospatialAsset>
    {
        if (request.manifest == nullptr || !logical_id_valid(request.dataset_id) ||
            (request.asset_id && !logical_id_valid(*request.asset_id)) ||
            !logical_id_valid(request.import_id) || request.import_revision == 0U ||
            !timestamp_valid(request.executed_at_utc) ||
            request.maximum_raster_cells == 0U || request.maximum_vector_features == 0U ||
            request.maximum_geometry_coordinates == 0U || request.case_root.empty()) {
            return tsunami::core::failure<ResolvedGeospatialAsset>(geo_error("geo.import.request_invalid", "geospatial import request is invalid", "geo.import.request.valid"));
        }
        if (auto valid = tsunami::data::validate_dataset_manifest(*request.manifest); !valid) {
            return tsunami::core::failure<ResolvedGeospatialAsset>(geo_error("geo.import.request_invalid", "dataset manifest is invalid", "geo.import.request.manifest.valid").with_cause_code(valid.error().code()));
        }
        const auto *dataset = request.manifest->find_dataset(request.dataset_id);
        if (dataset == nullptr) {
            return tsunami::core::failure<ResolvedGeospatialAsset>(geo_error("geo.import.dataset_missing", "dataset is missing from manifest", "geo.import.dataset.exists").add_context("dataset_id", request.dataset_id));
        }
        const auto expected_representation = import_kind == GeospatialImportKind::raster ? tsunami::data::DatasetRepresentationKind::raster : tsunami::data::DatasetRepresentationKind::vector;
        if (dataset->representation != expected_representation) {
            return tsunami::core::failure<ResolvedGeospatialAsset>(geo_error("geo.import.representation_mismatch", "dataset representation does not match requested import kind", "geo.import.dataset.representation").add_context("dataset_id", dataset->dataset_id));
        }

        const tsunami::data::DatasetAsset *asset = nullptr;
        if (request.asset_id) {
            for (const auto &candidate : dataset->assets) {
                if (candidate.asset_id == *request.asset_id) {
                    asset = &candidate;
                    break;
                }
            }
        } else {
            for (const auto &candidate : dataset->assets) {
                if (candidate.role == tsunami::data::DatasetAssetRole::primary) {
                    asset = &candidate;
                    break;
                }
            }
        }
        if (asset == nullptr) {
            return tsunami::core::failure<ResolvedGeospatialAsset>(geo_error("geo.import.asset_missing", "selected asset is missing from dataset", "geo.import.asset.exists").add_context("dataset_id", dataset->dataset_id));
        }
        if (asset->location.kind != tsunami::data::DatasetLocationKind::managed_path || !asset->location.managed_path) {
            return tsunami::core::failure<ResolvedGeospatialAsset>(geo_error("geo.import.asset_location_unsupported", "geospatial import requires a managed-path asset", "geo.import.asset.location.managed_path").add_context("asset_id", asset->asset_id));
        }
        if (!media_type_allowed(asset->media_type, import_kind)) {
            return tsunami::core::failure<ResolvedGeospatialAsset>(geo_error("geo.import.media_type_mismatch", "asset media type is not supported for requested import kind", "geo.import.asset.media_type").add_context("asset_id", asset->asset_id).add_context("actual", asset->media_type));
        }
        const auto &relative = *asset->location.managed_path;
        if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory() ||
            path_has_parent_reference(relative) || !extension_allowed(relative, import_kind)) {
            return tsunami::core::failure<ResolvedGeospatialAsset>(geo_error("geo.import.path_invalid", "managed asset path is invalid for geospatial import", "geo.import.asset.path.safe").add_context("managed_path", relative.generic_string()));
        }
        const auto root = std::filesystem::weakly_canonical(request.case_root);
        const auto absolute = std::filesystem::weakly_canonical(root / relative);
        if (!path_inside(absolute, root)) {
            return tsunami::core::failure<ResolvedGeospatialAsset>(geo_error("geo.import.path_escape", "managed asset path escapes the case root", "geo.import.asset.path.contained").add_context("managed_path", relative.generic_string()));
        }
        if (!std::filesystem::exists(absolute) || !std::filesystem::is_regular_file(absolute)) {
            return tsunami::core::failure<ResolvedGeospatialAsset>(geo_error("geo.import.file_missing", "managed asset file is missing", "geo.import.asset.file.exists").add_context("managed_path", relative.generic_string()));
        }
        return tsunami::core::success(ResolvedGeospatialAsset{dataset, asset, absolute});
    }

    auto make_raster_import_record(
        const GeospatialImportRequest &request,
        const tsunami::data::DatasetRecord &dataset,
        const tsunami::data::DatasetAsset &asset,
        std::string driver_short_name,
        std::string driver_long_name,
        const ImportedRaster &raster,
        std::vector<ImportWarning> warnings) -> tsunami::core::Result<GeospatialImportRecord>
    {
        if (auto valid = validate_datum_evidence_set(dataset, raster.spatial_reference(), request.datum_evidence); !valid) {
            return tsunami::core::failure<GeospatialImportRecord>(valid.error());
        }
        std::sort(warnings.begin(), warnings.end(), [](const auto &left, const auto &right) { return left.code < right.code; });
        auto record = GeospatialImportRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{geospatial_import_record_schema_name}, supported_geospatial_import_record_version};
        record.policy_version = std::string{supported_geospatial_import_record_policy_version};
        record.identity = GeospatialImportIdentity{
            request.import_id,
            request.import_revision,
            request.manifest->identity().case_revision,
            request.manifest->identity().manifest_id,
            request.manifest->identity().manifest_revision,
            dataset.dataset_id,
            asset.asset_id,
            request.executed_at_utc};
        record.import_kind = GeospatialImportKind::raster;
        record.adapter_name = "gdal";
        record.adapter_version = "g1";
        record.driver_short_name = std::move(driver_short_name);
        record.driver_long_name = std::move(driver_long_name);
        record.media_type = asset.media_type;
        record.managed_path = *asset.location.managed_path;
        record.declared_digest = asset.digest;
        record.digest_verification_status = "not_verified";
        record.native_spatial_reference = raster.spatial_reference();
        record.datum_evidence = request.datum_evidence;
        record.extent = raster.extent();
        record.raster = RasterImportSummary{
            raster.width(),
            raster.height(),
            raster.cell_count(),
            1U,
            raster.band().native_type,
            raster.transform(),
            raster.registration(),
            raster.band().nodata_value.has_value(),
            raster.band().nodata_value,
            raster.band().scale,
            raster.band().offset,
            dataset.resolution.spatial};
        record.warnings = std::move(warnings);
        if (!record.raster->has_nodata) {
            record.raster->nodata_value = std::nullopt;
        }
        if (auto valid = validate_geospatial_import_record(record); !valid) {
            return tsunami::core::failure<GeospatialImportRecord>(valid.error());
        }
        return tsunami::core::success(std::move(record));
    }

    auto make_vector_import_record(
        const GeospatialImportRequest &request,
        const tsunami::data::DatasetRecord &dataset,
        const tsunami::data::DatasetAsset &asset,
        std::string driver_short_name,
        std::string driver_long_name,
        const ImportedVectorLayer &layer,
        std::size_t coordinate_total,
        std::vector<ImportWarning> warnings) -> tsunami::core::Result<GeospatialImportRecord>
    {
        if (auto valid = validate_datum_evidence_set(dataset, layer.spatial_reference(), request.datum_evidence); !valid) {
            return tsunami::core::failure<GeospatialImportRecord>(valid.error());
        }
        std::sort(warnings.begin(), warnings.end(), [](const auto &left, const auto &right) { return left.code < right.code; });
        auto record = GeospatialImportRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{geospatial_import_record_schema_name}, supported_geospatial_import_record_version};
        record.policy_version = std::string{supported_geospatial_import_record_policy_version};
        record.identity = GeospatialImportIdentity{
            request.import_id,
            request.import_revision,
            request.manifest->identity().case_revision,
            request.manifest->identity().manifest_id,
            request.manifest->identity().manifest_revision,
            dataset.dataset_id,
            asset.asset_id,
            request.executed_at_utc};
        record.import_kind = GeospatialImportKind::vector;
        record.adapter_name = "gdal";
        record.adapter_version = "g1";
        record.driver_short_name = std::move(driver_short_name);
        record.driver_long_name = std::move(driver_long_name);
        record.media_type = asset.media_type;
        record.managed_path = *asset.location.managed_path;
        record.declared_digest = asset.digest;
        record.digest_verification_status = "not_verified";
        record.native_spatial_reference = layer.spatial_reference();
        record.datum_evidence = request.datum_evidence;
        record.extent = layer.extent();
        record.vector = VectorImportSummary{
            std::string{layer.layer_name()},
            static_cast<std::uint64_t>(layer.feature_count()),
            layer.geometry_kind(),
            static_cast<std::uint64_t>(layer.field_schema().size()),
            static_cast<std::uint64_t>(coordinate_total),
            layer.extent()};
        record.warnings = std::move(warnings);
        if (auto valid = validate_geospatial_import_record(record); !valid) {
            return tsunami::core::failure<GeospatialImportRecord>(valid.error());
        }
        return tsunami::core::success(std::move(record));
    }

} // namespace tsunami::geo
