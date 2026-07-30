#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/core/Identity.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/data/References.hpp>

namespace tsunami::data
{
    inline constexpr std::string_view case_configuration_schema_name{"tsunami.case_configuration"};
    inline constexpr tsunami::core::SemanticVersion supported_case_configuration_version{1U, 0U, 0U};
    inline constexpr std::string_view supported_case_policy_version{"0.1"};
    inline constexpr std::string_view authoritative_case_configuration_path{"case.json"};
    inline constexpr std::string_view case_configuration_media_type{"application/json"};

    enum class CaseSchemaCompatibility
    {
        exact,
        patch_equivalent,
        forward_compatible_minor,
        migration_required,
        unsupported_major
    };

    enum class CaseModelFamily
    {
        regional_2d
    };

    enum class HorizontalAxisOrder
    {
        east_north
    };

    enum class VerticalPositiveDirection
    {
        up
    };

    enum class ManningConfigurationKind
    {
        disabled,
        uniform,
        dataset
    };

    enum class CoriolisConfigurationKind
    {
        disabled,
        constant,
        dataset
    };

    enum class BedDeformationMapping
    {
        vertical_only,
        horizontal_slope_corrected
    };

    enum class SurfaceTransfer
    {
        passive_equal_to_effective_bed,
        prescribed
    };

    enum class RegionalTimeScheme
    {
        forward_euler,
        ssprk2,
        ssprk3
    };

    enum class RegionalBoundaryKind
    {
        transmissive,
        radiation
    };

