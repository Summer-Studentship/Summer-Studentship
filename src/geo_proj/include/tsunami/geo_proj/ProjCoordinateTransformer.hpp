#pragma once

#include <span>
#include <string>
#include <string_view>

#include <tsunami/geo/CoordinateTransformationPlan.hpp>
#include <tsunami/geo/TransformedVector.hpp>

namespace tsunami::geo_proj
{
    [[nodiscard]] auto transformation_runtime_version() -> std::string;
    [[nodiscard]] auto transformation_database_version() -> std::string;
    [[nodiscard]] auto transformation_network_enabled_by_default() -> bool;

    [[nodiscard]] auto transform_points_with_proj(
        const tsunami::geo::CoordinateTransformationRequest &request,
        std::span<const tsunami::geo::Coordinate3D> source_points)
        -> tsunami::core::Result<tsunami::geo::PointTransformationResult>;

    [[nodiscard]] auto transform_vector_layer_with_proj(
        const tsunami::geo::CoordinateTransformationRequest &request,
        const tsunami::geo::ImportedVectorLayer &source)
        -> tsunami::core::Result<tsunami::geo::VectorTransformationResult>;

    [[nodiscard]] auto plan_raster_transformation_with_proj(
        const tsunami::geo::CoordinateTransformationRequest &request,
        const tsunami::geo::ImportedRaster &source)
        -> tsunami::core::Result<tsunami::geo::RasterTransformationPlanResult>;

} // namespace tsunami::geo_proj
