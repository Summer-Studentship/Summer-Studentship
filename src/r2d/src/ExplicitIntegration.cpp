#include <tsunami/r2d/ExplicitIntegration.hpp>

#include <cmath>

#include <tsunami/r2d/ShallowWaterState.hpp>

namespace tsunami::r2d
{
    auto explicit_integration_metadata(ExplicitIntegrationScheme scheme) -> ExplicitIntegrationMetadata
    {
        switch (scheme) {
        case ExplicitIntegrationScheme::forward_euler:
            return ExplicitIntegrationMetadata{scheme, "forward_euler", 1U, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};
        case ExplicitIntegrationScheme::ssprk2:
            return ExplicitIntegrationMetadata{scheme, "ssprk2", 2U, {0.5, 0.0, 0.0}, {0.5, 0.0, 0.0}};
        case ExplicitIntegrationScheme::ssprk3:
            return ExplicitIntegrationMetadata{scheme, "ssprk3", 3U, {1.0 / 3.0, 0.0, 0.0}, {2.0 / 3.0, 0.0, 0.0}};
        }
        return ExplicitIntegrationMetadata{ExplicitIntegrationScheme::forward_euler, "forward_euler", 1U, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};
    }

    auto parse_explicit_integration_scheme(std::string_view id) -> tsunami::core::Result<ExplicitIntegrationScheme>
    {
        if (id == "forward_euler" || id == "forward-euler" || id == "euler") {
            return tsunami::core::success(ExplicitIntegrationScheme::forward_euler);
        }
        if (id == "ssprk2" || id == "ssp-rk2") {
            return tsunami::core::success(ExplicitIntegrationScheme::ssprk2);
        }
        if (id == "ssprk3" || id == "ssp-rk3") {
            return tsunami::core::success(ExplicitIntegrationScheme::ssprk3);
        }
        return tsunami::core::failure<ExplicitIntegrationScheme>(detail::r2d_error(
            "r2d.integration.scheme_unknown",
            "explicit integration scheme is unknown",
            "parse_explicit_integration_scheme",
            "SWE-R2D-TIM"));
    }

    auto to_string(ExplicitIntegrationScheme scheme) -> std::string_view
    {
        return explicit_integration_metadata(scheme).id;
    }

    auto advance_scalar_decay(
        ExplicitIntegrationScheme scheme,
        tsunami::core::Real value,
        tsunami::core::Real timestep) -> tsunami::core::Result<tsunami::core::Real>
    {
        if (!std::isfinite(value) || !std::isfinite(timestep) || timestep < 0.0) {
            return tsunami::core::failure<tsunami::core::Real>(detail::r2d_error(
                "r2d.integration.scalar_invalid",
                "scalar SSPRK inputs must be finite and use a nonnegative timestep",
                "advance_scalar_decay",
                "SWE-R2D-TIM"));
        }
        switch (scheme) {
        case ExplicitIntegrationScheme::forward_euler:
            return tsunami::core::success(value * (1.0 - timestep));
        case ExplicitIntegrationScheme::ssprk2: {
            const auto y1 = value * (1.0 - timestep);
            const auto y2_euler = y1 * (1.0 - timestep);
            return tsunami::core::success((0.5 * value) + (0.5 * y2_euler));
        }
        case ExplicitIntegrationScheme::ssprk3: {
            const auto y1 = value * (1.0 - timestep);
            const auto y2_euler = y1 * (1.0 - timestep);
            const auto y2 = (0.75 * value) + (0.25 * y2_euler);
            const auto y3_euler = y2 * (1.0 - timestep);
            return tsunami::core::success((value / 3.0) + ((2.0 / 3.0) * y3_euler));
        }
        }
        return tsunami::core::failure<tsunami::core::Real>(detail::r2d_error(
            "r2d.integration.scheme_unknown",
            "explicit integration scheme is unknown",
            "advance_scalar_decay",
            "SWE-R2D-TIM"));
    }

} // namespace tsunami::r2d
