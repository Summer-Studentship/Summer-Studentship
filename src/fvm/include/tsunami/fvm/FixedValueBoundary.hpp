#pragma once

#include <utility>

#include <tsunami/fvm/BoundaryPatchField.hpp>
#include <tsunami/fvm/BoundarySpecification.hpp>
#include <tsunami/fvm/MeshField.hpp>

namespace tsunami::fvm
{

    template <SupportedFieldValue Value>
    class FixedValueBoundary
    {
    public:
        FixedValueBoundary(BoundaryDescriptor descriptor, MeshBinding binding, BoundaryPatchField<Value> prescribed_values)
            : descriptor_{std::move(descriptor)}
            , binding_{std::move(binding)}
            , prescribed_values_{std::move(prescribed_values)}
        {
        }

        FixedValueBoundary(const FixedValueBoundary &) = delete;
        auto operator=(const FixedValueBoundary &) -> FixedValueBoundary & = delete;
        FixedValueBoundary(FixedValueBoundary &&) noexcept = default;
        auto operator=(FixedValueBoundary &&) noexcept -> FixedValueBoundary & = default;

        [[nodiscard]] auto descriptor() const -> BoundaryDescriptor { return descriptor_; }
        [[nodiscard]] auto binding() const noexcept -> const MeshBinding & { return binding_; }
        [[nodiscard]] auto patch_id() const noexcept -> BoundaryPatchId { return descriptor_.patch_id; }
        [[nodiscard]] auto prescribed_values() const noexcept -> const BoundaryPatchField<Value> & { return prescribed_values_; }

        [[nodiscard]] auto clone() const -> FixedValueBoundary
        {
            return FixedValueBoundary{descriptor_, binding_, prescribed_values_.clone()};
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

        auto apply(const MeshField<Value, FieldLocation::cell> &internal, BoundaryPatchField<Value> &target) const
            -> tsunami::core::Result<void>
        {
            if (internal.binding() != binding_) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.internal_field_incompatible",
                    "internal cell field mesh binding is incompatible",
                    &descriptor_,
                    "apply"));
            }
            auto validation = validate_target(target);
            if (!validation) {
                return validation;
            }
            auto copy = target.copy_values_from(prescribed_values_);
            if (!copy) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.target_field_incompatible",
                    "target patch field rejected prescribed values",
                    &descriptor_,
                    "apply"));
            }
            return tsunami::core::success();
        }

    private:
        BoundaryDescriptor descriptor_;
        MeshBinding binding_;
        BoundaryPatchField<Value> prescribed_values_;
    };

} // namespace tsunami::fvm
