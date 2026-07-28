#pragma once

#include <tsunami/r2d/RegionalDiagnostics.hpp>

namespace tsunami::r2d
{
    class RegionalSimulationState
    {
    public:
        RegionalSimulationState(
            RegionalConservedState conserved_state,
            tsunami::core::Time time,
            std::size_t accepted_step_count);

        RegionalSimulationState(const RegionalSimulationState &) = delete;
        auto operator=(const RegionalSimulationState &) -> RegionalSimulationState & = delete;
        RegionalSimulationState(RegionalSimulationState &&) noexcept = default;
        auto operator=(RegionalSimulationState &&) noexcept -> RegionalSimulationState & = default;

        [[nodiscard]] auto conserved_state() noexcept -> RegionalConservedState & { return conserved_state_; }
        [[nodiscard]] auto conserved_state() const noexcept -> const RegionalConservedState & { return conserved_state_; }
        [[nodiscard]] auto time() const noexcept -> tsunami::core::Time { return time_; }
        [[nodiscard]] auto accepted_step_count() const noexcept -> std::size_t { return accepted_step_count_; }

        auto accept_step(const RegionalConservedState &candidate, const RegionalStepDiagnostics &diagnostics)
            -> tsunami::core::Result<void>;

    private:
        RegionalConservedState conserved_state_;
        tsunami::core::Time time_{};
        std::size_t accepted_step_count_{};
    };

} // namespace tsunami::r2d
