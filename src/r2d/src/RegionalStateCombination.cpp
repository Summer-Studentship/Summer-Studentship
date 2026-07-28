#include <tsunami/r2d/RegionalStateCombination.hpp>

#include <cmath>

namespace tsunami::r2d
{
    RegionalStateCombinationWorkspace::RegionalStateCombinationWorkspace(std::vector<ConservedVariables2D> staging)
        : staging_{std::move(staging)}
    {
    }

    auto RegionalStateCombinationWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return staging_.size() == mesh.summary().cell_count;
    }

    auto make_regional_state_combination_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<RegionalStateCombinationWorkspace>
    {
        return tsunami::core::success(RegionalStateCombinationWorkspace{
            std::vector<ConservedVariables2D>(mesh.summary().cell_count, ConservedVariables2D{})});
    }

    auto convex_combine_regional_states(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &first,
        tsunami::core::Real first_weight,
        const RegionalConservedState &second,
        tsunami::core::Real second_weight,
        const ShallowWaterStatePolicy &policy,
        RegionalConservedState &destination,
        RegionalStateCombinationWorkspace &workspace) -> tsunami::core::Result<void>
    {
        const auto mesh_id = mesh.summary().id;
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return policy_validation;
        }
        if (!std::isfinite(first_weight) || !std::isfinite(second_weight) ||
            first_weight < 0.0 || second_weight < 0.0 || std::abs((first_weight + second_weight) - 1.0) > 1.0e-12) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.combination.weights_invalid",
                "regional state combination weights must be convex",
                "convex_combine_regional_states",
                "SWE-R2D-TIM",
                &mesh_id));
        }
        if (!first.is_bound_to(mesh) || !second.is_bound_to(mesh) || !destination.is_bound_to(mesh) ||
            !first.is_layout_compatible_with(second) || !destination.is_layout_compatible_with(first) ||
            !workspace.is_bound_to(mesh)) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.combination.state_incompatible",
                "regional state combination inputs are not compatible",
                "convex_combine_regional_states",
                "SWE-R2D-TIM",
                &mesh_id));
        }

        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto left = first.local_state(cell_id);
            const auto right = second.local_state(cell_id);
            auto combined = ConservedVariables2D{
                .depth = (first_weight * left.depth) + (second_weight * right.depth),
                .momentum_x = (first_weight * left.momentum_x) + (second_weight * right.momentum_x),
                .momentum_y = (first_weight * left.momentum_y) + (second_weight * right.momentum_y)};
            auto canonical = validate_and_canonicalise_state(combined, policy, cell_id);
            if (!canonical) {
                return tsunami::core::failure(canonical.error());
            }
            workspace.staging()[index] = canonical.value();
        }
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            destination.set_local_state(tsunami::fvm::CellId{index}, workspace.staging()[index]);
        }
        return tsunami::core::success();
    }

} // namespace tsunami::r2d
