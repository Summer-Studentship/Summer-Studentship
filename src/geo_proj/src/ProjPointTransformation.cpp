#include <tsunami/geo_proj/ProjCoordinateTransformer.hpp>

#include "ProjAdapterDetail.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace tsunami::geo_proj
{
    namespace
    {
        [[nodiscard]] auto extent_from_points(std::span<const tsunami::geo::Coordinate3D> points)
            -> tsunami::geo::BoundingBox2D
        {
            auto box = tsunami::geo::BoundingBox2D{
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity()};
            for (const auto point : points) {
                box.minimum_x = std::min(box.minimum_x, point.x);
                box.minimum_y = std::min(box.minimum_y, point.y);
                box.maximum_x = std::max(box.maximum_x, point.x);
                box.maximum_y = std::max(box.maximum_y, point.y);
            }
            return box;
        }

        [[nodiscard]] auto finite(tsunami::geo::Coordinate3D point) noexcept -> bool
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }
    }

    auto transform_points_with_proj(
        const tsunami::geo::CoordinateTransformationRequest &request,
        std::span<const tsunami::geo::Coordinate3D> source_points)
        -> tsunami::core::Result<tsunami::geo::PointTransformationResult>
    {
        if (source_points.empty() || !std::all_of(source_points.begin(), source_points.end(), finite)) {
            return tsunami::core::failure<tsunami::geo::PointTransformationResult>(detail::proj_error("geo.crs.coordinate_nonfinite", "source point coordinates must be finite", "geo.crs.coordinate.finite", "transform_points_with_proj"));
        }
        auto bundle = detail::create_operation(request);
        if (!bundle) {
            return tsunami::core::failure<tsunami::geo::PointTransformationResult>(bundle.error());
        }
        auto operation = std::move(bundle).value();
        auto transformed = std::vector<tsunami::geo::Coordinate3D>{};
        transformed.reserve(source_points.size());
        for (std::size_t i = 0; i < source_points.size(); ++i) {
            const auto input = source_points[i];
            proj_errno_reset(operation.operation.get());
            auto output = proj_trans(operation.operation.get(), PJ_FWD, proj_coord(input.x, input.y, input.z, 0.0));
            auto point = tsunami::geo::Coordinate3D{output.xyz.x, output.xyz.y, output.xyz.z};
            if (!finite(point) || proj_errno(operation.operation.get()) != 0) {
                return tsunami::core::failure<tsunami::geo::PointTransformationResult>(detail::proj_error("geo.crs.point_transform_failed", "coordinate transformation failed", "geo.crs.coordinate.finite", "transform_points_with_proj").add_context("coordinate_index", std::to_string(i)));
            }
            transformed.push_back(point);
        }
        const auto source_extent = extent_from_points(source_points);
        const auto target_extent = extent_from_points(transformed);
        auto diagnostics = tsunami::geo::CoordinateTransformationDiagnostics{
            static_cast<std::uint64_t>(source_points.size()),
            static_cast<std::uint64_t>(transformed.size()),
            0U,
            0.0,
            0.0,
            std::nullopt,
            source_extent,
            target_extent,
            {}};
        auto record = detail::make_base_record(request, operation.record, source_extent, target_extent, diagnostics);
        if (!record) {
            return tsunami::core::failure<tsunami::geo::PointTransformationResult>(record.error());
        }
        auto source_reference = tsunami::geo::source_horizontal_reference_from_import_record(*request.source_import_record);
        if (!source_reference) {
            return tsunami::core::failure<tsunami::geo::PointTransformationResult>(source_reference.error());
        }
        auto point_set = tsunami::geo::make_transformed_point_set(source_reference.value(), request.target, transformed);
        if (!point_set) {
            return tsunami::core::failure<tsunami::geo::PointTransformationResult>(point_set.error());
        }
        return tsunami::core::success(tsunami::geo::PointTransformationResult{
            std::move(point_set).value(),
            std::move(record).value(),
            diagnostics});
    }

} // namespace tsunami::geo_proj
