#pragma once

#include <span>
#include <vector>

#include <tsunami/fvm/BoundaryConditionSet.hpp>
#include <tsunami/fvm/BoundaryPatchField.hpp>
#include <tsunami/r2d/HydrostaticReconstruction.hpp>
#include <tsunami/r2d/RegionalBathymetry.hpp>
#include <tsunami/r2d/RegionalExteriorState.hpp>
#include <tsunami/r2d/RegionalRelaxationZone.hpp>
#include <tsunami/r2d/RegionalResidualEvaluation.hpp>

namespace tsunami::r2d
{
    class WellBalancedResidualWorkspace
    {
    public:
        WellBalancedResidualWorkspace(const WellBalancedResidualWorkspace &) = delete;
        auto operator=(const WellBalancedResidualWorkspace &) -> WellBalancedResidualWorkspace & = delete;
        WellBalancedResidualWorkspace(WellBalancedResidualWorkspace &&) noexcept = default;
        auto operator=(WellBalancedResidualWorkspace &&) noexcept -> WellBalancedResidualWorkspace & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto depth_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return depth_patches_; }
        [[nodiscard]] auto depth_patches() const noexcept -> std::span<const tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return depth_patches_; }
        [[nodiscard]] auto momentum_x_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return momentum_x_patches_; }
        [[nodiscard]] auto momentum_y_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return momentum_y_patches_; }
        [[nodiscard]] auto bed_elevation_patches() noexcept -> std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> { return bed_elevation_patches_; }
        [[nodiscard]] auto residual() noexcept -> RegionalResidual & { return residual_; }
        [[nodiscard]] auto residual() const noexcept -> const RegionalResidual & { return residual_; }
        [[nodiscard]] auto spectral_sum() noexcept -> tsunami::fvm::CellScalarField & { return spectral_sum_; }
        [[nodiscard]] auto spectral_sum() const noexcept -> const tsunami::fvm::CellScalarField & { return spectral_sum_; }
        [[nodiscard]] auto outgoing_mass_rate() noexcept -> tsunami::fvm::CellScalarField & { return outgoing_mass_rate_; }
        [[nodiscard]] auto outgoing_mass_rate() const noexcept -> const tsunami::fvm::CellScalarField & { return outgoing_mass_rate_; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        friend auto make_well_balanced_residual_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
            -> tsunami::core::Result<WellBalancedResidualWorkspace>;

        WellBalancedResidualWorkspace(
            tsunami::fvm::MeshBinding binding,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> depth_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_x_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_y_patches,
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> bed_elevation_patches,
            RegionalResidual residual,
            tsunami::fvm::CellScalarField spectral_sum,
            tsunami::fvm::CellScalarField outgoing_mass_rate);

        tsunami::fvm::MeshBinding binding_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> depth_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_x_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_y_patches_;
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> bed_elevation_patches_;
        RegionalResidual residual_;
        tsunami::fvm::CellScalarField spectral_sum_;
        tsunami::fvm::CellScalarField outgoing_mass_rate_;
    };

    [[nodiscard]] auto make_well_balanced_residual_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<WellBalancedResidualWorkspace>;

    class PhysicalBoundaryResidualWorkspace
    {
    public:
        PhysicalBoundaryResidualWorkspace(const PhysicalBoundaryResidualWorkspace &) = delete;
        auto operator=(const PhysicalBoundaryResidualWorkspace &) -> PhysicalBoundaryResidualWorkspace & = delete;
        PhysicalBoundaryResidualWorkspace(PhysicalBoundaryResidualWorkspace &&) noexcept = default;
        auto operator=(PhysicalBoundaryResidualWorkspace &&) noexcept -> PhysicalBoundaryResidualWorkspace & = default;

        [[nodiscard]] auto residual_workspace() noexcept -> WellBalancedResidualWorkspace & { return residual_; }
        [[nodiscard]] auto exterior_workspace() noexcept -> RegionalExteriorStateWorkspace & { return exterior_; }
        [[nodiscard]] auto relaxation_diagnostics() noexcept -> RegionalRelaxationDiagnostics & { return relaxation_; }
        [[nodiscard]] auto residual() noexcept -> RegionalResidual & { return residual_.residual(); }
        [[nodiscard]] auto spectral_sum() noexcept -> tsunami::fvm::CellScalarField & { return residual_.spectral_sum(); }
        [[nodiscard]] auto outgoing_mass_rate() noexcept -> tsunami::fvm::CellScalarField & { return residual_.outgoing_mass_rate(); }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        friend auto make_physical_boundary_residual_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
            -> tsunami::core::Result<PhysicalBoundaryResidualWorkspace>;

        PhysicalBoundaryResidualWorkspace(
            WellBalancedResidualWorkspace residual,
            RegionalExteriorStateWorkspace exterior);

        WellBalancedResidualWorkspace residual_;
        RegionalExteriorStateWorkspace exterior_;
        RegionalRelaxationDiagnostics relaxation_;
    };

    [[nodiscard]] auto make_physical_boundary_residual_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<PhysicalBoundaryResidualWorkspace>;

    auto evaluate_well_balanced_rusanov_residual(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        const ScalarBoundaryConditionSet &depth_boundaries,
        const ScalarBoundaryConditionSet &momentum_x_boundaries,
        const ScalarBoundaryConditionSet &momentum_y_boundaries,
        const ScalarBoundaryConditionSet &bathymetry_boundaries,
        const ShallowWaterStatePolicy &policy,
        RegionalResidual &destination_residual,
        tsunami::fvm::CellScalarField &destination_spectral_sum,
        tsunami::fvm::CellScalarField &destination_outgoing_mass_rate,
        tsunami::core::Real &destination_maximum_signal_speed,
        WellBalancedResidualWorkspace &workspace) -> tsunami::core::Result<void>;

    auto evaluate_well_balanced_rusanov_residual(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        const RegionalBoundaryConditionSet &boundaries,
        const RegionalRelaxationZoneSet &relaxation_zones,
        const ShallowWaterStatePolicy &policy,
        tsunami::core::Time time,
        RegionalResidual &destination_residual,
        tsunami::fvm::CellScalarField &destination_spectral_sum,
        tsunami::fvm::CellScalarField &destination_outgoing_mass_rate,
        tsunami::core::Real &destination_maximum_signal_speed,
        PhysicalBoundaryResidualWorkspace &workspace) -> tsunami::core::Result<void>;

} // namespace tsunami::r2d
