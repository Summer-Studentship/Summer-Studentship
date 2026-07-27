#include <tsunami/r2d/ForwardEulerUpdate.hpp>

#include <cmath>

namespace tsunami::r2d
{
    RegionalStateUpdateWorkspace::RegionalStateUpdateWorkspace(
        tsunami::fvm::MeshBinding binding,
        std::vector<ConservedVariables2D> staging_states)
        : binding_{std::move(binding)}
        , staging_states_{std::move(staging_states)}
    {
    }

    auto RegionalStateUpdateWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               staging_states_.size() == mesh.summary().cell_count;
    }

    auto make_regional_state_update_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<RegionalStateUpdateWorkspace>
    {
        return tsunami::core::success(RegionalStateUpdateWorkspace{
            tsunami::fvm::make_mesh_binding(mesh),
            std::vector<ConservedVariables2D>(mesh.summary().cell_count, ConservedVariables2D{})});
    }

    auto forward_euler_update(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &current,
        const RegionalResidual &residual,
        tsunami::core::Real timestep,
        const ShallowWaterStatePolicy &policy,
        RegionalConservedState &destination,
        RegionalStateUpdateWorkspace &workspace) -> tsunami::core::Result<void>
    {
        const auto mesh_id = mesh.summary().id;
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure(policy_validation.error());
        }
        if (!std::isfinite(timestep) || timestep <= 0.0) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.update.timestep_invalid",
                "forward Euler timestep must be finite and positive",
                "forward_euler_update",
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
        if (!current.is_bound_to(mesh)) {
            return tsunami::core::failure(detail::r2d_error("r2d.update.state_incompatible", "current state is not bound to the mesh", "forward_euler_update", "SWE-R2D-TIM", &mesh_id));
        }
        if (!residual.is_bound_to(mesh) || residual.size() != mesh.summary().cell_count) {
            return tsunami::core::failure(detail::r2d_error("r2d.update.residual_incompatible", "residual is not bound to the mesh", "forward_euler_update", "SWE-R2D-TIM", &mesh_id));
        }
        if (!destination.is_bound_to(mesh) || !destination.is_layout_compatible_with(current)) {
            return tsunami::core::failure(detail::r2d_error("r2d.update.destination_incompatible", "destination state is not compatible", "forward_euler_update", "SWE-R2D-TIM", &mesh_id));
        }
        if (!workspace.is_bound_to(mesh)) {
            return tsunami::core::failure(detail::r2d_error("r2d.update.mesh_incompatible", "update workspace is not compatible", "forward_euler_update", "SWE-R2D-TIM", &mesh_id));
        }

        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto measure = mesh.cell_geometry(cell_id).measure;
            if (!std::isfinite(measure) || measure <= 0.0) {
                return tsunami::core::failure(detail::r2d_error("r2d.update.candidate_invalid", "cell area must be finite and positive", "forward_euler_update", "SWE-R2D-TIM", &mesh_id, cell_id));
            }
            const auto scale = timestep / measure;
            const auto old = current.local_state(cell_id);
            const auto candidate = ConservedVariables2D{
                .depth = old.depth - (scale * residual.mass().at(index)),
                .momentum_x = old.momentum_x - (scale * residual.momentum_x().at(index)),
                .momentum_y = old.momentum_y - (scale * residual.momentum_y().at(index))};
            if (!std::isfinite(candidate.depth) || !std::isfinite(candidate.momentum_x) || !std::isfinite(candidate.momentum_y)) {
                return tsunami::core::failure(detail::r2d_error("r2d.update.candidate_invalid", "candidate update must be finite", "forward_euler_update", "SWE-R2D-TIM", &mesh_id, cell_id));
            }
            auto canonical = validate_and_canonicalise_state(candidate, policy, cell_id);
            if (!canonical) {
                return tsunami::core::failure(detail::r2d_error("r2d.update.candidate_invalid", "candidate update is not admissible", "forward_euler_update", "SWE-R2D-TIM", &mesh_id, cell_id).with_cause_code(canonical.error().code()));
            }
            workspace.staging_states()[index] = canonical.value();
        }

        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            destination.set_local_state(tsunami::fvm::CellId{index}, workspace.staging_states()[index]);
        }
        return tsunami::core::success();
    }

} // namespace tsunami::r2d
