#include <tsunami/r2d/RegionalSourceUpdate.hpp>

#include <algorithm>
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

        [[nodiscard]] auto update_error(
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

        auto choose_limiting_cell(
            std::optional<tsunami::fvm::CellId> &cell,
            tsunami::core::Real &maximum,
            tsunami::core::Real candidate,
            tsunami::fvm::CellId candidate_cell) -> void
        {
            if (candidate > maximum) {
                maximum = candidate;
                cell = candidate_cell;
            }
        }
    } // namespace

    RegionalSourceUpdateWorkspace::RegionalSourceUpdateWorkspace(
        tsunami::fvm::MeshBinding binding,
        RegionalConservedState candidate_state,
        std::vector<tsunami::core::Real> manning_rates,
        std::vector<tsunami::core::Real> coriolis_rates)
        : binding_{std::move(binding)}
        , candidate_state_{std::move(candidate_state)}
        , manning_rates_{std::move(manning_rates)}
        , coriolis_rates_{std::move(coriolis_rates)}
    {
    }

    auto RegionalSourceUpdateWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               candidate_state_.is_bound_to(mesh) &&
               manning_rates_.size() == mesh.summary().cell_count &&
               coriolis_rates_.size() == mesh.summary().cell_count;
    }

    auto make_regional_source_update_workspace(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &reference_state) -> tsunami::core::Result<RegionalSourceUpdateWorkspace>
    {
        if (!reference_state.is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalSourceUpdateWorkspace>(update_error(
                "r2d.source_update.state_incompatible",
                "reference state is not bound to the mesh",
                "make_regional_source_update_workspace",
                &mesh));
        }
        return tsunami::core::success(RegionalSourceUpdateWorkspace{
            tsunami::fvm::make_mesh_binding(mesh),
            reference_state.clone(),
            std::vector<tsunami::core::Real>(mesh.summary().cell_count, 0.0),
            std::vector<tsunami::core::Real>(mesh.summary().cell_count, 0.0)});
    }

    auto cell_kinetic_energy(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::CellId cell_id,
        ConservedVariables2D state,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<tsunami::core::Real>
    {
        auto canonical = validate_and_canonicalise_state(state, policy, cell_id);
        if (!canonical) {
            return tsunami::core::failure<tsunami::core::Real>(canonical.error());
        }
        const auto accepted = canonical.value();
        if (is_dry(accepted, policy)) {
            return tsunami::core::success(0.0);
        }
        const auto area = mesh.cell_geometry(cell_id).measure;
        const auto momentum_sq = (accepted.momentum_x * accepted.momentum_x) + (accepted.momentum_y * accepted.momentum_y);
        const auto energy = 0.5 * area * momentum_sq / accepted.depth;
        if (!finite(area) || area <= 0.0 || !finite(energy) || energy < 0.0) {
            return tsunami::core::failure<tsunami::core::Real>(update_error(
                "r2d.source_update.energy_invalid",
                "cell kinetic energy must be finite and nonnegative",
                "cell_kinetic_energy",
                &mesh,
                cell_id));
        }
        return tsunami::core::success(energy);
    }

    auto apply_regional_local_sources(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &current,
        const RegionalSourceTermSet &sources,
        const ShallowWaterStatePolicy &policy,
        tsunami::core::Real substep,
        RegionalConservedState &destination,
        RegionalSourceUpdateDiagnostics &diagnostics,
        RegionalSourceUpdateWorkspace &workspace) -> tsunami::core::Result<void>
    {
        if (!finite(substep) || substep < 0.0) {
            return tsunami::core::failure(update_error(
                "r2d.source_update.substep_invalid",
                "source substep must be finite and nonnegative",
                "apply_regional_local_sources",
                &mesh,
                std::nullopt,
                substep));
        }
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure(policy_validation.error());
        }
        if (!current.is_bound_to(mesh) || !destination.is_bound_to(mesh) || !destination.is_layout_compatible_with(current) ||
            !sources.is_bound_to(mesh) || !workspace.is_bound_to(mesh)) {
            return tsunami::core::failure(update_error(
                "r2d.source_update.workspace_incompatible",
                "source update inputs are incompatible",
                "apply_regional_local_sources",
                &mesh));
        }

        auto next_diagnostics = RegionalSourceUpdateDiagnostics{};
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto state = current.local_state(cell_id);
            auto canonical = validate_and_canonicalise_state(state, policy, cell_id);
            if (!canonical) {
                return tsunami::core::failure(update_error(
                    "r2d.source_update.state_incompatible",
                    "source update state is invalid",
                    "apply_regional_local_sources",
                    &mesh,
                    cell_id)
                        .with_cause_code(canonical.error().code()));
            }
            const auto n = manning_value(sources, index);
            const auto f = coriolis_value(sources, index);
            if (!finite(n) || n < 0.0 || !finite(f)) {
                return tsunami::core::failure(update_error(
                    "r2d.source_update.state_incompatible",
                    "source coefficients are invalid",
                    "apply_regional_local_sources",
                    &mesh,
                    cell_id));
            }

            auto candidate = canonical.value();
            auto initial_energy = cell_kinetic_energy(mesh, cell_id, candidate, policy);
            if (!initial_energy) {
                return tsunami::core::failure(initial_energy.error());
            }
            next_diagnostics.initial_kinetic_energy += initial_energy.value();
            next_diagnostics.maximum_manning_coefficient = std::max(next_diagnostics.maximum_manning_coefficient, n);
            next_diagnostics.maximum_coriolis_magnitude = std::max(next_diagnostics.maximum_coriolis_magnitude, std::abs(f));

            const auto r = std::hypot(candidate.momentum_x, candidate.momentum_y);
            auto manning_rate = tsunami::core::Real{0.0};
            const auto coriolis_rate = is_dry(candidate, policy) || r == 0.0 ? 0.0 : std::abs(f);
            if (is_dry(candidate, policy)) {
                candidate = ConservedVariables2D{};
            } else if (r > 0.0) {
                const auto depth_power = std::pow(candidate.depth, 7.0 / 3.0);
                if (!finite(depth_power) || depth_power <= 0.0) {
                    return tsunami::core::failure(update_error(
                        "r2d.source_update.depth_power_invalid",
                        "Manning depth power must be finite and positive for wet cells",
                        "apply_regional_local_sources",
                        &mesh,
                        cell_id));
                }
                const auto k = policy.gravity * n * n / depth_power;
                manning_rate = k * r;
                const auto denominator_increment = k * r * substep;
                const auto denominator = 1.0 + denominator_increment;
                if (!finite(k) || k < 0.0 || !finite(denominator_increment) || denominator_increment < 0.0 ||
                    !finite(denominator) || denominator < 1.0) {
                    return tsunami::core::failure(update_error(
                        "r2d.source_update.damping_denominator_invalid",
                        "Manning damping denominator is invalid",
                        "apply_regional_local_sources",
                        &mesh,
                        cell_id,
                        substep));
                }
                const auto gamma = 1.0 / denominator;
                const auto theta = f * substep;
                if (!finite(gamma) || gamma <= 0.0 || gamma > 1.0 || !finite(theta)) {
                    return tsunami::core::failure(update_error(
                        "r2d.source_update.rotation_invalid",
                        "source update scale or rotation is invalid",
                        "apply_regional_local_sources",
                        &mesh,
                        cell_id,
                        substep));
                }
                const auto cosine = std::cos(theta);
                const auto sine = std::sin(theta);
                const auto qx = gamma * ((cosine * candidate.momentum_x) + (sine * candidate.momentum_y));
                const auto qy = gamma * ((-sine * candidate.momentum_x) + (cosine * candidate.momentum_y));
                if (!finite(qx) || !finite(qy)) {
                    return tsunami::core::failure(update_error(
                        "r2d.source_update.candidate_nonfinite",
                        "source update candidate momentum must be finite",
                        "apply_regional_local_sources",
                        &mesh,
                        cell_id,
                        substep));
                }
                candidate.momentum_x = qx;
                candidate.momentum_y = qy;
            }

            auto accepted = validate_and_canonicalise_state(candidate, policy, cell_id);
            if (!accepted) {
                return tsunami::core::failure(update_error(
                    "r2d.source_update.candidate_nonfinite",
                    "source update candidate is not admissible",
                    "apply_regional_local_sources",
                    &mesh,
                    cell_id)
                        .with_cause_code(accepted.error().code()));
            }
            candidate = accepted.value();
            auto final_energy = cell_kinetic_energy(mesh, cell_id, candidate, policy);
            if (!final_energy) {
                return tsunami::core::failure(final_energy.error());
            }

            workspace.candidate_state().set_local_state(cell_id, candidate);
            workspace.manning_rates()[index] = manning_rate;
            workspace.coriolis_rates()[index] = coriolis_rate;
            if (manning_rate > 0.0) {
                ++next_diagnostics.manning_active_cell_count;
            }
            if (coriolis_rate > 0.0) {
                ++next_diagnostics.coriolis_active_cell_count;
            }
            choose_limiting_cell(next_diagnostics.manning_limiting_cell, next_diagnostics.maximum_manning_rate, manning_rate, cell_id);
            choose_limiting_cell(next_diagnostics.coriolis_limiting_cell, next_diagnostics.maximum_coriolis_rate, coriolis_rate, cell_id);
            next_diagnostics.momentum_x_change += candidate.momentum_x - state.momentum_x;
            next_diagnostics.momentum_y_change += candidate.momentum_y - state.momentum_y;
            next_diagnostics.final_kinetic_energy += final_energy.value();
            next_diagnostics.friction_kinetic_energy_removed += std::max(initial_energy.value() - final_energy.value(), 0.0);
        }

        auto copy = destination.copy_values_from(workspace.candidate_state());
        if (!copy) {
            return tsunami::core::failure(copy.error());
        }
        next_diagnostics.coriolis_kinetic_energy_error =
            sources.has_manning() ? 0.0 : next_diagnostics.final_kinetic_energy - next_diagnostics.initial_kinetic_energy;
        diagnostics = next_diagnostics;
        return tsunami::core::success();
    }

    auto combine_source_diagnostics(
        RegionalSourceUpdateDiagnostics first,
        RegionalSourceUpdateDiagnostics second) -> RegionalSourceUpdateDiagnostics
    {
        auto combined = RegionalSourceUpdateDiagnostics{};
        combined.manning_active_cell_count = first.manning_active_cell_count + second.manning_active_cell_count;
        combined.coriolis_active_cell_count = first.coriolis_active_cell_count + second.coriolis_active_cell_count;
        combined.maximum_manning_coefficient = std::max(first.maximum_manning_coefficient, second.maximum_manning_coefficient);
        combined.maximum_coriolis_magnitude = std::max(first.maximum_coriolis_magnitude, second.maximum_coriolis_magnitude);
        combined.maximum_manning_rate = std::max(first.maximum_manning_rate, second.maximum_manning_rate);
        combined.maximum_coriolis_rate = std::max(first.maximum_coriolis_rate, second.maximum_coriolis_rate);
        combined.manning_limiting_cell = first.maximum_manning_rate >= second.maximum_manning_rate ? first.manning_limiting_cell : second.manning_limiting_cell;
        combined.coriolis_limiting_cell = first.maximum_coriolis_rate >= second.maximum_coriolis_rate ? first.coriolis_limiting_cell : second.coriolis_limiting_cell;
        combined.momentum_x_change = first.momentum_x_change + second.momentum_x_change;
        combined.momentum_y_change = first.momentum_y_change + second.momentum_y_change;
        combined.initial_kinetic_energy = first.initial_kinetic_energy + second.initial_kinetic_energy;
        combined.final_kinetic_energy = first.final_kinetic_energy + second.final_kinetic_energy;
        combined.friction_kinetic_energy_removed = first.friction_kinetic_energy_removed + second.friction_kinetic_energy_removed;
        combined.coriolis_kinetic_energy_error = first.coriolis_kinetic_energy_error + second.coriolis_kinetic_energy_error;
        return combined;
    }

} // namespace tsunami::r2d
