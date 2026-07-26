#pragma once

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <tsunami/fvm/BoundaryCondition.hpp>

namespace tsunami::fvm
{

    template <SupportedFieldValue Value>
    class BoundaryConditionSet
    {
    public:
        BoundaryConditionSet(const BoundaryConditionSet &) = delete;
        auto operator=(const BoundaryConditionSet &) -> BoundaryConditionSet & = delete;
        BoundaryConditionSet(BoundaryConditionSet &&) noexcept = default;
        auto operator=(BoundaryConditionSet &&) noexcept -> BoundaryConditionSet & = default;

        [[nodiscard]] auto binding() const noexcept -> const MeshBinding & { return binding_; }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return conditions_.size(); }
        [[nodiscard]] auto empty() const noexcept -> bool { return conditions_.empty(); }
        [[nodiscard]] auto conditions() const noexcept -> std::span<const BoundaryCondition<Value>> { return conditions_; }

        [[nodiscard]] auto condition(BoundaryPatchId patch_id) const -> const BoundaryCondition<Value> *
        {
            const auto found = std::ranges::find_if(conditions_, [patch_id](const auto &condition) {
                return condition.patch_id() == patch_id;
            });
            if (found == conditions_.end()) {
                return nullptr;
            }
            return std::addressof(*found);
        }

        [[nodiscard]] auto clone() const -> BoundaryConditionSet
        {
            std::vector<BoundaryCondition<Value>> copies;
            copies.reserve(conditions_.size());
            for (const auto &condition : conditions_) {
                copies.push_back(condition.clone());
            }
            return BoundaryConditionSet{binding_, std::move(copies)};
        }

        [[nodiscard]] auto is_bound_to(const FiniteVolumeMesh &mesh) const -> bool
        {
            return binding_ == make_mesh_binding(mesh);
        }

        [[nodiscard]] auto is_complete_for(const FiniteVolumeMesh &mesh) const -> bool
        {
            if (!is_bound_to(mesh) || conditions_.size() != mesh.summary().boundary_patch_count) {
                return false;
            }
            for (std::size_t index = 0; index < conditions_.size(); ++index) {
                if (conditions_[index].patch_id().value != index) {
                    return false;
                }
            }
            return true;
        }

    private:
        template <SupportedFieldValue Other>
        friend auto make_boundary_condition_set(
            const FiniteVolumeMesh &mesh,
            std::vector<BoundarySpecification<Other>> specifications) -> tsunami::core::Result<BoundaryConditionSet<Other>>;

        BoundaryConditionSet(MeshBinding binding, std::vector<BoundaryCondition<Value>> conditions)
            : binding_{std::move(binding)}
            , conditions_{std::move(conditions)}
        {
        }

