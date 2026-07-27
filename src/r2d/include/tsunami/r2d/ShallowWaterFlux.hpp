#pragma once

#include <tsunami/r2d/ShallowWaterState.hpp>

namespace tsunami::r2d
{
    struct FaceNormal2D
    {
        tsunami::core::Real x{};
        tsunami::core::Real y{};
        tsunami::core::Real length{};
    };

    struct ShallowWaterFlux2D
    {
        tsunami::core::Real mass{};
        tsunami::core::Real momentum_x{};
        tsunami::core::Real momentum_y{};
    };

    [[nodiscard]] auto make_face_normal(
        tsunami::fvm::Vector3 area_vector,
        const ShallowWaterStatePolicy &policy,
        std::optional<tsunami::fvm::FaceId> face_id = std::nullopt) -> tsunami::core::Result<FaceNormal2D>;

    [[nodiscard]] auto negated(FaceNormal2D normal) -> FaceNormal2D;

    [[nodiscard]] auto physical_normal_flux(
        ConservedVariables2D state,
        FaceNormal2D normal,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<ShallowWaterFlux2D>;

    [[nodiscard]] auto characteristic_signal_speed(
        ConservedVariables2D state,
        FaceNormal2D normal,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<tsunami::core::Real>;

    [[nodiscard]] auto maximum_characteristic_signal_speed(
        ConservedVariables2D left,
        ConservedVariables2D right,
        FaceNormal2D normal,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<tsunami::core::Real>;

} // namespace tsunami::r2d
