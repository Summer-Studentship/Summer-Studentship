#pragma once

#include <optional>

#include <tsunami/r2d/ExplicitIntegration.hpp>
#include <tsunami/r2d/PositivityTimestep.hpp>
#include <tsunami/r2d/WetDryUpdate.hpp>

namespace tsunami::r2d
{
    struct RegionalIntegralDiagnostics
    {
        tsunami::core::Real water_volume{};
        tsunami::core::Real momentum_x{};
        tsunami::core::Real momentum_y{};
        std::size_t wet_cell_count{};
        std::size_t dry_cell_count{};
        tsunami::core::Real minimum_depth{};
        tsunami::core::Real maximum_depth{};
    };

    struct RegionalStepDiagnostics
    {
        std::size_t step_index{};
        tsunami::core::Time start_time{};
        tsunami::core::Time end_time{};
        tsunami::core::Real timestep{};
        ExplicitIntegrationScheme scheme{ExplicitIntegrationScheme::forward_euler};
        std::size_t attempted_stages{};
        std::size_t accepted_stages{};
        std::size_t rejected_attempts{};
        tsunami::core::Real maximum_signal_speed{};
        StableExplicitTimestepEstimate stable_timestep;
        WetDryUpdateDiagnostics wet_dry;
        RegionalIntegralDiagnostics integrals;
    };

    [[nodiscard]] auto calculate_regional_integrals(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<RegionalIntegralDiagnostics>;

} // namespace tsunami::r2d
