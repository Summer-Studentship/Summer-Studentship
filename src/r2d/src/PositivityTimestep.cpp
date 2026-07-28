#include <tsunami/r2d/PositivityTimestep.hpp>

#include <cmath>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto outgoing_unit = "m3/s";

        [[nodiscard]] auto valid_estimate_value(std::optional<tsunami::core::Real> value) -> bool
        {
            return !value || (std::isfinite(*value) && *value > 0.0);
        }
    } // namespace

    auto estimate_positivity_timestep(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const tsunami::fvm::CellScalarField &outgoing_mass_rate,
        tsunami::core::Real safety_factor) -> tsunami::core::Result<PositivityTimestepEstimate>
    {
        const auto mesh_id = mesh.summary().id;
        if (!std::isfinite(safety_factor) || safety_factor <= 0.0 || safety_factor > 1.0) {
            return tsunami::core::failure<PositivityTimestepEstimate>(detail::r2d_error(
                "r2d.positivity.safety_factor_invalid",
                "positivity safety factor must be finite and in (0, 1]",
                "estimate_positivity_timestep",
                "SWE-R2D-WD",
                &mesh_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                safety_factor));
        }
        if (!state.is_bound_to(mesh) || state.size() != mesh.summary().cell_count ||
            !outgoing_mass_rate.is_bound_to(mesh) || outgoing_mass_rate.size() != mesh.summary().cell_count ||
            outgoing_mass_rate.descriptor().unit_id != outgoing_unit) {
            return tsunami::core::failure<PositivityTimestepEstimate>(detail::r2d_error(
                "r2d.positivity.state_incompatible",
                "state or outgoing mass-rate field is incompatible",
                "estimate_positivity_timestep",
                "SWE-R2D-WD",
                &mesh_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                outgoing_mass_rate.descriptor().id.value,
                outgoing_unit,
                outgoing_mass_rate.descriptor().unit_id));
        }

        PositivityTimestepEstimate estimate;
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto area = mesh.cell_geometry(cell_id).measure;
            if (!std::isfinite(area) || area <= 0.0) {
                return tsunami::core::failure<PositivityTimestepEstimate>(detail::r2d_error(
                    "r2d.positivity.state_incompatible",
                    "cell area must be finite and positive",
                    "estimate_positivity_timestep",
                    "SWE-R2D-WD",
                    &mesh_id,
                    cell_id));
            }
            const auto outflow = outgoing_mass_rate.at(index);
            if (!std::isfinite(outflow)) {
                return tsunami::core::failure<PositivityTimestepEstimate>(detail::r2d_error(
                    "r2d.positivity.outgoing_rate_nonfinite",
                    "outgoing mass rate must be finite",
                    "estimate_positivity_timestep",
                    "SWE-R2D-WD",
                    &mesh_id,
                    cell_id));
            }
            if (outflow < 0.0) {
                return tsunami::core::failure<PositivityTimestepEstimate>(detail::r2d_error(
                    "r2d.positivity.outgoing_rate_negative",
                    "outgoing mass rate must be nonnegative",
                    "estimate_positivity_timestep",
                    "SWE-R2D-WD",
                    &mesh_id,
                    cell_id));
            }
            const auto depth = state.depth().at(index);
            if (!std::isfinite(depth) || depth < 0.0) {
                return tsunami::core::failure<PositivityTimestepEstimate>(detail::r2d_error(
                    "r2d.positivity.state_incompatible",
                    "state depth must be finite and nonnegative",
                    "estimate_positivity_timestep",
                    "SWE-R2D-WD",
                    &mesh_id,
                    cell_id));
            }
            if (outflow == 0.0) {
                continue;
            }
            if (depth == 0.0) {
                return tsunami::core::failure<PositivityTimestepEstimate>(detail::r2d_error(
                    "r2d.positivity.dry_cell_outflow",
                    "dry cells may not have positive outgoing mass rate",
                    "estimate_positivity_timestep",
                    "SWE-R2D-WD",
                    &mesh_id,
                    cell_id));
            }
            const auto candidate = safety_factor * area * depth / outflow;
            if (!std::isfinite(candidate) || candidate <= 0.0) {
                return tsunami::core::failure<PositivityTimestepEstimate>(detail::r2d_error(
                    "r2d.positivity.state_incompatible",
                    "positivity candidate timestep must be finite and positive",
                    "estimate_positivity_timestep",
                    "SWE-R2D-WD",
                    &mesh_id,
                    cell_id));
            }
            if (!estimate.stable_timestep || candidate < *estimate.stable_timestep) {
                estimate.stable_timestep = candidate;
                estimate.limiting_cell = cell_id;
            }
        }
        return tsunami::core::success(estimate);
    }

    auto select_stable_explicit_timestep(
        const CflTimestepEstimate &cfl,
        const PositivityTimestepEstimate &positivity,
        tsunami::core::Real comparison_tolerance) -> tsunami::core::Result<StableExplicitTimestepEstimate>
    {
        if (!std::isfinite(comparison_tolerance) || comparison_tolerance < 0.0 ||
            !valid_estimate_value(cfl.stable_timestep) || !valid_estimate_value(positivity.stable_timestep)) {
            return tsunami::core::failure<StableExplicitTimestepEstimate>(detail::r2d_error(
                "r2d.timestep.estimate_invalid",
                "timestep estimates and comparison tolerance must be finite and valid",
                "select_stable_explicit_timestep",
                "SWE-R2D-TIM"));
        }
        if (cfl.stable_timestep.has_value() != cfl.limiting_cell.has_value() ||
            positivity.stable_timestep.has_value() != positivity.limiting_cell.has_value()) {
            return tsunami::core::failure<StableExplicitTimestepEstimate>(detail::r2d_error(
                "r2d.timestep.estimate_invalid",
                "timestep estimate must provide limiting cell with its stable timestep",
                "select_stable_explicit_timestep",
                "SWE-R2D-TIM"));
        }
        if (!cfl.stable_timestep && !positivity.stable_timestep) {
            return tsunami::core::success(StableExplicitTimestepEstimate{});
        }
        if (cfl.stable_timestep && !positivity.stable_timestep) {
            return tsunami::core::success(StableExplicitTimestepEstimate{cfl.stable_timestep, cfl.limiting_cell, TimestepRestrictionKind::cfl});
        }
        if (!cfl.stable_timestep && positivity.stable_timestep) {
            return tsunami::core::success(StableExplicitTimestepEstimate{positivity.stable_timestep, positivity.limiting_cell, TimestepRestrictionKind::positivity});
        }
        const auto diff = std::abs(*cfl.stable_timestep - *positivity.stable_timestep);
        if (diff <= comparison_tolerance) {
            return tsunami::core::success(StableExplicitTimestepEstimate{std::min(*cfl.stable_timestep, *positivity.stable_timestep), cfl.limiting_cell, TimestepRestrictionKind::equal});
        }
        if (*cfl.stable_timestep < *positivity.stable_timestep) {
            return tsunami::core::success(StableExplicitTimestepEstimate{cfl.stable_timestep, cfl.limiting_cell, TimestepRestrictionKind::cfl});
        }
        return tsunami::core::success(StableExplicitTimestepEstimate{positivity.stable_timestep, positivity.limiting_cell, TimestepRestrictionKind::positivity});
    }

} // namespace tsunami::r2d
