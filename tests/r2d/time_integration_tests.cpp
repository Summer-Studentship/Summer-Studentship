#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <tsunami/r2d/ExplicitIntegration.hpp>
#include <tsunami/r2d/RegionalTimeIntegration.hpp>

namespace
{
    using Catch::Approx;
}

TEST_CASE("SSPRK scalar decay utility exposes authoritative coefficients", "[r2d][time]")
{
    const auto euler = tsunami::r2d::advance_scalar_decay(tsunami::r2d::ExplicitIntegrationScheme::forward_euler, 1.0, 0.1).value();
    const auto rk2 = tsunami::r2d::advance_scalar_decay(tsunami::r2d::ExplicitIntegrationScheme::ssprk2, 1.0, 0.1).value();
    const auto rk3 = tsunami::r2d::advance_scalar_decay(tsunami::r2d::ExplicitIntegrationScheme::ssprk3, 1.0, 0.1).value();

    REQUIRE(euler == Approx(0.9));
    REQUIRE(rk2 == Approx(0.905));
    REQUIRE(rk3 == Approx(0.9048333333333334));
    REQUIRE(std::abs(rk3 - std::exp(-0.1)) < std::abs(euler - std::exp(-0.1)));
}

TEST_CASE("Regional time integration policy rejects unsafe timestep controls", "[r2d][time]")
{
    REQUIRE(tsunami::r2d::make_regional_time_integration_policy(
                tsunami::r2d::ExplicitIntegrationScheme::ssprk3,
                0.45,
                0.95,
                1.0e-10,
                0.01)
                .has_value());
    REQUIRE_FALSE(tsunami::r2d::make_regional_time_integration_policy(
                      tsunami::r2d::ExplicitIntegrationScheme::ssprk3,
                      1.2,
                      0.95,
                      1.0e-10,
                      0.01)
                      .has_value());
}
