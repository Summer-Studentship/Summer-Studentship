#pragma once

#include <tsunami/r2d/RegionalBathymetry.hpp>
#include <tsunami/r2d/RegionalConservedState.hpp>

namespace tsunami::r2d
{
    auto calculate_free_surface_elevation(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        tsunami::fvm::CellScalarField &destination) -> tsunami::core::Result<void>;

} // namespace tsunami::r2d
