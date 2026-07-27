#include <tsunami/r2d/RegionalResidualEvaluation.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto depth_unit = "m";
        constexpr auto momentum_unit = "m2/s";
        constexpr auto spectral_unit = "m2/s";

        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto mesh_id(const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::fvm::MeshId
        {
            return mesh.summary().id;
        }

        auto add_flux(RegionalResidual &residual, tsunami::fvm::CellId cell_id, ShallowWaterFlux2D flux, tsunami::core::Real factor) -> void
        {
            residual.mass().at(cell_id.value) += factor * flux.mass;
            residual.momentum_x().at(cell_id.value) += factor * flux.momentum_x;
            residual.momentum_y().at(cell_id.value) += factor * flux.momentum_y;
        }

        [[nodiscard]] auto validate_boundary_set(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const ScalarBoundaryConditionSet &boundaries,
            std::string_view expected_unit,
            std::string operation) -> tsunami::core::Result<void>
        {
            const auto id = mesh_id(mesh);
            if (!boundaries.is_bound_to(mesh) || !boundaries.is_complete_for(mesh)) {
                return tsunami::core::failure(detail::r2d_error(
                    "r2d.residual.boundary_incompatible",
                    "boundary set is not complete for the supplied mesh",
                    std::move(operation),
                    "SWE-R2D-SOL",
                    &id));
            }
            for (const auto &condition : boundaries.conditions()) {
                const auto descriptor = condition.descriptor();
                if (descriptor.unit_id != expected_unit) {
                    return tsunami::core::failure(detail::r2d_error(
                        "r2d.residual.boundary_incompatible",
                        "boundary unit is incompatible",
                        std::move(operation),
                        "SWE-R2D-SOL",
                        &id,
                        std::nullopt,
                        std::nullopt,
                        descriptor.patch_id,
                        descriptor.id.value,
                        std::string{expected_unit},
                        descriptor.unit_id));
                }
                if (!condition.is_executable()) {
                    return tsunami::core::failure(detail::r2d_error(
                        "r2d.residual.boundary_not_executable",
                        "boundary condition is not executable",
                        std::move(operation),
                        "SWE-R2D-SOL",
                        &id,
                        std::nullopt,
                        std::nullopt,
                        descriptor.patch_id,
                        descriptor.id.value));
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto make_patch_fields(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string_view component,
            std::string_view unit) -> tsunami::core::Result<std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>>>
        {
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> patches;
            patches.reserve(mesh.summary().boundary_patch_count);
            for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
                const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
                auto patch = tsunami::fvm::make_filled_boundary_patch_field<tsunami::core::Real>(
                    mesh,
                    patch_id,
                    tsunami::fvm::FieldId{"regional.boundary." + std::string{component} + "." + std::to_string(index)},
                    "regional boundary " + std::string{component},
                    std::string{unit},
                    0.0);
                if (!patch) {
                    return tsunami::core::failure<std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>>>(patch.error());
                }
                patches.push_back(std::move(patch).value());
            }
            return tsunami::core::success(std::move(patches));
        }

        auto apply_boundaries(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const ScalarBoundaryConditionSet &boundaries,
            const tsunami::fvm::CellScalarField &source,
            std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> patches) -> tsunami::core::Result<void>
        {
            const auto id = mesh_id(mesh);
            for (const auto &condition : boundaries.conditions()) {
                const auto descriptor = condition.descriptor();
                auto applied = condition.apply(mesh, source, patches[descriptor.patch_id.value]);
                if (!applied) {
                    return tsunami::core::failure(detail::r2d_error(
                        "r2d.residual.boundary_not_executable",
                        "boundary condition application failed",
                        "evaluate_rusanov_residual",
                        "SWE-R2D-SOL",
                        &id,
                        std::nullopt,
                        std::nullopt,
                        descriptor.patch_id,
                        descriptor.id.value));
                }
            }
            return tsunami::core::success();
        }
    } // namespace

    RegionalResidualWorkspace::RegionalResidualWorkspace(
        tsunami::fvm::MeshBinding binding,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> depth_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_x_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_y_patches,
        RegionalResidual residual,
        tsunami::fvm::CellScalarField spectral_sum)
        : binding_{std::move(binding)}
        , depth_patches_{std::move(depth_patches)}
        , momentum_x_patches_{std::move(momentum_x_patches)}
        , momentum_y_patches_{std::move(momentum_y_patches)}
        , residual_{std::move(residual)}
        , spectral_sum_{std::move(spectral_sum)}
    {
    }

    auto RegionalResidualWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               residual_.is_bound_to(mesh) &&
               spectral_sum_.is_bound_to(mesh) &&
               depth_patches_.size() == mesh.summary().boundary_patch_count &&
               momentum_x_patches_.size() == mesh.summary().boundary_patch_count &&
               momentum_y_patches_.size() == mesh.summary().boundary_patch_count;
    }

    auto make_regional_residual_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<RegionalResidualWorkspace>
    {
        auto depth = make_patch_fields(mesh, "depth", depth_unit);
        if (!depth) {
            return tsunami::core::failure<RegionalResidualWorkspace>(depth.error());
        }
        auto momentum_x = make_patch_fields(mesh, "momentum_x", momentum_unit);
        if (!momentum_x) {
            return tsunami::core::failure<RegionalResidualWorkspace>(momentum_x.error());
        }
        auto momentum_y = make_patch_fields(mesh, "momentum_y", momentum_unit);
        if (!momentum_y) {
            return tsunami::core::failure<RegionalResidualWorkspace>(momentum_y.error());
        }
        auto residual = make_regional_residual(mesh);
        if (!residual) {
            return tsunami::core::failure<RegionalResidualWorkspace>(residual.error());
        }
        auto spectral = tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh, tsunami::fvm::FieldId{"regional.spectral_sum"}, "regional spectral sum", spectral_unit, 0.0);
        if (!spectral) {
            return tsunami::core::failure<RegionalResidualWorkspace>(spectral.error());
        }
        return tsunami::core::success(RegionalResidualWorkspace{
            tsunami::fvm::make_mesh_binding(mesh),
            std::move(depth).value(),
            std::move(momentum_x).value(),
            std::move(momentum_y).value(),
            std::move(residual).value(),
            std::move(spectral).value()});
    }

    auto evaluate_rusanov_residual(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const ScalarBoundaryConditionSet &depth_boundaries,
        const ScalarBoundaryConditionSet &momentum_x_boundaries,
        const ScalarBoundaryConditionSet &momentum_y_boundaries,
        const ShallowWaterStatePolicy &policy,
        RegionalResidual &destination_residual,
        tsunami::fvm::CellScalarField &destination_spectral_sum,
        tsunami::core::Real &destination_maximum_signal_speed,
        RegionalResidualWorkspace &workspace) -> tsunami::core::Result<void>
    {
        const auto id = mesh_id(mesh);
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure(policy_validation.error());
        }
        if (!state.is_bound_to(mesh)) {
            return tsunami::core::failure(detail::r2d_error("r2d.residual.state_incompatible", "state is not bound to the mesh", "evaluate_rusanov_residual", "SWE-R2D-SOL", &id));
        }
        if (!destination_residual.is_bound_to(mesh) || destination_residual.size() != mesh.summary().cell_count ||
            !destination_spectral_sum.is_bound_to(mesh) || destination_spectral_sum.size() != mesh.summary().cell_count ||
            destination_spectral_sum.descriptor().unit_id != spectral_unit) {
            return tsunami::core::failure(detail::r2d_error("r2d.residual.destination_incompatible", "residual or spectral destination is incompatible", "evaluate_rusanov_residual", "SWE-R2D-SOL", &id));
        }
        if (!workspace.is_bound_to(mesh)) {
            return tsunami::core::failure(detail::r2d_error("r2d.residual.workspace_incompatible", "residual workspace is incompatible", "evaluate_rusanov_residual", "SWE-R2D-SOL", &id));
        }
        for (const auto &cell : mesh.geometry().cells()) {
            if (!finite(cell.measure) || cell.measure <= 0.0) {
                return tsunami::core::failure(detail::r2d_error("r2d.residual.cell_geometry_invalid", "cell area must be finite and positive", "evaluate_rusanov_residual", "SWE-R2D-SOL", &id));
            }
        }
        for (const auto &face : mesh.geometry().faces()) {
            if (!finite(face.area_vector.x) || !finite(face.area_vector.y) || !finite(face.area_vector.z)) {
                return tsunami::core::failure(detail::r2d_error("r2d.residual.face_geometry_invalid", "face geometry must be finite", "evaluate_rusanov_residual", "SWE-R2D-SOL", &id));
            }
        }
        for (std::size_t index = 0; index < state.size(); ++index) {
            auto local = validate_and_canonicalise_state(state.local_state(tsunami::fvm::CellId{index}), policy, tsunami::fvm::CellId{index});
            if (!local) {
                return tsunami::core::failure(detail::r2d_error("r2d.residual.state_incompatible", "interior state is invalid", "evaluate_rusanov_residual", "SWE-R2D-SOL", &id, tsunami::fvm::CellId{index}).with_cause_code(local.error().code()));
            }
        }

        auto depth_boundary_validation = validate_boundary_set(mesh, depth_boundaries, depth_unit, "evaluate_rusanov_residual");
        auto momentum_x_boundary_validation = validate_boundary_set(mesh, momentum_x_boundaries, momentum_unit, "evaluate_rusanov_residual");
        auto momentum_y_boundary_validation = validate_boundary_set(mesh, momentum_y_boundaries, momentum_unit, "evaluate_rusanov_residual");
        if (!depth_boundary_validation) {
            return tsunami::core::failure(depth_boundary_validation.error());
        }
        if (!momentum_x_boundary_validation) {
            return tsunami::core::failure(momentum_x_boundary_validation.error());
        }
        if (!momentum_y_boundary_validation) {
            return tsunami::core::failure(momentum_y_boundary_validation.error());
        }

        auto depth_apply = apply_boundaries(mesh, depth_boundaries, state.depth(), workspace.depth_patches());
        if (!depth_apply) {
            return tsunami::core::failure(depth_apply.error());
        }
        auto momentum_x_apply = apply_boundaries(mesh, momentum_x_boundaries, state.momentum_x(), workspace.momentum_x_patches());
        if (!momentum_x_apply) {
            return tsunami::core::failure(momentum_x_apply.error());
        }
        auto momentum_y_apply = apply_boundaries(mesh, momentum_y_boundaries, state.momentum_y(), workspace.momentum_y_patches());
        if (!momentum_y_apply) {
            return tsunami::core::failure(momentum_y_apply.error());
        }

        workspace.residual().fill(ConservedVariables2D{});
        workspace.spectral_sum().fill(0.0);
        auto maximum_speed = tsunami::core::Real{0.0};

        for (const auto &face : mesh.topology().faces()) {
            auto normal = make_face_normal(mesh.face_geometry(face.id).area_vector, policy, face.id);
            if (!normal) {
                return tsunami::core::failure(detail::r2d_error("r2d.residual.face_geometry_invalid", "face normal is invalid", "evaluate_rusanov_residual", "SWE-R2D-SOL", &id, std::nullopt, face.id).with_cause_code(normal.error().code()));
            }
            auto left = state.local_state(face.owner);
            auto right = ConservedVariables2D{};
            if (face.neighbour) {
                right = state.local_state(*face.neighbour);
            } else {
                const auto patch_id = *face.boundary_patch;
                const auto &patch = mesh.boundary_patch(patch_id);
                const auto local_it = std::ranges::find(patch.faces, face.id);
                const auto local_index = static_cast<std::size_t>(local_it - patch.faces.begin());
                right = ConservedVariables2D{
                    .depth = workspace.depth_patches()[patch_id.value].at(local_index),
                    .momentum_x = workspace.momentum_x_patches()[patch_id.value].at(local_index),
                    .momentum_y = workspace.momentum_y_patches()[patch_id.value].at(local_index)};
                auto exterior = validate_and_canonicalise_state(right, policy, std::nullopt);
                if (!exterior) {
                    return tsunami::core::failure(detail::r2d_error("r2d.residual.boundary_state_invalid", "exterior boundary state is invalid", "evaluate_rusanov_residual", "SWE-R2D-SOL", &id, std::nullopt, face.id, patch_id).with_cause_code(exterior.error().code()));
                }
                right = exterior.value();
            }

            auto flux = rusanov_flux(left, right, normal.value(), policy);
            if (!flux) {
                return tsunami::core::failure(detail::r2d_error("r2d.residual.result_nonfinite", "Rusanov flux evaluation failed", "evaluate_rusanov_residual", "SWE-R2D-SOL", &id, std::nullopt, face.id).with_cause_code(flux.error().code()));
            }
            add_flux(workspace.residual(), face.owner, flux.value().flux, normal.value().length);
            workspace.spectral_sum().at(face.owner.value) += flux.value().maximum_signal_speed * normal.value().length;
            if (face.neighbour) {
                add_flux(workspace.residual(), *face.neighbour, flux.value().flux, -normal.value().length);
                workspace.spectral_sum().at(face.neighbour->value) += flux.value().maximum_signal_speed * normal.value().length;
            }
            maximum_speed = std::max(maximum_speed, flux.value().maximum_signal_speed);
        }

        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            if (!finite(workspace.residual().mass().at(index)) || !finite(workspace.residual().momentum_x().at(index)) ||
                !finite(workspace.residual().momentum_y().at(index)) || !finite(workspace.spectral_sum().at(index)) ||
                workspace.spectral_sum().at(index) < 0.0) {
                return tsunami::core::failure(detail::r2d_error("r2d.residual.result_nonfinite", "residual and spectral sums must be finite", "evaluate_rusanov_residual", "SWE-R2D-SOL", &id, tsunami::fvm::CellId{index}));
            }
        }

        auto residual_copy = destination_residual.copy_values_from(workspace.residual());
        if (!residual_copy) {
            return tsunami::core::failure(residual_copy.error());
        }
        auto spectral_copy = destination_spectral_sum.copy_values_from(workspace.spectral_sum());
        if (!spectral_copy) {
            return tsunami::core::failure(spectral_copy.error());
        }
        destination_maximum_signal_speed = maximum_speed;
        return tsunami::core::success();
    }

} // namespace tsunami::r2d
