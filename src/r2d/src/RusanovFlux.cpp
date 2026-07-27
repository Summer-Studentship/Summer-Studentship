#include <tsunami/r2d/RusanovFlux.hpp>

#include <cmath>

namespace tsunami::r2d
{
    namespace
    {
        [[nodiscard]] auto finite(ShallowWaterFlux2D flux) -> bool
        {
            return std::isfinite(flux.mass) && std::isfinite(flux.momentum_x) && std::isfinite(flux.momentum_y);
        }
    } // namespace

    auto rusanov_flux(
        ConservedVariables2D left,
        ConservedVariables2D right,
        FaceNormal2D normal,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<RusanovFluxResult>
    {
        auto left_state = validate_and_canonicalise_state(left, policy);
        if (!left_state) {
            return tsunami::core::failure<RusanovFluxResult>(left_state.error());
        }
        auto right_state = validate_and_canonicalise_state(right, policy);
        if (!right_state) {
            return tsunami::core::failure<RusanovFluxResult>(right_state.error());
        }
        auto left_flux = physical_normal_flux(left_state.value(), normal, policy);
        if (!left_flux) {
            return tsunami::core::failure<RusanovFluxResult>(left_flux.error());
        }
        auto right_flux = physical_normal_flux(right_state.value(), normal, policy);
        if (!right_flux) {
            return tsunami::core::failure<RusanovFluxResult>(right_flux.error());
        }
        auto alpha = maximum_characteristic_signal_speed(left_state.value(), right_state.value(), normal, policy);
        if (!alpha) {
            return tsunami::core::failure<RusanovFluxResult>(alpha.error());
        }
        const auto flux = ShallowWaterFlux2D{
            .mass = 0.5 * (left_flux.value().mass + right_flux.value().mass) -
                    (0.5 * alpha.value() * (right_state.value().depth - left_state.value().depth)),
            .momentum_x = 0.5 * (left_flux.value().momentum_x + right_flux.value().momentum_x) -
                          (0.5 * alpha.value() * (right_state.value().momentum_x - left_state.value().momentum_x)),
            .momentum_y = 0.5 * (left_flux.value().momentum_y + right_flux.value().momentum_y) -
                          (0.5 * alpha.value() * (right_state.value().momentum_y - left_state.value().momentum_y))};
        if (!finite(flux) || !std::isfinite(alpha.value())) {
            return tsunami::core::failure<RusanovFluxResult>(detail::r2d_error(
                "r2d.flux.result_nonfinite",
                "Rusanov flux result must be finite",
                "rusanov_flux",
                "SWE-R2D-FLX",
                nullptr,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                std::nullopt,
                policy.gravity,
                policy.dry_depth,
                std::nullopt,
                alpha.value()));
        }
        return tsunami::core::success(RusanovFluxResult{.flux = flux, .maximum_signal_speed = alpha.value()});
    }

} // namespace tsunami::r2d
