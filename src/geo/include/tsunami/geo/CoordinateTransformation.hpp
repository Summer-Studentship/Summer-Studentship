#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/data/CaseConfiguration.hpp>
#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/GeospatialImportRecord.hpp>

namespace tsunami::geo
{
    struct GeographicAreaOfInterest
    {
        double west_longitude_degrees{};
        double south_latitude_degrees{};
        double east_longitude_degrees{};
        double north_latitude_degrees{};

        [[nodiscard]] auto operator==(const GeographicAreaOfInterest &) const -> bool = default;
    };

    struct CoordinateReferenceDescriptor
    {
        std::optional<std::string> authority_name;
        std::optional<std::string> authority_code;
        std::string name;
        std::optional<std::string> canonical_wkt2;
        std::optional<std::string> canonical_projjson;
        std::optional<std::string> datum_name;
        std::optional<std::string> datum_realisation;
        std::optional<double> coordinate_epoch_decimal_year;
        std::vector<std::string> axis_names;
        std::vector<std::string> axis_directions;
        std::vector<std::string> axis_units;

        [[nodiscard]] auto operator==(const CoordinateReferenceDescriptor &) const -> bool = default;
    };

    enum class ComputationalAxisConvention
    {
        east_north,
        east_north_up
    };

    [[nodiscard]] auto to_string(ComputationalAxisConvention convention) noexcept -> std::string_view;

    struct ComputationalTargetReference
    {
        CoordinateReferenceDescriptor horizontal;
        std::optional<CoordinateReferenceDescriptor> vertical;
        ComputationalAxisConvention storage_axes{ComputationalAxisConvention::east_north};
        std::string horizontal_unit{"m"};
        std::optional<std::string> vertical_unit;
        std::optional<std::string> vertical_positive;

        [[nodiscard]] auto operator==(const ComputationalTargetReference &) const -> bool = default;
    };

    struct CoordinateTransformationIdentity
    {
        std::string transformation_id;
        std::uint64_t transformation_revision{1U};
        tsunami::data::CaseRevisionRef case_revision;
        std::string manifest_id;
        std::uint64_t manifest_revision{};
        std::string source_import_id;
        std::uint64_t source_import_revision{};
        std::string source_dataset_id;
        std::string source_asset_id;
        std::string output_dataset_id;
        std::string output_process_id;
        std::string executed_at_utc;

        [[nodiscard]] auto operator==(const CoordinateTransformationIdentity &) const -> bool = default;
    };

    struct CoordinateTransformationAccuracyPolicy
    {
        double maximum_operation_accuracy_m{};
        double projection_control_tolerance_m{0.001};
        double horizontal_control_tolerance_m{0.05};
        double vertical_control_tolerance_m{0.02};
        double round_trip_tolerance_m{0.001};
        bool require_reported_operation_accuracy{true};
        bool require_area_of_use_coverage{true};
        bool require_verified_grid_resources{true};

        [[nodiscard]] auto operator==(const CoordinateTransformationAccuracyPolicy &) const -> bool = default;
    };

    struct CoordinateOperationSelectionPolicy
    {
        GeographicAreaOfInterest area_of_interest;
        CoordinateTransformationAccuracyPolicy accuracy;
        std::optional<std::string> authority{"EPSG"};
        bool allow_ballpark{false};
        bool only_best{true};
        bool network_enabled{false};

        [[nodiscard]] auto operator==(const CoordinateOperationSelectionPolicy &) const -> bool = default;
    };

    enum class GeodeticResourceVerificationStatus
    {
        externally_verified,
        declared_not_verified,
        unavailable
    };

    [[nodiscard]] auto to_string(GeodeticResourceVerificationStatus status) noexcept -> std::string_view;

    struct GeodeticResourceEvidence
    {
        std::string resource_name;
        std::optional<std::filesystem::path> resolved_path;
        std::string provider;
        std::string source_document_title;
        std::string source_document_uri;
        std::optional<tsunami::data::ContentDigest> digest;
        GeodeticResourceVerificationStatus verification_status{GeodeticResourceVerificationStatus::unavailable};

        [[nodiscard]] auto operator==(const GeodeticResourceEvidence &) const -> bool = default;
    };

    enum class HorizontalTransformationClass
    {
        same_datum_projection,
        datum_transformation_and_projection,
        documented_reference_equivalence
    };

    enum class VerticalTransformationStepKind
    {
        identity,
        unit_scale,
        sign_inversion,
        constant_offset,
        geodetic_grid_operation
    };

    [[nodiscard]] auto to_string(HorizontalTransformationClass kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(VerticalTransformationStepKind kind) noexcept -> std::string_view;

    struct VerticalTransformationStep
    {
        VerticalTransformationStepKind kind{VerticalTransformationStepKind::identity};
        std::optional<double> scale_factor;
        std::optional<double> offset_m;
        std::optional<std::string> operation_authority;
        std::optional<std::string> operation_code;
        std::optional<std::string> required_resource_name;
        std::string source_reference;
        std::string target_reference;

        [[nodiscard]] auto operator==(const VerticalTransformationStep &) const -> bool = default;
    };

    struct VerticalTransformationSpecification
    {
        bool enabled{};
        std::vector<VerticalTransformationStep> steps;

        [[nodiscard]] auto operator==(const VerticalTransformationSpecification &) const -> bool = default;
    };

    struct CoordinateTransformationRequest
    {
        const tsunami::data::CaseConfiguration *configuration{};
        const tsunami::data::DatasetManifest *manifest{};
        const GeospatialImportRecord *source_import_record{};
        CoordinateTransformationIdentity identity;
        ComputationalTargetReference target;
        CoordinateOperationSelectionPolicy selection_policy;
        std::filesystem::path resource_root;
        std::vector<GeodeticResourceEvidence> resource_evidence;
        VerticalTransformationSpecification vertical;
    };

    struct Coordinate3D
    {
        double x{};
        double y{};
        double z{};

        [[nodiscard]] auto operator==(const Coordinate3D &) const -> bool = default;
    };

    [[nodiscard]] auto validate_geographic_area_of_interest(const GeographicAreaOfInterest &area)
        -> tsunami::core::Result<void>;
    [[nodiscard]] auto validate_coordinate_reference_descriptor(const CoordinateReferenceDescriptor &descriptor)
        -> tsunami::core::Result<void>;
    [[nodiscard]] auto validate_transformation_target_for_case(
        const ComputationalTargetReference &target,
        const tsunami::data::CaseConfiguration &configuration) -> tsunami::core::Result<void>;
    [[nodiscard]] auto validate_accuracy_policy(const CoordinateTransformationAccuracyPolicy &policy)
        -> tsunami::core::Result<void>;
    [[nodiscard]] auto validate_selection_policy(const CoordinateOperationSelectionPolicy &policy)
        -> tsunami::core::Result<void>;
    [[nodiscard]] auto validate_resource_evidence(const GeodeticResourceEvidence &evidence)
        -> tsunami::core::Result<void>;
    [[nodiscard]] auto validate_vertical_transformation(const VerticalTransformationSpecification &specification)
        -> tsunami::core::Result<void>;
    [[nodiscard]] auto validate_coordinate_transformation_request(const CoordinateTransformationRequest &request)
        -> tsunami::core::Result<void>;
    [[nodiscard]] auto source_horizontal_reference_from_import_record(const GeospatialImportRecord &record)
        -> tsunami::core::Result<CoordinateReferenceDescriptor>;

} // namespace tsunami::geo
