#pragma once

#include <optional>

#include <tsunami/fvm/FiniteVolumeMesh.hpp>
#include <tsunami/fvm/MeshField.hpp>
#include <tsunami/r2d/ShallowWaterState.hpp>

namespace tsunami::r2d
{
    struct CflTimestepEstimate
    {
        std::optional<tsunami::core::Real> stable_timestep;
        std::optional<tsunami::fvm::CellId> limiting_cell;
    };

    [[nodiscard]] auto estimate_cfl_timestep(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::fvm::CellScalarField &spectral_sum,
        tsunami::core::Real courant_number) -> tsunami::core::Result<CflTimestepEstimate>;

} // namespace tsunami::r2d
