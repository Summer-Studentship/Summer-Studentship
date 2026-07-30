#include <tsunami/geo/ImportedVector.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto geo_error(std::string code, std::string message, std::string rule_id)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_imported_vector_layer")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto finite(Point2D point) noexcept -> bool
        {
            return std::isfinite(point.x) && std::isfinite(point.y);
        }

        [[nodiscard]] auto closed_ring(const std::vector<Point2D> &ring) noexcept -> bool
        {
            return ring.size() >= 4U && ring.front() == ring.back();
        }

        [[nodiscard]] auto geometry_valid(const ImportedGeometry &geometry) noexcept -> bool
        {
            return std::visit([](const auto &value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, Point2D>) {
                    return finite(value);
                } else if constexpr (std::is_same_v<T, LineString2D>) {
                    return !value.points.empty() && std::all_of(value.points.begin(), value.points.end(), finite);
                } else {
                    if (!closed_ring(value.exterior_ring) ||
                        !std::all_of(value.exterior_ring.begin(), value.exterior_ring.end(), finite)) {
                        return false;
                    }
                    return std::all_of(value.interior_rings.begin(), value.interior_rings.end(), [](const auto &ring) {
                        return closed_ring(ring) && std::all_of(ring.begin(), ring.end(), finite);
                    });
                }
            }, geometry);
        }
    }

    ImportedVectorLayer::ImportedVectorLayer(
        std::string layer_name,
        ImportedGeometryKind geometry_kind,
        BoundingBox2D extent,
        NativeSpatialReference spatial_reference,
        std::vector<ImportedFieldSchema> field_schema,
        std::vector<ImportedVectorFeature> features)
        : layer_name_{std::move(layer_name)},
          geometry_kind_{geometry_kind},
          extent_{extent},
          spatial_reference_{std::move(spatial_reference)},
          field_schema_{std::move(field_schema)},
          features_{std::move(features)}
    {
    }

    auto to_string(ImportedGeometryKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case ImportedGeometryKind::point:
            return "point";
        case ImportedGeometryKind::linestring:
            return "linestring";
        case ImportedGeometryKind::polygon:
            return "polygon";
        }
        return "unknown";
    }

    auto to_string(ImportedFieldType type) noexcept -> std::string_view
    {
        switch (type) {
        case ImportedFieldType::integer:
            return "integer";
        case ImportedFieldType::integer64:
            return "integer64";
        case ImportedFieldType::real:
            return "real";
        case ImportedFieldType::boolean:
            return "boolean";
        case ImportedFieldType::string:
            return "string";
        }
        return "unknown";
    }

    auto coordinate_count(const ImportedGeometry &geometry) noexcept -> std::size_t
    {
        return std::visit([](const auto &value) -> std::size_t {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Point2D>) {
                return 1U;
            } else if constexpr (std::is_same_v<T, LineString2D>) {
                return value.points.size();
            } else {
                auto total = value.exterior_ring.size();
                for (const auto &ring : value.interior_rings) {
                    total += ring.size();
                }
                return total;
            }
        }, geometry);
    }

    auto make_imported_vector_layer(
        std::string layer_name,
        ImportedGeometryKind geometry_kind,
        BoundingBox2D extent,
        NativeSpatialReference spatial_reference,
        std::vector<ImportedFieldSchema> field_schema,
        std::vector<ImportedVectorFeature> features) -> tsunami::core::Result<ImportedVectorLayer>
    {
        if (layer_name.empty()) {
            return tsunami::core::failure<ImportedVectorLayer>(geo_error("geo.import.vector_layer_missing", "vector layer name is required", "geo.import.vector.layer.name"));
        }
        if (features.empty()) {
            return tsunami::core::failure<ImportedVectorLayer>(geo_error("geo.import.vector_read_failed", "vector layer contains no imported features", "geo.import.vector.features.nonempty"));
        }
        if (!std::isfinite(extent.minimum_x) || !std::isfinite(extent.maximum_x) ||
            !std::isfinite(extent.minimum_y) || !std::isfinite(extent.maximum_y) ||
            extent.minimum_x > extent.maximum_x || extent.minimum_y > extent.maximum_y) {
            return tsunami::core::failure<ImportedVectorLayer>(geo_error("geo.import.spatial_reference_invalid", "vector extent is invalid", "geo.import.vector.extent.valid"));
        }
        auto feature_ids = std::set<std::int64_t>{};
        for (const auto &feature : features) {
            if (!feature_ids.insert(feature.feature_id).second) {
                return tsunami::core::failure<ImportedVectorLayer>(geo_error("geo.import.vector_read_failed", "vector feature IDs must be unique", "geo.import.vector.feature_id.unique").add_context("feature_id", std::to_string(feature.feature_id)));
            }
            if (!geometry_valid(feature.geometry)) {
                return tsunami::core::failure<ImportedVectorLayer>(geo_error("geo.import.vector_geometry_unsupported", "vector geometry is empty or invalid", "geo.import.vector.geometry.valid").add_context("feature_id", std::to_string(feature.feature_id)));
            }
            if (feature.attributes.size() != field_schema.size()) {
                return tsunami::core::failure<ImportedVectorLayer>(geo_error("geo.import.vector_read_failed", "vector feature attributes do not match field schema", "geo.import.vector.attributes.schema"));
            }
        }
        return tsunami::core::success(ImportedVectorLayer{
            std::move(layer_name),
            geometry_kind,
            extent,
            std::move(spatial_reference),
            std::move(field_schema),
            std::move(features)});
    }

} // namespace tsunami::geo
