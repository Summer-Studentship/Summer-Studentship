#pragma once

#include <tsunami/r2d/RusanovFlux.hpp>

namespace tsunami::r2d
{
    struct HydrostaticReconstructionResult
    {
        ConservedVariables2D left;
        ConservedVariables2D right;

        tsunami::core::Real interface_bed_elevation{};

        ShallowWaterFlux2D left_pressure_correction;
        ShallowWaterFlux2D right_pressure_correction;
    };

    [[nodiscard]] auto hydrostatic_reconstruction(
        ConservedVariables2D left,
        ConservedVariables2D right,
        tsunami::core::Real left_bed_elevation,
        tsunami::core::Real right_bed_elevation,
        FaceNormal2D normal,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<HydrostaticReconstructionResult>;

} // namespace tsunami::r2d
