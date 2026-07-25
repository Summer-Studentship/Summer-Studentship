#pragma once

#include <algorithm>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <tsunami/fvm/MeshField.hpp>

namespace tsunami::fvm
{
    template <SupportedFieldValue Value>
    class BoundaryPatchField;

    template <SupportedFieldValue Value>
    [[nodiscard]] auto make_boundary_patch_field(
        const FiniteVolumeMesh &mesh,
        BoundaryPatchId patch_id,
        FieldId id,
        std::string name,
        std::string unit_id,
        std::vector<Value> values) -> tsunami::core::Result<BoundaryPatchField<Value>>;

    template <SupportedFieldValue Value>
    class BoundaryPatchField final : public IFieldView
    {
    public:
        BoundaryPatchField(const BoundaryPatchField &) = delete;
        auto operator=(const BoundaryPatchField &) -> BoundaryPatchField & = delete;
        BoundaryPatchField(BoundaryPatchField &&) noexcept = default;
        auto operator=(BoundaryPatchField &&) noexcept -> BoundaryPatchField & = default;
        ~BoundaryPatchField() override = default;

        [[nodiscard]] auto descriptor() const -> FieldDescriptor override { return descriptor_; }
        [[nodiscard]] auto binding() const noexcept -> const MeshBinding & { return binding_; }
        [[nodiscard]] auto patch_id() const noexcept -> BoundaryPatchId { return patch_id_; }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return values_.size(); }
        [[nodiscard]] auto empty() const noexcept -> bool { return values_.empty(); }
        [[nodiscard]] auto values() noexcept -> std::span<Value> { return values_; }
        [[nodiscard]] auto values() const noexcept -> std::span<const Value> { return values_; }
        [[nodiscard]] auto at(std::size_t local_index) -> Value & { return values_.at(local_index); }
        [[nodiscard]] auto at(std::size_t local_index) const -> const Value & { return values_.at(local_index); }

        auto fill(const Value &value) -> void
        {
            std::ranges::fill(values_, value);
        }

        [[nodiscard]] auto clone() const -> BoundaryPatchField
        {
            return BoundaryPatchField{descriptor_, binding_, patch_id_, values_};
        }

        [[nodiscard]] auto is_bound_to(const FiniteVolumeMesh &mesh) const -> bool
        {
            return binding_ == make_mesh_binding(mesh);
        }

