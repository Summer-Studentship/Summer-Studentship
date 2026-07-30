#pragma once

#include <tsunami/fvm/GreenGaussGradient.hpp>
#include <tsunami/fvm/LinearInterpolation.hpp>
#include <tsunami/r2d/FreeSurfaceElevation.hpp>
#include <tsunami/r2d/RegionalBathymetry.hpp>
#include <tsunami/r2d/RegionalEarthquakeDiagnostics.hpp>
#include <tsunami/r2d/RegionalResidualEvaluation.hpp>
#include <tsunami/r2d/RegionalSeabedDisplacement.hpp>
#include <tsunami/r2d/RegionalSimulationState.hpp>

namespace tsunami::r2d
{
    struct RegionalEarthquakeInitialisationRequest
    {
        const tsunami::fvm::FiniteVolumeMesh *mesh{};
        const RegionalBathymetry *pre_event_bathymetry{};
        const RegionalConservedState *pre_event_state{};
        const RegionalSeabedDisplacement *seabed_displacement{};
        RegionalBedDeformationMappingKind bed_mapping{RegionalBedDeformationMappingKind::vertical_only};
        RegionalSurfaceTransferKind surface_transfer{RegionalSurfaceTransferKind::passive_equal_to_effective_bed};
        const ScalarBoundaryConditionSet *bathymetry_boundaries{};
        const tsunami::fvm::CellScalarField *prescribed_surface_perturbation{};
        ShallowWaterStatePolicy state_policy;
        tsunami::core::Real zero_momentum_tolerance{1.0e-12};
        RegionalEarthquakeSourceMetadata metadata;
    };

    struct RegionalEarthquakeInitialisationResult
    {
        RegionalBathymetry post_event_bathymetry;
        RegionalSimulationState simulation_state;
        RegionalEarthquakeInitialisationDiagnostics diagnostics;
    };

    class RegionalEarthquakeInitialisationWorkspace
    {
    public:
        RegionalEarthquakeInitialisationWorkspace(const RegionalEarthquakeInitialisationWorkspace &) = delete;
        auto operator=(const RegionalEarthquakeInitialisationWorkspace &) -> RegionalEarthquakeInitialisationWorkspace & = delete;
        RegionalEarthquakeInitialisationWorkspace(RegionalEarthquakeInitialisationWorkspace &&) noexcept = default;
        auto operator=(RegionalEarthquakeInitialisationWorkspace &&) noexcept -> RegionalEarthquakeInitialisationWorkspace & = default;

        [[nodiscard]] auto face_bathymetry() noexcept -> tsunami::fvm::FaceScalarField & { return face_bathymetry_; }
        [[nodiscard]] auto bathymetry_gradient() noexcept -> tsunami::fvm::CellVectorField & { return bathymetry_gradient_; }
        [[nodiscard]] auto effective_bed_displacement() noexcept -> tsunami::fvm::CellScalarField & { return effective_bed_displacement_; }
        [[nodiscard]] auto surface_perturbation() noexcept -> tsunami::fvm::CellScalarField & { return surface_perturbation_; }
        [[nodiscard]] auto pre_event_free_surface() noexcept -> tsunami::fvm::CellScalarField & { return pre_event_free_surface_; }
        [[nodiscard]] auto post_event_free_surface() noexcept -> tsunami::fvm::CellScalarField & { return post_event_free_surface_; }
        [[nodiscard]] auto raw_post_event_depth() noexcept -> tsunami::fvm::CellScalarField & { return raw_post_event_depth_; }
        [[nodiscard]] auto interpolation_workspace() noexcept -> tsunami::fvm::ScalarLinearInterpolationWorkspace & { return interpolation_; }
        [[nodiscard]] auto gradient_workspace() noexcept -> tsunami::fvm::GreenGaussGradientWorkspace & { return gradient_; }
        [[nodiscard]] auto stencil() const noexcept -> const tsunami::fvm::LinearInterpolationStencil & { return stencil_; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

    private:
        friend auto make_regional_earthquake_initialisation_workspace(
            const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::core::Result<RegionalEarthquakeInitialisationWorkspace>;

        RegionalEarthquakeInitialisationWorkspace(
            tsunami::fvm::MeshBinding binding,
            tsunami::fvm::LinearInterpolationStencil stencil,
            tsunami::fvm::ScalarLinearInterpolationWorkspace interpolation,
            tsunami::fvm::GreenGaussGradientWorkspace gradient,
            tsunami::fvm::FaceScalarField face_bathymetry,
            tsunami::fvm::CellVectorField bathymetry_gradient,
            tsunami::fvm::CellScalarField effective_bed_displacement,
            tsunami::fvm::CellScalarField surface_perturbation,
            tsunami::fvm::CellScalarField pre_event_free_surface,
            tsunami::fvm::CellScalarField post_event_free_surface,
            tsunami::fvm::CellScalarField raw_post_event_depth);

        tsunami::fvm::MeshBinding binding_;
        tsunami::fvm::LinearInterpolationStencil stencil_;
        tsunami::fvm::ScalarLinearInterpolationWorkspace interpolation_;
        tsunami::fvm::GreenGaussGradientWorkspace gradient_;
        tsunami::fvm::FaceScalarField face_bathymetry_;
        tsunami::fvm::CellVectorField bathymetry_gradient_;
        tsunami::fvm::CellScalarField effective_bed_displacement_;
        tsunami::fvm::CellScalarField surface_perturbation_;
        tsunami::fvm::CellScalarField pre_event_free_surface_;
        tsunami::fvm::CellScalarField post_event_free_surface_;
        tsunami::fvm::CellScalarField raw_post_event_depth_;
    };

    [[nodiscard]] auto make_regional_earthquake_initialisation_workspace(
        const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::core::Result<RegionalEarthquakeInitialisationWorkspace>;

    auto calculate_effective_seabed_displacement(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalBathymetry &bathymetry,
        const RegionalSeabedDisplacement &displacement,
        RegionalBedDeformationMappingKind mapping,
        const ScalarBoundaryConditionSet *bathymetry_boundaries,
        tsunami::fvm::CellScalarField &destination,
        RegionalEarthquakeInitialisationWorkspace &workspace) -> tsunami::core::Result<void>;

    auto initialise_regional_earthquake_state(
        const RegionalEarthquakeInitialisationRequest &request,
        RegionalEarthquakeInitialisationWorkspace &workspace) -> tsunami::core::Result<RegionalEarthquakeInitialisationResult>;

} // namespace tsunami::r2d
