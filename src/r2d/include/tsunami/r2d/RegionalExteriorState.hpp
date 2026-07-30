#pragma once

#include <span>
#include <vector>

#include <tsunami/fvm/BoundaryPatchField.hpp>
#include <tsunami/r2d/RegionalBoundaryCondition.hpp>

namespace tsunami::r2d
{
    class RegionalExteriorStateWorkspace
    {
    public:
        RegionalExteriorStateWorkspace(const RegionalExteriorStateWorkspace &) = delete;
        auto operator=(const RegionalExteriorStateWorkspace &) -> RegionalExteriorStateWorkspace & = delete;
        RegionalExteriorStateWorkspace(RegionalExteriorStateWorkspace &&) noexcept = default;
        auto operator=(RegionalExteriorStateWorkspace &&) noexcept -> RegionalExteriorStateWorkspace & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto depth_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return depth_patches_; }
        [[nodiscard]] auto depth_patches() const noexcept -> std::span<const tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return depth_patches_; }
        [[nodiscard]] auto momentum_x_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return momentum_x_patches_; }
        [[nodiscard]] auto momentum_x_patches() const noexcept -> std::span<const tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return momentum_x_patches_; }
        [[nodiscard]] auto momentum_y_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return momentum_y_patches_; }
        [[nodiscard]] auto momentum_y_patches() const noexcept -> std::span<const tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return momentum_y_patches_; }
        [[nodiscard]] auto bed_elevation_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return bed_elevation_patches_; }
        [[nodiscard]] auto bed_elevation_patches() const noexcept -> std::span<const tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return bed_elevation_patches_; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        friend auto make_regional_exterior_state_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
            -> tsunami::core::Result<RegionalExteriorStateWorkspace>;

        friend auto populate_regional_exterior_states(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalConservedState &state,
            const RegionalBathymetry &bathymetry,
            const RegionalBoundaryConditionSet &boundaries,
            const ShallowWaterStatePolicy &policy,
            tsunami::core::Time time,
            RegionalExteriorStateWorkspace &workspace) -> tsunami::core::Result<void>;

        RegionalExteriorStateWorkspace(
            tsunami::fvm::MeshBinding binding,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> depth_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_x_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_y_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> bed_elevation_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_depth_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_momentum_x_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_momentum_y_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_bed_elevation_patches);

        tsunami::fvm::MeshBinding binding_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> depth_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_x_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_y_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> bed_elevation_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_depth_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_momentum_x_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_momentum_y_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> staged_bed_elevation_patches_;
    };

    [[nodiscard]] auto make_regional_exterior_state_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<RegionalExteriorStateWorkspace>;

    auto populate_regional_exterior_states(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        const RegionalBoundaryConditionSet &boundaries,
        const ShallowWaterStatePolicy &policy,
        tsunami::core::Time time,
        RegionalExteriorStateWorkspace &workspace) -> tsunami::core::Result<void>;

} // namespace tsunami::r2d
