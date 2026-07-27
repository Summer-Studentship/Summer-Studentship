#pragma once

#include <tsunami/r2d/ShallowWaterFlux.hpp>

namespace tsunami::r2d
{
    struct RusanovFluxResult
    {
        ShallowWaterFlux2D flux;
        tsunami::core::Real maximum_signal_speed{};
    };

    [[nodiscard]] auto rusanov_flux(
        ConservedVariables2D left,
        ConservedVariables2D right,
        FaceNormal2D normal,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<RusanovFluxResult>;

} // namespace tsunami::r2d
