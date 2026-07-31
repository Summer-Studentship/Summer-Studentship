#pragma once

#include <cstdint>
#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/geo/CoordinateTransformationRecord.hpp>
#include <tsunami/geo/ImportedRaster.hpp>

namespace tsunami::geo
{
    struct RasterTransformationPlan
    {
        CoordinateReferenceDescriptor source_reference;
        ComputationalTargetReference target_reference;
        RasterAffineTransform source_transform;
        BoundingBox2D source_extent;
        BoundingBox2D transformed_extent;
        std::vector<Point2D> transformed_boundary;
        std::uint64_t source_width{};
        std::uint64_t source_height{};
        std::uint32_t boundary_densification_points{21U};
        VerticalTransformationSpecification vertical;
        CoordinateOperationRecord horizontal_operation;

        [[nodiscard]] auto operator==(const RasterTransformationPlan &) const -> bool = default;
    };

    [[nodiscard]] auto validate_raster_transformation_plan(const RasterTransformationPlan &plan)
        -> tsunami::core::Result<void>;

    struct RasterTransformationPlanResult
    {
        RasterTransformationPlan plan;
        CoordinateTransformationRecord record;
        CoordinateTransformationDiagnostics diagnostics;
    };

} // namespace tsunami::geo
