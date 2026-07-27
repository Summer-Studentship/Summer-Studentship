#pragma once

#include <string>
#include <vector>

#include <tsunami/fvm/MeshField.hpp>
#include <tsunami/r2d/ShallowWaterState.hpp>

namespace tsunami::r2d
{
    class RegionalConservedState
    {
    public:
        RegionalConservedState(const RegionalConservedState &) = delete;
        auto operator=(const RegionalConservedState &) -> RegionalConservedState & = delete;
        RegionalConservedState(RegionalConservedState &&) noexcept = default;
        auto operator=(RegionalConservedState &&) noexcept -> RegionalConservedState & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return depth_.binding(); }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return depth_.size(); }

        [[nodiscard]] auto depth() noexcept -> tsunami::fvm::CellScalarField & { return depth_; }
        [[nodiscard]] auto depth() const noexcept -> const tsunami::fvm::CellScalarField & { return depth_; }
        [[nodiscard]] auto momentum_x() noexcept -> tsunami::fvm::CellScalarField & { return momentum_x_; }
        [[nodiscard]] auto momentum_x() const noexcept -> const tsunami::fvm::CellScalarField & { return momentum_x_; }
        [[nodiscard]] auto momentum_y() noexcept -> tsunami::fvm::CellScalarField & { return momentum_y_; }
        [[nodiscard]] auto momentum_y() const noexcept -> const tsunami::fvm::CellScalarField & { return momentum_y_; }

        [[nodiscard]] auto local_state(tsunami::fvm::CellId cell_id) const -> ConservedVariables2D;
        auto set_local_state(tsunami::fvm::CellId cell_id, ConservedVariables2D state) -> void;

        [[nodiscard]] auto clone() const -> RegionalConservedState;
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;
        [[nodiscard]] auto is_layout_compatible_with(const RegionalConservedState &other) const -> bool;
        auto copy_values_from(const RegionalConservedState &other) -> tsunami::core::Result<void>;

    private:
        friend auto make_regional_conserved_state(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            tsunami::fvm::FieldId depth_id,
            tsunami::fvm::FieldId momentum_x_id,
            tsunami::fvm::FieldId momentum_y_id,
            std::vector<tsunami::core::Real> depth,
            std::vector<tsunami::core::Real> momentum_x,
            std::vector<tsunami::core::Real> momentum_y,
            const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<RegionalConservedState>;

        RegionalConservedState(
            tsunami::fvm::CellScalarField depth,
            tsunami::fvm::CellScalarField momentum_x,
            tsunami::fvm::CellScalarField momentum_y);

        tsunami::fvm::CellScalarField depth_;
        tsunami::fvm::CellScalarField momentum_x_;
        tsunami::fvm::CellScalarField momentum_y_;
    };

    [[nodiscard]] auto make_regional_conserved_state(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::FieldId depth_id,
        tsunami::fvm::FieldId momentum_x_id,
        tsunami::fvm::FieldId momentum_y_id,
        std::vector<tsunami::core::Real> depth,
        std::vector<tsunami::core::Real> momentum_x,
        std::vector<tsunami::core::Real> momentum_y,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<RegionalConservedState>;

    [[nodiscard]] auto make_filled_regional_conserved_state(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::FieldId depth_id,
        tsunami::fvm::FieldId momentum_x_id,
        tsunami::fvm::FieldId momentum_y_id,
        ConservedVariables2D value,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<RegionalConservedState>;

    auto validate_and_canonicalise(
        RegionalConservedState &state,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<void>;

} // namespace tsunami::r2d
