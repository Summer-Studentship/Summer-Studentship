#pragma once

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <tsunami/core/Error.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/fvm/Field.hpp>
#include <tsunami/fvm/MeshBinding.hpp>

namespace tsunami::fvm
{
    template <SupportedFieldValue Value, FieldLocation Location>
        requires(Location == FieldLocation::cell || Location == FieldLocation::face)
    class MeshField;

    template <SupportedFieldValue Value, FieldLocation Location>
        requires(Location == FieldLocation::cell || Location == FieldLocation::face)
    [[nodiscard]] auto make_mesh_field(
        const FiniteVolumeMesh &mesh,
        FieldId id,
        std::string name,
        std::string unit_id,
        std::vector<Value> values) -> tsunami::core::Result<MeshField<Value, Location>>;

    namespace field_detail
    {
        inline constexpr auto rule_id = "SWE-FVM-FLD-WP1";

        [[nodiscard]] inline auto field_error(
            std::string code,
            std::string message,
            const FieldDescriptor *descriptor = nullptr,
            std::string operation = {},
            std::optional<std::size_t> expected_count = std::nullopt,
            std::optional<std::size_t> actual_count = std::nullopt,
            std::optional<BoundaryPatchId> patch_id = std::nullopt) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::numerical,
                tsunami::core::Severity::error};
            error.add_context("rule_id", rule_id).add_context("state_changed", "false");
            if (!operation.empty()) {
                error.add_context("operation", std::move(operation));
            }
            if (descriptor != nullptr) {
                error.add_context("field_id", descriptor->id.value)
                    .add_context("field_name", descriptor->name)
                    .add_context("mesh_id", descriptor->mesh_id.value)
                    .add_context("location", std::string{to_string(descriptor->location)})
                    .add_context("value_kind", std::string{to_string(descriptor->value_kind)});
                if (descriptor->boundary_patch) {
                    error.add_context("patch_id", std::to_string(descriptor->boundary_patch->value));
                }
            }
            if (expected_count) {
                error.add_context("expected_count", std::to_string(*expected_count));
            }
            if (actual_count) {
                error.add_context("actual_count", std::to_string(*actual_count));
            }
            if (patch_id) {
                error.add_context("patch_id", std::to_string(patch_id->value));
            }
            return error;
        }

        template <SupportedFieldValue Value>
        [[nodiscard]] inline auto validate_common_descriptor(
            const FieldDescriptor &descriptor,
            std::size_t value_count,
            std::string operation) -> tsunami::core::Result<void>
        {
            if (descriptor.id.value.empty()) {
                return tsunami::core::failure(field_error("fvm.field.id_required", "field id is required", &descriptor, std::move(operation)));
            }
            if (descriptor.name.empty()) {
                return tsunami::core::failure(field_error("fvm.field.name_required", "field name is required", &descriptor, std::move(operation)));
            }
            if (descriptor.mesh_id.value.empty()) {
                return tsunami::core::failure(field_error("fvm.field.mesh_id_required", "mesh id is required", &descriptor, std::move(operation)));
            }
            if (descriptor.unit_id.empty()) {
                return tsunami::core::failure(field_error("fvm.field.unit_required", "field unit id is required", &descriptor, std::move(operation)));
            }
            if (descriptor.value_kind != FieldValueTraits<Value>::kind ||
                descriptor.component_count != FieldValueTraits<Value>::component_count ||
                descriptor.entity_count == 0) {
                return tsunami::core::failure(field_error(
                    "fvm.field.descriptor_inconsistent",
                    "field descriptor does not match field value type or storage",
                    &descriptor,
                    std::move(operation),
                    descriptor.entity_count,
                    value_count));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] inline auto expected_mesh_field_count(const FiniteVolumeMesh &mesh, FieldLocation location)
            -> std::size_t
        {
            const auto summary = mesh.summary();
            return location == FieldLocation::cell ? summary.cell_count : summary.face_count;
        }

        [[nodiscard]] inline auto layout_compatible(const FieldDescriptor &left, const FieldDescriptor &right) -> bool
        {
            return left.location == right.location &&
                   left.entity_count == right.entity_count &&
                   left.value_kind == right.value_kind &&
                   left.component_count == right.component_count;
        }
    } // namespace field_detail

    template <SupportedFieldValue Value, FieldLocation Location>
        requires(Location == FieldLocation::cell || Location == FieldLocation::face)
    class MeshField final : public IFieldView
    {
    public:
        MeshField(const MeshField &) = delete;
        auto operator=(const MeshField &) -> MeshField & = delete;
        MeshField(MeshField &&) noexcept = default;
        auto operator=(MeshField &&) noexcept -> MeshField & = default;
        ~MeshField() override = default;

        [[nodiscard]] auto descriptor() const -> FieldDescriptor override { return descriptor_; }
        [[nodiscard]] auto binding() const noexcept -> const MeshBinding & { return binding_; }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return values_.size(); }
        [[nodiscard]] auto empty() const noexcept -> bool { return values_.empty(); }
        [[nodiscard]] auto values() noexcept -> std::span<Value> { return values_; }
        [[nodiscard]] auto values() const noexcept -> std::span<const Value> { return values_; }
        [[nodiscard]] auto at(std::size_t index) -> Value & { return values_.at(index); }
        [[nodiscard]] auto at(std::size_t index) const -> const Value & { return values_.at(index); }

        auto fill(const Value &value) -> void
        {
            std::ranges::fill(values_, value);
        }

        [[nodiscard]] auto clone() const -> MeshField
        {
            return MeshField{descriptor_, binding_, values_};
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
                    "require_compatible_mesh"));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto is_layout_compatible_with(const MeshField &other) const -> bool
        {
            return binding_ == other.binding_ &&
                   field_detail::layout_compatible(descriptor_, other.descriptor_);
        }

        auto copy_values_from(const MeshField &source) -> tsunami::core::Result<void>
        {
            if (binding_ != source.binding_) {
                return tsunami::core::failure(field_detail::field_error(
                    "fvm.field.mesh_incompatible",
                    "source field mesh binding is incompatible",
                    &descriptor_,
                    "copy_values_from",
                    values_.size(),
                    source.values_.size()));
            }
            if (!field_detail::layout_compatible(descriptor_, source.descriptor_)) {
                return tsunami::core::failure(field_detail::field_error(
                    "fvm.field.layout_incompatible",
                    "source field layout is incompatible",
                    &descriptor_,
                    "copy_values_from",
                    values_.size(),
                    source.values_.size()));
            }
            if (descriptor_.unit_id != source.descriptor_.unit_id) {
                return tsunami::core::failure(field_detail::field_error(
                    "fvm.field.unit_incompatible",
                    "source field unit is incompatible",
                    &descriptor_,
                    "copy_values_from"));
            }
            std::ranges::copy(source.values_, values_.begin());
            return tsunami::core::success();
        }

    private:
        friend auto make_mesh_field<Value, Location>(
            const FiniteVolumeMesh &mesh,
            FieldId id,
            std::string name,
            std::string unit_id,
            std::vector<Value> values) -> tsunami::core::Result<MeshField<Value, Location>>;

        MeshField(FieldDescriptor descriptor, MeshBinding binding, std::vector<Value> values)
            : descriptor_{std::move(descriptor)}
            , binding_{std::move(binding)}
            , values_{std::move(values)}
        {
        }

        FieldDescriptor descriptor_;
        MeshBinding binding_;
        std::vector<Value> values_;
    };

    template <SupportedFieldValue Value, FieldLocation Location>
        requires(Location == FieldLocation::cell || Location == FieldLocation::face)
    [[nodiscard]] auto make_mesh_field(
        const FiniteVolumeMesh &mesh,
        FieldId id,
        std::string name,
        std::string unit_id,
        std::vector<Value> values) -> tsunami::core::Result<MeshField<Value, Location>>
    {
        const auto binding = make_mesh_binding(mesh);
        const auto expected = field_detail::expected_mesh_field_count(mesh, Location);
        auto descriptor = FieldDescriptor{
            .id = std::move(id),
            .name = std::move(name),
            .mesh_id = binding.mesh_id,
            .location = Location,
            .value_kind = FieldValueTraits<Value>::kind,
            .component_count = FieldValueTraits<Value>::component_count,
            .entity_count = expected,
            .unit_id = std::move(unit_id),
            .boundary_patch = std::nullopt,
        };
        auto validation = field_detail::validate_common_descriptor<Value>(descriptor, values.size(), "make_mesh_field");
        if (!validation) {
            return tsunami::core::failure<MeshField<Value, Location>>(validation.error());
        }
        if (values.size() != expected) {
            return tsunami::core::failure<MeshField<Value, Location>>(field_detail::field_error(
                "fvm.field.entity_count_mismatch",
                "field value count does not match mesh entity count",
                &descriptor,
                "make_mesh_field",
                expected,
                values.size()));
        }
        return tsunami::core::success(MeshField<Value, Location>{std::move(descriptor), binding, std::move(values)});
    }

    template <SupportedFieldValue Value, FieldLocation Location>
        requires(Location == FieldLocation::cell || Location == FieldLocation::face)
    [[nodiscard]] auto make_filled_mesh_field(
        const FiniteVolumeMesh &mesh,
        FieldId id,
        std::string name,
        std::string unit_id,
        const Value &initial_value) -> tsunami::core::Result<MeshField<Value, Location>>
    {
        return make_mesh_field<Value, Location>(
            mesh,
            std::move(id),
            std::move(name),
            std::move(unit_id),
            std::vector<Value>(field_detail::expected_mesh_field_count(mesh, Location), initial_value));
    }

    using CellScalarField = MeshField<tsunami::core::Real, FieldLocation::cell>;
    using FaceScalarField = MeshField<tsunami::core::Real, FieldLocation::face>;
    using CellVectorField = MeshField<Vector3, FieldLocation::cell>;
    using FaceVectorField = MeshField<Vector3, FieldLocation::face>;

} // namespace tsunami::fvm
