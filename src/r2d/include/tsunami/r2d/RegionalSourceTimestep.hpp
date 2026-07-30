#pragma once

#include <optional>

#include <tsunami/r2d/RegionalSourceUpdate.hpp>

namespace tsunami::r2d
{
    enum class RegionalSourceRestrictionKind
    {
        none,
        manning,
        coriolis,
        multiple
    };

    struct RegionalSourceTimestepEstimate
    {
        std::optional<tsunami::core::Real> stable_timestep;
        std::optional<tsunami::fvm::CellId> limiting_cell;
        RegionalSourceRestrictionKind restriction{RegionalSourceRestrictionKind::none};
        tsunami::core::Real maximum_manning_rate{};
        tsunami::core::Real maximum_coriolis_rate{};
    };

    [[nodiscard]] auto estimate_regional_source_timestep(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalSourceTermSet &sources,
        const ShallowWaterStatePolicy &state_policy,
        tsunami::core::Real source_safety_factor,
        tsunami::core::Real comparison_tolerance) -> tsunami::core::Result<RegionalSourceTimestepEstimate>;

} // namespace tsunami::r2d
