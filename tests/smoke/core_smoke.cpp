#include <catch2/catch_test_macros.hpp>

#include "core/Constants.hpp"
#include "core/Types.hpp"

TEST_CASE("tsunami_core exposes foundation constants and types")
{
    tsunami::core::Real gravity = tsunami::core::gravity;

    REQUIRE(gravity > 9.0);
    REQUIRE(gravity < 10.0);
    REQUIRE(sizeof(tsunami::core::Index) >= sizeof(unsigned int));
}
