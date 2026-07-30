#include <tsunami/r2d/RegionalRelaxationZone.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto rate_unit = "1/s";

        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto relaxation_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh *mesh = nullptr,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt,
            std::optional<tsunami::fvm::BoundaryPatchId> patch_id = std::nullopt,
            std::optional<tsunami::core::Real> timestep = std::nullopt) -> tsunami::core::Error
        {
            const auto mesh_id = mesh ? mesh->summary().id : tsunami::fvm::MeshId{};
            return detail::r2d_error(
                std::move(code),
                std::move(message),
                std::move(operation),
                "SWE-R2D-BC",
                mesh ? &mesh_id : nullptr,
                cell_id,
                std::nullopt,
                patch_id,
                {},
                {},
                {},
                std::nullopt,
                std::nullopt,
                std::nullopt,
                timestep);
        }

        [[nodiscard]] auto distance(
            tsunami::fvm::Point3 left,
            tsunami::fvm::Point3 right) -> tsunami::core::Real
        {
            const auto dx = left.x - right.x;
            const auto dy = left.y - right.y;
            return std::sqrt((dx * dx) + (dy * dy));
        }

        [[nodiscard]] auto minimum_patch_distance(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::fvm::BoundaryPatchRecord &patch,
            tsunami::fvm::CellId cell_id) -> tsunami::core::Real
        {
            const auto centroid = mesh.cell_geometry(cell_id).centroid;
            auto best = std::numeric_limits<tsunami::core::Real>::infinity();
            for (const auto face_id : patch.faces) {
                best = std::min(best, distance(centroid, mesh.face_geometry(face_id).centroid));
            }
            return best;
        }
    } // namespace

    RegionalRelaxationZone::RegionalRelaxationZone(
        tsunami::fvm::BoundaryPatchId patch_id,
        RegionalFarFieldState reference_state,
        tsunami::fvm::CellScalarField damping_rate)
        : patch_id_{patch_id}
        , reference_state_{reference_state}
        , damping_rate_{std::move(damping_rate)}
    {
    }

    auto RegionalRelaxationZone::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return patch_id_.value < mesh.summary().boundary_patch_count &&
               damping_rate_.is_bound_to(mesh) &&
               damping_rate_.size() == mesh.summary().cell_count &&
               damping_rate_.descriptor().unit_id == rate_unit;
    }

    auto RegionalRelaxationZone::clone() const -> RegionalRelaxationZone
    {
        return RegionalRelaxationZone{patch_id_, reference_state_, damping_rate_.clone()};
    }

    RegionalRelaxationZoneSet::RegionalRelaxationZoneSet(
        tsunami::fvm::MeshBinding binding,
        std::vector<RegionalRelaxationZone> zones)
        : binding_{std::move(binding)}
        , zones_{std::move(zones)}
    {
    }

    auto RegionalRelaxationZoneSet::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               std::ranges::all_of(zones_, [&mesh](const auto &zone) { return zone.is_bound_to(mesh); });
    }

    auto RegionalRelaxationZoneSet::clone() const -> RegionalRelaxationZoneSet
    {
        std::vector<RegionalRelaxationZone> copies;
        copies.reserve(zones_.size());
        for (const auto &zone : zones_) {
            copies.push_back(zone.clone());
        }
        return RegionalRelaxationZoneSet{binding_, std::move(copies)};
    }

    auto make_regional_relaxation_zone_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::vector<PatchRelaxationZoneSpecification> specifications) -> tsunami::core::Result<RegionalRelaxationZoneSet>
    {
        std::map<std::string, tsunami::fvm::BoundaryPatchId, std::less<>> patches_by_name;
        for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
            patches_by_name.emplace(mesh.boundary_patch(patch_id).name, patch_id);
        }
        std::set<std::string, std::less<>> seen_tags;
        std::vector<RegionalRelaxationZone> zones;
        zones.reserve(specifications.size());
        for (const auto &specification : specifications) {
            if (specification.patch_tag.empty() || !finite(specification.width) || specification.width <= 0.0 ||
                !finite(specification.maximum_rate) || specification.maximum_rate < 0.0 ||
                !finite(specification.profile_exponent) || specification.profile_exponent <= 0.0) {
                return tsunami::core::failure<RegionalRelaxationZoneSet>(relaxation_error(
                    "r2d.relaxation.specification_invalid",
                    "patch relaxation specification is invalid",
                    "make_regional_relaxation_zone_set",
                    &mesh));
            }
            if (!seen_tags.insert(specification.patch_tag).second) {
                return tsunami::core::failure<RegionalRelaxationZoneSet>(relaxation_error(
                    "r2d.relaxation.patch_duplicate",
                    "patch relaxation specifications must be unique by patch",
                    "make_regional_relaxation_zone_set",
                    &mesh));
            }
            const auto patch = patches_by_name.find(specification.patch_tag);
            if (patch == patches_by_name.end()) {
                return tsunami::core::failure<RegionalRelaxationZoneSet>(relaxation_error(
                    "r2d.relaxation.patch_unknown",
                    "patch relaxation specification references an unknown patch",
                    "make_regional_relaxation_zone_set",
                    &mesh));
            }
            auto reference_validation = validate_regional_far_field_state(specification.reference_state);
            if (!reference_validation) {
                return tsunami::core::failure<RegionalRelaxationZoneSet>(reference_validation.error());
            }

            std::vector<tsunami::core::Real> rates(mesh.summary().cell_count, 0.0);
            const auto &patch_record = mesh.boundary_patch(patch->second);
            auto has_active_cell = false;
            for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
                const auto cell_id = tsunami::fvm::CellId{index};
                const auto d = minimum_patch_distance(mesh, patch_record, cell_id);
                if (!finite(d)) {
                    return tsunami::core::failure<RegionalRelaxationZoneSet>(relaxation_error(
                        "r2d.relaxation.mesh_incompatible",
                        "cell-to-patch distance must be finite",
                        "make_regional_relaxation_zone_set",
                        &mesh,
                        cell_id,
                        patch->second));
                }
                if (d >= specification.width || specification.maximum_rate == 0.0) {
                    continue;
                }
                const auto profile = std::pow(std::clamp(1.0 - (d / specification.width), 0.0, 1.0), specification.profile_exponent);
                rates[index] = specification.maximum_rate * profile;
                has_active_cell = has_active_cell || rates[index] > 0.0;
            }
            if (specification.maximum_rate > 0.0 && !has_active_cell) {
                return tsunami::core::failure<RegionalRelaxationZoneSet>(relaxation_error(
                    "r2d.relaxation.zone_empty",
                    "patch relaxation zone does not include any cells",
                    "make_regional_relaxation_zone_set",
                    &mesh,
                    std::nullopt,
                    patch->second));
            }
            auto field = tsunami::fvm::make_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
                mesh,
                tsunami::fvm::FieldId{"regional.relaxation.rate." + std::to_string(patch->second.value)},
                "regional relaxation damping rate",
                rate_unit,
                std::move(rates));
            if (!field) {
                return tsunami::core::failure<RegionalRelaxationZoneSet>(field.error());
            }
            zones.push_back(RegionalRelaxationZone{patch->second, specification.reference_state, std::move(field).value()});
        }
        return tsunami::core::success(RegionalRelaxationZoneSet{tsunami::fvm::make_mesh_binding(mesh), std::move(zones)});
    }

    auto apply_regional_relaxation_source(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        const RegionalRelaxationZoneSet &zones,
        const ShallowWaterStatePolicy &policy,
        RegionalResidual &residual,
        tsunami::fvm::CellScalarField &outgoing_mass_rate,
        RegionalRelaxationDiagnostics &diagnostics) -> tsunami::core::Result<void>
    {
        diagnostics = RegionalRelaxationDiagnostics{.zone_count = zones.size()};
        if (!zones.is_bound_to(mesh) || !state.is_bound_to(mesh) || !bathymetry.is_bound_to(mesh) ||
            !residual.is_bound_to(mesh) || !outgoing_mass_rate.is_bound_to(mesh)) {
            return tsunami::core::failure(relaxation_error(
                "r2d.relaxation.source_incompatible",
                "regional relaxation source inputs are incompatible",
                "apply_regional_relaxation_source",
                &mesh));
        }
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure(policy_validation.error());
        }
        std::vector<bool> active(mesh.summary().cell_count, false);
        for (const auto &zone : zones.zones()) {
            for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
                const auto rate = zone.damping_rate().at(index);
                if (!finite(rate) || rate < 0.0) {
                    return tsunami::core::failure(relaxation_error(
                        "r2d.relaxation.rate_invalid",
                        "regional relaxation damping rate must be finite and nonnegative",
                        "apply_regional_relaxation_source",
                        &mesh,
                        tsunami::fvm::CellId{index},
                        zone.patch_id()));
                }
                if (rate == 0.0) {
                    continue;
                }
                const auto cell_id = tsunami::fvm::CellId{index};
                const auto area = mesh.cell_geometry(cell_id).measure;
                const auto current = state.local_state(cell_id);
                auto reference = regional_reference_conserved_state(zone.reference_state(), bathymetry.local_bed_elevation(cell_id), policy, cell_id);
                if (!reference || !finite(area) || area <= 0.0) {
                    return tsunami::core::failure(relaxation_error(
                        "r2d.relaxation.source_incompatible",
                        "regional relaxation source state or cell area is invalid",
                        "apply_regional_relaxation_source",
                        &mesh,
                        cell_id,
                        zone.patch_id()));
                }
                const auto scale = area * rate;
                residual.mass().at(index) += scale * (current.depth - reference.value().depth);
                residual.momentum_x().at(index) += scale * (current.momentum_x - reference.value().momentum_x);
                residual.momentum_y().at(index) += scale * (current.momentum_y - reference.value().momentum_y);
                const auto outgoing = area * std::max(rate * (current.depth - reference.value().depth), 0.0);
                outgoing_mass_rate.at(index) += outgoing;
                diagnostics.integrated_mass_source_rate += scale * (reference.value().depth - current.depth);
                diagnostics.outgoing_mass_rate += outgoing;
                diagnostics.maximum_rate = std::max(diagnostics.maximum_rate, rate);
                active[index] = true;
            }
        }
        diagnostics.active_cell_count = static_cast<std::size_t>(std::ranges::count(active, true));
        return tsunami::core::success();
    }

    auto estimate_relaxation_timestep(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalRelaxationZoneSet &zones,
        tsunami::core::Real safety_factor) -> tsunami::core::Result<RelaxationTimestepEstimate>
    {
        if (!finite(safety_factor) || safety_factor <= 0.0 || safety_factor > 1.0 || !zones.is_bound_to(mesh)) {
            return tsunami::core::failure<RelaxationTimestepEstimate>(relaxation_error(
                "r2d.relaxation.timestep_invalid",
                "relaxation timestep inputs are invalid",
                "estimate_relaxation_timestep",
                &mesh,
                std::nullopt,
                std::nullopt,
                safety_factor));
        }
        RelaxationTimestepEstimate estimate;
        std::vector<tsunami::core::Real> aggregate(mesh.summary().cell_count, 0.0);
        for (const auto &zone : zones.zones()) {
            for (std::size_t index = 0; index < aggregate.size(); ++index) {
                aggregate[index] += zone.damping_rate().at(index);
            }
        }
        for (std::size_t index = 0; index < aggregate.size(); ++index) {
            const auto rate = aggregate[index];
            if (!finite(rate) || rate < 0.0) {
                return tsunami::core::failure<RelaxationTimestepEstimate>(relaxation_error(
                    "r2d.relaxation.rate_invalid",
                    "aggregate relaxation rate must be finite and nonnegative",
                    "estimate_relaxation_timestep",
                    &mesh,
                    tsunami::fvm::CellId{index}));
            }
            if (rate == 0.0) {
                continue;
            }
            const auto candidate = safety_factor / rate;
            estimate.maximum_rate = std::max(estimate.maximum_rate, rate);
            if (!estimate.stable_timestep || candidate < *estimate.stable_timestep) {
                estimate.stable_timestep = candidate;
                estimate.limiting_cell = tsunami::fvm::CellId{index};
            }
        }
        return tsunami::core::success(estimate);
    }

} // namespace tsunami::r2d
