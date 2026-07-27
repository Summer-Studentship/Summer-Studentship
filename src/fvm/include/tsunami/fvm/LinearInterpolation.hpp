#pragma once

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <tsunami/fvm/BoundaryConditionSet.hpp>
#include <tsunami/fvm/LinearInterpolationStencil.hpp>

namespace tsunami::fvm
{

    template <SupportedFieldValue Value>
    class LinearInterpolationWorkspace
    {
    public:
        LinearInterpolationWorkspace(const LinearInterpolationWorkspace &) = delete;
        auto operator=(const LinearInterpolationWorkspace &) -> LinearInterpolationWorkspace & = delete;
        LinearInterpolationWorkspace(LinearInterpolationWorkspace &&) noexcept = default;
        auto operator=(LinearInterpolationWorkspace &&) noexcept -> LinearInterpolationWorkspace & = default;

        [[nodiscard]] auto binding() const noexcept -> const MeshBinding & { return binding_; }
        [[nodiscard]] auto unit_id() const noexcept -> const std::string & { return unit_id_; }
        [[nodiscard]] auto staging_field() noexcept -> MeshField<Value, FieldLocation::face> & { return staging_; }
        [[nodiscard]] auto staging_field() const noexcept -> const MeshField<Value, FieldLocation::face> & { return staging_; }
        [[nodiscard]] auto patch_workspaces() noexcept -> std::span<BoundaryPatchField<Value>> { return patch_workspaces_; }
        [[nodiscard]] auto patch_workspaces() const noexcept -> std::span<const BoundaryPatchField<Value>> { return patch_workspaces_; }
        [[nodiscard]] auto coverage() noexcept -> std::span<unsigned char> { return coverage_; }
        [[nodiscard]] auto coverage() const noexcept -> std::span<const unsigned char> { return coverage_; }

        [[nodiscard]] auto is_bound_to(const FiniteVolumeMesh &mesh) const -> bool
        {
            return binding_ == make_mesh_binding(mesh);
        }

    private:
        template <SupportedFieldValue Other>
        friend auto make_linear_interpolation_workspace(
            const FiniteVolumeMesh &mesh,
            std::string unit_id) -> tsunami::core::Result<LinearInterpolationWorkspace<Other>>;

        LinearInterpolationWorkspace(
            MeshBinding binding,
            std::string unit_id,
            MeshField<Value, FieldLocation::face> staging,
            std::vector<BoundaryPatchField<Value>> patch_workspaces,
            std::vector<unsigned char> coverage)
            : binding_{std::move(binding)}
            , unit_id_{std::move(unit_id)}
            , staging_{std::move(staging)}
            , patch_workspaces_{std::move(patch_workspaces)}
            , coverage_{std::move(coverage)}
        {
        }

        MeshBinding binding_;
        std::string unit_id_;
        MeshField<Value, FieldLocation::face> staging_;
        std::vector<BoundaryPatchField<Value>> patch_workspaces_;
        std::vector<unsigned char> coverage_;
    };

    template <SupportedFieldValue Value>
    [[nodiscard]] auto make_linear_interpolation_workspace(
        const FiniteVolumeMesh &mesh,
        std::string unit_id) -> tsunami::core::Result<LinearInterpolationWorkspace<Value>>
    {
        const auto binding = make_mesh_binding(mesh);
        auto staging = make_filled_mesh_field<Value, FieldLocation::face>(
            mesh,
            FieldId{"linear-interpolation.staging"},
            "linear interpolation staging",
            unit_id,
            Value{});
        if (!staging) {
            return tsunami::core::failure<LinearInterpolationWorkspace<Value>>(staging.error());
        }

        std::vector<BoundaryPatchField<Value>> patches;
        patches.reserve(mesh.summary().boundary_patch_count);
        for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = BoundaryPatchId{index};
            auto patch = make_filled_boundary_patch_field<Value>(
                mesh,
                patch_id,
                FieldId{"linear-interpolation.patch-staging." + std::to_string(index)},
                "linear interpolation patch staging",
                unit_id,
                Value{});
            if (!patch) {
                return tsunami::core::failure<LinearInterpolationWorkspace<Value>>(patch.error());
            }
            patches.push_back(std::move(patch).value());
        }

