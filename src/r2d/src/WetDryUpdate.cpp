#include <tsunami/r2d/WetDryUpdate.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tsunami::r2d
{
    namespace
    {
        [[nodiscard]] auto valid_stable_limit(const StableExplicitTimestepEstimate &limit) -> bool
        {
            if (limit.stable_timestep.has_value() != limit.limiting_cell.has_value()) {
                return false;
            }
            if (!limit.stable_timestep) {
                return limit.restriction == TimestepRestrictionKind::none;
            }
            return std::isfinite(*limit.stable_timestep) && *limit.stable_timestep > 0.0 &&
                   limit.restriction != TimestepRestrictionKind::none;
        }
    } // namespace

    WetDryUpdateWorkspace::WetDryUpdateWorkspace(tsunami::fvm::MeshBinding binding, std::vector<ConservedVariables2D> staging_states)
        : binding_{std::move(binding)}
        , staging_states_{std::move(staging_states)}
    {
    }

    auto WetDryUpdateWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               staging_states_.size() == mesh.summary().cell_count;
    }

    auto make_wet_dry_update_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<WetDryUpdateWorkspace>
    {
        return tsunami::core::success(WetDryUpdateWorkspace{
            tsunami::fvm::make_mesh_binding(mesh),
            std::vector<ConservedVariables2D>(mesh.summary().cell_count, ConservedVariables2D{})});
    }

    auto wet_dry_forward_euler_update(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &current,
        const RegionalResidual &residual,
        tsunami::core::Real timestep,
        const StableExplicitTimestepEstimate &stable_limit,
        const ShallowWaterStatePolicy &policy,
        RegionalConservedState &destination,
        WetDryUpdateDiagnostics &destination_diagnostics,
        WetDryUpdateWorkspace &workspace) -> tsunami::core::Result<void>
    {
        const auto mesh_id = mesh.summary().id;
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure(policy_validation.error());
        }
        if (!std::isfinite(timestep) || timestep <= 0.0) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.wet_dry.timestep_invalid",
                "wet/dry timestep must be finite and positive",
                "wet_dry_forward_euler_update",
                "SWE-R2D-WD",
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
        if (!valid_stable_limit(stable_limit)) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.timestep.estimate_invalid",
                "stable timestep estimate is invalid",
                "wet_dry_forward_euler_update",
                "SWE-R2D-WD",
                &mesh_id));
        }
        constexpr auto timestep_tolerance = 8.0 * std::numeric_limits<tsunami::core::Real>::epsilon();
        if (stable_limit.stable_timestep && timestep > (*stable_limit.stable_timestep * (1.0 + timestep_tolerance))) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.wet_dry.timestep_exceeds_bound",
                "wet/dry timestep exceeds the supplied stable bound",
                "wet_dry_forward_euler_update",
                "SWE-R2D-WD",
                &mesh_id,
                stable_limit.limiting_cell,
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
        if (!current.is_bound_to(mesh) || current.size() != mesh.summary().cell_count) {
            return tsunami::core::failure(detail::r2d_error("r2d.wet_dry.state_incompatible", "current state is not bound to the mesh", "wet_dry_forward_euler_update", "SWE-R2D-WD", &mesh_id));
        }
        if (!residual.is_bound_to(mesh) || residual.size() != mesh.summary().cell_count) {
            return tsunami::core::failure(detail::r2d_error("r2d.wet_dry.residual_incompatible", "residual is not bound to the mesh", "wet_dry_forward_euler_update", "SWE-R2D-WD", &mesh_id));
        }
        if (!destination.is_bound_to(mesh) || !destination.is_layout_compatible_with(current)) {
            return tsunami::core::failure(detail::r2d_error("r2d.wet_dry.destination_incompatible", "destination state is not compatible", "wet_dry_forward_euler_update", "SWE-R2D-WD", &mesh_id));
        }
        if (!workspace.is_bound_to(mesh)) {
            return tsunami::core::failure(detail::r2d_error("r2d.wet_dry.state_incompatible", "wet/dry update workspace is not compatible", "wet_dry_forward_euler_update", "SWE-R2D-WD", &mesh_id));
        }

        auto diagnostics = WetDryUpdateDiagnostics{};
        diagnostics.minimum_candidate_depth = std::numeric_limits<tsunami::core::Real>::infinity();
        diagnostics.minimum_accepted_depth = std::numeric_limits<tsunami::core::Real>::infinity();

        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto area = mesh.cell_geometry(cell_id).measure;
            if (!std::isfinite(area) || area <= 0.0) {
                return tsunami::core::failure(detail::r2d_error("r2d.wet_dry.candidate_invalid", "cell area must be finite and positive", "wet_dry_forward_euler_update", "SWE-R2D-WD", &mesh_id, cell_id));
            }
            const auto mass = residual.mass().at(index);
            const auto qx = residual.momentum_x().at(index);
            const auto qy = residual.momentum_y().at(index);
            if (!std::isfinite(mass) || !std::isfinite(qx) || !std::isfinite(qy)) {
                return tsunami::core::failure(detail::r2d_error("r2d.wet_dry.residual_incompatible", "residual values must be finite", "wet_dry_forward_euler_update", "SWE-R2D-WD", &mesh_id, cell_id));
            }
            const auto scale = timestep / area;
            const auto old = current.local_state(cell_id);
            const auto candidate = ConservedVariables2D{
                .depth = old.depth - (scale * mass),
                .momentum_x = old.momentum_x - (scale * qx),
                .momentum_y = old.momentum_y - (scale * qy)};
            diagnostics.minimum_candidate_depth = std::min(diagnostics.minimum_candidate_depth, candidate.depth);
            if (!std::isfinite(candidate.depth) || !std::isfinite(candidate.momentum_x) || !std::isfinite(candidate.momentum_y) ||
                candidate.depth < -policy.depth_tolerance) {
                return tsunami::core::failure(detail::r2d_error("r2d.wet_dry.candidate_invalid", "candidate update is outside wet/dry admissibility", "wet_dry_forward_euler_update", "SWE-R2D-WD", &mesh_id, cell_id));
            }
            auto accepted = candidate;
            const auto canonicalised = candidate.depth <= policy.dry_depth;
            if (canonicalised) {
                if (candidate.depth > 0.0) {
                    diagnostics.dry_threshold_removed_water_volume += area * candidate.depth;
                } else if (candidate.depth < 0.0) {
                    diagnostics.negative_tolerance_correction_volume += area * (-candidate.depth);
                }
                accepted = ConservedVariables2D{};
                ++diagnostics.cells_canonicalised;
            }
            auto accepted_validation = validate_and_canonicalise_state(accepted, policy, cell_id);
            if (!accepted_validation) {
                return tsunami::core::failure(detail::r2d_error("r2d.wet_dry.candidate_invalid", "accepted update is not admissible", "wet_dry_forward_euler_update", "SWE-R2D-WD", &mesh_id, cell_id).with_cause_code(accepted_validation.error().code()));
            }
            accepted = accepted_validation.value();
            diagnostics.minimum_accepted_depth = std::min(diagnostics.minimum_accepted_depth, accepted.depth);
            const auto was_wet = old.depth > policy.dry_depth;
            const auto is_now_wet = accepted.depth > policy.dry_depth;
            if (!was_wet && is_now_wet) {
                ++diagnostics.cells_wetted;
            } else if (was_wet && !is_now_wet) {
                ++diagnostics.cells_dried;
            } else if (was_wet) {
                ++diagnostics.cells_remaining_wet;
            } else {
                ++diagnostics.cells_remaining_dry;
            }
            workspace.staging_states()[index] = accepted;
        }

        if (workspace.staging_states().empty()) {
            diagnostics.minimum_candidate_depth = 0.0;
            diagnostics.minimum_accepted_depth = 0.0;
        }
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            destination.set_local_state(tsunami::fvm::CellId{index}, workspace.staging_states()[index]);
        }
        destination_diagnostics = diagnostics;
        return tsunami::core::success();
    }

} // namespace tsunami::r2d
