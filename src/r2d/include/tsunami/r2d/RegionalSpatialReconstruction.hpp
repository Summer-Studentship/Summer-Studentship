#pragma once

#include <span>
#include <vector>

#include <tsunami/fvm/FiniteVolumeMesh.hpp>
#include <tsunami/r2d/ShallowWaterState.hpp>

namespace tsunami::r2d
{
    enum class RegionalReconstructionScheme
    {
        first_order,
        limited_linear,
        unlimited_linear_for_verification
    };

    struct RegionalReconstructionPolicy
    {
        RegionalReconstructionScheme scheme{RegionalReconstructionScheme::first_order};
        tsunami::core::Real conditioning_tolerance{1.0e-12};
        tsunami::core::Real minimum_distance_weight_denominator{1.0e-24};
    };

    struct RegionalGradient2D
    {
        tsunami::core::Real x{};
        tsunami::core::Real y{};
        bool valid{false};
    };

    struct RegionalScalarReconstruction
    {
        std::vector<RegionalGradient2D> gradients;
        std::vector<tsunami::core::Real> limiter;
    };

    [[nodiscard]] auto validate_reconstruction_policy(const RegionalReconstructionPolicy &policy)
        -> tsunami::core::Result<void>;

    [[nodiscard]] auto compute_weighted_least_squares_gradients(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::span<const tsunami::core::Real> cell_values,
        std::span<const tsunami::core::Real> boundary_face_values,
        const RegionalReconstructionPolicy &policy) -> tsunami::core::Result<std::vector<RegionalGradient2D>>;

    [[nodiscard]] auto compute_barth_jespersen_limiters(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::span<const tsunami::core::Real> cell_values,
        std::span<const tsunami::core::Real> boundary_face_values,
        std::span<const RegionalGradient2D> gradients) -> tsunami::core::Result<std::vector<tsunami::core::Real>>;

    [[nodiscard]] auto make_regional_scalar_reconstruction(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::span<const tsunami::core::Real> cell_values,
        std::span<const tsunami::core::Real> boundary_face_values,
        const RegionalReconstructionPolicy &policy) -> tsunami::core::Result<RegionalScalarReconstruction>;

    [[nodiscard]] auto reconstruct_cell_scalar_to_face(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::CellId cell_id,
        tsunami::fvm::FaceId face_id,
        std::span<const tsunami::core::Real> cell_values,
        const RegionalScalarReconstruction &reconstruction) -> tsunami::core::Real;

} // namespace tsunami::r2d
