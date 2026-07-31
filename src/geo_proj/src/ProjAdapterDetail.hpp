#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <proj.h>

#include <tsunami/core/Error.hpp>
#include <tsunami/geo/CoordinateTransformationPlan.hpp>
#include <tsunami/geo/TransformedVector.hpp>

namespace tsunami::geo_proj::detail
{
    struct ContextDeleter
    {
        auto operator()(PJ_CONTEXT *context) const noexcept -> void
        {
            if (context != nullptr) {
                proj_context_destroy(context);
            }
        }
    };

    struct ObjectDeleter
    {
        auto operator()(PJ *object) const noexcept -> void
        {
            if (object != nullptr) {
                proj_destroy(object);
            }
        }
    };

    struct AreaDeleter
    {
        auto operator()(PJ_AREA *area) const noexcept -> void
        {
            if (area != nullptr) {
                proj_area_destroy(area);
            }
        }
    };

    using ContextHandle = std::unique_ptr<PJ_CONTEXT, ContextDeleter>;
    using ObjectHandle = std::unique_ptr<PJ, ObjectDeleter>;
    using AreaHandle = std::unique_ptr<PJ_AREA, AreaDeleter>;

    struct OperationBundle
    {
        ContextHandle context;
        ObjectHandle operation;
        tsunami::geo::CoordinateOperationRecord record;
    };

    [[nodiscard]] auto proj_error(
        std::string code,
        std::string message,
        std::string rule_id,
        std::string operation) -> tsunami::core::Error;

    [[nodiscard]] auto make_context(const std::filesystem::path &resource_root)
        -> tsunami::core::Result<ContextHandle>;

    [[nodiscard]] auto create_operation(const tsunami::geo::CoordinateTransformationRequest &request)
        -> tsunami::core::Result<OperationBundle>;

    [[nodiscard]] auto descriptor_definition(const tsunami::geo::CoordinateReferenceDescriptor &descriptor)
        -> std::string;

    [[nodiscard]] auto descriptor_from_object(PJ_CONTEXT *context, PJ *object, const tsunami::geo::CoordinateReferenceDescriptor &fallback)
        -> tsunami::geo::CoordinateReferenceDescriptor;

    [[nodiscard]] auto grids_from_operation(PJ_CONTEXT *context, PJ *operation)
        -> std::vector<tsunami::geo::CoordinateOperationGrid>;

    [[nodiscard]] auto make_base_record(
        const tsunami::geo::CoordinateTransformationRequest &request,
        const tsunami::geo::CoordinateOperationRecord &operation,
        tsunami::geo::BoundingBox2D source_extent,
        tsunami::geo::BoundingBox2D target_extent,
        tsunami::geo::CoordinateTransformationDiagnostics diagnostics)
        -> tsunami::core::Result<tsunami::geo::CoordinateTransformationRecord>;

} // namespace tsunami::geo_proj::detail
