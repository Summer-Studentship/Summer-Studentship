#include <tsunami/r2d/RegionalConservedState.hpp>

#include <algorithm>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto depth_unit = "m";
        constexpr auto momentum_unit = "m2/s";

        [[nodiscard]] auto component_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string field_id = {},
            std::string expected_unit = {},
            std::string actual_unit = {}) -> tsunami::core::Error
        {
            const auto mesh_id = mesh.summary().id;
            return detail::r2d_error(
                std::move(code),
                std::move(message),
                std::move(operation),
                "SWE-R2D-STA",
                &mesh_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::move(field_id),
                std::move(expected_unit),
                std::move(actual_unit));
        }

        [[nodiscard]] auto validate_component(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::fvm::CellScalarField &field,
            std::string_view expected_unit,
            std::string operation) -> tsunami::core::Result<void>
        {
            const auto descriptor = field.descriptor();
            if (!field.is_bound_to(mesh) || field.size() != mesh.summary().cell_count) {
                return tsunami::core::failure(component_error(
                    "r2d.state.component_incompatible",
                    "state component field is not compatible with the supplied mesh",
                    std::move(operation),
                    mesh,
                    descriptor.id.value));
            }
            if (descriptor.unit_id != expected_unit) {
                return tsunami::core::failure(component_error(
                    "r2d.state.component_incompatible",
                    "state component unit is incompatible",
                    std::move(operation),
                    mesh,
                    descriptor.id.value,
                    std::string{expected_unit},
                    descriptor.unit_id));
            }
            return tsunami::core::success();
        }
    } // namespace

    RegionalConservedState::RegionalConservedState(
        tsunami::fvm::CellScalarField depth,
        tsunami::fvm::CellScalarField momentum_x,
        tsunami::fvm::CellScalarField momentum_y)
        : depth_{std::move(depth)}
        , momentum_x_{std::move(momentum_x)}
        , momentum_y_{std::move(momentum_y)}
    {
    }

    auto RegionalConservedState::local_state(tsunami::fvm::CellId cell_id) const -> ConservedVariables2D
    {
        return ConservedVariables2D{
            .depth = depth_.at(cell_id.value),
            .momentum_x = momentum_x_.at(cell_id.value),
            .momentum_y = momentum_y_.at(cell_id.value)};
    }

    auto RegionalConservedState::set_local_state(tsunami::fvm::CellId cell_id, ConservedVariables2D state) -> void
    {
        depth_.at(cell_id.value) = state.depth;
        momentum_x_.at(cell_id.value) = state.momentum_x;
        momentum_y_.at(cell_id.value) = state.momentum_y;
    }

    auto RegionalConservedState::clone() const -> RegionalConservedState
    {
        return RegionalConservedState{depth_.clone(), momentum_x_.clone(), momentum_y_.clone()};
    }

    auto RegionalConservedState::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return depth_.is_bound_to(mesh) && momentum_x_.is_bound_to(mesh) && momentum_y_.is_bound_to(mesh);
    }

    auto RegionalConservedState::is_layout_compatible_with(const RegionalConservedState &other) const -> bool
    {
        return depth_.is_layout_compatible_with(other.depth_) &&
               momentum_x_.is_layout_compatible_with(other.momentum_x_) &&
               momentum_y_.is_layout_compatible_with(other.momentum_y_);
    }

    auto RegionalConservedState::copy_values_from(const RegionalConservedState &other) -> tsunami::core::Result<void>
    {
        if (!is_layout_compatible_with(other)) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.state.destination_incompatible",
                "regional conserved states are not layout compatible",
                "copy_values_from",
                "SWE-R2D-STA"));
        }
        auto depth_copy = depth_.copy_values_from(other.depth_);
        if (!depth_copy) {
            return tsunami::core::failure(depth_copy.error());
        }
        auto momentum_x_copy = momentum_x_.copy_values_from(other.momentum_x_);
        if (!momentum_x_copy) {
            return tsunami::core::failure(momentum_x_copy.error());
        }
        return momentum_y_.copy_values_from(other.momentum_y_);
    }

    auto make_regional_conserved_state(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::FieldId depth_id,
        tsunami::fvm::FieldId momentum_x_id,
        tsunami::fvm::FieldId momentum_y_id,
        std::vector<tsunami::core::Real> depth,
        std::vector<tsunami::core::Real> momentum_x,
        std::vector<tsunami::core::Real> momentum_y,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<RegionalConservedState>
    {
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure<RegionalConservedState>(policy_validation.error());
        }
        const auto expected = mesh.summary().cell_count;
        if (depth.size() != expected || momentum_x.size() != expected || momentum_y.size() != expected) {
            return tsunami::core::failure<RegionalConservedState>(component_error(
                "r2d.state.component_incompatible",
                "state component counts must match mesh cell count",
                "make_regional_conserved_state",
                mesh));
        }

        for (std::size_t index = 0; index < expected; ++index) {
            auto canonical = validate_and_canonicalise_state(
                ConservedVariables2D{depth[index], momentum_x[index], momentum_y[index]},
                policy,
                tsunami::fvm::CellId{index});
            if (!canonical) {
                return tsunami::core::failure<RegionalConservedState>(canonical.error());
            }
            depth[index] = canonical.value().depth;
            momentum_x[index] = canonical.value().momentum_x;
            momentum_y[index] = canonical.value().momentum_y;
        }

        auto depth_field = tsunami::fvm::make_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh, std::move(depth_id), "regional water depth", depth_unit, std::move(depth));
        if (!depth_field) {
            return tsunami::core::failure<RegionalConservedState>(depth_field.error());
        }
        auto momentum_x_field = tsunami::fvm::make_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh, std::move(momentum_x_id), "regional x momentum", momentum_unit, std::move(momentum_x));
        if (!momentum_x_field) {
            return tsunami::core::failure<RegionalConservedState>(momentum_x_field.error());
        }
        auto momentum_y_field = tsunami::fvm::make_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh, std::move(momentum_y_id), "regional y momentum", momentum_unit, std::move(momentum_y));
        if (!momentum_y_field) {
            return tsunami::core::failure<RegionalConservedState>(momentum_y_field.error());
        }

        auto state = RegionalConservedState{
            std::move(depth_field).value(),
            std::move(momentum_x_field).value(),
            std::move(momentum_y_field).value()};
        auto depth_validation = validate_component(mesh, state.depth(), depth_unit, "make_regional_conserved_state");
        auto momentum_x_validation = validate_component(mesh, state.momentum_x(), momentum_unit, "make_regional_conserved_state");
        auto momentum_y_validation = validate_component(mesh, state.momentum_y(), momentum_unit, "make_regional_conserved_state");
        if (!depth_validation) {
            return tsunami::core::failure<RegionalConservedState>(depth_validation.error());
        }
        if (!momentum_x_validation) {
            return tsunami::core::failure<RegionalConservedState>(momentum_x_validation.error());
        }
        if (!momentum_y_validation) {
            return tsunami::core::failure<RegionalConservedState>(momentum_y_validation.error());
        }
        return tsunami::core::success(std::move(state));
    }

    auto make_filled_regional_conserved_state(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::FieldId depth_id,
        tsunami::fvm::FieldId momentum_x_id,
        tsunami::fvm::FieldId momentum_y_id,
        ConservedVariables2D value,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<RegionalConservedState>
    {
        const auto count = mesh.summary().cell_count;
        return make_regional_conserved_state(
            mesh,
            std::move(depth_id),
            std::move(momentum_x_id),
            std::move(momentum_y_id),
            std::vector<tsunami::core::Real>(count, value.depth),
            std::vector<tsunami::core::Real>(count, value.momentum_x),
            std::vector<tsunami::core::Real>(count, value.momentum_y),
            policy);
    }

    auto validate_and_canonicalise(
        RegionalConservedState &state,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<void>
    {
        std::vector<ConservedVariables2D> accepted;
        accepted.reserve(state.size());
        for (std::size_t index = 0; index < state.size(); ++index) {
            auto canonical = validate_and_canonicalise_state(state.local_state(tsunami::fvm::CellId{index}), policy, tsunami::fvm::CellId{index});
            if (!canonical) {
                return tsunami::core::failure(canonical.error());
            }
            accepted.push_back(canonical.value());
        }
        for (std::size_t index = 0; index < accepted.size(); ++index) {
            state.set_local_state(tsunami::fvm::CellId{index}, accepted[index]);
        }
        return tsunami::core::success();
    }

} // namespace tsunami::r2d
