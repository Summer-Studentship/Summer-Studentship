#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <tsunami/core/Error.hpp>
#include <tsunami/fvm/Field.hpp>
#include <tsunami/fvm/MeshBinding.hpp>

namespace tsunami::fvm
{

    struct BoundaryConditionId
    {
        std::string value;

        friend auto operator<=>(const BoundaryConditionId &, const BoundaryConditionId &) = default;
    };

    enum class BoundaryConditionKind
    {
        fixed_value,
        zero_gradient,
        named_reference
    };

    [[nodiscard]] constexpr auto to_string(BoundaryConditionKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case BoundaryConditionKind::fixed_value:
            return "fixed_value";
        case BoundaryConditionKind::zero_gradient:
            return "zero_gradient";
        case BoundaryConditionKind::named_reference:
            return "named_reference";
        }
        return "unknown";
    }

    [[nodiscard]] inline auto boundary_condition_kind_from_string(std::string_view value)
        -> std::optional<BoundaryConditionKind>
    {
        if (value == "fixed_value") {
            return BoundaryConditionKind::fixed_value;
        }
        if (value == "zero_gradient") {
            return BoundaryConditionKind::zero_gradient;
        }
        if (value == "named_reference") {
            return BoundaryConditionKind::named_reference;
        }
        return std::nullopt;
    }

    struct BoundaryDescriptor
    {
        BoundaryConditionId id;
        std::string name;
        MeshId mesh_id;
        BoundaryPatchId patch_id;
        std::string patch_name;
        BoundaryConditionKind kind{BoundaryConditionKind::fixed_value};
        FieldValueKind value_kind{FieldValueKind::scalar};
        std::size_t component_count{};
        std::size_t entity_count{};
        std::string unit_id;
        bool executable{};
    };

    class IBoundaryConditionView
    {
    public:
        virtual ~IBoundaryConditionView() = default;

        [[nodiscard]] virtual auto descriptor() const -> BoundaryDescriptor = 0;
    };

    namespace boundary_detail
    {
        inline constexpr auto rule_id = "SWE-FVM-BC-WP1";

        [[nodiscard]] inline auto boundary_error(
            std::string code,
            std::string message,
            const BoundaryDescriptor *descriptor = nullptr,
            std::string operation = {},
            std::string patch_tag = {},
            std::optional<std::size_t> expected_count = std::nullopt,
            std::optional<std::size_t> actual_count = std::nullopt,
            std::string requested_type = {}) -> tsunami::core::Error
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
                error.add_context("boundary_id", descriptor->id.value)
                    .add_context("boundary_name", descriptor->name)
                    .add_context("boundary_kind", std::string{to_string(descriptor->kind)})
                    .add_context("mesh_id", descriptor->mesh_id.value)
                    .add_context("patch_id", std::to_string(descriptor->patch_id.value))
                    .add_context("patch_name", descriptor->patch_name)
                    .add_context("unit_id", descriptor->unit_id);
            }
            if (!patch_tag.empty()) {
                error.add_context("patch_tag", std::move(patch_tag));
            }
            if (expected_count) {
                error.add_context("expected_count", std::to_string(*expected_count));
            }
            if (actual_count) {
                error.add_context("actual_count", std::to_string(*actual_count));
            }
            if (!requested_type.empty()) {
                error.add_context("requested_type", std::move(requested_type));
            }
            return error;
        }

        template <SupportedFieldValue Value>
        [[nodiscard]] inline auto descriptor_consistent(const BoundaryDescriptor &descriptor) -> bool
        {
            return !descriptor.id.value.empty() &&
                   !descriptor.name.empty() &&
                   !descriptor.mesh_id.value.empty() &&
                   !descriptor.patch_name.empty() &&
                   !descriptor.unit_id.empty() &&
                   descriptor.value_kind == FieldValueTraits<Value>::kind &&
                   descriptor.component_count == FieldValueTraits<Value>::component_count &&
                   descriptor.entity_count > 0 &&
                   descriptor.executable == (descriptor.kind != BoundaryConditionKind::named_reference);
        }

        [[nodiscard]] inline auto patch_field_descriptor_matches(
            const BoundaryDescriptor &descriptor,
            const FieldDescriptor &target) -> bool
        {
            return target.location == FieldLocation::boundary_patch &&
                   target.boundary_patch.has_value() &&
                   *target.boundary_patch == descriptor.patch_id &&
                   target.value_kind == descriptor.value_kind &&
                   target.component_count == descriptor.component_count &&
                   target.entity_count == descriptor.entity_count;
        }

        template <SupportedFieldValue Value>
        [[nodiscard]] inline auto make_descriptor(
            BoundaryConditionId id,
            std::string name,
            const MeshBinding &binding,
            const BoundaryPatchRecord &patch,
            BoundaryConditionKind kind,
            std::string unit_id) -> BoundaryDescriptor
        {
            return BoundaryDescriptor{
                .id = std::move(id),
                .name = std::move(name),
                .mesh_id = binding.mesh_id,
                .patch_id = patch.id,
                .patch_name = patch.name,
                .kind = kind,
                .value_kind = FieldValueTraits<Value>::kind,
                .component_count = FieldValueTraits<Value>::component_count,
                .entity_count = patch.faces.size(),
                .unit_id = std::move(unit_id),
                .executable = kind != BoundaryConditionKind::named_reference,
            };
        }
    } // namespace boundary_detail

} // namespace tsunami::fvm
