#pragma once

#include <array>
#include <string_view>

#include <tsunami/core/Result.hpp>
#include <tsunami/core/Types.hpp>

namespace tsunami::r2d
{
    enum class ExplicitIntegrationScheme
    {
        forward_euler,
        ssprk2,
        ssprk3
    };

    struct ExplicitIntegrationMetadata
    {
        ExplicitIntegrationScheme scheme{ExplicitIntegrationScheme::forward_euler};
        std::string_view id;
        std::size_t stage_count{};
        std::array<tsunami::core::Real, 3> final_state_weight{};
        std::array<tsunami::core::Real, 3> final_euler_weight{};
    };

    [[nodiscard]] auto explicit_integration_metadata(ExplicitIntegrationScheme scheme)
        -> ExplicitIntegrationMetadata;

    [[nodiscard]] auto parse_explicit_integration_scheme(std::string_view id)
        -> tsunami::core::Result<ExplicitIntegrationScheme>;

    [[nodiscard]] auto to_string(ExplicitIntegrationScheme scheme) -> std::string_view;

    [[nodiscard]] auto advance_scalar_decay(
        ExplicitIntegrationScheme scheme,
        tsunami::core::Real value,
        tsunami::core::Real timestep) -> tsunami::core::Result<tsunami::core::Real>;

} // namespace tsunami::r2d
