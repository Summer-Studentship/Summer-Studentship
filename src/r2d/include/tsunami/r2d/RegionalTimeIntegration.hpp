#pragma once

#include <optional>

#include <tsunami/r2d/ExplicitIntegration.hpp>
#include <tsunami/r2d/RegionalDiagnostics.hpp>
#include <tsunami/r2d/RegionalStateCombination.hpp>
#include <tsunami/r2d/WellBalancedResidualEvaluation.hpp>
#include <tsunami/r2d/WetDryUpdate.hpp>

namespace tsunami::r2d
{
    struct RegionalTimeIntegrationPolicy
    {
        ExplicitIntegrationScheme scheme{ExplicitIntegrationScheme::ssprk3};
        tsunami::core::Real courant_number{0.45};
        tsunami::core::Real positivity_safety_factor{0.95};
        tsunami::core::Real relaxation_safety_factor{1.0};
        tsunami::core::Real source_safety_factor{1.0};
        tsunami::core::Real timestep_comparison_tolerance{1.0e-12};
        tsunami::core::Real minimum_timestep{1.0e-12};
        tsunami::core::Real maximum_timestep{1.0};
        tsunami::core::Real retry_factor{0.5};
        std::size_t maximum_stage_retries{16};
        RegionalReconstructionPolicy reconstruction{};
    };

    enum class RegionalStepAttemptStatus
    {
        accepted,
        retry_with_smaller_timestep
    };

    struct RegionalStepAttemptResult
    {
        RegionalStepAttemptStatus status{RegionalStepAttemptStatus::accepted};
        std::optional<tsunami::core::Real> suggested_timestep;
        RegionalStepDiagnostics diagnostics;
    };

    class RegionalTimeIntegrationWorkspace
    {
    public:
        RegionalTimeIntegrationWorkspace(
            RegionalConservedState stage_1,
            RegionalConservedState stage_2,
            RegionalConservedState euler_stage,
            RegionalConservedState candidate,
            RegionalStateCombinationWorkspace combination,
            WellBalancedResidualWorkspace residual,
            PhysicalBoundaryResidualWorkspace physical_residual,
            RegionalConservedState source_half_state,
            RegionalConservedState source_candidate_state,
            RegionalSourceUpdateWorkspace source_update,
            WetDryUpdateWorkspace wet_dry,
            tsunami::fvm::CellScalarField free_surface);

        RegionalTimeIntegrationWorkspace(const RegionalTimeIntegrationWorkspace &) = delete;
        auto operator=(const RegionalTimeIntegrationWorkspace &) -> RegionalTimeIntegrationWorkspace & = delete;
        RegionalTimeIntegrationWorkspace(RegionalTimeIntegrationWorkspace &&) noexcept = default;
        auto operator=(RegionalTimeIntegrationWorkspace &&) noexcept -> RegionalTimeIntegrationWorkspace & = default;

