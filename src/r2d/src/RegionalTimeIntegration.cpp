#include <tsunami/r2d/RegionalTimeIntegration.hpp>

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
            auto cfl = estimate_cfl_timestep(mesh, workspace.spectral_sum(), time_policy.courant_number);
            if (!cfl) {
                return tsunami::core::failure<StageLimit>(cfl.error());
            }
            auto positivity = estimate_positivity_timestep(mesh, state, workspace.outgoing_mass_rate(), time_policy.positivity_safety_factor);
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
            auto update = wet_dry_forward_euler_update(
                mesh,
                source,
                workspace.residual_workspace().residual(),
                timestep,
                limit.value().stable,
                state_policy,
                destination,
                wet_dry,
                workspace.wet_dry_workspace());
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
        WetDryUpdateWorkspace wet_dry,
        tsunami::fvm::CellScalarField free_surface)
        : stage_1_{std::move(stage_1)}
        , stage_2_{std::move(stage_2)}
        , euler_stage_{std::move(euler_stage)}
        , candidate_{std::move(candidate)}
        , combination_{std::move(combination)}
        , residual_{std::move(residual)}
        , wet_dry_{std::move(wet_dry)}
        , free_surface_{std::move(free_surface)}
    {
    }

    auto RegionalTimeIntegrationWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return stage_1_.is_bound_to(mesh) && stage_2_.is_bound_to(mesh) && euler_stage_.is_bound_to(mesh) &&
               candidate_.is_bound_to(mesh) && combination_.is_bound_to(mesh) && residual_.is_bound_to(mesh) &&
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
            auto integrals = calculate_regional_integrals(mesh, workspace.candidate(), state_policy);
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
            auto combine = convex_combine_regional_states(
                mesh,
                current,
                0.5,
                workspace.euler_stage(),
                0.5,
                state_policy,
                workspace.candidate(),
                workspace.combination());
            if (!combine) {
                return tsunami::core::failure<RegionalStepAttemptResult>(combine.error());
            }
            auto integrals = calculate_regional_integrals(mesh, workspace.candidate(), state_policy);
            if (!integrals) {
                return tsunami::core::failure<RegionalStepAttemptResult>(integrals.error());
            }
            diagnostics.integrals = integrals.value();
            return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
        }

        auto combine_stage_2 = convex_combine_regional_states(
            mesh,
            current,
            0.75,
            workspace.euler_stage(),
            0.25,
            state_policy,
            workspace.stage_2(),
            workspace.combination());
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

        auto combine_candidate = convex_combine_regional_states(
            mesh,
            current,
            1.0 / 3.0,
            workspace.euler_stage(),
            2.0 / 3.0,
            state_policy,
            workspace.candidate(),
            workspace.combination());
        if (!combine_candidate) {
            return tsunami::core::failure<RegionalStepAttemptResult>(combine_candidate.error());
        }
        auto integrals = calculate_regional_integrals(mesh, workspace.candidate(), state_policy);
        if (!integrals) {
            return tsunami::core::failure<RegionalStepAttemptResult>(integrals.error());
        }
        diagnostics.integrals = integrals.value();
        return tsunami::core::success(RegionalStepAttemptResult{RegionalStepAttemptStatus::accepted, std::nullopt, diagnostics});
    }

} // namespace tsunami::r2d
