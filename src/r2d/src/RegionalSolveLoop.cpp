#include <tsunami/r2d/RegionalSolveLoop.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace tsunami::r2d
{
    namespace
    {
        [[nodiscard]] auto solve_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh *mesh = nullptr,
            std::optional<tsunami::core::Real> timestep = std::nullopt) -> tsunami::core::Error
        {
            const auto mesh_id = mesh ? mesh->summary().id : tsunami::fvm::MeshId{};
            return detail::r2d_error(
                std::move(code),
                std::move(message),
                std::move(operation),
                "SWE-R2D-SOL",
                mesh ? &mesh_id : nullptr,
                std::nullopt,
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

        [[nodiscard]] auto make_summary(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalSimulationState &state,
            const ShallowWaterStatePolicy &state_policy,
            RegionalSolveTerminationReason reason,
            bool completed,
            std::size_t rejected_attempts,
            tsunami::core::Real last_timestep) -> tsunami::core::Result<RegionalSolveSummary>
        {
            auto integrals = calculate_regional_integrals(mesh, state.conserved_state(), state_policy);
            if (!integrals) {
                return tsunami::core::failure<RegionalSolveSummary>(integrals.error());
            }
            return tsunami::core::success(RegionalSolveSummary{
                reason,
                completed,
                state.accepted_step_count(),
                rejected_attempts,
                state.time(),
                last_timestep,
                integrals.value()});
        }

        auto emit_snapshot(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalSimulationState &state,
            const RegionalBathymetry &bathymetry,
            RegionalSnapshotSink &sink,
            RegionalTimeIntegrationWorkspace &workspace) -> tsunami::core::Result<void>
        {
            if (!sink) {
                return tsunami::core::success();
            }
            auto snapshot = make_regional_snapshot(
                mesh,
                state.conserved_state(),
                bathymetry,
                state.time(),
                state.accepted_step_count(),
                workspace.free_surface_workspace());
            if (!snapshot) {
                return tsunami::core::failure(snapshot.error());
            }
            return sink(snapshot.value());
        }
    } // namespace

    auto solve_regional_model(
        const RegionalSolveRequest &request,
        RegionalSimulationState &simulation_state,
        RegionalTimeIntegrationWorkspace &workspace) -> tsunami::core::Result<RegionalSolveSummary>
    {
        if (request.mesh == nullptr || request.bathymetry == nullptr) {
            return tsunami::core::failure<RegionalSolveSummary>(solve_error(
                "r2d.solve.request_invalid",
                "regional solve request is missing required mesh, bathymetry or boundary references",
                "solve_regional_model"));
        }
        const auto &mesh = *request.mesh;
        const auto uses_physical_boundaries = request.regional_boundaries != nullptr;
        const auto has_legacy_boundaries = request.depth_boundaries != nullptr || request.momentum_x_boundaries != nullptr ||
                                           request.momentum_y_boundaries != nullptr || request.bathymetry_boundaries != nullptr;
        if (uses_physical_boundaries == has_legacy_boundaries) {
            return tsunami::core::failure<RegionalSolveSummary>(solve_error(
                "r2d.solve.request_invalid",
                "regional solve request must specify exactly one boundary mode",
                "solve_regional_model",
                &mesh));
        }
        auto empty_relaxation_storage = make_regional_relaxation_zone_set(mesh, {});
        if (!empty_relaxation_storage) {
            return tsunami::core::failure<RegionalSolveSummary>(empty_relaxation_storage.error());
        }
        const auto *relaxation_zones = request.relaxation_zones;
        if (uses_physical_boundaries && relaxation_zones == nullptr) {
            relaxation_zones = std::addressof(empty_relaxation_storage.value());
        }
        auto state_policy_validation = validate_policy(request.state_policy);
        auto time_policy_validation = validate_regional_time_integration_policy(request.time_policy);
        if (!state_policy_validation) {
            return tsunami::core::failure<RegionalSolveSummary>(state_policy_validation.error());
        }
        if (!time_policy_validation) {
            return tsunami::core::failure<RegionalSolveSummary>(time_policy_validation.error());
        }
        if (!std::isfinite(request.final_time) || request.final_time < simulation_state.time() ||
            !simulation_state.conserved_state().is_bound_to(mesh) || !workspace.is_bound_to(mesh) ||
            !request.bathymetry->is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalSolveSummary>(solve_error(
                "r2d.solve.request_invalid",
                "regional solve request is invalid or incompatible with the simulation state",
                "solve_regional_model",
                &mesh));
        }
        if (uses_physical_boundaries) {
            if (!request.regional_boundaries->is_complete_for(mesh) || relaxation_zones == nullptr ||
                !relaxation_zones->is_bound_to(mesh)) {
                return tsunami::core::failure<RegionalSolveSummary>(solve_error(
                    "r2d.solve.request_invalid",
                    "regional physical boundary request is invalid or incompatible",
                    "solve_regional_model",
                    &mesh));
            }
        } else if (request.depth_boundaries == nullptr || request.momentum_x_boundaries == nullptr ||
                   request.momentum_y_boundaries == nullptr || request.bathymetry_boundaries == nullptr ||
                   !request.depth_boundaries->is_complete_for(mesh) ||
                   !request.momentum_x_boundaries->is_complete_for(mesh) ||
                   !request.momentum_y_boundaries->is_complete_for(mesh) ||
                   !request.bathymetry_boundaries->is_complete_for(mesh)) {
            return tsunami::core::failure<RegionalSolveSummary>(solve_error(
                "r2d.solve.request_invalid",
                "regional scalar boundary request is invalid or incomplete",
                "solve_regional_model",
                &mesh));
        }
        if (request.output_policy.interval &&
            (!std::isfinite(*request.output_policy.interval) || *request.output_policy.interval <= 0.0)) {
            return tsunami::core::failure<RegionalSolveSummary>(solve_error(
                "r2d.solve.output_policy_invalid",
                "snapshot interval must be finite and positive",
                "solve_regional_model",
                &mesh));
        }

        auto diagnostics_sink = request.diagnostics_sink;
        auto snapshot_sink = request.snapshot_sink;
        std::size_t rejected_attempts = 0U;
        auto last_timestep = tsunami::core::Real{0.0};
        auto next_snapshot_time = request.output_policy.interval
                                      ? simulation_state.time() + *request.output_policy.interval
                                      : std::numeric_limits<tsunami::core::Real>::infinity();
        auto last_snapshot_time = std::optional<tsunami::core::Time>{};

        if (request.output_policy.emit_initial_snapshot) {
            auto snapshot = emit_snapshot(mesh, simulation_state, *request.bathymetry, snapshot_sink, workspace);
            if (!snapshot) {
                return tsunami::core::failure<RegionalSolveSummary>(snapshot.error());
            }
            last_snapshot_time = simulation_state.time();
        }

        constexpr auto time_tolerance = 1.0e-12;
        while (simulation_state.time() < request.final_time - time_tolerance) {
            if (request.stop_token.stop_requested()) {
                return make_summary(mesh, simulation_state, request.state_policy, RegionalSolveTerminationReason::cancelled, true, rejected_attempts, last_timestep);
            }
            if (simulation_state.accepted_step_count() >= request.maximum_steps) {
                return make_summary(mesh, simulation_state, request.state_policy, RegionalSolveTerminationReason::maximum_steps_reached, true, rejected_attempts, last_timestep);
            }

            auto remaining = request.final_time - simulation_state.time();
            auto timestep = std::min(request.time_policy.maximum_timestep, remaining);
            if (request.output_policy.interval && next_snapshot_time < request.final_time - time_tolerance) {
                timestep = std::min(timestep, next_snapshot_time - simulation_state.time());
            }
            if (!std::isfinite(timestep) || timestep <= 0.0) {
                return tsunami::core::failure<RegionalSolveSummary>(solve_error(
                    "r2d.solve.timestep_invalid",
                    "candidate solve timestep is invalid",
                    "solve_regional_model",
                    &mesh,
                    timestep));
            }

            auto accepted = false;
            RegionalStepDiagnostics accepted_diagnostics;
            auto step_rejected_attempts = std::size_t{0U};
            for (std::size_t attempt = 0; attempt <= request.time_policy.maximum_stage_retries; ++attempt) {
                auto step = uses_physical_boundaries
                                ? attempt_regional_explicit_step(
                                      mesh,
                                      simulation_state.conserved_state(),
                                      *request.bathymetry,
                                      *request.regional_boundaries,
                                      *relaxation_zones,
                                      request.state_policy,
                                      request.time_policy,
                                      simulation_state.time(),
                                      timestep,
                                      simulation_state.accepted_step_count(),
                                      workspace)
                                : attempt_regional_explicit_step(
                                      mesh,
                                      simulation_state.conserved_state(),
                                      *request.bathymetry,
                                      *request.depth_boundaries,
                                      *request.momentum_x_boundaries,
                                      *request.momentum_y_boundaries,
                                      *request.bathymetry_boundaries,
                                      request.state_policy,
                                      request.time_policy,
                                      simulation_state.time(),
                                      timestep,
                                      simulation_state.accepted_step_count(),
                                      workspace);
                if (!step) {
                    return tsunami::core::failure<RegionalSolveSummary>(step.error());
                }
                if (step.value().status == RegionalStepAttemptStatus::accepted) {
                    accepted_diagnostics = step.value().diagnostics;
                    accepted_diagnostics.rejected_attempts = step_rejected_attempts;
                    auto accept = simulation_state.accept_step(workspace.candidate(), accepted_diagnostics);
                    if (!accept) {
                        return tsunami::core::failure<RegionalSolveSummary>(accept.error());
                    }
                    last_timestep = timestep;
                    accepted = true;
                    break;
                }

                ++rejected_attempts;
                ++step_rejected_attempts;
                auto suggested = step.value().suggested_timestep.value_or(timestep * request.time_policy.retry_factor);
                timestep = std::min(timestep * request.time_policy.retry_factor, suggested * request.time_policy.retry_factor);
                if (!std::isfinite(timestep) || timestep < request.time_policy.minimum_timestep) {
                    return make_summary(mesh, simulation_state, request.state_policy, RegionalSolveTerminationReason::minimum_timestep_reached, false, rejected_attempts, last_timestep);
                }
            }
            if (!accepted) {
                return make_summary(mesh, simulation_state, request.state_policy, RegionalSolveTerminationReason::minimum_timestep_reached, false, rejected_attempts, last_timestep);
            }

            if (diagnostics_sink) {
                auto emitted = diagnostics_sink(accepted_diagnostics);
                if (!emitted) {
                    return tsunami::core::failure<RegionalSolveSummary>(emitted.error());
                }
            }
            if (request.output_policy.interval && simulation_state.time() >= next_snapshot_time - time_tolerance) {
                auto snapshot = emit_snapshot(mesh, simulation_state, *request.bathymetry, snapshot_sink, workspace);
                if (!snapshot) {
                    return tsunami::core::failure<RegionalSolveSummary>(snapshot.error());
                }
                last_snapshot_time = simulation_state.time();
                next_snapshot_time += *request.output_policy.interval;
            }
        }

        const auto final_already_emitted = last_snapshot_time && std::abs(*last_snapshot_time - simulation_state.time()) <= time_tolerance;
        if (request.output_policy.emit_final_snapshot && !final_already_emitted) {
            auto snapshot = emit_snapshot(mesh, simulation_state, *request.bathymetry, snapshot_sink, workspace);
            if (!snapshot) {
                return tsunami::core::failure<RegionalSolveSummary>(snapshot.error());
            }
        }
        return make_summary(mesh, simulation_state, request.state_policy, RegionalSolveTerminationReason::final_time_reached, true, rejected_attempts, last_timestep);
    }

} // namespace tsunami::r2d