        return tsunami::core::success(LinearInterpolationWorkspace<Value>{
            binding,
            std::move(unit_id),
            std::move(staging).value(),
            std::move(patches),
            std::vector<unsigned char>(mesh.summary().face_count, 0U)});
    }

    template <SupportedFieldValue Value>
    auto interpolate_cell_to_face(
        const FiniteVolumeMesh &mesh,
        const LinearInterpolationStencil &stencil,
        const MeshField<Value, FieldLocation::cell> &source,
        const BoundaryConditionSet<Value> &boundaries,
        MeshField<Value, FieldLocation::face> &destination,
        LinearInterpolationWorkspace<Value> &workspace) -> tsunami::core::Result<void>
    {
        const auto mesh_id = numerics_detail::mesh_id_from(mesh);
        const auto source_descriptor = source.descriptor();
        const auto destination_descriptor = destination.descriptor();
        const auto staging_descriptor = workspace.staging_field().descriptor();
        if (!stencil.is_bound_to(mesh)) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.interpolation.mesh_incompatible",
                "linear interpolation stencil is not bound to the supplied mesh",
                "interpolate_cell_to_face",
                &mesh_id));
        }
        if (source.binding() != make_mesh_binding(mesh)) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.interpolation.source_incompatible",
                "source cell field is not bound to the supplied mesh",
                "interpolate_cell_to_face",
                &mesh_id,
                &source_descriptor,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                mesh.summary().cell_count,
                source.size()));
        }
        if (destination.binding() != make_mesh_binding(mesh) || destination.size() != mesh.summary().face_count) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.interpolation.destination_incompatible",
                "destination face field is not compatible with the supplied mesh",
                "interpolate_cell_to_face",
                &mesh_id,
                &destination_descriptor,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                mesh.summary().face_count,
                destination.size()));
        }
        if (!workspace.is_bound_to(mesh) || workspace.staging_field().size() != mesh.summary().face_count ||
            workspace.patch_workspaces().size() != mesh.summary().boundary_patch_count ||
            workspace.coverage().size() != mesh.summary().face_count) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.interpolation.workspace_incompatible",
                "linear interpolation workspace is not compatible with the supplied mesh",
                "interpolate_cell_to_face",
                &mesh_id));
        }
        if (!boundaries.is_bound_to(mesh) || !boundaries.is_complete_for(mesh)) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.interpolation.boundary_set_incompatible",
                "boundary condition set is not complete for the supplied mesh",
                "interpolate_cell_to_face",
                &mesh_id,
                nullptr,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                mesh.summary().boundary_patch_count,
                boundaries.size()));
        }
        if (source_descriptor.unit_id != destination_descriptor.unit_id ||
            workspace.unit_id() != source_descriptor.unit_id ||
            staging_descriptor.unit_id != source_descriptor.unit_id) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.interpolation.unit_incompatible",
                "source, destination and workspace units must match",
                "interpolate_cell_to_face",
                &mesh_id,
                &source_descriptor));
        }

        for (std::size_t index = 0; index < workspace.patch_workspaces().size(); ++index) {
            const auto patch_id = BoundaryPatchId{index};
            const auto &patch = mesh.boundary_patch(patch_id);
            const auto &patch_workspace = workspace.patch_workspaces()[index];
            const auto patch_descriptor = patch_workspace.descriptor();
            if (patch_workspace.patch_id() != patch_id || patch_workspace.size() != patch.faces.size()) {
                return tsunami::core::failure(numerics_detail::numerics_error(
                    "fvm.numerics.interpolation.workspace_incompatible",
                    "patch workspace does not match mesh patch layout",
                    "interpolate_cell_to_face",
                    &mesh_id,
                    nullptr,
                    std::nullopt,
                    std::nullopt,
                    patch_id,
                    std::nullopt,
                    patch.faces.size(),
                    patch_workspace.size()));
            }
            if (patch_workspace.descriptor().unit_id != source.descriptor().unit_id) {
                return tsunami::core::failure(numerics_detail::numerics_error(
                    "fvm.numerics.interpolation.unit_incompatible",
                    "patch workspace unit must match source unit",
                    "interpolate_cell_to_face",
                    &mesh_id,
                    &patch_descriptor,
                    std::nullopt,
                    std::nullopt,
                    patch_id));
            }
        }

        for (const auto &condition : boundaries.conditions()) {
            const auto descriptor = condition.descriptor();
            if (descriptor.unit_id != source_descriptor.unit_id) {
                return tsunami::core::failure(numerics_detail::numerics_error(
                    "fvm.numerics.interpolation.unit_incompatible",
                    "boundary condition unit must match source unit",
                    "interpolate_cell_to_face",
                    &mesh_id,
                    nullptr,
                    std::nullopt,
                    std::nullopt,
                    descriptor.patch_id,
                    descriptor.kind));
            }
            if (!condition.is_executable()) {
                return tsunami::core::failure(numerics_detail::numerics_error(
                    "fvm.numerics.interpolation.boundary_not_executable",
                    "boundary condition is not executable",
                    "interpolate_cell_to_face",
                    &mesh_id,
                    nullptr,
                    std::nullopt,
                    std::nullopt,
                    descriptor.patch_id,
                    descriptor.kind));
            }
        }

        std::ranges::fill(workspace.coverage(), static_cast<unsigned char>(0U));

        for (const auto &entry : stencil.entries()) {
            if (entry.face.value >= workspace.staging_field().size() ||
                entry.owner.value >= source.size() ||
                entry.neighbour.value >= source.size() ||
                workspace.coverage()[entry.face.value] != 0U) {
                return tsunami::core::failure(numerics_detail::numerics_error(
                    "fvm.numerics.interpolation.face_coverage_failed",
                    "internal face coverage is inconsistent",
                    "interpolate_cell_to_face",
                    &mesh_id,
                    nullptr,
                    entry.face));
            }
            workspace.staging_field().at(entry.face.value) = numerics_detail::interpolate_value(
                source.at(entry.owner.value),
                source.at(entry.neighbour.value),
                entry.owner_weight,
                entry.neighbour_weight);
            workspace.coverage()[entry.face.value] = 1U;
        }

        for (auto &condition : boundaries.conditions()) {
            const auto descriptor = condition.descriptor();
            auto &patch_workspace = workspace.patch_workspaces()[descriptor.patch_id.value];
            auto applied = condition.apply(mesh, source, patch_workspace);
            if (!applied) {
                return tsunami::core::failure(numerics_detail::numerics_error(
                    "fvm.numerics.interpolation.boundary_not_executable",
                    "boundary condition application failed",
                    "interpolate_cell_to_face",
                    &mesh_id,
                    nullptr,
                    std::nullopt,
                    std::nullopt,
                    descriptor.patch_id,
                    descriptor.kind));
            }

            const auto &patch = mesh.boundary_patch(descriptor.patch_id);
            for (std::size_t local_index = 0; local_index < patch.faces.size(); ++local_index) {
                const auto face_id = patch.faces[local_index];
                if (face_id.value >= workspace.staging_field().size() ||
                    workspace.coverage()[face_id.value] != 0U) {
                    return tsunami::core::failure(numerics_detail::numerics_error(
                        "fvm.numerics.interpolation.face_coverage_failed",
                        "boundary face coverage is inconsistent",
                        "interpolate_cell_to_face",
                        &mesh_id,
                        nullptr,
                        face_id,
                        std::nullopt,
                        descriptor.patch_id,
                        descriptor.kind));
                }
                workspace.staging_field().at(face_id.value) = patch_workspace.at(local_index);
                workspace.coverage()[face_id.value] = 1U;
            }
        }

        const auto populated = std::ranges::count(workspace.coverage(), static_cast<unsigned char>(1U));
        if (static_cast<std::size_t>(populated) != mesh.summary().face_count) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.interpolation.face_coverage_failed",
                "not every face received exactly one interpolated value",
                "interpolate_cell_to_face",
                &mesh_id,
                nullptr,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                mesh.summary().face_count,
                static_cast<std::size_t>(populated)));
        }

        auto copy = destination.copy_values_from(workspace.staging_field());
        if (!copy) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.interpolation.destination_incompatible",
                "destination rejected the complete staged face field",
                "interpolate_cell_to_face",
                &mesh_id,
                &destination_descriptor));
        }
        return tsunami::core::success();
    }

    using ScalarLinearInterpolationWorkspace = LinearInterpolationWorkspace<tsunami::core::Real>;
    using VectorLinearInterpolationWorkspace = LinearInterpolationWorkspace<Vector3>;

} // namespace tsunami::fvm
