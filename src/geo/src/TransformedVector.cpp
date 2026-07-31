#include <tsunami/geo/TransformedVector.hpp>

#include <algorithm>
#include <cmath>
#include <set>

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
            error.add_context("operation", "validate_transformed_vector")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto finite(Coordinate3D point) noexcept -> bool
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
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

        [[nodiscard]] auto geometry_valid(const ImportedGeometry &geometry) noexcept -> bool
        {
            return std::visit([](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, Point2D>) {
                    return finite(value);
                } else if constexpr (std::is_same_v<T, LineString2D>) {
                    return !value.points.empty() && std::all_of(value.points.begin(), value.points.end(), [](Point2D point) {
                        return finite(point);
                    });
                } else {
                    if (value.exterior_ring.size() < 4U || value.exterior_ring.front() != value.exterior_ring.back()) {
                        return false;
                    }
                    if (!std::all_of(value.exterior_ring.begin(), value.exterior_ring.end(), [](Point2D point) {
                            return finite(point);
                        })) {
                        return false;
                    }
                    return std::all_of(value.interior_rings.begin(), value.interior_rings.end(), [](const auto &ring) {
                        return ring.size() >= 4U && ring.front() == ring.back() &&
                            std::all_of(ring.begin(), ring.end(), [](Point2D point) {
                                return finite(point);
                            });
                    });
                }
            }, geometry);
        }
    }

    TransformedPointSet::TransformedPointSet(
        CoordinateReferenceDescriptor source_reference,
        ComputationalTargetReference target_reference,
        std::vector<Coordinate3D> coordinates)
        : source_reference_{std::move(source_reference)},
          target_reference_{std::move(target_reference)},
          coordinates_{std::move(coordinates)}
    {
    }

    TransformedVectorLayer::TransformedVectorLayer(
        std::string source_layer_name,
        ImportedGeometryKind geometry_kind,
        ComputationalTargetReference target_reference,
        std::vector<ImportedFieldSchema> field_schema,
        std::vector<ImportedVectorFeature> features,
        BoundingBox2D extent)
        : source_layer_name_{std::move(source_layer_name)},
          geometry_kind_{geometry_kind},
          target_reference_{std::move(target_reference)},
          field_schema_{std::move(field_schema)},
          features_{std::move(features)},
          extent_{extent}
    {
    }

    auto make_transformed_point_set(
        CoordinateReferenceDescriptor source_reference,
        ComputationalTargetReference target_reference,
        std::vector<Coordinate3D> coordinates) -> tsunami::core::Result<TransformedPointSet>
    {
        if (auto source = validate_coordinate_reference_descriptor(source_reference); !source) {
            return tsunami::core::failure<TransformedPointSet>(source.error());
        }
        if (target_reference.horizontal_unit != "m") {
            return tsunami::core::failure<TransformedPointSet>(crs_error("geo.crs.target_reference_invalid", "transformed points require metric target storage", "geo.crs.unit.horizontal_metres"));
        }
        if (coordinates.empty() || !std::all_of(coordinates.begin(), coordinates.end(), [](Coordinate3D point) {
                return finite(point);
            })) {
            return tsunami::core::failure<TransformedPointSet>(crs_error("geo.crs.coordinate_nonfinite", "transformed point coordinates must be finite", "geo.crs.coordinate.finite"));
        }
        return tsunami::core::success(TransformedPointSet{std::move(source_reference), std::move(target_reference), std::move(coordinates)});
    }

    auto make_transformed_vector_layer(
        std::string source_layer_name,
        ImportedGeometryKind geometry_kind,
        ComputationalTargetReference target_reference,
        std::vector<ImportedFieldSchema> field_schema,
        std::vector<ImportedVectorFeature> features,
        BoundingBox2D extent) -> tsunami::core::Result<TransformedVectorLayer>
    {
        if (source_layer_name.empty() || features.empty() || target_reference.horizontal_unit != "m" || !finite_extent(extent)) {
            return tsunami::core::failure<TransformedVectorLayer>(crs_error("geo.crs.vector_transform_failed", "transformed vector layer metadata is invalid", "geo.crs.vector.valid"));
        }
        auto feature_ids = std::set<std::int64_t>{};
        for (const auto &feature : features) {
            if (!feature_ids.insert(feature.feature_id).second || !geometry_valid(feature.geometry) ||
                feature.attributes.size() != field_schema.size()) {
                return tsunami::core::failure<TransformedVectorLayer>(crs_error("geo.crs.vector_transform_failed", "transformed vector features are invalid", "geo.crs.vector.valid").add_context("feature_id", std::to_string(feature.feature_id)));
            }
        }
        return tsunami::core::success(TransformedVectorLayer{
            std::move(source_layer_name),
            geometry_kind,
            std::move(target_reference),
            std::move(field_schema),
            std::move(features),
            extent});
    }

} // namespace tsunami::geo