    [[nodiscard]] auto to_string(CaseSchemaCompatibility compatibility) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(CaseModelFamily family) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(HorizontalAxisOrder order) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(VerticalPositiveDirection direction) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(ManningConfigurationKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(CoriolisConfigurationKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(BedDeformationMapping mapping) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(SurfaceTransfer transfer) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(RegionalTimeScheme scheme) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(RegionalBoundaryKind kind) noexcept -> std::string_view;

    struct CaseIdentity
    {
        tsunami::core::CaseId case_id;
        std::string case_slug;
        std::uint64_t revision{};
        std::string created_at_utc;
        std::string created_by;

        [[nodiscard]] auto operator==(const CaseIdentity &) const -> bool = default;
    };

    struct ScenarioConfiguration
    {
        std::string scenario_id;
        std::string event_id;
        std::string target_site;
        CaseModelFamily model_family{CaseModelFamily::regional_2d};

        [[nodiscard]] auto operator==(const ScenarioConfiguration &) const -> bool = default;
    };

    struct CoordinateFrameConfiguration
    {
        std::string horizontal_crs;
        std::string vertical_datum;
        std::string horizontal_unit;
        std::string vertical_unit;
        HorizontalAxisOrder axis_order{HorizontalAxisOrder::east_north};
        VerticalPositiveDirection vertical_positive{VerticalPositiveDirection::up};

        [[nodiscard]] auto operator==(const CoordinateFrameConfiguration &) const -> bool = default;
    };

    struct DatasetBindings
    {
        std::filesystem::path manifest_path;
        std::string bathymetry;
        std::string topography;
        std::optional<std::string> earthquake_displacement;
        std::optional<std::string> prescribed_surface;
        std::optional<std::string> manning;
        std::optional<std::string> coriolis;
        std::vector<std::string> observations;

        [[nodiscard]] auto operator==(const DatasetBindings &) const -> bool = default;
    };

    struct CorridorOrigin
    {
        double x{};
        double y{};

        [[nodiscard]] auto operator==(const CorridorOrigin &) const -> bool = default;
    };

    struct CorridorNarrowingConfiguration
    {
        bool enabled{};
        std::optional<double> inland_width_m;

        [[nodiscard]] auto operator==(const CorridorNarrowingConfiguration &) const -> bool = default;
    };

    struct CorridorSpongeConfiguration
    {
        double offshore_width_m{};
        double side_width_m{};

        [[nodiscard]] auto operator==(const CorridorSpongeConfiguration &) const -> bool = default;
    };

    struct CorridorRequest
    {
        std::string trajectory_id;
        CorridorOrigin origin;
        double bearing_degrees_clockwise_from_north{};
        double width_m{};
        double offshore_extent_m{};
        double inland_extent_m{};
        CorridorNarrowingConfiguration narrowing;
        CorridorSpongeConfiguration sponge;

        [[nodiscard]] auto operator==(const CorridorRequest &) const -> bool = default;
    };

    struct ManningConfiguration
    {
        ManningConfigurationKind kind{ManningConfigurationKind::disabled};
        std::optional<double> value_s_per_m_one_third;
        std::optional<std::string> dataset_binding;

        [[nodiscard]] auto operator==(const ManningConfiguration &) const -> bool = default;
    };

    struct CoriolisConfiguration
    {
        CoriolisConfigurationKind kind{CoriolisConfigurationKind::disabled};
        std::optional<double> value_per_s;
        std::optional<std::string> dataset_binding;

        [[nodiscard]] auto operator==(const CoriolisConfiguration &) const -> bool = default;
    };

    struct EarthquakeConfiguration
    {
        bool enabled{};
        std::optional<std::string> displacement_binding;
        BedDeformationMapping bed_mapping{BedDeformationMapping::vertical_only};
        SurfaceTransfer surface_transfer{SurfaceTransfer::passive_equal_to_effective_bed};
        std::optional<std::string> prescribed_surface_binding;

        [[nodiscard]] auto operator==(const EarthquakeConfiguration &) const -> bool = default;
    };

    struct RegionalPhysicsConfiguration
    {
        double gravity_m_per_s2{9.80665};
        ManningConfiguration manning;
        CoriolisConfiguration coriolis;
        EarthquakeConfiguration earthquake;

        [[nodiscard]] auto operator==(const RegionalPhysicsConfiguration &) const -> bool = default;
    };

    struct RegionalNumericsConfiguration
    {
        RegionalTimeScheme scheme{RegionalTimeScheme::ssprk3};
        double courant_number{};
        double positivity_safety_factor{};
        double relaxation_safety_factor{};
        double source_safety_factor{};
        double minimum_timestep_s{};
        double maximum_timestep_s{};
        double final_time_s{};
        std::uint64_t maximum_steps{};

        [[nodiscard]] auto operator==(const RegionalNumericsConfiguration &) const -> bool = default;
    };

    struct CorridorBoundaryConfiguration
    {
        RegionalBoundaryKind offshore{RegionalBoundaryKind::radiation};
        RegionalBoundaryKind inland{RegionalBoundaryKind::transmissive};
        RegionalBoundaryKind left_side{RegionalBoundaryKind::radiation};
        RegionalBoundaryKind right_side{RegionalBoundaryKind::radiation};

        [[nodiscard]] auto operator==(const CorridorBoundaryConfiguration &) const -> bool = default;
    };

    struct RelaxationConfiguration
    {
        bool enabled{};
        std::optional<double> maximum_rate_per_s;
        std::optional<double> profile_exponent;

        [[nodiscard]] auto operator==(const RelaxationConfiguration &) const -> bool = default;
    };

    struct Regional2DCaseConfiguration
    {
        CorridorRequest corridor;
        RegionalPhysicsConfiguration physics;
        RegionalNumericsConfiguration numerics;
        CorridorBoundaryConfiguration boundaries;
        RelaxationConfiguration relaxation;

        [[nodiscard]] auto operator==(const Regional2DCaseConfiguration &) const -> bool = default;
    };

    struct CaseOutputConfiguration
    {
        std::optional<double> snapshot_interval_s;
        bool diagnostics_enabled{};
        bool initialisation_diagnostics_enabled{};
        std::optional<double> checkpoint_interval_s;

        [[nodiscard]] auto operator==(const CaseOutputConfiguration &) const -> bool = default;
    };

    struct CaseExtension
    {
        std::string name;
        std::string canonical_json;

        [[nodiscard]] auto operator==(const CaseExtension &) const -> bool = default;
    };

    struct CaseExtensions
    {
        std::vector<CaseExtension> values;

        [[nodiscard]] auto operator==(const CaseExtensions &) const -> bool = default;
    };

    class CaseConfiguration
    {
    public:
        [[nodiscard]] auto schema_identity() const noexcept -> const SchemaIdentity & { return schema_; }
        [[nodiscard]] auto compatibility() const noexcept -> CaseSchemaCompatibility { return compatibility_; }
        [[nodiscard]] auto policy_version() const noexcept -> std::string_view { return policy_version_; }
        [[nodiscard]] auto identity() const noexcept -> const CaseIdentity & { return identity_; }
        [[nodiscard]] auto scenario() const noexcept -> const ScenarioConfiguration & { return scenario_; }
        [[nodiscard]] auto coordinate_frame() const noexcept -> const CoordinateFrameConfiguration & { return coordinate_frame_; }
        [[nodiscard]] auto datasets() const noexcept -> const DatasetBindings & { return datasets_; }
        [[nodiscard]] auto regional_2d() const noexcept -> const Regional2DCaseConfiguration & { return regional_2d_; }
        [[nodiscard]] auto outputs() const noexcept -> const CaseOutputConfiguration & { return outputs_; }
        [[nodiscard]] auto extensions() const noexcept -> const CaseExtensions & { return extensions_; }

        [[nodiscard]] auto operator==(const CaseConfiguration &) const -> bool = default;

    private:
        friend auto make_case_configuration(
            SchemaIdentity schema,
            CaseSchemaCompatibility compatibility,
            std::string policy_version,
            CaseIdentity identity,
            ScenarioConfiguration scenario,
            CoordinateFrameConfiguration coordinate_frame,
            DatasetBindings datasets,
            Regional2DCaseConfiguration regional_2d,
            CaseOutputConfiguration outputs,
            CaseExtensions extensions) -> tsunami::core::Result<CaseConfiguration>;

        CaseConfiguration(
            SchemaIdentity schema,
            CaseSchemaCompatibility compatibility,
            std::string policy_version,
            CaseIdentity identity,
            ScenarioConfiguration scenario,
            CoordinateFrameConfiguration coordinate_frame,
            DatasetBindings datasets,
            Regional2DCaseConfiguration regional_2d,
            CaseOutputConfiguration outputs,
            CaseExtensions extensions);

        SchemaIdentity schema_;
        CaseSchemaCompatibility compatibility_{CaseSchemaCompatibility::exact};
        std::string policy_version_;
        CaseIdentity identity_;
        ScenarioConfiguration scenario_;
        CoordinateFrameConfiguration coordinate_frame_;
        DatasetBindings datasets_;
        Regional2DCaseConfiguration regional_2d_;
        CaseOutputConfiguration outputs_;
        CaseExtensions extensions_;
    };

    [[nodiscard]] auto make_case_configuration(
        SchemaIdentity schema,
        CaseSchemaCompatibility compatibility,
        std::string policy_version,
        CaseIdentity identity,
        ScenarioConfiguration scenario,
        CoordinateFrameConfiguration coordinate_frame,
        DatasetBindings datasets,
        Regional2DCaseConfiguration regional_2d,
        CaseOutputConfiguration outputs,
        CaseExtensions extensions) -> tsunami::core::Result<CaseConfiguration>;

} // namespace tsunami::data
