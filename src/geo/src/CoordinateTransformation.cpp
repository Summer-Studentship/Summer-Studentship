#include <tsunami/geo/CoordinateTransformation.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <regex>
#include <set>
#include <string>

#include <tsunami/data/CaseConfigurationValidation.hpp>
#include <tsunami/data/DatasetManifestValidation.hpp>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto crs_error(std::string code, std::string message, std::string rule_id)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_coordinate_transformation")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto text_present(const std::string &text) -> bool
        {
            return !text.empty() && text.find('\0') == std::string::npos;
        }

        [[nodiscard]] auto optional_text_present(const std::optional<std::string> &text) -> bool
        {
            return !text || text_present(*text);
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

        [[nodiscard]] auto safe_relative_path(const std::filesystem::path &path) -> bool
        {
            if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
                return false;
            }
            return std::none_of(path.begin(), path.end(), [](const auto &part) {
                return part == "..";
            });
        }

        [[nodiscard]] auto equivalent_text(std::string_view left, std::string_view right) -> bool
        {
            auto norm = [](std::string_view value) {
                auto out = std::string{};
                for (const auto ch : value) {
                    if (ch != ' ' && ch != '_' && ch != '-') {
                        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                    }
                }
                return out;
            };
            return norm(left) == norm(right);
        }

        [[nodiscard]] auto finite_positive(double value) noexcept -> bool
        {
            return std::isfinite(value) && value > 0.0;
        }
    }

    auto to_string(ComputationalAxisConvention convention) noexcept -> std::string_view
    {
        switch (convention) {
        case ComputationalAxisConvention::east_north:
            return "east_north";
        case ComputationalAxisConvention::east_north_up:
            return "east_north_up";
        }
        return "unknown";
    }

    auto to_string(GeodeticResourceVerificationStatus status) noexcept -> std::string_view
    {
        switch (status) {
        case GeodeticResourceVerificationStatus::externally_verified:
            return "externally_verified";
        case GeodeticResourceVerificationStatus::declared_not_verified:
            return "declared_not_verified";
        case GeodeticResourceVerificationStatus::unavailable:
            return "unavailable";
        }
        return "unknown";
    }

    auto to_string(HorizontalTransformationClass kind) noexcept -> std::string_view
    {
        switch (kind) {
        case HorizontalTransformationClass::same_datum_projection:
            return "same_datum_projection";
        case HorizontalTransformationClass::datum_transformation_and_projection:
            return "datum_transformation_and_projection";
        case HorizontalTransformationClass::documented_reference_equivalence:
            return "documented_reference_equivalence";
        }
        return "unknown";
    }

    auto to_string(VerticalTransformationStepKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case VerticalTransformationStepKind::identity:
            return "identity";
        case VerticalTransformationStepKind::unit_scale:
            return "unit_scale";
        case VerticalTransformationStepKind::sign_inversion:
            return "sign_inversion";
        case VerticalTransformationStepKind::constant_offset:
            return "constant_offset";
        case VerticalTransformationStepKind::geodetic_grid_operation:
            return "geodetic_grid_operation";
        }
        return "unknown";
    }

    auto validate_geographic_area_of_interest(const GeographicAreaOfInterest &area)
        -> tsunami::core::Result<void>
    {
        if (!std::isfinite(area.west_longitude_degrees) || !std::isfinite(area.east_longitude_degrees) ||
            !std::isfinite(area.south_latitude_degrees) || !std::isfinite(area.north_latitude_degrees) ||
            area.west_longitude_degrees < -180.0 || area.east_longitude_degrees > 180.0 ||
            area.south_latitude_degrees < -90.0 || area.north_latitude_degrees > 90.0 ||
            area.west_longitude_degrees >= area.east_longitude_degrees ||
            area.south_latitude_degrees >= area.north_latitude_degrees) {
            return tsunami::core::failure(crs_error("geo.crs.area_of_interest_invalid", "area of interest is invalid", "geo.crs.area.request_valid"));
        }
        return tsunami::core::success();
    }

    auto validate_coordinate_reference_descriptor(const CoordinateReferenceDescriptor &descriptor)
        -> tsunami::core::Result<void>
    {
        if (!text_present(descriptor.name) || !optional_text_present(descriptor.authority_name) ||
            !optional_text_present(descriptor.authority_code) || !optional_text_present(descriptor.canonical_wkt2) ||
            !optional_text_present(descriptor.canonical_projjson) || !optional_text_present(descriptor.datum_name) ||
            !optional_text_present(descriptor.datum_realisation)) {
            return tsunami::core::failure(crs_error("geo.crs.target_reference_invalid", "coordinate reference descriptor contains invalid text", "geo.crs.target.reference.valid"));
        }
        if ((!descriptor.authority_name || !descriptor.authority_code) &&
            !descriptor.canonical_wkt2 && !descriptor.canonical_projjson) {
            return tsunami::core::failure(crs_error("geo.crs.source_reference_missing", "coordinate reference descriptor needs authority or canonical CRS text", "geo.crs.source.reference.present"));
        }
        if (descriptor.coordinate_epoch_decimal_year && !std::isfinite(*descriptor.coordinate_epoch_decimal_year)) {
            return tsunami::core::failure(crs_error("geo.crs.target_reference_invalid", "coordinate epoch must be finite", "geo.crs.epoch.valid"));
        }
        if (descriptor.axis_names.size() != descriptor.axis_directions.size() ||
            descriptor.axis_names.size() != descriptor.axis_units.size()) {
            return tsunami::core::failure(crs_error("geo.crs.axis_conflict", "axis metadata vectors must have matching lengths", "geo.crs.axis.authority_preserved"));
        }
        for (std::size_t i = 0; i < descriptor.axis_names.size(); ++i) {
            if (!text_present(descriptor.axis_names[i]) || !text_present(descriptor.axis_directions[i]) ||
                !text_present(descriptor.axis_units[i])) {
                return tsunami::core::failure(crs_error("geo.crs.axis_conflict", "axis metadata entries must be nonempty", "geo.crs.axis.authority_preserved"));
            }
        }
        return tsunami::core::success();
    }

    auto validate_transformation_target_for_case(
        const ComputationalTargetReference &target,
        const tsunami::data::CaseConfiguration &configuration) -> tsunami::core::Result<void>
    {
        if (auto valid = validate_coordinate_reference_descriptor(target.horizontal); !valid) {
            return valid;
        }
        const auto &frame = configuration.coordinate_frame();
        const auto target_code = target.horizontal.authority_name && target.horizontal.authority_code
            ? *target.horizontal.authority_name + ":" + *target.horizontal.authority_code
            : target.horizontal.name;
        if (!equivalent_text(target_code, frame.horizontal_crs)) {
            return tsunami::core::failure(crs_error("geo.crs.case_target_mismatch", "target horizontal CRS does not match case configuration", "geo.crs.target.case_consistent")
                                             .add_context("expected", frame.horizontal_crs)
                                             .add_context("actual", target_code));
        }
        if (target.horizontal_unit != frame.horizontal_unit || frame.horizontal_unit != "m") {
            return tsunami::core::failure(crs_error("geo.crs.target_reference_invalid", "target horizontal unit must match case metric unit", "geo.crs.unit.horizontal_metres"));
        }
        if (configuration.coordinate_frame().axis_order != tsunami::data::HorizontalAxisOrder::east_north ||
            target.storage_axes == ComputationalAxisConvention::east_north_up) {
            if (!target.vertical) {
                return tsunami::core::failure(crs_error("geo.crs.vertical_reference_missing", "three-dimensional storage requires a vertical target", "geo.crs.vertical.positive_up"));
            }
        }
        if (target.vertical) {
            if (auto valid = validate_coordinate_reference_descriptor(*target.vertical); !valid) {
                return valid;
            }
            const auto vertical_code = target.vertical->authority_name && target.vertical->authority_code
                ? *target.vertical->authority_name + ":" + *target.vertical->authority_code
                : target.vertical->name;
            if (!equivalent_text(vertical_code, frame.vertical_datum)) {
                return tsunami::core::failure(crs_error("geo.crs.case_target_mismatch", "target vertical reference does not match case configuration", "geo.crs.target.case_consistent")
                                                 .add_context("expected", frame.vertical_datum)
                                                 .add_context("actual", vertical_code));
            }
            if (!target.vertical_unit || *target.vertical_unit != frame.vertical_unit || frame.vertical_unit != "m") {
                return tsunami::core::failure(crs_error("geo.crs.target_reference_invalid", "target vertical unit must match case metric unit", "geo.crs.unit.vertical_metres"));
            }
            if (!target.vertical_positive || *target.vertical_positive != "up" ||
                frame.vertical_positive != tsunami::data::VerticalPositiveDirection::up) {
                return tsunami::core::failure(crs_error("geo.crs.vertical_reference_missing", "target vertical positive direction must be up", "geo.crs.vertical.positive_up"));
            }
        }
        return tsunami::core::success();
    }

    auto validate_accuracy_policy(const CoordinateTransformationAccuracyPolicy &policy)
        -> tsunami::core::Result<void>
    {
        if (!finite_positive(policy.maximum_operation_accuracy_m) ||
            !finite_positive(policy.projection_control_tolerance_m) ||
            !finite_positive(policy.horizontal_control_tolerance_m) ||
            !finite_positive(policy.vertical_control_tolerance_m) ||
            !finite_positive(policy.round_trip_tolerance_m)) {
            return tsunami::core::failure(crs_error("geo.crs.request_invalid", "accuracy policy tolerances must be finite and positive", "geo.crs.operation.accuracy_accepted"));
        }
        return tsunami::core::success();
    }

    auto validate_selection_policy(const CoordinateOperationSelectionPolicy &policy)
        -> tsunami::core::Result<void>
    {
        if (auto area = validate_geographic_area_of_interest(policy.area_of_interest); !area) {
            return area;
        }
        if (auto accuracy = validate_accuracy_policy(policy.accuracy); !accuracy) {
            return accuracy;
        }
        if (!optional_text_present(policy.authority)) {
            return tsunami::core::failure(crs_error("geo.crs.request_invalid", "operation authority filter is invalid", "geo.crs.operation.best_required"));
        }
        if (policy.allow_ballpark) {
            return tsunami::core::failure(crs_error("geo.crs.operation_ballpark", "ballpark operations are disabled for production transformations", "geo.crs.operation.ballpark_forbidden"));
        }
        if (!policy.only_best) {
            return tsunami::core::failure(crs_error("geo.crs.operation_not_found", "only-best operation selection is required", "geo.crs.operation.best_required"));
        }
        if (policy.network_enabled) {
            return tsunami::core::failure(crs_error("geo.crs.network_forbidden", "network resource access is forbidden", "geo.crs.network.disabled"));
        }
        return tsunami::core::success();
    }

    auto validate_resource_evidence(const GeodeticResourceEvidence &evidence)
        -> tsunami::core::Result<void>
    {
        if (!text_present(evidence.resource_name) || !text_present(evidence.provider) ||
            !text_present(evidence.source_document_title) || !uri_safe(evidence.source_document_uri)) {
            return tsunami::core::failure(crs_error("geo.crs.resource_unverified", "resource evidence has invalid required fields", "geo.crs.operation.resources_verified"));
        }
        if (evidence.resolved_path && !safe_relative_path(*evidence.resolved_path)) {
            return tsunami::core::failure(crs_error("geo.crs.resource_conflict", "resolved resource paths must be safe relative paths", "geo.crs.operation.resources_available"));
        }
        return tsunami::core::success();
    }

    auto validate_vertical_transformation(const VerticalTransformationSpecification &specification)
        -> tsunami::core::Result<void>
    {
        if (!specification.enabled && !specification.steps.empty()) {
            return tsunami::core::failure(crs_error("geo.crs.vertical_operation_unsupported", "disabled vertical transformation cannot contain steps", "geo.crs.vertical.steps_consistent"));
        }
        if (specification.enabled && specification.steps.empty()) {
            return tsunami::core::failure(crs_error("geo.crs.vertical_reference_missing", "enabled vertical transformation requires explicit steps", "geo.crs.vertical.steps_consistent"));
        }
        for (const auto &step : specification.steps) {
            if (!text_present(step.source_reference) || !text_present(step.target_reference)) {
                return tsunami::core::failure(crs_error("geo.crs.vertical_operation_unsupported", "vertical step source and target references are required", "geo.crs.vertical.relationship_authoritative"));
            }
            if (step.kind == VerticalTransformationStepKind::unit_scale &&
                (!step.scale_factor || !finite_positive(*step.scale_factor))) {
                return tsunami::core::failure(crs_error("geo.crs.vertical_operation_unsupported", "unit-scale vertical step requires a positive scale", "geo.crs.vertical.steps_consistent"));
            }
            if (step.kind == VerticalTransformationStepKind::constant_offset &&
                (!step.offset_m || !std::isfinite(*step.offset_m))) {
                return tsunami::core::failure(crs_error("geo.crs.vertical_operation_unsupported", "constant-offset vertical step requires a finite offset", "geo.crs.vertical.steps_consistent"));
            }
            if (step.kind == VerticalTransformationStepKind::geodetic_grid_operation &&
                (!step.operation_authority || !step.operation_code || !step.required_resource_name)) {
                return tsunami::core::failure(crs_error("geo.crs.vertical_resource_missing", "grid vertical step requires operation and resource metadata", "geo.crs.vertical.coverage_valid"));
            }
        }
        return tsunami::core::success();
    }

    auto validate_coordinate_transformation_request(const CoordinateTransformationRequest &request)
        -> tsunami::core::Result<void>
    {
        if (request.configuration == nullptr || request.manifest == nullptr || request.source_import_record == nullptr) {
            return tsunami::core::failure(crs_error("geo.crs.request_invalid", "transformation request pointers are required", "geo.crs.request.references_match"));
        }
        if (auto case_valid = tsunami::data::validate_case_configuration(*request.configuration); !case_valid) {
            return tsunami::core::failure(crs_error("geo.crs.request_invalid", "case configuration is invalid", "geo.crs.target.case_consistent").with_cause_code(case_valid.error().code()));
        }
        if (auto manifest_valid = tsunami::data::validate_dataset_manifest(*request.manifest); !manifest_valid) {
            return tsunami::core::failure(crs_error("geo.crs.request_invalid", "dataset manifest is invalid", "geo.crs.request.references_match").with_cause_code(manifest_valid.error().code()));
        }
        const auto &id = request.identity;
        if (!logical_id_valid(id.transformation_id) || id.transformation_revision == 0U ||
            !logical_id_valid(id.manifest_id) || id.manifest_revision == 0U ||
            !logical_id_valid(id.source_import_id) || id.source_import_revision == 0U ||
            !logical_id_valid(id.source_dataset_id) || !logical_id_valid(id.source_asset_id) ||
            !logical_id_valid(id.output_dataset_id) || !logical_id_valid(id.output_process_id) ||
            id.output_dataset_id == id.source_dataset_id || !timestamp_valid(id.executed_at_utc)) {
            return tsunami::core::failure(crs_error("geo.crs.request_invalid", "transformation identity is invalid", "geo.crs.request.references_match"));
        }
        if (id.case_revision.case_id != request.configuration->identity().case_id ||
            id.case_revision.revision != request.configuration->identity().revision ||
            id.case_revision != request.manifest->identity().case_revision ||
            id.manifest_id != request.manifest->identity().manifest_id ||
            id.manifest_revision != request.manifest->identity().manifest_revision ||
            id.source_import_id != request.source_import_record->identity.import_id ||
            id.source_import_revision != request.source_import_record->identity.import_revision ||
            id.source_dataset_id != request.source_import_record->identity.dataset_id ||
            id.source_asset_id != request.source_import_record->identity.asset_id) {
            return tsunami::core::failure(crs_error("geo.crs.request_invalid", "case, manifest and source import references do not match", "geo.crs.request.references_match"));
        }
        if (auto target = validate_transformation_target_for_case(request.target, *request.configuration); !target) {
            return target;
        }
        if (auto policy = validate_selection_policy(request.selection_policy); !policy) {
            return policy;
        }
        for (const auto &resource : request.resource_evidence) {
            if (auto valid = validate_resource_evidence(resource); !valid) {
                return valid;
            }
        }
        if (auto vertical = validate_vertical_transformation(request.vertical); !vertical) {
            return vertical;
        }
        return tsunami::core::success();
    }

    auto source_horizontal_reference_from_import_record(const GeospatialImportRecord &record)
        -> tsunami::core::Result<CoordinateReferenceDescriptor>
    {
        const auto &native = record.native_spatial_reference;
        auto descriptor = CoordinateReferenceDescriptor{};
        descriptor.authority_name = native.authority_name;
        descriptor.authority_code = native.authority_code;
        descriptor.name = native.crs_name.value_or(native.authority_name && native.authority_code ? *native.authority_name + ":" + *native.authority_code : "native_source_crs");
        descriptor.canonical_wkt2 = native.canonical_wkt2;
        descriptor.datum_name = native.datum_name;
        descriptor.datum_realisation = record.datum_evidence.horizontal.datum_realisation;
        if (record.datum_evidence.horizontal.coordinate_epoch) {
            try {
                descriptor.coordinate_epoch_decimal_year = std::stod(*record.datum_evidence.horizontal.coordinate_epoch);
            } catch (...) {
                return tsunami::core::failure<CoordinateReferenceDescriptor>(crs_error("geo.crs.source_reference_missing", "source coordinate epoch is not numeric", "geo.crs.epoch.valid"));
            }
        }
        descriptor.axis_names = native.axis_names;
        descriptor.axis_directions = native.axis_directions;
        descriptor.axis_units = native.axis_units;
        if (auto valid = validate_coordinate_reference_descriptor(descriptor); !valid) {
            return tsunami::core::failure<CoordinateReferenceDescriptor>(valid.error());
        }
        return tsunami::core::success(std::move(descriptor));
    }

} // namespace tsunami::geo
