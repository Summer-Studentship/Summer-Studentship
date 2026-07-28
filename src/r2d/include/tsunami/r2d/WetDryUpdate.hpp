#pragma once

#include <span>
#include <vector>

#include <tsunami/r2d/ForwardEulerUpdate.hpp>
#include <tsunami/r2d/PositivityTimestep.hpp>

namespace tsunami::r2d
{
    struct WetDryUpdateDiagnostics
    {
        std::size_t cells_wetted{};
        std::size_t cells_dried{};
        std::size_t cells_remaining_wet{};
        std::size_t cells_remaining_dry{};
        std::size_t cells_canonicalised{};

        tsunami::core::Real dry_threshold_removed_water_volume{};
        tsunami::core::Real negative_tolerance_correction_volume{};

        tsunami::core::Real minimum_candidate_depth{};
        tsunami::core::Real minimum_accepted_depth{};
    };

    class WetDryUpdateWorkspace
    {
    public:
        WetDryUpdateWorkspace(const WetDryUpdateWorkspace &) = delete;
        auto operator=(const WetDryUpdateWorkspace &) -> WetDryUpdateWorkspace & = delete;
        WetDryUpdateWorkspace(WetDryUpdateWorkspace &&) noexcept = default;
        auto operator=(WetDryUpdateWorkspace &&) noexcept -> WetDryUpdateWorkspace & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto staging_states() noexcept -> std::span<ConservedVariables2D> { return staging_states_; }
        [[nodiscard]] auto staging_states() const noexcept -> std::span<const ConservedVariables2D> { return staging_states_; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        friend auto make_wet_dry_update_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
            -> tsunami::core::Result<WetDryUpdateWorkspace>;

        WetDryUpdateWorkspace(tsunami::fvm::MeshBinding binding, std::vector<ConservedVariables2D> staging_states);

        tsunami::fvm::MeshBinding binding_;
        std::vector<ConservedVariables2D> staging_states_;
    };

    [[nodiscard]] auto make_wet_dry_update_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<WetDryUpdateWorkspace>;

    auto wet_dry_forward_euler_update(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &current,
        const RegionalResidual &residual,
        tsunami::core::Real timestep,
        const StableExplicitTimestepEstimate &stable_limit,
        const ShallowWaterStatePolicy &policy,
        RegionalConservedState &destination,
        WetDryUpdateDiagnostics &destination_diagnostics,
        WetDryUpdateWorkspace &workspace) -> tsunami::core::Result<void>;

} // namespace tsunami::r2d
