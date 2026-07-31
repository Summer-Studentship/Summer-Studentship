#include "ProjAdapterDetail.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace tsunami::geo_proj::detail
{
    namespace
    {
        [[nodiscard]] auto contains(
            const tsunami::geo::GeographicAreaOfInterest &outer,
            const tsunami::geo::GeographicAreaOfInterest &inner) noexcept -> bool
        {
            return outer.west_longitude_degrees <= inner.west_longitude_degrees &&
                outer.east_longitude_degrees >= inner.east_longitude_degrees &&
                outer.south_latitude_degrees <= inner.south_latitude_degrees &&
                outer.north_latitude_degrees >= inner.north_latitude_degrees;
        }

        [[nodiscard]] auto crs_from_descriptor(
            PJ_CONTEXT *context,
            const tsunami::geo::CoordinateReferenceDescriptor &descriptor,
            std::string_view code) -> tsunami::core::Result<ObjectHandle>
        {
            const auto definition = descriptor_definition(descriptor);
            auto object = ObjectHandle{proj_create(context, definition.c_str())};
            if (!object || proj_is_crs(object.get()) == 0) {
                return tsunami::core::failure<ObjectHandle>(proj_error(
                    std::string{code},
                    "could not create coordinate reference object",
                    "geo.crs.reference.create",
                    "create_coordinate_reference"));
            }
            return tsunami::core::success(std::move(object));
        }

        [[nodiscard]] auto optional_text(const char *value) -> std::optional<std::string>
        {
            if (value == nullptr || *value == '\0') {
                return std::nullopt;
            }
            return std::string{value};
        }

        [[nodiscard]] auto area_of_use(PJ_CONTEXT *context, PJ *object) -> std::optional<tsunami::geo::GeographicAreaOfInterest>
        {
            double west = 0.0;
            double south = 0.0;
            double east = 0.0;
            double north = 0.0;
            const char *name = nullptr;
            if (proj_get_area_of_use(context, object, &west, &south, &east, &north, &name) == 0) {
                return std::nullopt;
            }
            return tsunami::geo::GeographicAreaOfInterest{west, south, east, north};
        }

        [[nodiscard]] auto matching_verified_evidence(
            const std::vector<tsunami::geo::GeodeticResourceEvidence> &evidence,
            std::string_view grid_name) -> bool
        {
            return std::any_of(evidence.begin(), evidence.end(), [grid_name](const auto &item) {
                return item.resource_name == grid_name &&
                    item.verification_status == tsunami::geo::GeodeticResourceVerificationStatus::externally_verified;
            });
        }

        auto apply_resource_evidence(
            const std::vector<tsunami::geo::GeodeticResourceEvidence> &evidence,
            std::vector<tsunami::geo::CoordinateOperationGrid> &grids) -> void
        {
            for (auto &grid : grids) {
                for (const auto &item : evidence) {
                    if (item.resource_name == grid.short_name) {
                        if (item.resolved_path) {
                            grid.full_path = item.resolved_path->generic_string();
                        }
                        grid.source_uri = item.source_document_uri;
                        grid.declared_digest = item.digest;
                        grid.verification_status = item.verification_status;
                    }
                }
            }
        }
    }

    auto create_operation(const tsunami::geo::CoordinateTransformationRequest &request)
        -> tsunami::core::Result<OperationBundle>
    {
        if (auto valid = tsunami::geo::validate_coordinate_transformation_request(request); !valid) {
            return tsunami::core::failure<OperationBundle>(valid.error());
        }
        auto context_result = make_context(request.resource_root);
        if (!context_result) {
            return tsunami::core::failure<OperationBundle>(context_result.error());
        }
        auto context = std::move(context_result).value();
        auto source_descriptor = tsunami::geo::source_horizontal_reference_from_import_record(*request.source_import_record);
        if (!source_descriptor) {
            return tsunami::core::failure<OperationBundle>(source_descriptor.error());
        }
        auto source_crs = crs_from_descriptor(context.get(), source_descriptor.value(), "geo.crs.source_crs_creation_failed");
        if (!source_crs) {
            return tsunami::core::failure<OperationBundle>(source_crs.error());
        }
        auto target_crs = crs_from_descriptor(context.get(), request.target.horizontal, "geo.crs.target_crs_creation_failed");
        if (!target_crs) {
            return tsunami::core::failure<OperationBundle>(target_crs.error());
        }

        auto area = AreaHandle{proj_area_create()};
        proj_area_set_bbox(
            area.get(),
            request.selection_policy.area_of_interest.west_longitude_degrees,
            request.selection_policy.area_of_interest.south_latitude_degrees,
            request.selection_policy.area_of_interest.east_longitude_degrees,
            request.selection_policy.area_of_interest.north_latitude_degrees);

        auto option_strings = std::vector<std::string>{"ALLOW_BALLPARK=NO", "ONLY_BEST=YES"};
        if (request.selection_policy.authority) {
            option_strings.push_back("AUTHORITY=" + *request.selection_policy.authority);
        }
        option_strings.push_back("ACCURACY=" + std::to_string(request.selection_policy.accuracy.maximum_operation_accuracy_m));
        auto option_ptrs = std::vector<const char *>{};
        for (const auto &option : option_strings) {
            option_ptrs.push_back(option.c_str());
        }
        option_ptrs.push_back(nullptr);

        auto raw = ObjectHandle{proj_create_crs_to_crs_from_pj(
            context.get(),
            source_crs.value().get(),
            target_crs.value().get(),
            area.get(),
            option_ptrs.data())};
        if (!raw) {
            return tsunami::core::failure<OperationBundle>(proj_error("geo.crs.operation_not_found", "no coordinate operation satisfies the request", "geo.crs.operation.best_required", "select_coordinate_operation"));
        }
        auto operation = ObjectHandle{proj_normalize_for_visualization(context.get(), raw.get())};
        if (!operation) {
            return tsunami::core::failure<OperationBundle>(proj_error("geo.crs.operation_not_instantiable", "could not normalise coordinate operation for project storage axes", "geo.crs.axis.project_storage_explicit", "select_coordinate_operation"));
        }
        const auto normalise_errno = proj_errno(operation.get());
        if (normalise_errno != 0) {
            return tsunami::core::failure<OperationBundle>(proj_error("geo.crs.resource_missing", "selected operation could not open a required local transformation resource", "geo.crs.operation.resources_available", "select_coordinate_operation")
                                                             .add_context("native_errno", std::to_string(normalise_errno))
                                                             .add_context("native_error", proj_errno_string(normalise_errno) != nullptr ? proj_errno_string(normalise_errno) : "unknown"));
        }
        auto *metadata_operation = raw.get();

        proj_errno_reset(metadata_operation);
        const auto ballpark = proj_coordoperation_has_ballpark_transformation(context.get(), metadata_operation) != 0;
        const auto metadata_errno = proj_errno(metadata_operation);
        if (metadata_errno != 0) {
            return tsunami::core::failure<OperationBundle>(proj_error("geo.crs.resource_missing", "selected operation metadata could not be resolved from local transformation resources", "geo.crs.operation.resources_available", "select_coordinate_operation")
                                                             .add_context("native_errno", std::to_string(metadata_errno))
                                                             .add_context("native_error", proj_errno_string(metadata_errno) != nullptr ? proj_errno_string(metadata_errno) : "unknown"));
        }
        if (ballpark) {
            return tsunami::core::failure<OperationBundle>(proj_error("geo.crs.operation_ballpark", "selected coordinate operation is ballpark", "geo.crs.operation.ballpark_forbidden", "select_coordinate_operation"));
        }
        auto accuracy = proj_coordoperation_get_accuracy(context.get(), metadata_operation);
        auto accuracy_value = std::optional<double>{};
        if (accuracy >= 0.0 && std::isfinite(accuracy)) {
            accuracy_value = accuracy;
        } else if (request.selection_policy.accuracy.require_reported_operation_accuracy) {
            return tsunami::core::failure<OperationBundle>(proj_error("geo.crs.operation_accuracy_unknown", "selected coordinate operation has no reported accuracy", "geo.crs.operation.accuracy_accepted", "select_coordinate_operation"));
        }
        if (accuracy_value && *accuracy_value > request.selection_policy.accuracy.maximum_operation_accuracy_m) {
            return tsunami::core::failure<OperationBundle>(proj_error("geo.crs.operation_accuracy_exceeded", "selected coordinate operation exceeds requested accuracy", "geo.crs.operation.accuracy_accepted", "select_coordinate_operation")
                                                             .add_context("operation_accuracy_m", std::to_string(*accuracy_value)));
        }
        const auto op_area = area_of_use(context.get(), metadata_operation);
        if (request.selection_policy.accuracy.require_area_of_use_coverage &&
            (!op_area || !contains(*op_area, request.selection_policy.area_of_interest))) {
            return tsunami::core::failure<OperationBundle>(proj_error("geo.crs.area_of_use_mismatch", "selected operation area does not cover request", "geo.crs.area.operation_covers", "select_coordinate_operation"));
        }

        auto grids = grids_from_operation(context.get(), metadata_operation);
        apply_resource_evidence(request.resource_evidence, grids);
        for (const auto &grid : grids) {
            if (!grid.available) {
                return tsunami::core::failure<OperationBundle>(proj_error("geo.crs.resource_missing", "selected operation requires an unavailable grid", "geo.crs.operation.resources_available", "select_coordinate_operation").add_context("grid_name", grid.short_name));
            }
            if (request.selection_policy.accuracy.require_verified_grid_resources &&
                !matching_verified_evidence(request.resource_evidence, grid.short_name)) {
                return tsunami::core::failure<OperationBundle>(proj_error("geo.crs.resource_unverified", "selected operation grid lacks accepted resource evidence", "geo.crs.operation.resources_verified", "select_coordinate_operation").add_context("grid_name", grid.short_name));
            }
        }

        const auto *engine = "PROJ";
        const auto info = proj_info();
        auto operation_record = tsunami::geo::CoordinateOperationRecord{};
        operation_record.operation_name = proj_get_name(metadata_operation) != nullptr ? proj_get_name(metadata_operation) : "selected_coordinate_operation";
        operation_record.operation_authority = optional_text(proj_get_id_auth_name(metadata_operation, 0));
        operation_record.operation_code = optional_text(proj_get_id_code(metadata_operation, 0));
        const char *method_name = nullptr;
        const char *method_auth = nullptr;
        const char *method_code = nullptr;
        if (proj_coordoperation_get_method_info(context.get(), metadata_operation, &method_name, &method_auth, &method_code) != 0) {
            operation_record.operation_method = optional_text(method_name);
        }
        proj_errno_reset(metadata_operation);
        operation_record.operation_accuracy_m = accuracy_value;
        operation_record.scope = optional_text(proj_get_scope(metadata_operation));
        operation_record.area_of_use = op_area;
        operation_record.canonical_wkt2 = optional_text(proj_as_wkt(context.get(), metadata_operation, PJ_WKT2_2019, nullptr));
        operation_record.canonical_projjson = optional_text(proj_as_projjson(context.get(), metadata_operation, nullptr));
        operation_record.canonical_pipeline = optional_text(proj_as_proj_string(context.get(), operation.get(), PJ_PROJ_5, nullptr));
        operation_record.ballpark = false;
        operation_record.source_crs = descriptor_from_object(context.get(), source_crs.value().get(), source_descriptor.value());
        operation_record.target_crs = descriptor_from_object(context.get(), target_crs.value().get(), request.target.horizontal);
        operation_record.grids = grids;
        operation_record.engine_name = engine;
        operation_record.engine_version = info.version != nullptr ? std::string{info.version} : std::string{};
        operation_record.database_version = optional_text(proj_context_get_database_metadata(context.get(), "DATABASE.LAYOUT.VERSION.MAJOR"));
        if (auto valid = tsunami::geo::validate_coordinate_operation_record(operation_record); !valid) {
            return tsunami::core::failure<OperationBundle>(valid.error());
        }
        return tsunami::core::success(OperationBundle{std::move(context), std::move(operation), std::move(operation_record)});
    }

} // namespace tsunami::geo_proj::detail
