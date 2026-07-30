#include <tsunami/r2d/RegionalSourceTimestep.hpp>

#include <cmath>
#include <string>

namespace tsunami::r2d
{
    namespace
    {
        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto timestep_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh *mesh = nullptr,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt,
            std::optional<tsunami::core::Real> timestep = std::nullopt) -> tsunami::core::Error
        {
            const auto mesh_id = mesh ? mesh->summary().id : tsunami::fvm::MeshId{};
            return detail::r2d_error(
                std::move(code),
                std::move(message),
                std::move(operation),
                "SWE-R2D-SRC",
                mesh ? &mesh_id : nullptr,
                cell_id,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                timestep);
        }

        [[nodiscard]] auto manning_value(const RegionalSourceTermSet &sources, std::size_t index) -> tsunami::core::Real
        {
            const auto *field = sources.manning_coefficient();
            return field == nullptr ? 0.0 : field->at(index);
        }

        [[nodiscard]] auto coriolis_value(const RegionalSourceTermSet &sources, std::size_t index) -> tsunami::core::Real
        {
            const auto *field = sources.coriolis_parameter();
            return field == nullptr ? 0.0 : field->at(index);
        }
    } // namespace

    auto estimate_regional_source_timestep(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalSourceTermSet &sources,
        const ShallowWaterStatePolicy &state_policy,
        tsunami::core::Real source_safety_factor,
        tsunami::core::Real comparison_tolerance) -> tsunami::core::Result<RegionalSourceTimestepEstimate>
    {
        if (!finite(source_safety_factor) || source_safety_factor <= 0.0 || source_safety_factor > 1.0) {
            return tsunami::core::failure<RegionalSourceTimestepEstimate>(timestep_error(
                "r2d.source_timestep.safety_factor_invalid",
                "source safety factor must be finite and in (0, 1]",
                "estimate_regional_source_timestep",
                &mesh,
                std::nullopt,
                source_safety_factor));
        }
        if (!finite(comparison_tolerance) || comparison_tolerance < 0.0) {
            return tsunami::core::failure<RegionalSourceTimestepEstimate>(timestep_error(
                "r2d.source_timestep.comparison_tolerance_invalid",
                "source timestep comparison tolerance must be finite and nonnegative",
                "estimate_regional_source_timestep",
                &mesh));
        }
        auto policy_validation = validate_policy(state_policy);
        if (!policy_validation) {
            return tsunami::core::failure<RegionalSourceTimestepEstimate>(policy_validation.error());
        }
        if (!state.is_bound_to(mesh) || !sources.is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalSourceTimestepEstimate>(timestep_error(
                "r2d.source.mesh_incompatible",
                "source timestep inputs are incompatible",
                "estimate_regional_source_timestep",
                &mesh));
        }

        auto estimate = RegionalSourceTimestepEstimate{};
        std::optional<tsunami::fvm::CellId> manning_cell;
        std::optional<tsunami::fvm::CellId> coriolis_cell;
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            auto canonical = validate_and_canonicalise_state(state.local_state(cell_id), state_policy, cell_id);
            if (!canonical) {
                return tsunami::core::failure<RegionalSourceTimestepEstimate>(canonical.error());
            }
            const auto accepted = canonical.value();
            const auto n = manning_value(sources, index);
            const auto f = coriolis_value(sources, index);
            if (!finite(n) || n < 0.0 || !finite(f)) {
                return tsunami::core::failure<RegionalSourceTimestepEstimate>(timestep_error(
                    "r2d.source_timestep.rate_invalid",
                    "source coefficient is invalid",
                    "estimate_regional_source_timestep",
                    &mesh,
                    cell_id));
            }
            const auto r = std::hypot(accepted.momentum_x, accepted.momentum_y);
            auto manning_rate = tsunami::core::Real{0.0};
            if (is_wet(accepted, state_policy) && r > 0.0 && n > 0.0) {
                const auto depth_power = std::pow(accepted.depth, 7.0 / 3.0);
                manning_rate = state_policy.gravity * n * n * r / depth_power;
            }
            const auto coriolis_rate = (is_wet(accepted, state_policy) && r > 0.0) ? std::abs(f) : 0.0;
            if (!finite(manning_rate) || manning_rate < 0.0 || !finite(coriolis_rate) || coriolis_rate < 0.0) {
                return tsunami::core::failure<RegionalSourceTimestepEstimate>(timestep_error(
                    "r2d.source_timestep.rate_invalid",
                    "source rate is invalid",
                    "estimate_regional_source_timestep",
                    &mesh,
                    cell_id));
            }
            if (manning_rate > estimate.maximum_manning_rate) {
                estimate.maximum_manning_rate = manning_rate;
                manning_cell = cell_id;
            }
            if (coriolis_rate > estimate.maximum_coriolis_rate) {
                estimate.maximum_coriolis_rate = coriolis_rate;
                coriolis_cell = cell_id;
            }
        }

        const auto maximum_rate = std::max(estimate.maximum_manning_rate, estimate.maximum_coriolis_rate);
        if (maximum_rate == 0.0) {
            return tsunami::core::success(estimate);
        }
        estimate.stable_timestep = source_safety_factor / maximum_rate;
        const auto tied = std::abs(estimate.maximum_manning_rate - estimate.maximum_coriolis_rate) <= comparison_tolerance;
        if (tied) {
            estimate.restriction = RegionalSourceRestrictionKind::multiple;
            if (manning_cell && coriolis_cell) {
                estimate.limiting_cell = manning_cell->value <= coriolis_cell->value ? manning_cell : coriolis_cell;
            } else {
                estimate.limiting_cell = manning_cell ? manning_cell : coriolis_cell;
            }
        } else if (estimate.maximum_manning_rate > estimate.maximum_coriolis_rate) {
            estimate.restriction = RegionalSourceRestrictionKind::manning;
            estimate.limiting_cell = manning_cell;
        } else {
            estimate.restriction = RegionalSourceRestrictionKind::coriolis;
            estimate.limiting_cell = coriolis_cell;
        }
        if (!estimate.stable_timestep || !finite(*estimate.stable_timestep) || *estimate.stable_timestep <= 0.0 ||
            !estimate.limiting_cell) {
            return tsunami::core::failure<RegionalSourceTimestepEstimate>(timestep_error(
                "r2d.source_timestep.result_invalid",
                "source timestep estimate is invalid",
                "estimate_regional_source_timestep",
                &mesh));
        }
        return tsunami::core::success(estimate);
    }

} // namespace tsunami::r2d