        [[nodiscard]] auto require_compatible_mesh(const FiniteVolumeMesh &mesh) const -> tsunami::core::Result<void>
        {
            if (!is_bound_to(mesh)) {
                return tsunami::core::failure(field_detail::field_error(
                    "fvm.field.mesh_incompatible",
                    "field is not bound to the supplied mesh",
                    &descriptor_,
                    "require_compatible_mesh",
                    std::nullopt,
                    std::nullopt,
                    patch_id_));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto is_layout_compatible_with(const BoundaryPatchField &other) const -> bool
        {
            return binding_ == other.binding_ &&
                   patch_id_ == other.patch_id_ &&
                   field_detail::layout_compatible(descriptor_, other.descriptor_);
        }

        auto copy_values_from(const BoundaryPatchField &source) -> tsunami::core::Result<void>
        {
            if (binding_ != source.binding_) {
                return tsunami::core::failure(field_detail::field_error(
                    "fvm.field.mesh_incompatible",
                    "source patch field mesh binding is incompatible",
                    &descriptor_,
                    "copy_values_from",
                    values_.size(),
                    source.values_.size(),
                    patch_id_));
            }
            if (patch_id_ != source.patch_id_) {
                return tsunami::core::failure(field_detail::field_error(
                    "fvm.field.patch_incompatible",
                    "source patch field uses a different boundary patch",
                    &descriptor_,
                    "copy_values_from",
                    values_.size(),
                    source.values_.size(),
                    patch_id_));
            }
            if (!field_detail::layout_compatible(descriptor_, source.descriptor_)) {
                return tsunami::core::failure(field_detail::field_error(
                    "fvm.field.layout_incompatible",
                    "source patch field layout is incompatible",
                    &descriptor_,
                    "copy_values_from",
                    values_.size(),
                    source.values_.size(),
                    patch_id_));
            }
            if (descriptor_.unit_id != source.descriptor_.unit_id) {
                return tsunami::core::failure(field_detail::field_error(
                    "fvm.field.unit_incompatible",
                    "source patch field unit is incompatible",
                    &descriptor_,
                    "copy_values_from",
                    std::nullopt,
                    std::nullopt,
                    patch_id_));
            }
            std::ranges::copy(source.values_, values_.begin());
            return tsunami::core::success();
        }

    private:
        friend auto make_boundary_patch_field<Value>(
            const FiniteVolumeMesh &mesh,
            BoundaryPatchId patch_id,
            FieldId id,
            std::string name,
            std::string unit_id,
            std::vector<Value> values) -> tsunami::core::Result<BoundaryPatchField<Value>>;

        BoundaryPatchField(FieldDescriptor descriptor, MeshBinding binding, BoundaryPatchId patch_id, std::vector<Value> values)
            : descriptor_{std::move(descriptor)}
            , binding_{std::move(binding)}
            , patch_id_{patch_id}
            , values_{std::move(values)}
        {
        }

        FieldDescriptor descriptor_;
        MeshBinding binding_;
        BoundaryPatchId patch_id_;
        std::vector<Value> values_;
    };

    template <SupportedFieldValue Value>
    [[nodiscard]] auto make_boundary_patch_field(
        const FiniteVolumeMesh &mesh,
        BoundaryPatchId patch_id,
        FieldId id,
        std::string name,
        std::string unit_id,
        std::vector<Value> values) -> tsunami::core::Result<BoundaryPatchField<Value>>
    {
        const auto summary = mesh.summary();
        if (patch_id.value >= summary.boundary_patch_count) {
            auto descriptor = FieldDescriptor{
                .id = std::move(id),
                .name = std::move(name),
                .mesh_id = summary.id,
                .location = FieldLocation::boundary_patch,
                .value_kind = FieldValueTraits<Value>::kind,
                .component_count = FieldValueTraits<Value>::component_count,
                .entity_count = values.size(),
                .unit_id = std::move(unit_id),
                .boundary_patch = patch_id,
            };
            return tsunami::core::failure<BoundaryPatchField<Value>>(field_detail::field_error(
                "fvm.field.patch_id_out_of_range",
                "boundary patch id is outside the mesh patch range",
                &descriptor,
                "make_boundary_patch_field",
                summary.boundary_patch_count,
                patch_id.value,
                patch_id));
        }

        const auto binding = make_mesh_binding(mesh);
        const auto expected = mesh.boundary_patch(patch_id).faces.size();
        auto descriptor = FieldDescriptor{
            .id = std::move(id),
            .name = std::move(name),
            .mesh_id = binding.mesh_id,
            .location = FieldLocation::boundary_patch,
            .value_kind = FieldValueTraits<Value>::kind,
            .component_count = FieldValueTraits<Value>::component_count,
            .entity_count = expected,
            .unit_id = std::move(unit_id),
            .boundary_patch = patch_id,
        };
        auto validation = field_detail::validate_common_descriptor<Value>(descriptor, values.size(), "make_boundary_patch_field");
        if (!validation) {
            return tsunami::core::failure<BoundaryPatchField<Value>>(validation.error());
        }
        if (!descriptor.boundary_patch) {
            return tsunami::core::failure<BoundaryPatchField<Value>>(field_detail::field_error(
                "fvm.field.descriptor_inconsistent",
                "boundary patch field descriptor requires a patch id",
                &descriptor,
                "make_boundary_patch_field",
                expected,
                values.size(),
                patch_id));
        }
        if (values.size() != expected) {
            return tsunami::core::failure<BoundaryPatchField<Value>>(field_detail::field_error(
                "fvm.field.patch_entity_count_mismatch",
                "patch field value count does not match patch face count",
                &descriptor,
                "make_boundary_patch_field",
                expected,
                values.size(),
                patch_id));
        }
        return tsunami::core::success(BoundaryPatchField<Value>{std::move(descriptor), binding, patch_id, std::move(values)});
    }

    template <SupportedFieldValue Value>
    [[nodiscard]] auto make_filled_boundary_patch_field(
        const FiniteVolumeMesh &mesh,
        BoundaryPatchId patch_id,
        FieldId id,
        std::string name,
        std::string unit_id,
        const Value &initial_value) -> tsunami::core::Result<BoundaryPatchField<Value>>
    {
        if (patch_id.value >= mesh.summary().boundary_patch_count) {
            return make_boundary_patch_field<Value>(
                mesh,
                patch_id,
                std::move(id),
                std::move(name),
                std::move(unit_id),
                {});
        }
        return make_boundary_patch_field<Value>(
            mesh,
            patch_id,
            std::move(id),
            std::move(name),
            std::move(unit_id),
            std::vector<Value>(mesh.boundary_patch(patch_id).faces.size(), initial_value));
    }

    using PatchScalarField = BoundaryPatchField<tsunami::core::Real>;
    using PatchVectorField = BoundaryPatchField<Vector3>;

} // namespace tsunami::fvm
