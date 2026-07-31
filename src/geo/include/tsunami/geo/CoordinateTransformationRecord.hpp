#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/CoordinateTransformation.hpp>

namespace tsunami::geo
{
    inline constexpr std::string_view coordinate_transformation_record_schema_name{"tsunami.coordinate_transformation_record"};
    inline constexpr tsunami::core::SemanticVersion supported_coordinate_transformation_record_version{1U, 0U, 0U};
    inline constexpr std::string_view supported_coordinate_transformation_record_policy_version{"0.1"};

    struct CoordinateOperationGrid
    {
        std::string short_name;
        std::optional<std::string> full_path;
        std::optional<std::string> package_name;
        std::optional<std::string> source_uri;
        bool available{};
        bool open_licence{};
        std::optional<tsunami::data::ContentDigest> declared_digest;
        GeodeticResourceVerificationStatus verification_status{GeodeticResourceVerificationStatus::unavailable};

        [[nodiscard]] auto operator==(const CoordinateOperationGrid &) const -> bool = default;
    };

    struct CoordinateOperationRecord
    {
        std::string operation_name;
        std::optional<std::string> operation_authority;
        std::optional<std::string> operation_code;
        std::optional<std::string> operation_method;
        std::optional<double> operation_accuracy_m;
        std::optional<std::string> scope;
        std::optional<GeographicAreaOfInterest> area_of_use;
        std::optional<std::string> canonical_wkt2;
        std::optional<std::string> canonical_projjson;
        std::optional<std::string> canonical_pipeline;
        bool ballpark{};
        CoordinateReferenceDescriptor source_crs;
        CoordinateReferenceDescriptor target_crs;
        std::vector<CoordinateOperationGrid> grids;
        std::string engine_name;
        std::string engine_version;
        std::optional<std::string> database_version;

        [[nodiscard]] auto operator==(const CoordinateOperationRecord &) const -> bool = default;
    };

    struct CoordinateTransformationDiagnostics
    {
        std::uint64_t coordinate_count{};
        std::uint64_t transformed_coordinate_count{};
        std::uint64_t failed_coordinate_count{};
        double maximum_forward_control_residual_m{};
        double maximum_inverse_round_trip_residual_m{};
        std::optional<double> maximum_vertical_control_residual_m;
        BoundingBox2D source_extent;
        BoundingBox2D target_extent;
        std::vector<std::string> warnings;

        [[nodiscard]] auto operator==(const CoordinateTransformationDiagnostics &) const -> bool = default;
    };

    struct CoordinateTransformationRecord
    {
        tsunami::data::SchemaIdentity schema;
        std::string policy_version;
        CoordinateTransformationIdentity identity;
        CoordinateReferenceDescriptor source_horizontal;
        std::optional<CoordinateReferenceDescriptor> source_vertical;
        ComputationalTargetReference target;
        GeographicAreaOfInterest area_of_interest;
        CoordinateOperationRecord horizontal_operation;
        VerticalTransformationSpecification vertical_operation;
        ComputationalAxisConvention storage_axes{ComputationalAxisConvention::east_north};
        std::vector<CoordinateOperationGrid> grids;
        BoundingBox2D source_extent;
        BoundingBox2D target_extent;
        CoordinateTransformationDiagnostics diagnostics;
        std::vector<std::string> warnings;

        [[nodiscard]] auto operator==(const CoordinateTransformationRecord &) const -> bool = default;
    };

    [[nodiscard]] auto default_coordinate_transformation_record_path(
        std::string_view source_dataset_id,
        std::string_view transformation_id) -> std::filesystem::path;

    [[nodiscard]] auto validate_coordinate_operation_grid(const CoordinateOperationGrid &grid)
        -> tsunami::core::Result<void>;
    [[nodiscard]] auto validate_coordinate_operation_record(const CoordinateOperationRecord &record)
        -> tsunami::core::Result<void>;
    [[nodiscard]] auto validate_coordinate_transformation_diagnostics(
        const CoordinateTransformationDiagnostics &diagnostics) -> tsunami::core::Result<void>;
    [[nodiscard]] auto validate_coordinate_transformation_record(const CoordinateTransformationRecord &record)
        -> tsunami::core::Result<void>;

} // namespace tsunami::geo
