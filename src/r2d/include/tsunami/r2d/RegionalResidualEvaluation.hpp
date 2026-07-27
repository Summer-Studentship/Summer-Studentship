#pragma once

#include <span>
#include <vector>

#include <tsunami/fvm/BoundaryConditionSet.hpp>
#include <tsunami/fvm/BoundaryPatchField.hpp>
#include <tsunami/r2d/RegionalResidual.hpp>
#include <tsunami/r2d/RusanovFlux.hpp>

namespace tsunami::r2d
{
    using ScalarBoundaryConditionSet = tsunami::fvm::BoundaryConditionSet<tsunami::core::Real>;

    class RegionalResidualWorkspace
    {
    public:
        RegionalResidualWorkspace(const RegionalResidualWorkspace &) = delete;
        auto operator=(const RegionalResidualWorkspace &) -> RegionalResidualWorkspace & = delete;
        RegionalResidualWorkspace(RegionalResidualWorkspace &&) noexcept = default;
        auto operator=(RegionalResidualWorkspace &&) noexcept -> RegionalResidualWorkspace & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto depth_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return depth_patches_; }
        [[nodiscard]] auto depth_patches() const noexcept -> std::span<const tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return depth_patches_; }
        [[nodiscard]] auto momentum_x_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return momentum_x_patches_; }
        [[nodiscard]] auto momentum_y_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return momentum_y_patches_; }
        [[nodiscard]] auto residual() noexcept -> RegionalResidual & { return residual_; }
        [[nodiscard]] auto residual() const noexcept -> const RegionalResidual & { return residual_; }
        [[nodiscard]] auto spectral_sum() noexcept -> tsunami::fvm::CellScalarField & { return spectral_sum_; }
        [[nodiscard]] auto spectral_sum() const noexcept -> const tsunami::fvm::CellScalarField & { return spectral_sum_; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        friend auto make_regional_residual_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
            -> tsunami::core::Result<RegionalResidualWorkspace>;

        RegionalResidualWorkspace(
            tsunami::fvm::MeshBinding binding,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> depth_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_x_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_y_patches,
            RegionalResidual residual,
            tsunami::fvm::CellScalarField spectral_sum);

        tsunami::fvm::MeshBinding binding_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> depth_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_x_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_y_patches_;
        RegionalResidual residual_;
        tsunami::fvm::CellScalarField spectral_sum_;
    };

    [[nodiscard]] auto make_regional_residual_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<RegionalResidualWorkspace>;

    auto evaluate_rusanov_residual(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const ScalarBoundaryConditionSet &depth_boundaries,
        const ScalarBoundaryConditionSet &momentum_x_boundaries,
        const ScalarBoundaryConditionSet &momentum_y_boundaries,
        const ShallowWaterStatePolicy &policy,
        RegionalResidual &destination_residual,
        tsunami::fvm::CellScalarField &destination_spectral_sum,
        tsunami::core::Real &destination_maximum_signal_speed,
        RegionalResidualWorkspace &workspace) -> tsunami::core::Result<void>;

} // namespace tsunami::r2d
