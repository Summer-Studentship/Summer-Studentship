#pragma once

#include <optional>

#include <tsunami/r2d/CflTimestep.hpp>
#include <tsunami/r2d/RegionalConservedState.hpp>

namespace tsunami::r2d
{
    struct PositivityTimestepEstimate
    {
        std::optional<tsunami::core::Real> stable_timestep;
        std::optional<tsunami::fvm::CellId> limiting_cell;
    };

    enum class TimestepRestrictionKind
    {
        none,
        cfl,
        positivity,
        equal
    };

    struct StableExplicitTimestepEstimate
    {
        std::optional<tsunami::core::Real> stable_timestep;
        std::optional<tsunami::fvm::CellId> limiting_cell;
        TimestepRestrictionKind restriction{TimestepRestrictionKind::none};
    };

    [[nodiscard]] auto estimate_positivity_timestep(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const tsunami::fvm::CellScalarField &outgoing_mass_rate,
        tsunami::core::Real safety_factor) -> tsunami::core::Result<PositivityTimestepEstimate>;

    [[nodiscard]] auto select_stable_explicit_timestep(
        const CflTimestepEstimate &cfl,
        const PositivityTimestepEstimate &positivity,
        tsunami::core::Real comparison_tolerance) -> tsunami::core::Result<StableExplicitTimestepEstimate>;

} // namespace tsunami::r2d
