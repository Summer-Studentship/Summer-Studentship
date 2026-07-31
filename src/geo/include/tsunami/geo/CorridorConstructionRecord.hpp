#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/data/CaseConfiguration.hpp>
#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/ConstructedCorridor.hpp>
#include <tsunami/geo/CoordinateTransformationRecord.hpp>
#include <tsunami/geo/TransformedVector.hpp>

namespace tsunami::geo
{
    inline constexpr std::string_view corridor_construction_record_schema_name{"tsunami.corridor_construction_record"};
    inline constexpr tsunami::core::SemanticVersion supported_corridor_construction_record_version{1U, 0U, 0U};
    inline constexpr std::string_view supported_corridor_construction_record_policy_version{"0.1"};
    inline constexpr std::string_view corridor_construction_formula_version{"flat-ended-epicentre-target-v1"};

    enum class CorridorReferencePointRole
    {
        epicentre,
        target
    };

    [[nodiscard]] auto to_string(CorridorReferencePointRole role) noexcept -> std::string_view;

    struct CorridorReferencePointRequest
    {
        CorridorReferencePointRole role{CorridorReferencePointRole::epicentre};
        const TransformedPointSet *point_set{};
        std::size_t coordinate_index{};
        const CoordinateTransformationRecord *transformation_record{};
        std::string point_id;
        std::string definition;
        std::optional<std::string> source_feature_id;
        std::string source_document_title;
        std::string source_document_uri;
        std::string accessed_at_utc;
    };

    struct CorridorReferencePointEvidence
    {
        CorridorReferencePointRole role{CorridorReferencePointRole::epicentre};
        std::string point_id;
        std::string definition;
        Coordinate3D coordinate;
        std::size_t coordinate_index{};
        std::optional<std::string> source_feature_id;
        CoordinateTransformationIdentity transformation_identity;
        CoordinateReferenceDescriptor source_reference;
        ComputationalTargetReference target_reference;
        std::string source_document_title;
        std::string source_document_uri;
        std::string accessed_at_utc;

        [[nodiscard]] auto operator==(const CorridorReferencePointEvidence &) const -> bool = default;
    };

    struct CorridorConstructionIdentity
    {
        std::string corridor_id;
        std::uint64_t corridor_revision{1U};
        tsunami::data::CaseRevisionRef case_revision;
        std::string trajectory_id;
        std::string output_dataset_id;
        std::string output_process_id;
        std::string executed_at_utc;

        [[nodiscard]] auto operator==(const CorridorConstructionIdentity &) const -> bool = default;
    };

    struct CorridorConstructionPolicy
    {
        double minimum_reference_separation_m{};
        double origin_tolerance_m{};
        double bearing_tolerance_degrees{};
        double basis_orthonormal_tolerance{};
        double geometry_absolute_tolerance_m{};
        double geometry_relative_tolerance{};
        std::string tolerance_basis;

        [[nodiscard]] auto operator==(const CorridorConstructionPolicy &) const -> bool = default;
    };

    struct CorridorConstructionDiagnostics
    {
        double origin_residual_m{};
        double bearing_residual_degrees{};
        double basis_tangent_norm_residual{};
        double basis_normal_norm_residual{};
        double basis_orthogonality_residual{};
        double basis_determinant_residual{};
        double analytic_area_m2{};
        double polygon_area_m2{};
        double area_residual_m2{};
        double analytic_perimeter_m{};
        double polygon_perimeter_m{};
        double perimeter_residual_m{};
        std::vector<std::string> warnings;

        [[nodiscard]] auto operator==(const CorridorConstructionDiagnostics &) const -> bool = default;
    };

    struct CorridorConstructionRecord
    {
        tsunami::data::SchemaIdentity schema;
        std::string policy_version;
        CorridorConstructionIdentity identity;
        std::string scenario_id;
        std::string target_site;
        CorridorReferencePointEvidence epicentre;
        CorridorReferencePointEvidence target;
        ComputationalTargetReference target_reference;
        CorridorConstructionPolicy policy;
        Point2D configured_origin;
        double configured_bearing_degrees{};
        double derived_bearing_degrees{};
        double origin_residual_m{};
        double bearing_residual_degrees{};
        double offshore_extent_m{};
        double epicentre_target_distance_m{};
        double inland_extent_m{};
        double total_length_m{};
        double offshore_width_m{};
        double inland_width_m{};
        bool narrowing_enabled{};
        std::string narrowing_rule;
        CorridorLocalBasis local_basis;
        CorridorLongitudinalStations stations;
        CorridorSpongeLimits sponge_limits;
        Polygon2D polygon;
        std::string vertex_order_convention;
        BoundingBox2D extent;
        double area_m2{};
        double perimeter_m{};
        CorridorConstructionDiagnostics diagnostics;
        std::vector<std::string> configured_field_paths;
        std::vector<std::string> warnings;
    };

    [[nodiscard]] auto default_corridor_construction_record_path(
        std::string_view trajectory_id) -> std::filesystem::path;
    [[nodiscard]] auto validate_corridor_construction_record(const CorridorConstructionRecord &record)
        -> tsunami::core::Result<void>;

} // namespace tsunami::geo
