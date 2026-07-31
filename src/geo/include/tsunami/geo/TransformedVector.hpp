#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/geo/CoordinateTransformation.hpp>
#include <tsunami/geo/CoordinateTransformationRecord.hpp>
#include <tsunami/geo/ImportedVector.hpp>

namespace tsunami::geo
{
    class TransformedPointSet
    {
    public:
        [[nodiscard]] auto source_reference() const noexcept -> const CoordinateReferenceDescriptor & { return source_reference_; }
        [[nodiscard]] auto target_reference() const noexcept -> const ComputationalTargetReference & { return target_reference_; }
        [[nodiscard]] auto coordinates() const noexcept -> const std::vector<Coordinate3D> & { return coordinates_; }

        [[nodiscard]] auto operator==(const TransformedPointSet &) const -> bool = default;

    private:
        friend auto make_transformed_point_set(
            CoordinateReferenceDescriptor source_reference,
            ComputationalTargetReference target_reference,
            std::vector<Coordinate3D> coordinates) -> tsunami::core::Result<TransformedPointSet>;

        TransformedPointSet(
            CoordinateReferenceDescriptor source_reference,
            ComputationalTargetReference target_reference,
            std::vector<Coordinate3D> coordinates);

        CoordinateReferenceDescriptor source_reference_;
        ComputationalTargetReference target_reference_;
        std::vector<Coordinate3D> coordinates_;
    };

    class TransformedVectorLayer
    {
    public:
        [[nodiscard]] auto source_layer_name() const noexcept -> std::string_view { return source_layer_name_; }
        [[nodiscard]] auto geometry_kind() const noexcept -> ImportedGeometryKind { return geometry_kind_; }
        [[nodiscard]] auto target_reference() const noexcept -> const ComputationalTargetReference & { return target_reference_; }
        [[nodiscard]] auto field_schema() const noexcept -> const std::vector<ImportedFieldSchema> & { return field_schema_; }
        [[nodiscard]] auto features() const noexcept -> const std::vector<ImportedVectorFeature> & { return features_; }
        [[nodiscard]] auto extent() const noexcept -> const BoundingBox2D & { return extent_; }

        [[nodiscard]] auto operator==(const TransformedVectorLayer &) const -> bool = default;

    private:
        friend auto make_transformed_vector_layer(
            std::string source_layer_name,
            ImportedGeometryKind geometry_kind,
            ComputationalTargetReference target_reference,
            std::vector<ImportedFieldSchema> field_schema,
            std::vector<ImportedVectorFeature> features,
            BoundingBox2D extent) -> tsunami::core::Result<TransformedVectorLayer>;

        TransformedVectorLayer(
            std::string source_layer_name,
            ImportedGeometryKind geometry_kind,
            ComputationalTargetReference target_reference,
            std::vector<ImportedFieldSchema> field_schema,
            std::vector<ImportedVectorFeature> features,
            BoundingBox2D extent);

        std::string source_layer_name_;
        ImportedGeometryKind geometry_kind_{ImportedGeometryKind::point};
        ComputationalTargetReference target_reference_;
        std::vector<ImportedFieldSchema> field_schema_;
        std::vector<ImportedVectorFeature> features_;
        BoundingBox2D extent_;
    };

    [[nodiscard]] auto make_transformed_point_set(
        CoordinateReferenceDescriptor source_reference,
        ComputationalTargetReference target_reference,
        std::vector<Coordinate3D> coordinates) -> tsunami::core::Result<TransformedPointSet>;

    [[nodiscard]] auto make_transformed_vector_layer(
        std::string source_layer_name,
        ImportedGeometryKind geometry_kind,
        ComputationalTargetReference target_reference,
        std::vector<ImportedFieldSchema> field_schema,
        std::vector<ImportedVectorFeature> features,
        BoundingBox2D extent) -> tsunami::core::Result<TransformedVectorLayer>;

    struct PointTransformationResult
    {
        TransformedPointSet points;
        CoordinateTransformationRecord record;
        CoordinateTransformationDiagnostics diagnostics;
    };

    struct VectorTransformationResult
    {
        TransformedVectorLayer layer;
        CoordinateTransformationRecord record;
        CoordinateTransformationDiagnostics diagnostics;
    };

} // namespace tsunami::geo
