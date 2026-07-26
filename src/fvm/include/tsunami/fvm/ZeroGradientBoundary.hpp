#pragma once

#include <utility>
#include <vector>

#include <tsunami/fvm/BoundaryPatchField.hpp>
#include <tsunami/fvm/BoundarySpecification.hpp>
#include <tsunami/fvm/MeshField.hpp>

namespace tsunami::fvm
{

    template <SupportedFieldValue Value>
    class ZeroGradientBoundary
    {
    public:
        ZeroGradientBoundary(BoundaryDescriptor descriptor, MeshBinding binding)
            : descriptor_{std::move(descriptor)}
            , binding_{std::move(binding)}
        {
        }

        ZeroGradientBoundary(const ZeroGradientBoundary &) = delete;
        auto operator=(const ZeroGradientBoundary &) -> ZeroGradientBoundary & = delete;
        ZeroGradientBoundary(ZeroGradientBoundary &&) noexcept = default;
        auto operator=(ZeroGradientBoundary &&) noexcept -> ZeroGradientBoundary & = default;

        [[nodiscard]] auto descriptor() const -> BoundaryDescriptor { return descriptor_; }
        [[nodiscard]] auto binding() const noexcept -> const MeshBinding & { return binding_; }
        [[nodiscard]] auto patch_id() const noexcept -> BoundaryPatchId { return descriptor_.patch_id; }

        [[nodiscard]] auto clone() const -> ZeroGradientBoundary
        {
            return ZeroGradientBoundary{descriptor_, binding_};
        }

        [[nodiscard]] auto is_bound_to(const FiniteVolumeMesh &mesh) const -> bool
        {
            return binding_ == make_mesh_binding(mesh);
        }

        [[nodiscard]] auto validate_target(const BoundaryPatchField<Value> &target) const -> tsunami::core::Result<void>
        {
            if (binding_ != target.binding()) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.mesh_incompatible",
                    "target patch field mesh binding is incompatible",
                    &descriptor_,
                    "validate_target"));
            }
            if (target.patch_id() != descriptor_.patch_id) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.patch_incompatible",
                    "target patch field uses a different patch",
                    &descriptor_,
                    "validate_target"));
            }
            if (!boundary_detail::patch_field_descriptor_matches(descriptor_, target.descriptor())) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.target_field_incompatible",
                    "target patch field descriptor is incompatible",
                    &descriptor_,
                    "validate_target",
                    {},
                    descriptor_.entity_count,
                    target.size()));
            }
            if (target.descriptor().unit_id != descriptor_.unit_id) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.unit_incompatible",
                    "target patch field unit is incompatible",
                    &descriptor_,
                    "validate_target"));
            }
            return tsunami::core::success();
        }

        auto apply(
            const FiniteVolumeMesh &mesh,
            const MeshField<Value, FieldLocation::cell> &internal,
            BoundaryPatchField<Value> &target) const -> tsunami::core::Result<void>
        {
            if (!is_bound_to(mesh)) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.mesh_incompatible",
                    "mesh is incompatible with the boundary condition",
                    &descriptor_,
                    "apply"));
            }
            if (internal.binding() != binding_) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.internal_field_incompatible",
                    "internal cell field mesh binding is incompatible",
                    &descriptor_,
                    "apply"));
            }
            if (internal.descriptor().unit_id != descriptor_.unit_id) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.unit_incompatible",
                    "internal cell field unit is incompatible",
                    &descriptor_,
                    "apply"));
            }
            auto validation = validate_target(target);
            if (!validation) {
                return validation;
            }

            const auto &patch = mesh.boundary_patch(descriptor_.patch_id);
            if (patch.faces.size() != descriptor_.entity_count || target.size() != patch.faces.size()) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.patch_entity_count_mismatch",
                    "patch face count does not match boundary descriptor",
                    &descriptor_,
                    "apply",
                    {},
                    descriptor_.entity_count,
                    patch.faces.size()));
            }

            std::vector<Value> values;
            values.reserve(patch.faces.size());
            for (const auto face_id : patch.faces) {
                const auto owner = mesh.face(face_id).owner;
                if (owner.value >= internal.size()) {
                    return tsunami::core::failure(boundary_detail::boundary_error(
                        "fvm.boundary.internal_field_incompatible",
                        "patch face owner is outside the internal field",
                        &descriptor_,
                        "apply",
                        {},
                        internal.size(),
                        owner.value + 1U));
                }
                values.push_back(internal.at(owner.value));
            }

            for (std::size_t index = 0; index < values.size(); ++index) {
                target.at(index) = values[index];
            }
            return tsunami::core::success();
        }

    private:
        BoundaryDescriptor descriptor_;
        MeshBinding binding_;
    };

} // namespace tsunami::fvm
