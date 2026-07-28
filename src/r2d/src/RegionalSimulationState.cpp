#include <tsunami/r2d/RegionalSimulationState.hpp>

namespace tsunami::r2d
{
    RegionalSimulationState::RegionalSimulationState(
        RegionalConservedState conserved_state,
        tsunami::core::Time time,
        std::size_t accepted_step_count)
        : conserved_state_{std::move(conserved_state)}
        , time_{time}
        , accepted_step_count_{accepted_step_count}
    {
    }

    auto RegionalSimulationState::accept_step(
        const RegionalConservedState &candidate,
        const RegionalStepDiagnostics &diagnostics) -> tsunami::core::Result<void>
    {
        auto copy = conserved_state_.copy_values_from(candidate);
        if (!copy) {
            return copy;
        }
        time_ = diagnostics.end_time;
        accepted_step_count_ = diagnostics.step_index + 1U;
        return tsunami::core::success();
    }

} // namespace tsunami::r2d