        [[nodiscard]] auto stage_1() noexcept -> RegionalConservedState & { return stage_1_; }
        [[nodiscard]] auto stage_2() noexcept -> RegionalConservedState & { return stage_2_; }
        [[nodiscard]] auto euler_stage() noexcept -> RegionalConservedState & { return euler_stage_; }
        [[nodiscard]] auto candidate() noexcept -> RegionalConservedState & { return candidate_; }
        [[nodiscard]] auto candidate() const noexcept -> const RegionalConservedState & { return candidate_; }
        [[nodiscard]] auto combination() noexcept -> RegionalStateCombinationWorkspace & { return combination_; }
        [[nodiscard]] auto residual_workspace() noexcept -> WellBalancedResidualWorkspace & { return residual_; }
        [[nodiscard]] auto physical_residual_workspace() noexcept -> PhysicalBoundaryResidualWorkspace & { return physical_residual_; }
        [[nodiscard]] auto source_half_state() noexcept -> RegionalConservedState & { return source_half_state_; }
        [[nodiscard]] auto source_candidate_state() noexcept -> RegionalConservedState & { return source_candidate_state_; }
        [[nodiscard]] auto source_update_workspace() noexcept -> RegionalSourceUpdateWorkspace & { return source_update_; }
        [[nodiscard]] auto wet_dry_workspace() noexcept -> WetDryUpdateWorkspace & { return wet_dry_; }
        [[nodiscard]] auto free_surface_workspace() noexcept -> tsunami::fvm::CellScalarField & { return free_surface_; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        RegionalConservedState stage_1_;
        RegionalConservedState stage_2_;
        RegionalConservedState euler_stage_;
        RegionalConservedState candidate_;
        RegionalStateCombinationWorkspace combination_;
        WellBalancedResidualWorkspace residual_;
        PhysicalBoundaryResidualWorkspace physical_residual_;
        RegionalConservedState source_half_state_;
        RegionalConservedState source_candidate_state_;
        RegionalSourceUpdateWorkspace source_update_;
        WetDryUpdateWorkspace wet_dry_;
        tsunami::fvm::CellScalarField free_surface_;
    };

    [[nodiscard]] auto make_regional_time_integration_policy(
        ExplicitIntegrationScheme scheme,
        tsunami::core::Real courant_number,
        tsunami::core::Real positivity_safety_factor,
        tsunami::core::Real minimum_timestep,
        tsunami::core::Real maximum_timestep) -> tsunami::core::Result<RegionalTimeIntegrationPolicy>;

    [[nodiscard]] auto validate_regional_time_integration_policy(const RegionalTimeIntegrationPolicy &policy)
        -> tsunami::core::Result<void>;

    [[nodiscard]] auto make_regional_time_integration_workspace(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &reference_state) -> tsunami::core::Result<RegionalTimeIntegrationWorkspace>;

    [[nodiscard]] auto attempt_regional_explicit_step(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &current,
        const RegionalBathymetry &bathymetry,
        const ScalarBoundaryConditionSet &depth_boundaries,
        const ScalarBoundaryConditionSet &momentum_x_boundaries,
        const ScalarBoundaryConditionSet &momentum_y_boundaries,
        const ScalarBoundaryConditionSet &bathymetry_boundaries,
        const ShallowWaterStatePolicy &state_policy,
        const RegionalTimeIntegrationPolicy &time_policy,
        tsunami::core::Time start_time,
        tsunami::core::Real timestep,
        std::size_t step_index,
        RegionalTimeIntegrationWorkspace &workspace) -> tsunami::core::Result<RegionalStepAttemptResult>;

    [[nodiscard]] auto attempt_regional_explicit_step(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &current,
        const RegionalBathymetry &bathymetry,
        const RegionalBoundaryConditionSet &boundaries,
        const RegionalRelaxationZoneSet &relaxation_zones,
        const ShallowWaterStatePolicy &state_policy,
        const RegionalTimeIntegrationPolicy &time_policy,
        tsunami::core::Time start_time,
        tsunami::core::Real timestep,
        std::size_t step_index,
        RegionalTimeIntegrationWorkspace &workspace) -> tsunami::core::Result<RegionalStepAttemptResult>;

    [[nodiscard]] auto attempt_regional_explicit_step(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &current,
        const RegionalBathymetry &bathymetry,
        const RegionalBoundaryConditionSet &boundaries,
        const RegionalRelaxationZoneSet &relaxation_zones,
        const RegionalSourceTermSet &local_sources,
        const ShallowWaterStatePolicy &state_policy,
        const RegionalTimeIntegrationPolicy &time_policy,
        tsunami::core::Time start_time,
        tsunami::core::Real timestep,
        std::size_t step_index,
        RegionalTimeIntegrationWorkspace &workspace) -> tsunami::core::Result<RegionalStepAttemptResult>;

} // namespace tsunami::r2d
