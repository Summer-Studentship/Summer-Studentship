#include <tsunami/r2d/RegionalTimeIntegration.hpp>

#include <tsunami/r2d/RegionalPerformanceTiming.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#include <tsunami/fvm/MeshField.hpp>

namespace tsunami::r2d
{
    namespace
    {
        struct StageLimit
        {
            StableExplicitTimestepEstimate stable;
            tsunami::core::Real maximum_signal_speed{};
        };

        [[nodiscard]] auto time_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt,
            std::optional<tsunami::core::Real> timestep = std::nullopt) -> tsunami::core::Error
        {
            const auto mesh_id = mesh.summary().id;
            return detail::r2d_error(
                std::move(code),
                std::move(message),
                std::move(operation),
                "SWE-R2D-TIM",
                &mesh_id,
                cell_id,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                timestep);
        }

        [[nodiscard]] auto requested_timestep_is_within(
            tsunami::core::Real timestep,
            const StableExplicitTimestepEstimate &stable) -> bool
        {
            if (!stable.stable_timestep) {
                return true;
            }
            return timestep <= (*stable.stable_timestep * (1.0 + 8.0e-14));
        }

        [[nodiscard]] auto evaluate_stage_limit(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalConservedState &state,
            const RegionalBathymetry &bathymetry,
            const ScalarBoundaryConditionSet &depth_boundaries,
            const ScalarBoundaryConditionSet &momentum_x_boundaries,
            const ScalarBoundaryConditionSet &momentum_y_boundaries,
            const ScalarBoundaryConditionSet &bathymetry_boundaries,
            const ShallowWaterStatePolicy &state_policy,
            const RegionalTimeIntegrationPolicy &time_policy,
            WellBalancedResidualWorkspace &workspace) -> tsunami::core::Result<StageLimit>
        {
            StageLimit limit;
            auto residual = evaluate_well_balanced_rusanov_residual(
                mesh,
                state,
                bathymetry,
                depth_boundaries,
                momentum_x_boundaries,
                momentum_y_boundaries,
                bathymetry_boundaries,
                state_policy,
                workspace.residual(),
                workspace.spectral_sum(),
                workspace.outgoing_mass_rate(),
                limit.maximum_signal_speed,
                workspace);
            if (!residual) {
                return tsunami::core::failure<StageLimit>(residual.error());
            }
            auto cfl = tsunami::core::Result<CflTimestepEstimate>{CflTimestepEstimate{}};
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::cfl_reduction};
                cfl = estimate_cfl_timestep(mesh, workspace.spectral_sum(), time_policy.courant_number);
            }
            if (!cfl) {
                return tsunami::core::failure<StageLimit>(cfl.error());
            }
            auto positivity = tsunami::core::Result<PositivityTimestepEstimate>{PositivityTimestepEstimate{}};
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::positivity_reduction};
                positivity = estimate_positivity_timestep(mesh, state, workspace.outgoing_mass_rate(), time_policy.positivity_safety_factor);
            }
            if (!positivity) {
                return tsunami::core::failure<StageLimit>(positivity.error());
            }
            auto stable = select_stable_explicit_timestep(cfl.value(), positivity.value(), time_policy.timestep_comparison_tolerance);
            if (!stable) {
                return tsunami::core::failure<StageLimit>(stable.error());
            }
            limit.stable = stable.value();
            return tsunami::core::success(limit);
        }

        [[nodiscard]] auto evaluate_physical_stage_limit(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalConservedState &state,
            const RegionalBathymetry &bathymetry,
            const RegionalBoundaryConditionSet &boundaries,
            const RegionalRelaxationZoneSet &relaxation_zones,
            const ShallowWaterStatePolicy &state_policy,
            const RegionalTimeIntegrationPolicy &time_policy,
            tsunami::core::Time time,
            PhysicalBoundaryResidualWorkspace &workspace) -> tsunami::core::Result<StageLimit>
        {
            StageLimit limit;
            auto residual = evaluate_well_balanced_rusanov_residual(
                mesh,
                state,
                bathymetry,
                boundaries,
                relaxation_zones,
                state_policy,
                time,
                workspace.residual(),
                workspace.spectral_sum(),
                workspace.outgoing_mass_rate(),
                limit.maximum_signal_speed,
                workspace);
            if (!residual) {
                return tsunami::core::failure<StageLimit>(residual.error());
            }
            auto cfl = tsunami::core::Result<CflTimestepEstimate>{CflTimestepEstimate{}};
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::cfl_reduction};
                cfl = estimate_cfl_timestep(mesh, workspace.spectral_sum(), time_policy.courant_number);
            }
            if (!cfl) {
                return tsunami::core::failure<StageLimit>(cfl.error());
            }
            auto positivity = tsunami::core::Result<PositivityTimestepEstimate>{PositivityTimestepEstimate{}};
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::positivity_reduction};
                positivity = estimate_positivity_timestep(mesh, state, workspace.outgoing_mass_rate(), time_policy.positivity_safety_factor);
            }
            if (!positivity) {
                return tsunami::core::failure<StageLimit>(positivity.error());
            }
            auto relaxation = tsunami::core::Result<RelaxationTimestepEstimate>{RelaxationTimestepEstimate{}};
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::relaxation_timestep};
                relaxation = estimate_relaxation_timestep(mesh, relaxation_zones, time_policy.relaxation_safety_factor);
            }
            if (!relaxation) {
                return tsunami::core::failure<StageLimit>(relaxation.error());
            }
            auto stable = select_stable_explicit_timestep(cfl.value(), positivity.value(), relaxation.value(), time_policy.timestep_comparison_tolerance);
            if (!stable) {
                return tsunami::core::failure<StageLimit>(stable.error());
            }
            limit.stable = stable.value();
            return tsunami::core::success(limit);
        }

        [[nodiscard]] auto retry_result(
            const RegionalStepDiagnostics &diagnostics,
            const StableExplicitTimestepEstimate &stable) -> RegionalStepAttemptResult
        {
            auto result = RegionalStepAttemptResult{};
            result.status = RegionalStepAttemptStatus::retry_with_smaller_timestep;
            result.suggested_timestep = stable.stable_timestep;
            result.diagnostics = diagnostics;
            result.diagnostics.stable_timestep = stable;
            return result;
        }

        [[nodiscard]] auto source_stable_estimate(const RegionalSourceTimestepEstimate &source) -> StableExplicitTimestepEstimate
        {
            auto restriction = TimestepRestrictionKind::none;
            if (source.stable_timestep) {
                restriction = source.restriction == RegionalSourceRestrictionKind::multiple
                                  ? TimestepRestrictionKind::multiple
                                  : TimestepRestrictionKind::source;
            }
            return StableExplicitTimestepEstimate{source.stable_timestep, source.limiting_cell, restriction};
        }

        [[nodiscard]] auto euler_stage(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalConservedState &source,
            const RegionalBathymetry &bathymetry,
            const ScalarBoundaryConditionSet &depth_boundaries,
            const ScalarBoundaryConditionSet &momentum_x_boundaries,
            const ScalarBoundaryConditionSet &momentum_y_boundaries,
            const ScalarBoundaryConditionSet &bathymetry_boundaries,
            const ShallowWaterStatePolicy &state_policy,
            const RegionalTimeIntegrationPolicy &time_policy,
            tsunami::core::Real timestep,
            RegionalConservedState &destination,
            WetDryUpdateDiagnostics &wet_dry,
            RegionalTimeIntegrationWorkspace &workspace,
            RegionalStepDiagnostics &diagnostics) -> tsunami::core::Result<RegionalStepAttemptResult>
        {
            auto limit = evaluate_stage_limit(
                mesh,
                source,
                bathymetry,
                depth_boundaries,
                momentum_x_boundaries,
                momentum_y_boundaries,
                bathymetry_boundaries,
                state_policy,
                time_policy,
                workspace.residual_workspace());
            if (!limit) {
                return tsunami::core::failure<RegionalStepAttemptResult>(limit.error());
            }
            diagnostics.stable_timestep = limit.value().stable;
            diagnostics.maximum_signal_speed = std::max(diagnostics.maximum_signal_speed, limit.value().maximum_signal_speed);
            ++diagnostics.attempted_stages;
            if (!requested_timestep_is_within(timestep, limit.value().stable)) {
                return tsunami::core::success(retry_result(diagnostics, limit.value().stable));
            }
            auto update = tsunami::core::success();
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::state_update};
                update = wet_dry_forward_euler_update(
                    mesh,
                    source,
                    workspace.residual_workspace().residual(),
                    timestep,
                    limit.value().stable,
                    state_policy,
                    destination,
                    wet_dry,
                    workspace.wet_dry_workspace());
            }
            if (!update) {
                return tsunami::core::failure<RegionalStepAttemptResult>(update.error());
            }
            ++diagnostics.accepted_stages;
            diagnostics.wet_dry = wet_dry;
            return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
        }

        [[nodiscard]] auto physical_euler_stage(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalConservedState &source,
            const RegionalBathymetry &bathymetry,
            const RegionalBoundaryConditionSet &boundaries,
            const RegionalRelaxationZoneSet &relaxation_zones,
            const ShallowWaterStatePolicy &state_policy,
            const RegionalTimeIntegrationPolicy &time_policy,
            tsunami::core::Time stage_time,
            tsunami::core::Real timestep,
            RegionalConservedState &destination,
            WetDryUpdateDiagnostics &wet_dry,
            RegionalTimeIntegrationWorkspace &workspace,
            RegionalStepDiagnostics &diagnostics) -> tsunami::core::Result<RegionalStepAttemptResult>
        {
            auto limit = evaluate_physical_stage_limit(
                mesh,
                source,
                bathymetry,
                boundaries,
                relaxation_zones,
                state_policy,
                time_policy,
                stage_time,
                workspace.physical_residual_workspace());
            if (!limit) {
                return tsunami::core::failure<RegionalStepAttemptResult>(limit.error());
            }
            diagnostics.stable_timestep = limit.value().stable;
            diagnostics.maximum_signal_speed = std::max(diagnostics.maximum_signal_speed, limit.value().maximum_signal_speed);
            auto &relaxation = workspace.physical_residual_workspace().relaxation_diagnostics();
            diagnostics.relaxation.zone_count = std::max(diagnostics.relaxation.zone_count, relaxation.zone_count);
            diagnostics.relaxation.active_cell_count = std::max(diagnostics.relaxation.active_cell_count, relaxation.active_cell_count);
            diagnostics.relaxation.maximum_rate = std::max(diagnostics.relaxation.maximum_rate, relaxation.maximum_rate);
            diagnostics.relaxation.integrated_mass_source_rate += relaxation.integrated_mass_source_rate;
            diagnostics.relaxation.outgoing_mass_rate += relaxation.outgoing_mass_rate;
            ++diagnostics.attempted_stages;
            if (!requested_timestep_is_within(timestep, limit.value().stable)) {
                return tsunami::core::success(retry_result(diagnostics, limit.value().stable));
            }
            auto update = tsunami::core::success();
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::state_update};
                update = wet_dry_forward_euler_update(
                    mesh,
                    source,
                    workspace.physical_residual_workspace().residual(),
                    timestep,
                    limit.value().stable,
                    state_policy,
                    destination,
                    wet_dry,
                    workspace.wet_dry_workspace());
            }
            if (!update) {
                return tsunami::core::failure<RegionalStepAttemptResult>(update.error());
            }
            ++diagnostics.accepted_stages;
            diagnostics.wet_dry = wet_dry;
            return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
        }
    } // namespace

    RegionalTimeIntegrationWorkspace::RegionalTimeIntegrationWorkspace(
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
        tsunami::fvm::CellScalarField free_surface)
        : stage_1_{std::move(stage_1)}
        , stage_2_{std::move(stage_2)}
        , euler_stage_{std::move(euler_stage)}
        , candidate_{std::move(candidate)}
        , combination_{std::move(combination)}
        , residual_{std::move(residual)}
        , physical_residual_{std::move(physical_residual)}
        , source_half_state_{std::move(source_half_state)}
        , source_candidate_state_{std::move(source_candidate_state)}
        , source_update_{std::move(source_update)}
        , wet_dry_{std::move(wet_dry)}
        , free_surface_{std::move(free_surface)}
    {
    }

    auto RegionalTimeIntegrationWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return stage_1_.is_bound_to(mesh) && stage_2_.is_bound_to(mesh) && euler_stage_.is_bound_to(mesh) &&
               candidate_.is_bound_to(mesh) && combination_.is_bound_to(mesh) && residual_.is_bound_to(mesh) &&
               physical_residual_.is_bound_to(mesh) && source_half_state_.is_bound_to(mesh) &&
               source_candidate_state_.is_bound_to(mesh) && source_update_.is_bound_to(mesh) &&
               wet_dry_.is_bound_to(mesh) && free_surface_.is_bound_to(mesh) &&
               free_surface_.size() == mesh.summary().cell_count;
    }

    auto make_regional_time_integration_policy(
        ExplicitIntegrationScheme scheme,
        tsunami::core::Real courant_number,
        tsunami::core::Real positivity_safety_factor,
        tsunami::core::Real minimum_timestep,
        tsunami::core::Real maximum_timestep) -> tsunami::core::Result<RegionalTimeIntegrationPolicy>
    {
        auto policy = RegionalTimeIntegrationPolicy{};
        policy.scheme = scheme;
        policy.courant_number = courant_number;
        policy.positivity_safety_factor = positivity_safety_factor;
        policy.minimum_timestep = minimum_timestep;
        policy.maximum_timestep = maximum_timestep;
        auto validation = validate_regional_time_integration_policy(policy);
        if (!validation) {
            return tsunami::core::failure<RegionalTimeIntegrationPolicy>(validation.error());
        }
        return tsunami::core::success(policy);
    }

    auto validate_regional_time_integration_policy(const RegionalTimeIntegrationPolicy &policy)
        -> tsunami::core::Result<void>
    {
        if (!std::isfinite(policy.courant_number) || policy.courant_number <= 0.0 || policy.courant_number > 1.0 ||
            !std::isfinite(policy.positivity_safety_factor) || policy.positivity_safety_factor <= 0.0 || policy.positivity_safety_factor > 1.0 ||
            !std::isfinite(policy.relaxation_safety_factor) || policy.relaxation_safety_factor <= 0.0 ||
            policy.relaxation_safety_factor > 1.0 ||
            !std::isfinite(policy.source_safety_factor) || policy.source_safety_factor <= 0.0 ||
            policy.source_safety_factor > 1.0 ||
            !std::isfinite(policy.timestep_comparison_tolerance) || policy.timestep_comparison_tolerance < 0.0 ||
            !std::isfinite(policy.minimum_timestep) || policy.minimum_timestep <= 0.0 ||
            !std::isfinite(policy.maximum_timestep) || policy.maximum_timestep <= 0.0 ||
            policy.minimum_timestep > policy.maximum_timestep ||
            !std::isfinite(policy.retry_factor) || policy.retry_factor <= 0.0 || policy.retry_factor >= 1.0 ||
            policy.maximum_stage_retries == 0U) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.time.policy_invalid",
                "regional time integration policy is invalid",
                "validate_regional_time_integration_policy",
                "SWE-R2D-TIM"));
        }
        return tsunami::core::success();
    }

    auto make_regional_time_integration_workspace(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &reference_state) -> tsunami::core::Result<RegionalTimeIntegrationWorkspace>
    {
        if (!reference_state.is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalTimeIntegrationWorkspace>(time_error(
                "r2d.time.state_incompatible",
                "reference state is not bound to the mesh",
                "make_regional_time_integration_workspace",
                mesh));
        }
        auto combination = make_regional_state_combination_workspace(mesh);
        auto residual = make_well_balanced_residual_workspace(mesh);
        auto physical_residual = make_physical_boundary_residual_workspace(mesh);
        auto source_update = make_regional_source_update_workspace(mesh, reference_state);
        auto wet_dry = make_wet_dry_update_workspace(mesh);
        auto free_surface = tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh,
            tsunami::fvm::FieldId{"r2d.free_surface.workspace"},
            "regional free-surface workspace",
            "m",
            0.0);
        if (!combination) {
            return tsunami::core::failure<RegionalTimeIntegrationWorkspace>(combination.error());
        }
        if (!residual) {
            return tsunami::core::failure<RegionalTimeIntegrationWorkspace>(residual.error());
        }
        if (!physical_residual) {
            return tsunami::core::failure<RegionalTimeIntegrationWorkspace>(physical_residual.error());
        }
        if (!source_update) {
            return tsunami::core::failure<RegionalTimeIntegrationWorkspace>(source_update.error());
        }
        if (!wet_dry) {
            return tsunami::core::failure<RegionalTimeIntegrationWorkspace>(wet_dry.error());
        }
        if (!free_surface) {
            return tsunami::core::failure<RegionalTimeIntegrationWorkspace>(free_surface.error());
        }
        return tsunami::core::success(RegionalTimeIntegrationWorkspace{
            reference_state.clone(),
            reference_state.clone(),
            reference_state.clone(),
            reference_state.clone(),
            std::move(combination).value(),
            std::move(residual).value(),
            std::move(physical_residual).value(),
            reference_state.clone(),
            reference_state.clone(),
            std::move(source_update).value(),
            std::move(wet_dry).value(),
            std::move(free_surface).value()});
    }

    auto attempt_regional_explicit_step(
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
        RegionalTimeIntegrationWorkspace &workspace) -> tsunami::core::Result<RegionalStepAttemptResult>
    {
        auto attempt_timer = RegionalScopedTimer{RegionalTimingRegion::timestep_attempt};
        const auto mesh_id = mesh.summary().id;
        auto state_policy_validation = validate_policy(state_policy);
        auto time_policy_validation = validate_regional_time_integration_policy(time_policy);
        if (!state_policy_validation) {
            return tsunami::core::failure<RegionalStepAttemptResult>(state_policy_validation.error());
        }
        if (!time_policy_validation) {
            return tsunami::core::failure<RegionalStepAttemptResult>(time_policy_validation.error());
        }
        if (!std::isfinite(start_time) || !std::isfinite(timestep) || timestep <= 0.0 ||
            !current.is_bound_to(mesh) || !bathymetry.is_bound_to(mesh) || !workspace.is_bound_to(mesh) ||
            !depth_boundaries.is_complete_for(mesh) || !momentum_x_boundaries.is_complete_for(mesh) ||
            !momentum_y_boundaries.is_complete_for(mesh) || !bathymetry_boundaries.is_complete_for(mesh)) {
            return tsunami::core::failure<RegionalStepAttemptResult>(detail::r2d_error(
                "r2d.time.request_invalid",
                "regional time-step attempt inputs are invalid",
                "attempt_regional_explicit_step",
                "SWE-R2D-TIM",
                &mesh_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                timestep));
        }

        auto diagnostics = RegionalStepDiagnostics{};
        diagnostics.step_index = step_index;
        diagnostics.start_time = start_time;
        diagnostics.end_time = start_time + timestep;
        diagnostics.timestep = timestep;
        diagnostics.scheme = time_policy.scheme;

        auto wet_dry = WetDryUpdateDiagnostics{};
        auto first_stage = euler_stage(
            mesh,
            current,
            bathymetry,
            depth_boundaries,
            momentum_x_boundaries,
            momentum_y_boundaries,
            bathymetry_boundaries,
            state_policy,
            time_policy,
            timestep,
            workspace.stage_1(),
            wet_dry,
            workspace,
            diagnostics);
        if (!first_stage || first_stage.value().status == RegionalStepAttemptStatus::retry_with_smaller_timestep) {
            return first_stage;
        }
        diagnostics = first_stage.value().diagnostics;

        if (time_policy.scheme == ExplicitIntegrationScheme::forward_euler) {
            auto copy = workspace.candidate().copy_values_from(workspace.stage_1());
            if (!copy) {
                return tsunami::core::failure<RegionalStepAttemptResult>(copy.error());
            }
            auto integrals = tsunami::core::Result<RegionalIntegralDiagnostics>{RegionalIntegralDiagnostics{}};
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::integrals};
                integrals = calculate_regional_integrals(mesh, workspace.candidate(), state_policy);
            }
            if (!integrals) {
                return tsunami::core::failure<RegionalStepAttemptResult>(integrals.error());
            }
            diagnostics.integrals = integrals.value();
            return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
        }

        auto second_stage_euler = euler_stage(
            mesh,
            workspace.stage_1(),
            bathymetry,
            depth_boundaries,
            momentum_x_boundaries,
            momentum_y_boundaries,
            bathymetry_boundaries,
            state_policy,
            time_policy,
            timestep,
            workspace.euler_stage(),
            wet_dry,
            workspace,
            diagnostics);
        if (!second_stage_euler || second_stage_euler.value().status == RegionalStepAttemptStatus::retry_with_smaller_timestep) {
            return second_stage_euler;
        }
        diagnostics = second_stage_euler.value().diagnostics;

        if (time_policy.scheme == ExplicitIntegrationScheme::ssprk2) {
            auto combine = tsunami::core::success();
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::state_combination};
                combine = convex_combine_regional_states(
                    mesh,
                    current,
                    0.5,
                    workspace.euler_stage(),
                    0.5,
                    state_policy,
                    workspace.candidate(),
                    workspace.combination());
            }
            if (!combine) {
                return tsunami::core::failure<RegionalStepAttemptResult>(combine.error());
            }
            auto integrals = tsunami::core::Result<RegionalIntegralDiagnostics>{RegionalIntegralDiagnostics{}};
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::integrals};
                integrals = calculate_regional_integrals(mesh, workspace.candidate(), state_policy);
            }
            if (!integrals) {
                return tsunami::core::failure<RegionalStepAttemptResult>(integrals.error());
            }
            diagnostics.integrals = integrals.value();
            return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
        }

        auto combine_stage_2 = tsunami::core::success();
        {
            auto timer = RegionalScopedTimer{RegionalTimingRegion::state_combination};
            combine_stage_2 = convex_combine_regional_states(
                mesh,
                current,
                0.75,
                workspace.euler_stage(),
                0.25,
                state_policy,
                workspace.stage_2(),
                workspace.combination());
        }
        if (!combine_stage_2) {
            return tsunami::core::failure<RegionalStepAttemptResult>(combine_stage_2.error());
        }

        auto third_stage_euler = euler_stage(
            mesh,
            workspace.stage_2(),
            bathymetry,
            depth_boundaries,
            momentum_x_boundaries,
            momentum_y_boundaries,
            bathymetry_boundaries,
            state_policy,
            time_policy,
            timestep,
            workspace.euler_stage(),
            wet_dry,
            workspace,
            diagnostics);
        if (!third_stage_euler || third_stage_euler.value().status == RegionalStepAttemptStatus::retry_with_smaller_timestep) {
            return third_stage_euler;
        }
        diagnostics = third_stage_euler.value().diagnostics;

        auto combine_candidate = tsunami::core::success();
        {
            auto timer = RegionalScopedTimer{RegionalTimingRegion::state_combination};
            combine_candidate = convex_combine_regional_states(
                mesh,
                current,
                1.0 / 3.0,
                workspace.euler_stage(),
                2.0 / 3.0,
                state_policy,
                workspace.candidate(),
                workspace.combination());
        }
        if (!combine_candidate) {
            return tsunami::core::failure<RegionalStepAttemptResult>(combine_candidate.error());
        }
        auto integrals = tsunami::core::Result<RegionalIntegralDiagnostics>{RegionalIntegralDiagnostics{}};
        {
            auto timer = RegionalScopedTimer{RegionalTimingRegion::integrals};
            integrals = calculate_regional_integrals(mesh, workspace.candidate(), state_policy);
        }
        if (!integrals) {
            return tsunami::core::failure<RegionalStepAttemptResult>(integrals.error());
        }
        diagnostics.integrals = integrals.value();
        return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
    }

    auto attempt_regional_explicit_step(
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
        RegionalTimeIntegrationWorkspace &workspace) -> tsunami::core::Result<RegionalStepAttemptResult>
    {
        auto attempt_timer = RegionalScopedTimer{RegionalTimingRegion::timestep_attempt};
        const auto mesh_id = mesh.summary().id;
        auto state_policy_validation = validate_policy(state_policy);
        auto time_policy_validation = validate_regional_time_integration_policy(time_policy);
        if (!state_policy_validation) {
            return tsunami::core::failure<RegionalStepAttemptResult>(state_policy_validation.error());
        }
        if (!time_policy_validation) {
            return tsunami::core::failure<RegionalStepAttemptResult>(time_policy_validation.error());
        }
        if (!std::isfinite(start_time) || !std::isfinite(timestep) || timestep <= 0.0 ||
            !current.is_bound_to(mesh) || !bathymetry.is_bound_to(mesh) || !workspace.is_bound_to(mesh) ||
            !boundaries.is_complete_for(mesh) || !relaxation_zones.is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalStepAttemptResult>(detail::r2d_error(
                "r2d.time.request_invalid",
                "regional physical time-step attempt inputs are invalid",
                "attempt_regional_explicit_step",
                "SWE-R2D-TIM",
                &mesh_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                timestep));
        }

        auto diagnostics = RegionalStepDiagnostics{};
        diagnostics.step_index = step_index;
        diagnostics.start_time = start_time;
        diagnostics.end_time = start_time + timestep;
        diagnostics.timestep = timestep;
        diagnostics.scheme = time_policy.scheme;

        auto wet_dry = WetDryUpdateDiagnostics{};
        auto first_stage = physical_euler_stage(
            mesh,
            current,
            bathymetry,
            boundaries,
            relaxation_zones,
            state_policy,
            time_policy,
            start_time,
            timestep,
            workspace.stage_1(),
            wet_dry,
            workspace,
            diagnostics);
        if (!first_stage || first_stage.value().status == RegionalStepAttemptStatus::retry_with_smaller_timestep) {
            return first_stage;
        }
        diagnostics = first_stage.value().diagnostics;

        if (time_policy.scheme == ExplicitIntegrationScheme::forward_euler) {
            auto copy = workspace.candidate().copy_values_from(workspace.stage_1());
            if (!copy) {
                return tsunami::core::failure<RegionalStepAttemptResult>(copy.error());
            }
            auto integrals = tsunami::core::Result<RegionalIntegralDiagnostics>{RegionalIntegralDiagnostics{}};
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::integrals};
                integrals = calculate_regional_integrals(mesh, workspace.candidate(), state_policy);
            }
            if (!integrals) {
                return tsunami::core::failure<RegionalStepAttemptResult>(integrals.error());
            }
            diagnostics.integrals = integrals.value();
            return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
        }

        auto second_stage_euler = physical_euler_stage(
            mesh,
            workspace.stage_1(),
            bathymetry,
            boundaries,
            relaxation_zones,
            state_policy,
            time_policy,
            start_time + timestep,
            timestep,
            workspace.euler_stage(),
            wet_dry,
            workspace,
            diagnostics);
        if (!second_stage_euler || second_stage_euler.value().status == RegionalStepAttemptStatus::retry_with_smaller_timestep) {
            return second_stage_euler;
        }
        diagnostics = second_stage_euler.value().diagnostics;

        if (time_policy.scheme == ExplicitIntegrationScheme::ssprk2) {
            auto combine = tsunami::core::success();
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::state_combination};
                combine = convex_combine_regional_states(
                    mesh,
                    current,
                    0.5,
                    workspace.euler_stage(),
                    0.5,
                    state_policy,
                    workspace.candidate(),
                    workspace.combination());
            }
            if (!combine) {
                return tsunami::core::failure<RegionalStepAttemptResult>(combine.error());
            }
            auto integrals = tsunami::core::Result<RegionalIntegralDiagnostics>{RegionalIntegralDiagnostics{}};
            {
                auto timer = RegionalScopedTimer{RegionalTimingRegion::integrals};
                integrals = calculate_regional_integrals(mesh, workspace.candidate(), state_policy);
            }
            if (!integrals) {
                return tsunami::core::failure<RegionalStepAttemptResult>(integrals.error());
            }
            diagnostics.integrals = integrals.value();
            return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
        }

        auto combine_stage_2 = tsunami::core::success();
        {
            auto timer = RegionalScopedTimer{RegionalTimingRegion::state_combination};
            combine_stage_2 = convex_combine_regional_states(
                mesh,
                current,
                0.75,
                workspace.euler_stage(),
                0.25,
                state_policy,
                workspace.stage_2(),
                workspace.combination());
        }
        if (!combine_stage_2) {
            return tsunami::core::failure<RegionalStepAttemptResult>(combine_stage_2.error());
        }

        auto third_stage_euler = physical_euler_stage(
            mesh,
            workspace.stage_2(),
            bathymetry,
            boundaries,
            relaxation_zones,
            state_policy,
            time_policy,
            start_time + (0.5 * timestep),
            timestep,
            workspace.euler_stage(),
            wet_dry,
            workspace,
            diagnostics);
        if (!third_stage_euler || third_stage_euler.value().status == RegionalStepAttemptStatus::retry_with_smaller_timestep) {
            return third_stage_euler;
        }
        diagnostics = third_stage_euler.value().diagnostics;

        auto combine_candidate = tsunami::core::success();
        {
            auto timer = RegionalScopedTimer{RegionalTimingRegion::state_combination};
            combine_candidate = convex_combine_regional_states(
                mesh,
                current,
                1.0 / 3.0,
                workspace.euler_stage(),
                2.0 / 3.0,
                state_policy,
                workspace.candidate(),
                workspace.combination());
        }
        if (!combine_candidate) {
            return tsunami::core::failure<RegionalStepAttemptResult>(combine_candidate.error());
        }
        auto integrals = tsunami::core::Result<RegionalIntegralDiagnostics>{RegionalIntegralDiagnostics{}};
        {
            auto timer = RegionalScopedTimer{RegionalTimingRegion::integrals};
            integrals = calculate_regional_integrals(mesh, workspace.candidate(), state_policy);
        }
        if (!integrals) {
            return tsunami::core::failure<RegionalStepAttemptResult>(integrals.error());
        }
        diagnostics.integrals = integrals.value();
        return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
    }

    auto attempt_regional_explicit_step(
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
        RegionalTimeIntegrationWorkspace &workspace) -> tsunami::core::Result<RegionalStepAttemptResult>
    {
        if (local_sources.empty()) {
            return attempt_regional_explicit_step(
                mesh,
                current,
                bathymetry,
                boundaries,
                relaxation_zones,
                state_policy,
                time_policy,
                start_time,
                timestep,
                step_index,
                workspace);
        }

        auto attempt_timer = RegionalScopedTimer{RegionalTimingRegion::timestep_attempt};
        const auto mesh_id = mesh.summary().id;
        auto state_policy_validation = validate_policy(state_policy);
        auto time_policy_validation = validate_regional_time_integration_policy(time_policy);
        if (!state_policy_validation) {
            return tsunami::core::failure<RegionalStepAttemptResult>(state_policy_validation.error());
        }
        if (!time_policy_validation) {
            return tsunami::core::failure<RegionalStepAttemptResult>(time_policy_validation.error());
        }
        if (!std::isfinite(start_time) || !std::isfinite(timestep) || timestep <= 0.0 ||
            !current.is_bound_to(mesh) || !bathymetry.is_bound_to(mesh) || !workspace.is_bound_to(mesh) ||
            !boundaries.is_complete_for(mesh) || !relaxation_zones.is_bound_to(mesh) ||
            !local_sources.is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalStepAttemptResult>(detail::r2d_error(
                "r2d.time.source_configuration_invalid",
                "regional source-enabled time-step attempt inputs are invalid",
                "attempt_regional_explicit_step",
                "SWE-R2D-SRC",
                &mesh_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                timestep));
        }

        auto diagnostics = RegionalStepDiagnostics{};
        diagnostics.step_index = step_index;
        diagnostics.start_time = start_time;
        diagnostics.end_time = start_time + timestep;
        diagnostics.timestep = timestep;
        diagnostics.scheme = time_policy.scheme;

        auto accepted_source_bound = tsunami::core::Result<RegionalSourceTimestepEstimate>{RegionalSourceTimestepEstimate{}};
        {
            auto timer = RegionalScopedTimer{RegionalTimingRegion::source_timestep};
            accepted_source_bound = estimate_regional_source_timestep(
                mesh,
                current,
                local_sources,
                state_policy,
                time_policy.source_safety_factor,
                time_policy.timestep_comparison_tolerance);
        }
        if (!accepted_source_bound) {
            return tsunami::core::failure<RegionalStepAttemptResult>(accepted_source_bound.error());
        }
        if (!requested_timestep_is_within(timestep, source_stable_estimate(accepted_source_bound.value()))) {
            diagnostics.stable_timestep = source_stable_estimate(accepted_source_bound.value());
            return tsunami::core::success(retry_result(diagnostics, diagnostics.stable_timestep));
        }

        auto first_source_diagnostics = RegionalSourceUpdateDiagnostics{};
        auto source_half = tsunami::core::success();
        {
            auto timer = RegionalScopedTimer{RegionalTimingRegion::source_update};
            source_half = apply_regional_local_sources(
                mesh,
                current,
                local_sources,
                state_policy,
                0.5 * timestep,
                workspace.source_half_state(),
                first_source_diagnostics,
                workspace.source_update_workspace());
        }
        if (!source_half) {
            return tsunami::core::failure<RegionalStepAttemptResult>(source_half.error());
        }

        auto hydro = attempt_regional_explicit_step(
            mesh,
            workspace.source_half_state(),
            bathymetry,
            boundaries,
            relaxation_zones,
            state_policy,
            time_policy,
            start_time,
            timestep,
            step_index,
            workspace);
        if (!hydro || hydro.value().status == RegionalStepAttemptStatus::retry_with_smaller_timestep) {
            return hydro;
        }

        auto candidate_source_bound = estimate_regional_source_timestep(
            mesh,
            workspace.candidate(),
            local_sources,
            state_policy,
            time_policy.source_safety_factor,
            time_policy.timestep_comparison_tolerance);
        if (!candidate_source_bound) {
            return tsunami::core::failure<RegionalStepAttemptResult>(candidate_source_bound.error());
        }
        if (!requested_timestep_is_within(timestep, source_stable_estimate(candidate_source_bound.value()))) {
            auto rejected = hydro.value().diagnostics;
            rejected.sources = first_source_diagnostics;
            rejected.stable_timestep = source_stable_estimate(candidate_source_bound.value());
            return tsunami::core::success(retry_result(rejected, rejected.stable_timestep));
        }

        auto second_source_diagnostics = RegionalSourceUpdateDiagnostics{};
        auto final_source = tsunami::core::success();
        {
            auto timer = RegionalScopedTimer{RegionalTimingRegion::source_update};
            final_source = apply_regional_local_sources(
                mesh,
                workspace.candidate(),
                local_sources,
                state_policy,
                0.5 * timestep,
                workspace.source_candidate_state(),
                second_source_diagnostics,
                workspace.source_update_workspace());
        }
        if (!final_source) {
            return tsunami::core::failure<RegionalStepAttemptResult>(final_source.error());
        }
        auto copy = workspace.candidate().copy_values_from(workspace.source_candidate_state());
        if (!copy) {
            return tsunami::core::failure<RegionalStepAttemptResult>(copy.error());
        }

        diagnostics = hydro.value().diagnostics;
        diagnostics.start_time = start_time;
        diagnostics.end_time = start_time + timestep;
        diagnostics.timestep = timestep;
        diagnostics.sources = combine_source_diagnostics(first_source_diagnostics, second_source_diagnostics);
        auto integrals = calculate_regional_integrals(mesh, workspace.candidate(), state_policy);
        if (!integrals) {
            return tsunami::core::failure<RegionalStepAttemptResult>(integrals.error());
        }
        diagnostics.integrals = integrals.value();
        return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
    }

} // namespace tsunami::r2d
