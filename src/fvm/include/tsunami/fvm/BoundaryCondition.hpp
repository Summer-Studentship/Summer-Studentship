#pragma once

#include <type_traits>
#include <variant>

#include <tsunami/fvm/FixedValueBoundary.hpp>
#include <tsunami/fvm/NamedBoundaryReference.hpp>
#include <tsunami/fvm/ZeroGradientBoundary.hpp>

namespace tsunami::fvm
{

    template <SupportedFieldValue Value>
    class BoundaryCondition final : public IBoundaryConditionView
    {
    public:
        using Operation = std::variant<
            FixedValueBoundary<Value>,
            ZeroGradientBoundary<Value>,
            NamedBoundaryReference<Value>>;

        explicit BoundaryCondition(Operation operation)
            : operation_{std::move(operation)}
        {
        }

        BoundaryCondition(const BoundaryCondition &) = delete;
        auto operator=(const BoundaryCondition &) -> BoundaryCondition & = delete;
        BoundaryCondition(BoundaryCondition &&) noexcept = default;
        auto operator=(BoundaryCondition &&) noexcept -> BoundaryCondition & = default;
        ~BoundaryCondition() override = default;

        [[nodiscard]] auto descriptor() const -> BoundaryDescriptor override
        {
            return std::visit([](const auto &operation) { return operation.descriptor(); }, operation_);
        }

        [[nodiscard]] auto kind() const -> BoundaryConditionKind { return descriptor().kind; }
        [[nodiscard]] auto patch_id() const -> BoundaryPatchId { return descriptor().patch_id; }
        [[nodiscard]] auto is_executable() const -> bool { return descriptor().executable; }

        [[nodiscard]] auto clone() const -> BoundaryCondition
        {
            return std::visit(
                [](const auto &operation) {
                    return BoundaryCondition{Operation{operation.clone()}};
                },
                operation_);
        }

        [[nodiscard]] auto is_bound_to(const FiniteVolumeMesh &mesh) const -> bool
        {
            return std::visit([&mesh](const auto &operation) { return operation.is_bound_to(mesh); }, operation_);
        }

        auto apply(
            const FiniteVolumeMesh &mesh,
            const MeshField<Value, FieldLocation::cell> &internal,
            BoundaryPatchField<Value> &destination) const -> tsunami::core::Result<void>
        {
            const auto current = descriptor();
            if (current.value_kind != FieldValueTraits<Value>::kind ||
                current.component_count != FieldValueTraits<Value>::component_count) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.value_kind_incompatible",
                    "boundary descriptor value kind does not match the boundary value type",
                    &current,
                    "apply"));
            }
            if (!boundary_detail::descriptor_consistent<Value>(current)) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.descriptor_inconsistent",
                    "boundary descriptor does not match value type or operation kind",
                    &current,
                    "apply"));
            }
            if (!is_bound_to(mesh)) {
                return tsunami::core::failure(boundary_detail::boundary_error(
                    "fvm.boundary.mesh_incompatible",
                    "mesh is incompatible with the boundary condition",
                    &current,
                    "apply"));
            }
            return std::visit(
                [&mesh, &internal, &destination](const auto &operation) -> tsunami::core::Result<void> {
                    if constexpr (std::is_same_v<std::decay_t<decltype(operation)>, ZeroGradientBoundary<Value>>) {
                        return operation.apply(mesh, internal, destination);
                    } else {
                        return operation.apply(internal, destination);
                    }
                },
                operation_);
        }

    private:
        Operation operation_;
    };

} // namespace tsunami::fvm
