#pragma once

#include <stop_token>

#include <tsunami/r2d/RegionalSimulationState.hpp>
#include <tsunami/r2d/RegionalSnapshot.hpp>
#include <tsunami/r2d/RegionalTimeIntegration.hpp>

namespace tsunami::r2d
{
    enum class RegionalSolveTerminationReason
    {
        final_time_reached,
        maximum_steps_reached,
        minimum_timestep_reached,
        cancelled,
        callback_failed,
        numerical_failure
    };

    struct RegionalSolveSummary
    {
        RegionalSolveTerminationReason termination_reason{RegionalSolveTerminationReason::final_time_reached};
        bool completed_successfully{true};
        std::size_t accepted_step_count{};
        std::size_t rejected_attempt_count{};
        tsunami::core::Time final_time{};
        tsunami::core::Real last_timestep{};
        RegionalIntegralDiagnostics final_integrals;
    };

    struct RegionalSolveRequest
    {
        const tsunami::fvm::FiniteVolumeMesh *mesh{};
        const RegionalBathymetry *bathymetry{};
        const ScalarBoundaryConditionSet *depth_boundaries{};
        const ScalarBoundaryConditionSet *momentum_x_boundaries{};
        const ScalarBoundaryConditionSet *momentum_y_boundaries{};
        const ScalarBoundaryConditionSet *bathymetry_boundaries{};
        const RegionalBoundaryConditionSet *regional_boundaries{};
        const RegionalRelaxationZoneSet *relaxation_zones{};
        ShallowWaterStatePolicy state_policy;
        RegionalTimeIntegrationPolicy time_policy;
        RegionalSnapshotOutputPolicy output_policy;
        tsunami::core::Time final_time{};
        std::size_t maximum_steps{1000};
        RegionalStepDiagnosticsSink diagnostics_sink;
        RegionalSnapshotSink snapshot_sink;
        std::stop_token stop_token{};
    };

    [[nodiscard]] auto solve_regional_model(
        const RegionalSolveRequest &request,
        RegionalSimulationState &simulation_state,
        RegionalTimeIntegrationWorkspace &workspace) -> tsunami::core::Result<RegionalSolveSummary>;

} // namespace tsunami::r2d
