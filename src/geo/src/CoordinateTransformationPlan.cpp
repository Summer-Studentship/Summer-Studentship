#include <tsunami/geo/CoordinateTransformationPlan.hpp>

#include <algorithm>
#include <cmath>

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
            error.add_context("operation", "validate_raster_transformation_plan")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto finite(Point2D point) noexcept -> bool
        {
            return std::isfinite(point.x) && std::isfinite(point.y);
        }

        [[nodiscard]] auto finite_extent(const BoundingBox2D &extent) noexcept -> bool
        {
            return std::isfinite(extent.minimum_x) && std::isfinite(extent.minimum_y) &&
                std::isfinite(extent.maximum_x) && std::isfinite(extent.maximum_y) &&
                extent.minimum_x <= extent.maximum_x && extent.minimum_y <= extent.maximum_y;
        }
    }

    auto validate_raster_transformation_plan(const RasterTransformationPlan &plan)
        -> tsunami::core::Result<void>
    {
        if (auto source = validate_coordinate_reference_descriptor(plan.source_reference); !source) {
            return source;
        }
        if (plan.source_width == 0U || plan.source_height == 0U ||
            plan.boundary_densification_points < 4U || plan.transformed_boundary.size() < 4U ||
            !finite_extent(plan.source_extent) || !finite_extent(plan.transformed_extent)) {
            return tsunami::core::failure(crs_error("geo.crs.raster_plan_failed", "raster transformation plan metadata is invalid", "geo.crs.raster.plan.valid"));
        }
        if (!std::isfinite(plan.source_transform.origin_x) ||
            !std::isfinite(plan.source_transform.pixel_width) ||
            !std::isfinite(plan.source_transform.row_rotation) ||
            !std::isfinite(plan.source_transform.origin_y) ||
            !std::isfinite(plan.source_transform.column_rotation) ||
            !std::isfinite(plan.source_transform.pixel_height)) {
            return tsunami::core::failure(crs_error("geo.crs.raster_plan_failed", "source affine transform is invalid", "geo.crs.raster.plan.valid"));
        }
        if (!std::all_of(plan.transformed_boundary.begin(), plan.transformed_boundary.end(), finite)) {
            return tsunami::core::failure(crs_error("geo.crs.bounds_transform_failed", "transformed raster boundary contains nonfinite coordinates", "geo.crs.raster.bounds.valid"));
        }
        if (auto vertical = validate_vertical_transformation(plan.vertical); !vertical) {
            return vertical;
        }
        if (auto operation = validate_coordinate_operation_record(plan.horizontal_operation); !operation) {
            return operation;
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
