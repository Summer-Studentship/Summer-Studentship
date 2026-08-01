#pragma once

#include <optional>
#include <span>
#include <stop_token>
#include <string>

#include <tsunami/data/CaseConfiguration.hpp>
#include <tsunami/fvm/BoundaryConditionSet.hpp>
#include <tsunami/geo/CorridorConstructionRecord.hpp>
#include <tsunami/r2d/RegionalBoundaryCondition.hpp>
#include <tsunami/r2d/RegionalEarthquakeInitialisation.hpp>
#include <tsunami/r2d/RegionalGeometryPreflight.hpp>
#include <tsunami/r2d/RegionalRelaxationZone.hpp>
#include <tsunami/r2d/RegionalSolveLoop.hpp>
#include <tsunami/r2d/RegionalSourceTerms.hpp>
#include <tsunami/r2d/RegionalTerrainTransfer.hpp>

namespace tsunami::r2d
{
    struct RegionalCasePreparationPolicy
    {
        tsunami::core::Real pre_event_free_surface_elevation_m{};
        tsunami::core::Real dry_depth_m{};
        tsunami::core::Real depth_tolerance_m{};
        tsunami::core::Real normal_tolerance{};
        tsunami::core::Real zero_momentum_tolerance{};

        [[nodiscard]] auto operator==(const RegionalCasePreparationPolicy &) const -> bool = default;
    };

    struct RegionalCasePreparationDiagnostics
    {
        std::string case_id;
        std::uint64_t case_revision{};
        std::string scenario_id;
        std::string target_site;
        std::string corridor_id;
        std::string mesh_id;
        std::string terrain_id;
        std::size_t cell_count{};
        std::size_t wet_cell_count{};
        std::size_t dry_cell_count{};
        tsunami::core::Real minimum_depth_m{};
        tsunami::core::Real maximum_depth_m{};
        tsunami::core::Real total_water_volume_m3{};
        tsunami::core::Real maximum_initial_momentum_m2_per_s{};
        std::size_t physical_boundary_count{};
        std::size_t relaxation_zone_count{};
        bool has_manning_source{};
        bool has_coriolis_source{};
        bool earthquake_initialised{};
        tsunami::core::Real retry_factor{};
        std::size_t maximum_stage_retries{};
        tsunami::core::Real timestep_comparison_tolerance{};
    };

    struct RegionalCasePreparationRequest
    {
        const tsunami::data::CaseConfiguration *configuration{};
        const tsunami::geo::CorridorConstructionRecord *corridor_record{};
        const RegionalGeometryPreflightReport *preflight{};
        const RegionalTerrainTransferDiagnostics *terrain_transfer{};
        const tsunami::fvm::FiniteVolumeMesh *mesh{};
        const RegionalBathymetry *pre_event_bathymetry{};
        RegionalCasePreparationPolicy policy;
        const RegionalSeabedDisplacement *seabed_displacement{};
        const tsunami::fvm::CellScalarField *prescribed_surface_perturbation{};
        std::optional<std::span<const tsunami::core::Real>> manning_values;
        std::optional<std::span<const tsunami::core::Real>> coriolis_values;
        const RegionalEarthquakeSourceMetadata *earthquake_metadata{};
    };

