#pragma once

#include <tsunami/r2d/RegionalConservedState.hpp>

namespace tsunami::r2d
{
    class RegionalResidual
    {
    public:
        RegionalResidual(const RegionalResidual &) = delete;
        auto operator=(const RegionalResidual &) -> RegionalResidual & = delete;
        RegionalResidual(RegionalResidual &&) noexcept = default;
        auto operator=(RegionalResidual &&) noexcept -> RegionalResidual & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return mass_.binding(); }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return mass_.size(); }

        [[nodiscard]] auto mass() noexcept -> tsunami::fvm::CellScalarField & { return mass_; }
        [[nodiscard]] auto mass() const noexcept -> const tsunami::fvm::CellScalarField & { return mass_; }
        [[nodiscard]] auto momentum_x() noexcept -> tsunami::fvm::CellScalarField & { return momentum_x_; }
        [[nodiscard]] auto momentum_x() const noexcept -> const tsunami::fvm::CellScalarField & { return momentum_x_; }
        [[nodiscard]] auto momentum_y() noexcept -> tsunami::fvm::CellScalarField & { return momentum_y_; }
        [[nodiscard]] auto momentum_y() const noexcept -> const tsunami::fvm::CellScalarField & { return momentum_y_; }

        auto fill(ConservedVariables2D value) -> void;
        [[nodiscard]] auto clone() const -> RegionalResidual;
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;
        [[nodiscard]] auto is_layout_compatible_with(const RegionalResidual &other) const -> bool;
        auto copy_values_from(const RegionalResidual &other) -> tsunami::core::Result<void>;

    private:
        friend auto make_regional_residual(const tsunami::fvm::FiniteVolumeMesh &mesh)
            -> tsunami::core::Result<RegionalResidual>;

        RegionalResidual(
            tsunami::fvm::CellScalarField mass,
            tsunami::fvm::CellScalarField momentum_x,
            tsunami::fvm::CellScalarField momentum_y);

        tsunami::fvm::CellScalarField mass_;
        tsunami::fvm::CellScalarField momentum_x_;
        tsunami::fvm::CellScalarField momentum_y_;
    };

    [[nodiscard]] auto make_regional_residual(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<RegionalResidual>;

} // namespace tsunami::r2d
