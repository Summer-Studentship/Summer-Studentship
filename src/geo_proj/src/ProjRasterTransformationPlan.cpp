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
        auto include(tsunami::geo::BoundingBox2D &box, tsunami::geo::Point2D point) -> void
        {
            box.minimum_x = std::min(box.minimum_x, point.x);
            box.minimum_y = std::min(box.minimum_y, point.y);
            box.maximum_x = std::max(box.maximum_x, point.x);
            box.maximum_y = std::max(box.maximum_y, point.y);
        }

        [[nodiscard]] auto empty_box() -> tsunami::geo::BoundingBox2D
        {
            return tsunami::geo::BoundingBox2D{
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity()};
        }

        [[nodiscard]] auto transform_xy(PJ *operation, tsunami::geo::Point2D point)
            -> std::optional<tsunami::geo::Point2D>
        {
            proj_errno_reset(operation);
            auto out = proj_trans(operation, PJ_FWD, proj_coord(point.x, point.y, 0.0, 0.0));
            auto result = tsunami::geo::Point2D{out.xy.x, out.xy.y};
            if (proj_errno(operation) != 0 || !std::isfinite(result.x) || !std::isfinite(result.y)) {
                return std::nullopt;
            }
            return result;
        }

        [[nodiscard]] auto densified_boundary(const tsunami::geo::BoundingBox2D &box, std::uint32_t densification)
            -> std::vector<tsunami::geo::Point2D>
        {
            auto points = std::vector<tsunami::geo::Point2D>{};
            const auto steps = static_cast<std::uint32_t>(std::max(2U, densification));
            auto add_edge = [&points, steps](tsunami::geo::Point2D a, tsunami::geo::Point2D b) {
                for (std::uint32_t i = 0; i < steps; ++i) {
                    const auto t = static_cast<double>(i) / static_cast<double>(steps - 1U);
                    points.push_back(tsunami::geo::Point2D{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
                }
            };
            const auto sw = tsunami::geo::Point2D{box.minimum_x, box.minimum_y};
            const auto se = tsunami::geo::Point2D{box.maximum_x, box.minimum_y};
            const auto ne = tsunami::geo::Point2D{box.maximum_x, box.maximum_y};
            const auto nw = tsunami::geo::Point2D{box.minimum_x, box.maximum_y};
            add_edge(sw, se);
            add_edge(se, ne);
            add_edge(ne, nw);
            add_edge(nw, sw);
            return points;
        }
    }

    auto plan_raster_transformation_with_proj(
        const tsunami::geo::CoordinateTransformationRequest &request,
        const tsunami::geo::ImportedRaster &source)
        -> tsunami::core::Result<tsunami::geo::RasterTransformationPlanResult>
    {
        auto bundle = detail::create_operation(request);
        if (!bundle) {
            return tsunami::core::failure<tsunami::geo::RasterTransformationPlanResult>(bundle.error());
        }
        auto operation = std::move(bundle).value();
        constexpr auto densification = 21U;
        auto boundary = densified_boundary(source.extent(), densification);
        auto transformed_boundary = std::vector<tsunami::geo::Point2D>{};
        transformed_boundary.reserve(boundary.size());
        auto target_extent = empty_box();
        for (const auto point : boundary) {
            auto transformed = transform_xy(operation.operation.get(), point);
            if (!transformed) {
                return tsunami::core::failure<tsunami::geo::RasterTransformationPlanResult>(detail::proj_error("geo.crs.bounds_transform_failed", "raster boundary transformation failed", "geo.crs.raster.bounds.valid", "plan_raster_transformation_with_proj"));
            }
            transformed_boundary.push_back(*transformed);
            include(target_extent, *transformed);
        }
        auto source_reference = tsunami::geo::source_horizontal_reference_from_import_record(*request.source_import_record);
        if (!source_reference) {
            return tsunami::core::failure<tsunami::geo::RasterTransformationPlanResult>(source_reference.error());
        }
        auto plan = tsunami::geo::RasterTransformationPlan{
            source_reference.value(),
            request.target,
            source.transform(),
            source.extent(),
            target_extent,
            transformed_boundary,
            source.width(),
            source.height(),
            densification,
            request.vertical,
            operation.record};
        if (auto valid = tsunami::geo::validate_raster_transformation_plan(plan); !valid) {
            return tsunami::core::failure<tsunami::geo::RasterTransformationPlanResult>(valid.error());
        }
        auto diagnostics = tsunami::geo::CoordinateTransformationDiagnostics{
            static_cast<std::uint64_t>(boundary.size()),
            static_cast<std::uint64_t>(transformed_boundary.size()),
            0U,
            0.0,
            0.0,
            std::nullopt,
            source.extent(),
            target_extent,
            {}};
        auto record = detail::make_base_record(request, operation.record, source.extent(), target_extent, diagnostics);
        if (!record) {
            return tsunami::core::failure<tsunami::geo::RasterTransformationPlanResult>(record.error());
        }
        return tsunami::core::success(tsunami::geo::RasterTransformationPlanResult{
            std::move(plan),
            std::move(record).value(),
            diagnostics});
    }

} // namespace tsunami::geo_proj
