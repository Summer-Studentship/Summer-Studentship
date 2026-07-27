#pragma once

#include <optional>
#include <string>

#include <tsunami/core/Error.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/core/Types.hpp>
#include <tsunami/fvm/Mesh.hpp>
#include <tsunami/fvm/MeshRecords.hpp>

namespace tsunami::r2d
{
    struct ConservedVariables2D
    {
        tsunami::core::Real depth{};
        tsunami::core::Real momentum_x{};
        tsunami::core::Real momentum_y{};
    };

    struct PrimitiveVariables2D
    {
        tsunami::core::Real depth{};
        tsunami::core::Real velocity_x{};
        tsunami::core::Real velocity_y{};
    };

    struct ShallowWaterStatePolicy
    {
        tsunami::core::Real gravity{};
        tsunami::core::Real dry_depth{};
        tsunami::core::Real depth_tolerance{};
        tsunami::core::Real normal_tolerance{};
    };

    namespace detail
    {
        [[nodiscard]] auto r2d_error(
            std::string code,
            std::string message,
            std::string operation,
            std::string rule_id,
            const tsunami::fvm::MeshId *mesh_id = nullptr,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt,
            std::optional<tsunami::fvm::FaceId> face_id = std::nullopt,
            std::optional<tsunami::fvm::BoundaryPatchId> patch_id = std::nullopt,
            std::string field_id = {},
            std::string expected_unit = {},
            std::string actual_unit = {},
            std::optional<tsunami::core::Real> depth = std::nullopt,
            std::optional<tsunami::core::Real> gravity = std::nullopt,
            std::optional<tsunami::core::Real> dry_depth = std::nullopt,
            std::optional<tsunami::core::Real> timestep = std::nullopt,
            std::optional<tsunami::core::Real> signal_speed = std::nullopt,
            std::optional<tsunami::core::Real> spectral_sum = std::nullopt) -> tsunami::core::Error;
    } // namespace detail

    [[nodiscard]] auto validate_policy(const ShallowWaterStatePolicy &policy)
        -> tsunami::core::Result<void>;

    [[nodiscard]] auto make_shallow_water_state_policy(
        tsunami::core::Real gravity,
        tsunami::core::Real dry_depth,
        tsunami::core::Real depth_tolerance,
        tsunami::core::Real normal_tolerance) -> tsunami::core::Result<ShallowWaterStatePolicy>;

    [[nodiscard]] auto is_dry(ConservedVariables2D state, const ShallowWaterStatePolicy &policy) -> bool;
    [[nodiscard]] auto is_wet(ConservedVariables2D state, const ShallowWaterStatePolicy &policy) -> bool;

    [[nodiscard]] auto validate_and_canonicalise_state(
        ConservedVariables2D state,
        const ShallowWaterStatePolicy &policy,
        std::optional<tsunami::fvm::CellId> cell_id = std::nullopt) -> tsunami::core::Result<ConservedVariables2D>;

    [[nodiscard]] auto recover_primitive_variables(
        ConservedVariables2D state,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<PrimitiveVariables2D>;

} // namespace tsunami::r2d
