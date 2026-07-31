#include <tsunami/geo_proj/ProjCoordinateTransformer.hpp>

#include "ProjAdapterDetail.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

namespace tsunami::geo_proj
{
    namespace
    {
        [[nodiscard]] auto finite(tsunami::geo::Point2D point) noexcept -> bool
        {
            return std::isfinite(point.x) && std::isfinite(point.y);
        }

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

        [[nodiscard]] auto transform_point(PJ *operation, tsunami::geo::Point2D point)
            -> std::optional<tsunami::geo::Point2D>
        {
            proj_errno_reset(operation);
            auto out = proj_trans(operation, PJ_FWD, proj_coord(point.x, point.y, 0.0, 0.0));
            auto result = tsunami::geo::Point2D{out.xy.x, out.xy.y};
            if (proj_errno(operation) != 0 || !finite(result)) {
                return std::nullopt;
            }
            return result;
        }

        [[nodiscard]] auto transform_geometry(PJ *operation, const tsunami::geo::ImportedGeometry &geometry)
            -> std::optional<tsunami::geo::ImportedGeometry>
        {
            return std::visit([operation](const auto &value) -> std::optional<tsunami::geo::ImportedGeometry> {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, tsunami::geo::Point2D>) {
                    auto point = transform_point(operation, value);
                    if (!point) {
                        return std::nullopt;
                    }
                    return tsunami::geo::ImportedGeometry{*point};
                } else if constexpr (std::is_same_v<T, tsunami::geo::LineString2D>) {
                    auto line = tsunami::geo::LineString2D{};
                    line.points.reserve(value.points.size());
                    for (const auto point : value.points) {
                        auto transformed = transform_point(operation, point);
                        if (!transformed) {
                            return std::nullopt;
                        }
                        line.points.push_back(*transformed);
                    }
                    return tsunami::geo::ImportedGeometry{std::move(line)};
                } else {
                    auto polygon = tsunami::geo::Polygon2D{};
                    polygon.exterior_ring.reserve(value.exterior_ring.size());
                    for (const auto point : value.exterior_ring) {
                        auto transformed = transform_point(operation, point);
                        if (!transformed) {
                            return std::nullopt;
                        }
                        polygon.exterior_ring.push_back(*transformed);
                    }
                    polygon.interior_rings.reserve(value.interior_rings.size());
                    for (const auto &ring : value.interior_rings) {
                        auto out_ring = std::vector<tsunami::geo::Point2D>{};
                        out_ring.reserve(ring.size());
                        for (const auto point : ring) {
                            auto transformed = transform_point(operation, point);
                            if (!transformed) {
                                return std::nullopt;
                            }
                            out_ring.push_back(*transformed);
                        }
                        polygon.interior_rings.push_back(std::move(out_ring));
                    }
                    return tsunami::geo::ImportedGeometry{std::move(polygon)};
                }
            }, geometry);
        }

        auto include_geometry(tsunami::geo::BoundingBox2D &box, const tsunami::geo::ImportedGeometry &geometry) -> void
        {
            std::visit([&box](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, tsunami::geo::Point2D>) {
                    include(box, value);
                } else if constexpr (std::is_same_v<T, tsunami::geo::LineString2D>) {
                    for (const auto point : value.points) {
                        include(box, point);
                    }
                } else {
                    for (const auto point : value.exterior_ring) {
                        include(box, point);
                    }
                    for (const auto &ring : value.interior_rings) {
                        for (const auto point : ring) {
                            include(box, point);
                        }
                    }
                }
            }, geometry);
        }
    }

    auto transform_vector_layer_with_proj(
        const tsunami::geo::CoordinateTransformationRequest &request,
        const tsunami::geo::ImportedVectorLayer &source)
        -> tsunami::core::Result<tsunami::geo::VectorTransformationResult>
    {
        auto bundle = detail::create_operation(request);
        if (!bundle) {
            return tsunami::core::failure<tsunami::geo::VectorTransformationResult>(bundle.error());
        }
        auto operation = std::move(bundle).value();
        auto features = std::vector<tsunami::geo::ImportedVectorFeature>{};
        features.reserve(source.features().size());
        auto target_extent = empty_box();
        auto coordinate_count = std::uint64_t{0U};
        for (const auto &feature : source.features()) {
            auto geometry = transform_geometry(operation.operation.get(), feature.geometry);
            if (!geometry) {
                return tsunami::core::failure<tsunami::geo::VectorTransformationResult>(detail::proj_error("geo.crs.vector_transform_failed", "vector coordinate transformation failed", "geo.crs.coordinate.finite", "transform_vector_layer_with_proj").add_context("feature_id", std::to_string(feature.feature_id)));
            }
            coordinate_count += static_cast<std::uint64_t>(tsunami::geo::coordinate_count(*geometry));
            include_geometry(target_extent, *geometry);
            features.push_back(tsunami::geo::ImportedVectorFeature{feature.feature_id, std::move(*geometry), feature.attributes});
        }
        auto diagnostics = tsunami::geo::CoordinateTransformationDiagnostics{
            coordinate_count,
            coordinate_count,
            0U,
            0.0,
            0.0,
            std::nullopt,
            source.extent(),
            target_extent,
            {}};
        auto record = detail::make_base_record(request, operation.record, source.extent(), target_extent, diagnostics);
        if (!record) {
            return tsunami::core::failure<tsunami::geo::VectorTransformationResult>(record.error());
        }
        auto layer = tsunami::geo::make_transformed_vector_layer(
            std::string{source.layer_name()},
            source.geometry_kind(),
            request.target,
            source.field_schema(),
            std::move(features),
            target_extent);
        if (!layer) {
            return tsunami::core::failure<tsunami::geo::VectorTransformationResult>(layer.error());
        }
        return tsunami::core::success(tsunami::geo::VectorTransformationResult{
            std::move(layer).value(),
            std::move(record).value(),
            diagnostics});
    }

} // namespace tsunami::geo_proj
