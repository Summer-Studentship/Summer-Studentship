#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/geo/ImportedRaster.hpp>
#include <tsunami/geo/SpatialReferenceEvidence.hpp>

namespace tsunami::geo
{
    struct Point2D
    {
        double x{};
        double y{};

        [[nodiscard]] auto operator==(const Point2D &) const -> bool = default;
    };

    struct LineString2D
    {
        std::vector<Point2D> points;

        [[nodiscard]] auto operator==(const LineString2D &) const -> bool = default;
    };

    struct Polygon2D
    {
        std::vector<Point2D> exterior_ring;
        std::vector<std::vector<Point2D>> interior_rings;

        [[nodiscard]] auto operator==(const Polygon2D &) const -> bool = default;
    };

    using ImportedGeometry = std::variant<Point2D, LineString2D, Polygon2D>;
    using ImportedAttributeValue = std::variant<std::monostate, std::int64_t, double, bool, std::string>;

    enum class ImportedGeometryKind
    {
        point,
        linestring,
        polygon
    };

    enum class ImportedFieldType
    {
        integer,
        integer64,
        real,
        boolean,
        string
    };

    [[nodiscard]] auto to_string(ImportedGeometryKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(ImportedFieldType type) noexcept -> std::string_view;

    struct ImportedAttribute
    {
        std::string name;
        ImportedAttributeValue value;

        [[nodiscard]] auto operator==(const ImportedAttribute &) const -> bool = default;
    };

    struct ImportedFieldSchema
    {
        std::string name;
        ImportedFieldType type{ImportedFieldType::string};

        [[nodiscard]] auto operator==(const ImportedFieldSchema &) const -> bool = default;
    };

    struct ImportedVectorFeature
    {
        std::int64_t feature_id{};
        ImportedGeometry geometry;
        std::vector<ImportedAttribute> attributes;

        [[nodiscard]] auto operator==(const ImportedVectorFeature &) const -> bool = default;
    };

    class ImportedVectorLayer
    {
    public:
        [[nodiscard]] auto layer_name() const noexcept -> std::string_view { return layer_name_; }
        [[nodiscard]] auto geometry_kind() const noexcept -> ImportedGeometryKind { return geometry_kind_; }
        [[nodiscard]] auto feature_count() const noexcept -> std::size_t { return features_.size(); }
        [[nodiscard]] auto extent() const noexcept -> const BoundingBox2D & { return extent_; }
        [[nodiscard]] auto spatial_reference() const noexcept -> const NativeSpatialReference & { return spatial_reference_; }
        [[nodiscard]] auto field_schema() const noexcept -> const std::vector<ImportedFieldSchema> & { return field_schema_; }
        [[nodiscard]] auto features() const noexcept -> const std::vector<ImportedVectorFeature> & { return features_; }

        [[nodiscard]] auto operator==(const ImportedVectorLayer &) const -> bool = default;

    private:
        friend auto make_imported_vector_layer(
            std::string layer_name,
            ImportedGeometryKind geometry_kind,
            BoundingBox2D extent,
            NativeSpatialReference spatial_reference,
            std::vector<ImportedFieldSchema> field_schema,
            std::vector<ImportedVectorFeature> features) -> tsunami::core::Result<ImportedVectorLayer>;

        ImportedVectorLayer(
            std::string layer_name,
            ImportedGeometryKind geometry_kind,
            BoundingBox2D extent,
            NativeSpatialReference spatial_reference,
            std::vector<ImportedFieldSchema> field_schema,
            std::vector<ImportedVectorFeature> features);

        std::string layer_name_;
        ImportedGeometryKind geometry_kind_{ImportedGeometryKind::point};
        BoundingBox2D extent_;
        NativeSpatialReference spatial_reference_;
        std::vector<ImportedFieldSchema> field_schema_;
        std::vector<ImportedVectorFeature> features_;
    };

    [[nodiscard]] auto make_imported_vector_layer(
        std::string layer_name,
        ImportedGeometryKind geometry_kind,
        BoundingBox2D extent,
        NativeSpatialReference spatial_reference,
        std::vector<ImportedFieldSchema> field_schema,
        std::vector<ImportedVectorFeature> features) -> tsunami::core::Result<ImportedVectorLayer>;

    [[nodiscard]] auto coordinate_count(const ImportedGeometry &geometry) noexcept -> std::size_t;

} // namespace tsunami::geo
