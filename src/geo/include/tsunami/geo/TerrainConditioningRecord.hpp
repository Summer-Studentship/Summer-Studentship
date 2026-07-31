#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/ConditionedTerrainRaster.hpp>
#include <tsunami/geo/CorridorConstructionRecord.hpp>
#include <tsunami/geo/TerrainResampling.hpp>

namespace tsunami::geo
{
    inline constexpr std::string_view terrain_conditioning_record_schema_name{"tsunami.terrain_conditioning_record"};
    inline constexpr tsunami::core::SemanticVersion supported_terrain_conditioning_record_version{1U, 0U, 0U};
    inline constexpr std::string_view supported_terrain_conditioning_record_policy_version{"0.1"};
    inline constexpr std::string_view terrain_conditioning_formula_version{"corridor-grid-priority-merge-v1"};

    struct TerrainConditioningIdentity
    {
        std::string terrain_id;
        std::uint64_t terrain_revision{1U};
        tsunami::data::CaseRevisionRef case_revision;
        std::string manifest_id;
        std::uint64_t manifest_revision{};
        std::string output_dataset_id;
        std::string output_process_id;
        std::string executed_at_utc;

        [[nodiscard]] auto operator==(const TerrainConditioningIdentity &) const -> bool = default;
    };

    enum class TerrainOverlapConflictPolicy
    {
        reject,
        accept_priority_with_warning
    };

    enum class TerrainGapResolutionKind
    {
        reject,
        bounded_inverse_distance
    };

    enum class TerrainUncertaintyCombination
    {
        not_computed,
        root_sum_square,
        conservative_sum
    };

    [[nodiscard]] auto to_string(TerrainOverlapConflictPolicy value) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(TerrainGapResolutionKind value) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(TerrainUncertaintyCombination value) noexcept -> std::string_view;

    struct TerrainMergePolicy
    {
        std::string first_priority_dataset_id;
        std::string second_priority_dataset_id;
        double maximum_overlap_disagreement_m{};
        TerrainOverlapConflictPolicy conflict_policy{TerrainOverlapConflictPolicy::reject};
        std::string priority_basis;

        [[nodiscard]] auto operator==(const TerrainMergePolicy &) const -> bool = default;
    };

    struct TerrainOverlapDiagnostics
    {
        std::uint64_t overlap_cell_count{};
        std::uint64_t disagreement_exceedance_count{};
        double mean_signed_difference_m{};
        double root_mean_square_difference_m{};
        double maximum_absolute_difference_m{};

        [[nodiscard]] auto operator==(const TerrainOverlapDiagnostics &) const -> bool = default;
    };

    struct TerrainGapResolutionPolicy
    {
        TerrainGapResolutionKind kind{TerrainGapResolutionKind::reject};
        double maximum_fill_distance_m{};
        double maximum_component_diameter_m{};
        std::uint64_t maximum_component_cells{};
        std::uint64_t minimum_donor_count{};
        double distance_exponent{};
        double maximum_filled_fraction{};
        std::string policy_basis;

        [[nodiscard]] auto operator==(const TerrainGapResolutionPolicy &) const -> bool = default;
    };

    struct TerrainUncertaintyPolicy
    {
        TerrainUncertaintyCombination combination{TerrainUncertaintyCombination::not_computed};
        std::optional<double> bathymetry_resampling_uncertainty_m;
        std::optional<double> topography_resampling_uncertainty_m;
        std::optional<double> gap_fill_uncertainty_m;
        std::string basis;

        [[nodiscard]] auto operator==(const TerrainUncertaintyPolicy &) const -> bool = default;
    };

    struct TerrainConditioningDiagnostics
    {
        std::uint64_t total_cell_count{};
        std::uint64_t active_cell_count{};
        std::uint64_t outside_corridor_cell_count{};
        std::uint64_t excluded_boundary_cell_count{};
        std::uint64_t bathymetry_selected_cell_count{};
        std::uint64_t topography_selected_cell_count{};
        std::uint64_t overlap_cell_count{};
        std::uint64_t overlap_conflict_cell_count{};
        std::uint64_t initially_unresolved_cell_count{};
        std::uint64_t filled_cell_count{};
        std::uint64_t unresolved_cell_count{};
        TerrainOverlapDiagnostics overlap;
        double minimum_elevation_m{};
        double maximum_elevation_m{};
        std::vector<std::string> warnings;

        [[nodiscard]] auto operator==(const TerrainConditioningDiagnostics &) const -> bool = default;
    };

    struct TerrainConditioningRecord
    {
        tsunami::data::SchemaIdentity schema;
        std::string policy_version;
        std::string formula_version;
        TerrainConditioningIdentity identity;
        std::string scenario_id;
        std::string target_site;
        std::string bathymetry_dataset_id;
        std::string bathymetry_asset_id;
        GeospatialImportIdentity bathymetry_import_identity;
        CoordinateTransformationIdentity bathymetry_transformation_identity;
        std::string topography_dataset_id;
        std::string topography_asset_id;
        GeospatialImportIdentity topography_import_identity;
        CoordinateTransformationIdentity topography_transformation_identity;
        CorridorConstructionIdentity corridor_identity;
        ComputationalTargetReference target_reference;
        TerrainTargetGrid grid;
        TerrainTargetGridPolicy grid_policy;
        RasterResamplingRecord bathymetry_resampling;
        RasterResamplingRecord topography_resampling;
        TerrainMergePolicy merge_policy;
        TerrainGapResolutionPolicy gap_policy;
        TerrainConditioningDiagnostics diagnostics;
        tsunami::data::DatasetUncertainty output_uncertainty;
        std::string output_media_type;
        std::filesystem::path output_path;
        std::string digest_status;
        std::vector<std::string> warnings;
    };

    [[nodiscard]] auto default_terrain_conditioning_record_path(
        std::string_view output_dataset_id) -> std::filesystem::path;
    [[nodiscard]] auto default_conditioned_terrain_path(
        std::string_view output_dataset_id) -> std::filesystem::path;
    [[nodiscard]] auto validate_terrain_conditioning_record(
        const TerrainConditioningRecord &record) -> tsunami::core::Result<void>;

} // namespace tsunami::geo