    class RegionalPreparedCase
    {
    public:
        RegionalPreparedCase(const RegionalPreparedCase &) = delete;
        auto operator=(const RegionalPreparedCase &) -> RegionalPreparedCase & = delete;
        RegionalPreparedCase(RegionalPreparedCase &&) noexcept = default;
        auto operator=(RegionalPreparedCase &&) noexcept -> RegionalPreparedCase & = default;
        ~RegionalPreparedCase() = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto bathymetry() const noexcept -> const RegionalBathymetry & { return bathymetry_; }
        [[nodiscard]] auto simulation_state() noexcept -> RegionalSimulationState & { return simulation_state_; }
        [[nodiscard]] auto simulation_state() const noexcept -> const RegionalSimulationState & { return simulation_state_; }
        [[nodiscard]] auto regional_boundaries() const noexcept -> const RegionalBoundaryConditionSet & { return regional_boundaries_; }
        [[nodiscard]] auto relaxation_zones() const noexcept -> const RegionalRelaxationZoneSet & { return relaxation_zones_; }
        [[nodiscard]] auto local_sources() const noexcept -> const RegionalSourceTermSet & { return local_sources_; }
        [[nodiscard]] auto state_policy() const noexcept -> ShallowWaterStatePolicy { return state_policy_; }
        [[nodiscard]] auto time_policy() const noexcept -> RegionalTimeIntegrationPolicy { return time_policy_; }
        [[nodiscard]] auto output_policy() const noexcept -> RegionalSnapshotOutputPolicy { return output_policy_; }
        [[nodiscard]] auto final_time() const noexcept -> tsunami::core::Time { return final_time_; }
        [[nodiscard]] auto maximum_steps() const noexcept -> std::size_t { return maximum_steps_; }
        [[nodiscard]] auto workspace() noexcept -> RegionalTimeIntegrationWorkspace & { return workspace_; }
        [[nodiscard]] auto workspace() const noexcept -> const RegionalTimeIntegrationWorkspace & { return workspace_; }
        [[nodiscard]] auto earthquake_diagnostics() const noexcept
            -> const std::optional<RegionalEarthquakeInitialisationDiagnostics> &
        {
            return earthquake_diagnostics_;
        }
        [[nodiscard]] auto diagnostics() const noexcept -> const RegionalCasePreparationDiagnostics &
        {
            return diagnostics_;
        }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        friend auto prepare_regional_case(const RegionalCasePreparationRequest &request)
            -> tsunami::core::Result<RegionalPreparedCase>;

        RegionalPreparedCase(
            tsunami::fvm::MeshBinding binding,
            RegionalBathymetry bathymetry,
            RegionalSimulationState simulation_state,
            RegionalBoundaryConditionSet regional_boundaries,
            RegionalRelaxationZoneSet relaxation_zones,
            RegionalSourceTermSet local_sources,
            ShallowWaterStatePolicy state_policy,
            RegionalTimeIntegrationPolicy time_policy,
            RegionalSnapshotOutputPolicy output_policy,
            RegionalTimeIntegrationWorkspace workspace,
            tsunami::core::Time final_time,
            std::size_t maximum_steps,
            std::optional<RegionalEarthquakeInitialisationDiagnostics> earthquake_diagnostics,
            RegionalCasePreparationDiagnostics diagnostics);

        tsunami::fvm::MeshBinding binding_;
        RegionalBathymetry bathymetry_;
        RegionalSimulationState simulation_state_;
        RegionalBoundaryConditionSet regional_boundaries_;
        RegionalRelaxationZoneSet relaxation_zones_;
        RegionalSourceTermSet local_sources_;
        ShallowWaterStatePolicy state_policy_;
        RegionalTimeIntegrationPolicy time_policy_;
        RegionalSnapshotOutputPolicy output_policy_;
        RegionalTimeIntegrationWorkspace workspace_;
        tsunami::core::Time final_time_{};
        std::size_t maximum_steps_{};
        std::optional<RegionalEarthquakeInitialisationDiagnostics> earthquake_diagnostics_;
        RegionalCasePreparationDiagnostics diagnostics_;
    };

    [[nodiscard]] auto prepare_regional_case(const RegionalCasePreparationRequest &request)
        -> tsunami::core::Result<RegionalPreparedCase>;

    [[nodiscard]] auto make_regional_solve_request(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        RegionalPreparedCase &prepared,
        RegionalStepDiagnosticsSink diagnostics_sink = {},
        RegionalSnapshotSink snapshot_sink = {},
        std::stop_token stop_token = {}) -> tsunami::core::Result<RegionalSolveRequest>;

} // namespace tsunami::r2d