        MeshBinding binding_;
        std::vector<BoundaryCondition<Value>> conditions_;
    };

    template <SupportedFieldValue Value>
    [[nodiscard]] auto make_boundary_condition_set(
        const FiniteVolumeMesh &mesh,
        std::vector<BoundarySpecification<Value>> specifications) -> tsunami::core::Result<BoundaryConditionSet<Value>>
    {
        const auto binding = make_mesh_binding(mesh);
        std::map<std::string, BoundaryPatchId, std::less<>> patches_by_name;
        for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = BoundaryPatchId{index};
            patches_by_name.emplace(mesh.boundary_patch(patch_id).name, patch_id);
        }

        std::set<std::string, std::less<>> seen_ids;
        std::set<BoundaryPatchId> seen_patches;
        std::vector<BoundaryCondition<Value>> conditions;
        conditions.reserve(specifications.size());

        for (auto &specification : specifications) {
            if (specification.id.value.empty()) {
                return tsunami::core::failure<BoundaryConditionSet<Value>>(boundary_detail::boundary_error(
                    "fvm.boundary.id_required",
                    "boundary condition id is required",
                    nullptr,
                    "make_boundary_condition_set",
                    specification.patch_tag));
            }
            if (!seen_ids.insert(specification.id.value).second) {
                return tsunami::core::failure<BoundaryConditionSet<Value>>(boundary_detail::boundary_error(
                    "fvm.boundary.id_duplicate",
                    "boundary condition id must be unique",
                    nullptr,
                    "make_boundary_condition_set",
                    specification.patch_tag));
            }
            if (specification.name.empty()) {
                return tsunami::core::failure<BoundaryConditionSet<Value>>(boundary_detail::boundary_error(
                    "fvm.boundary.name_required",
                    "boundary condition name is required",
                    nullptr,
                    "make_boundary_condition_set",
                    specification.patch_tag));
            }
            if (specification.patch_tag.empty()) {
                return tsunami::core::failure<BoundaryConditionSet<Value>>(boundary_detail::boundary_error(
                    "fvm.boundary.patch_tag_required",
                    "boundary patch tag is required",
                    nullptr,
                    "make_boundary_condition_set"));
            }
            if (specification.unit_id.empty()) {
                return tsunami::core::failure<BoundaryConditionSet<Value>>(boundary_detail::boundary_error(
                    "fvm.boundary.unit_required",
                    "boundary condition unit id is required",
                    nullptr,
                    "make_boundary_condition_set",
                    specification.patch_tag));
            }

            const auto patch = patches_by_name.find(specification.patch_tag);
            if (patch == patches_by_name.end()) {
                return tsunami::core::failure<BoundaryConditionSet<Value>>(boundary_detail::boundary_error(
                    "fvm.boundary.patch_unknown",
                    "boundary patch tag does not match a mesh boundary patch",
                    nullptr,
                    "make_boundary_condition_set",
                    specification.patch_tag));
            }
            if (!seen_patches.insert(patch->second).second) {
                const auto &duplicate_patch = mesh.boundary_patch(patch->second);
                const auto descriptor = boundary_detail::make_descriptor<Value>(
                    specification.id,
                    specification.name,
                    binding,
                    duplicate_patch,
                    BoundaryConditionKind::named_reference,
                    specification.unit_id);
                return tsunami::core::failure<BoundaryConditionSet<Value>>(boundary_detail::boundary_error(
                    "fvm.boundary.patch_duplicate",
                    "each mesh boundary patch may have only one boundary condition",
                    &descriptor,
                    "make_boundary_condition_set",
                    specification.patch_tag));
            }

            const auto &resolved_patch = mesh.boundary_patch(patch->second);
            auto condition = std::visit(
                [&](auto &operation) -> tsunami::core::Result<BoundaryCondition<Value>> {
                    using Operation = std::decay_t<decltype(operation)>;
                    if constexpr (std::is_same_v<Operation, FixedValueSpecification<Value>>) {
                        auto descriptor = boundary_detail::make_descriptor<Value>(
                            specification.id,
                            specification.name,
                            binding,
                            resolved_patch,
                            BoundaryConditionKind::fixed_value,
                            specification.unit_id);
                        if (operation.values.size() != resolved_patch.faces.size()) {
                            return tsunami::core::failure<BoundaryCondition<Value>>(boundary_detail::boundary_error(
                                "fvm.boundary.patch_entity_count_mismatch",
                                "fixed-value count does not match patch face count",
                                &descriptor,
                                "make_boundary_condition_set",
                                specification.patch_tag,
                                resolved_patch.faces.size(),
                                operation.values.size()));
                        }
                        auto values = make_boundary_patch_field<Value>(
                            mesh,
                            resolved_patch.id,
                            FieldId{descriptor.id.value + ".prescribed"},
                            descriptor.name + " prescribed values",
                            descriptor.unit_id,
                            std::move(operation.values));
                        if (!values) {
                            return tsunami::core::failure<BoundaryCondition<Value>>(boundary_detail::boundary_error(
                                "fvm.boundary.descriptor_inconsistent",
                                "fixed-value prescribed patch field could not be constructed",
                                &descriptor,
                                "make_boundary_condition_set",
                                specification.patch_tag));
                        }
                        return tsunami::core::success(BoundaryCondition<Value>{
                            FixedValueBoundary<Value>{std::move(descriptor), binding, std::move(values).value()}});
                    } else if constexpr (std::is_same_v<Operation, ZeroGradientSpecification>) {
                        auto descriptor = boundary_detail::make_descriptor<Value>(
                            specification.id,
                            specification.name,
                            binding,
                            resolved_patch,
                            BoundaryConditionKind::zero_gradient,
                            specification.unit_id);
                        return tsunami::core::success(BoundaryCondition<Value>{
                            ZeroGradientBoundary<Value>{std::move(descriptor), binding}});
                    } else {
                        if (operation.requested_type.empty()) {
                            return tsunami::core::failure<BoundaryCondition<Value>>(boundary_detail::boundary_error(
                                "fvm.boundary.named_type_required",
                                "named boundary reference type is required",
                                nullptr,
                                "make_boundary_condition_set",
                                specification.patch_tag));
                        }
                        auto descriptor = boundary_detail::make_descriptor<Value>(
                            specification.id,
                            specification.name,
                            binding,
                            resolved_patch,
                            BoundaryConditionKind::named_reference,
                            specification.unit_id);
                        return tsunami::core::success(BoundaryCondition<Value>{
                            NamedBoundaryReference<Value>{std::move(descriptor), binding, std::move(operation.requested_type)}});
                    }
                },
                specification.operation);
            if (!condition) {
                return tsunami::core::failure<BoundaryConditionSet<Value>>(condition.error());
            }
            conditions.push_back(std::move(condition).value());
        }

        for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = BoundaryPatchId{index};
            if (!seen_patches.contains(patch_id)) {
                const auto &patch = mesh.boundary_patch(patch_id);
                const auto descriptor = boundary_detail::make_descriptor<Value>(
                    BoundaryConditionId{},
                    {},
                    binding,
                    patch,
                    BoundaryConditionKind::named_reference,
                    {});
                return tsunami::core::failure<BoundaryConditionSet<Value>>(boundary_detail::boundary_error(
                    "fvm.boundary.patch_missing",
                    "boundary condition set must cover every mesh boundary patch",
                    &descriptor,
                    "make_boundary_condition_set",
                    patch.name));
            }
        }

        std::ranges::sort(conditions, [](const auto &left, const auto &right) {
            return left.patch_id() < right.patch_id();
        });

        auto set = BoundaryConditionSet<Value>{binding, std::move(conditions)};
        if (!set.is_complete_for(mesh)) {
            return tsunami::core::failure<BoundaryConditionSet<Value>>(boundary_detail::boundary_error(
                "fvm.boundary.set_incomplete",
                "boundary condition set is not complete for the mesh",
                nullptr,
                "make_boundary_condition_set"));
        }
        return tsunami::core::success(std::move(set));
    }

    using ScalarBoundaryCondition = BoundaryCondition<tsunami::core::Real>;
    using VectorBoundaryCondition = BoundaryCondition<Vector3>;
    using ScalarBoundaryConditionSet = BoundaryConditionSet<tsunami::core::Real>;
    using VectorBoundaryConditionSet = BoundaryConditionSet<Vector3>;

} // namespace tsunami::fvm
