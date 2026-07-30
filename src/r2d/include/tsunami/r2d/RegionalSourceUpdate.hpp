#pragma once

#include <optional>
#include <span>
#include <vector>

#include <tsunami/r2d/RegionalConservedState.hpp>
#include <tsunami/r2d/RegionalSourceTerms.hpp>

namespace tsunami::r2d
{
    struct RegionalSourceUpdateDiagnostics
    {
        std::size_t manning_active_cell_count{};
        std::size_t coriolis_active_cell_count{};
        tsunami::core::Real maximum_manning_coefficient{};
        tsunami::core::Real maximum_coriolis_magnitude{};
        tsunami::core::Real maximum_manning_rate{};
        tsunami::core::Real maximum_coriolis_rate{};
        std::optional<tsunami::fvm::CellId> manning_limiting_cell;
        std::optional<tsunami::fvm::CellId> coriolis_limiting_cell;
        tsunami::core::Real momentum_x_change{};
        tsunami::core::Real momentum_y_change{};
        tsunami::core::Real initial_kinetic_energy{};
        tsunami::core::Real final_kinetic_energy{};
        tsunami::core::Real friction_kinetic_energy_removed{};
        tsunami::core::Real coriolis_kinetic_energy_error{};
    };

    class RegionalSourceUpdateWorkspace
    {
    public:
        RegionalSourceUpdateWorkspace(const RegionalSourceUpdateWorkspace &) = delete;
        auto operator=(const RegionalSourceUpdateWorkspace &) -> RegionalSourceUpdateWorkspace & = delete;
        RegionalSourceUpdateWorkspace(RegionalSourceUpdateWorkspace &&) noexcept = default;
        auto operator=(RegionalSourceUpdateWorkspace &&) noexcept -> RegionalSourceUpdateWorkspace & = default;

        [[nodiscard]] auto candidate_state() noexcept -> RegionalConservedState & { return candidate_state_; }
        [[nodiscard]] auto manning_rates() noexcept -> std::span<tsunami::core::Real> { return manning_rates_; }
        [[nodiscard]] auto coriolis_rates() noexcept -> std::span<tsunami::core::Real> { return coriolis_rates_; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        friend auto make_regional_source_update_workspace(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalConservedState &reference_state) -> tsunami::core::Result<RegionalSourceUpdateWorkspace>;

        RegionalSourceUpdateWorkspace(
            tsunami::fvm::MeshBinding binding,
            RegionalConservedState candidate_state,
            std::vector<tsunami::core::Real> manning_rates,
            std::vector<tsunami::core::Real> coriolis_rates);

        tsunami::fvm::MeshBinding binding_;
        RegionalConservedState candidate_state_;
        std::vector<tsunami::core::Real> manning_rates_;
        std::vector<tsunami::core::Real> coriolis_rates_;
    };

    [[nodiscard]] auto make_regional_source_update_workspace(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &reference_state) -> tsunami::core::Result<RegionalSourceUpdateWorkspace>;

    [[nodiscard]] auto cell_kinetic_energy(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::CellId cell_id,
        ConservedVariables2D state,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<tsunami::core::Real>;

    auto apply_regional_local_sources(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &current,
        const RegionalSourceTermSet &sources,
        const ShallowWaterStatePolicy &policy,
        tsunami::core::Real substep,
        RegionalConservedState &destination,
        RegionalSourceUpdateDiagnostics &diagnostics,
        RegionalSourceUpdateWorkspace &workspace) -> tsunami::core::Result<void>;

    [[nodiscard]] auto combine_source_diagnostics(
        RegionalSourceUpdateDiagnostics first,
        RegionalSourceUpdateDiagnostics second) -> RegionalSourceUpdateDiagnostics;

} // namespace tsunami::r2d
