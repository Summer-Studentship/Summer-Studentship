#pragma once

#include <span>
#include <vector>

#include <tsunami/r2d/RegionalResidual.hpp>

namespace tsunami::r2d
{
    class RegionalStateUpdateWorkspace
    {
    public:
        RegionalStateUpdateWorkspace(const RegionalStateUpdateWorkspace &) = delete;
        auto operator=(const RegionalStateUpdateWorkspace &) -> RegionalStateUpdateWorkspace & = delete;
        RegionalStateUpdateWorkspace(RegionalStateUpdateWorkspace &&) noexcept = default;
        auto operator=(RegionalStateUpdateWorkspace &&) noexcept -> RegionalStateUpdateWorkspace & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto staging_states() noexcept -> std::span<ConservedVariables2D> { return staging_states_; }
        [[nodiscard]] auto staging_states() const noexcept -> std::span<const ConservedVariables2D> { return staging_states_; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        friend auto make_regional_state_update_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
            -> tsunami::core::Result<RegionalStateUpdateWorkspace>;

        RegionalStateUpdateWorkspace(tsunami::fvm::MeshBinding binding, std::vector<ConservedVariables2D> staging_states);

        tsunami::fvm::MeshBinding binding_;
        std::vector<ConservedVariables2D> staging_states_;
    };

    [[nodiscard]] auto make_regional_state_update_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<RegionalStateUpdateWorkspace>;

    auto forward_euler_update(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &current,
        const RegionalResidual &residual,
        tsunami::core::Real timestep,
        const ShallowWaterStatePolicy &policy,
        RegionalConservedState &destination,
        RegionalStateUpdateWorkspace &workspace) -> tsunami::core::Result<void>;

} // namespace tsunami::r2d
