#pragma once

#include <span>
#include <vector>

#include <tsunami/r2d/RegionalConservedState.hpp>

namespace tsunami::r2d
{
    class RegionalStateCombinationWorkspace
    {
    public:
        explicit RegionalStateCombinationWorkspace(std::vector<ConservedVariables2D> staging);

        [[nodiscard]] auto staging() noexcept -> std::span<ConservedVariables2D> { return staging_; }
        [[nodiscard]] auto staging() const noexcept -> std::span<const ConservedVariables2D> { return staging_; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        std::vector<ConservedVariables2D> staging_;
    };

    [[nodiscard]] auto make_regional_state_combination_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<RegionalStateCombinationWorkspace>;

    auto convex_combine_regional_states(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &first,
        tsunami::core::Real first_weight,
        const RegionalConservedState &second,
        tsunami::core::Real second_weight,
        const ShallowWaterStatePolicy &policy,
        RegionalConservedState &destination,
        RegionalStateCombinationWorkspace &workspace) -> tsunami::core::Result<void>;

} // namespace tsunami::r2d
