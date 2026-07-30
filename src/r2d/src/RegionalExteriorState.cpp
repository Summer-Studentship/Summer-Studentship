#include <tsunami/r2d/RegionalExteriorState.hpp>

#include <cmath>
#include <string>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto depth_unit = "m";
        constexpr auto momentum_unit = "m2/s";
        constexpr auto bed_unit = "m";

        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto make_patch_fields(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string_view namespace_id,
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
                    tsunami::fvm::FieldId{"regional.exterior." + std::string{namespace_id} + "." + std::string{component} + "." + std::to_string(index)},
                    "regional exterior " + std::string{component},
                    std::string{unit},
                    0.0);
                if (!patch) {
                    return tsunami::core::failure<std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>>>(patch.error());
                }
                patches.push_back(std::move(patch).value());
            }
            return tsunami::core::success(std::move(patches));
        }

        [[nodiscard]] auto copy_patches(
            std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> destination,
            std::span<const tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> source) -> tsunami::core::Result<void>
        {
            for (std::size_t index = 0; index < destination.size(); ++index) {
                auto copied = destination[index].copy_values_from(source[index]);
                if (!copied) {
                    return tsunami::core::failure(copied.error());
                }
            }
            return tsunami::core::success();
        }
    } // namespace

    RegionalExteriorStateWorkspace::RegionalExteriorStateWorkspace(
        tsunami::fvm::MeshBinding binding,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> depth_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_x_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_y_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> bed_elevation_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_depth_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_momentum_x_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_momentum_y_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_bed_elevation_patches)
        : binding_{std::move(binding)}
        , depth_patches_{std::move(depth_patches)}
        , momentum_x_patches_{std::move(momentum_x_patches)}
        , momentum_y_patches_{std::move(momentum_y_patches)}
        , bed_elevation_patches_{std::move(bed_elevation_patches)}
        , staged_depth_patches_{std::move(staged_depth_patches)}
        , staged_momentum_x_patches_{std::move(staged_momentum_x_patches)}
        , staged_momentum_y_patches_{std::move(staged_momentum_y_patches)}
        , staged_bed_elevation_patches_{std::move(staged_bed_elevation_patches)}
    {
    }

    auto RegionalExteriorStateWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        const auto patch_count = mesh.summary().boundary_patch_count;
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               depth_patches_.size() == patch_count &&
               momentum_x_patches_.size() == patch_count &&
               momentum_y_patches_.size() == patch_count &&
               bed_elevation_patches_.size() == patch_count &&
               staged_depth_patches_.size() == patch_count &&
               staged_momentum_x_patches_.size() == patch_count &&
               staged_momentum_y_patches_.size() == patch_count &&
               staged_bed_elevation_patches_.size() == patch_count;
    }

    auto make_regional_exterior_state_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<RegionalExteriorStateWorkspace>
    {
        auto depth = make_patch_fields(mesh, "published", "depth", depth_unit);
        auto momentum_x = make_patch_fields(mesh, "published", "momentum_x", momentum_unit);
        auto momentum_y = make_patch_fields(mesh, "published", "momentum_y", momentum_unit);
        auto bed = make_patch_fields(mesh, "published", "bed_elevation", bed_unit);
        auto staged_depth = make_patch_fields(mesh, "staged", "depth", depth_unit);
        auto staged_momentum_x = make_patch_fields(mesh, "staged", "momentum_x", momentum_unit);
        auto staged_momentum_y = make_patch_fields(mesh, "staged", "momentum_y", momentum_unit);
        auto staged_bed = make_patch_fields(mesh, "staged", "bed_elevation", bed_unit);
        if (!depth) {
            return tsunami::core::failure<RegionalExteriorStateWorkspace>(depth.error());
        }
        if (!momentum_x) {
            return tsunami::core::failure<RegionalExteriorStateWorkspace>(momentum_x.error());
        }
        if (!momentum_y) {
            return tsunami::core::failure<RegionalExteriorStateWorkspace>(momentum_y.error());
        }
        if (!bed) {
            return tsunami::core::failure<RegionalExteriorStateWorkspace>(bed.error());
        }
        if (!staged_depth) {
            return tsunami::core::failure<RegionalExteriorStateWorkspace>(staged_depth.error());
        }
        if (!staged_momentum_x) {
            return tsunami::core::failure<RegionalExteriorStateWorkspace>(staged_momentum_x.error());
        }
        if (!staged_momentum_y) {
            return tsunami::core::failure<RegionalExteriorStateWorkspace>(staged_momentum_y.error());
        }
        if (!staged_bed) {
            return tsunami::core::failure<RegionalExteriorStateWorkspace>(staged_bed.error());
        }
        return tsunami::core::success(RegionalExteriorStateWorkspace{
            tsunami::fvm::make_mesh_binding(mesh),
            std::move(depth).value(),
            std::move(momentum_x).value(),
            std::move(momentum_y).value(),
            std::move(bed).value(),
            std::move(staged_depth).value(),
            std::move(staged_momentum_x).value(),
            std::move(staged_momentum_y).value(),
            std::move(staged_bed).value()});
    }

    auto populate_regional_exterior_states(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        const RegionalBoundaryConditionSet &boundaries,
        const ShallowWaterStatePolicy &policy,
        tsunami::core::Time time,
        RegionalExteriorStateWorkspace &workspace) -> tsunami::core::Result<void>
    {
        const auto mesh_id = mesh.summary().id;
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure(policy_validation.error());
        }
        if (!finite(time) || !state.is_bound_to(mesh) || !bathymetry.is_bound_to(mesh) ||
            !boundaries.is_complete_for(mesh) || !workspace.is_bound_to(mesh)) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.boundary.populate_incompatible",
                "regional exterior-state population inputs are incompatible",
                "populate_regional_exterior_states",
                "SWE-R2D-BC",
                &mesh_id));
        }
        for (const auto &condition : boundaries.conditions()) {
            const auto result = std::visit(
                [&](const auto &operation) -> tsunami::core::Result<void> {
                    const auto patch_id = operation.patch_id();
                    if constexpr (std::is_same_v<std::decay_t<decltype(operation)>, RegionalComponentwiseBoundary>) {
                        return operation.populate(
                            mesh,
                            state,
                            bathymetry,
                            workspace.staged_depth_patches_[patch_id.value],
                            workspace.staged_momentum_x_patches_[patch_id.value],
                            workspace.staged_momentum_y_patches_[patch_id.value],
                            workspace.staged_bed_elevation_patches_[patch_id.value]);
                    } else if constexpr (std::is_same_v<std::decay_t<decltype(operation)>, RegionalTransmissiveBoundary>) {
                        return operation.populate(
                            mesh,
                            state,
                            bathymetry,
                            workspace.staged_depth_patches_[patch_id.value],
                            workspace.staged_momentum_x_patches_[patch_id.value],
                            workspace.staged_momentum_y_patches_[patch_id.value],
                            workspace.staged_bed_elevation_patches_[patch_id.value]);
                    } else {
                        return operation.populate(
                            mesh,
                            state,
                            bathymetry,
                            policy,
                            time,
                            workspace.staged_depth_patches_[patch_id.value],
                            workspace.staged_momentum_x_patches_[patch_id.value],
                            workspace.staged_momentum_y_patches_[patch_id.value],
                            workspace.staged_bed_elevation_patches_[patch_id.value]);
                    }
                },
                condition);
            if (!result) {
                return tsunami::core::failure(result.error());
            }
        }
        auto depth_copy = copy_patches(workspace.depth_patches_, workspace.staged_depth_patches_);
        if (!depth_copy) {
            return tsunami::core::failure(depth_copy.error());
        }
        auto qx_copy = copy_patches(workspace.momentum_x_patches_, workspace.staged_momentum_x_patches_);
        if (!qx_copy) {
            return tsunami::core::failure(qx_copy.error());
        }
        auto qy_copy = copy_patches(workspace.momentum_y_patches_, workspace.staged_momentum_y_patches_);
        if (!qy_copy) {
            return tsunami::core::failure(qy_copy.error());
        }
        return copy_patches(workspace.bed_elevation_patches_, workspace.staged_bed_elevation_patches_);
    }

} // namespace tsunami::r2d
