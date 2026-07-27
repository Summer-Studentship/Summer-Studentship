#include <tsunami/r2d/ShallowWaterState.hpp>

#include <cmath>
#include <string>
#include <utility>

#include <tsunami/core/Diagnostic.hpp>

namespace tsunami::r2d
{
    namespace
    {
        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        auto add_optional_real(tsunami::core::Error &error, std::string key, std::optional<tsunami::core::Real> value) -> void
        {
            if (value) {
                error.add_context(std::move(key), std::to_string(*value));
            }
        }
    } // namespace

    auto detail::r2d_error(
        std::string code,
        std::string message,
        std::string operation,
        std::string rule_id,
        const tsunami::fvm::MeshId *mesh_id,
        std::optional<tsunami::fvm::CellId> cell_id,
        std::optional<tsunami::fvm::FaceId> face_id,
        std::optional<tsunami::fvm::BoundaryPatchId> patch_id,
        std::string field_id,
        std::string expected_unit,
        std::string actual_unit,
        std::optional<tsunami::core::Real> depth,
        std::optional<tsunami::core::Real> gravity,
        std::optional<tsunami::core::Real> dry_depth,
        std::optional<tsunami::core::Real> timestep,
        std::optional<tsunami::core::Real> signal_speed,
        std::optional<tsunami::core::Real> spectral_sum) -> tsunami::core::Error
    {
        auto error = tsunami::core::Error{
            std::move(code),
            std::move(message),
            tsunami::core::DiagnosticCategory::numerical,
            tsunami::core::Severity::error};
        error.add_context("operation", std::move(operation))
            .add_context("rule_id", std::move(rule_id))
            .add_context("state_changed", "false");
        if (mesh_id != nullptr) {
            error.add_context("mesh_id", mesh_id->value);
        }
        if (cell_id) {
            error.add_context("cell_id", std::to_string(cell_id->value));
        }
        if (face_id) {
            error.add_context("face_id", std::to_string(face_id->value));
        }
        if (patch_id) {
            error.add_context("patch_id", std::to_string(patch_id->value));
        }
        if (!field_id.empty()) {
            error.add_context("field_id", std::move(field_id));
        }
        if (!expected_unit.empty()) {
            error.add_context("expected_unit", std::move(expected_unit));
        }
        if (!actual_unit.empty()) {
            error.add_context("actual_unit", std::move(actual_unit));
        }
        add_optional_real(error, "depth", depth);
        add_optional_real(error, "gravity", gravity);
        add_optional_real(error, "dry_depth", dry_depth);
        add_optional_real(error, "timestep", timestep);
        add_optional_real(error, "signal_speed", signal_speed);
        add_optional_real(error, "spectral_sum", spectral_sum);
        return error;
    }

    auto validate_policy(const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<void>
    {
        if (!finite(policy.gravity) || policy.gravity <= 0.0) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.state.policy_invalid",
                "gravity must be finite and positive",
                "validate_policy",
                "SWE-R2D-STA",
                nullptr,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                std::nullopt,
                policy.gravity));
        }
        if (!finite(policy.dry_depth) || policy.dry_depth <= 0.0) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.state.policy_invalid",
                "dry depth must be finite and positive",
                "validate_policy",
                "SWE-R2D-STA",
                nullptr,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                std::nullopt,
                policy.gravity,
                policy.dry_depth));
        }
        if (!finite(policy.depth_tolerance) || policy.depth_tolerance < 0.0 || policy.depth_tolerance > policy.dry_depth) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.state.policy_invalid",
                "depth tolerance must be finite, nonnegative and no larger than dry depth",
                "validate_policy",
                "SWE-R2D-STA",
                nullptr,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                policy.depth_tolerance,
                policy.gravity,
                policy.dry_depth));
        }
        if (!finite(policy.normal_tolerance) || policy.normal_tolerance <= 0.0) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.state.policy_invalid",
                "normal tolerance must be finite and positive",
                "validate_policy",
                "SWE-R2D-STA"));
        }
        return tsunami::core::success();
    }

    auto make_shallow_water_state_policy(
        tsunami::core::Real gravity,
        tsunami::core::Real dry_depth,
        tsunami::core::Real depth_tolerance,
        tsunami::core::Real normal_tolerance) -> tsunami::core::Result<ShallowWaterStatePolicy>
    {
        auto policy = ShallowWaterStatePolicy{
            .gravity = gravity,
            .dry_depth = dry_depth,
            .depth_tolerance = depth_tolerance,
            .normal_tolerance = normal_tolerance};
        auto validation = validate_policy(policy);
        if (!validation) {
            return tsunami::core::failure<ShallowWaterStatePolicy>(validation.error());
        }
        return tsunami::core::success(policy);
    }

    auto is_dry(ConservedVariables2D state, const ShallowWaterStatePolicy &policy) -> bool
    {
        return state.depth <= policy.dry_depth;
    }

    auto is_wet(ConservedVariables2D state, const ShallowWaterStatePolicy &policy) -> bool
    {
        return !is_dry(state, policy);
    }

    auto validate_and_canonicalise_state(
        ConservedVariables2D state,
        const ShallowWaterStatePolicy &policy,
        std::optional<tsunami::fvm::CellId> cell_id) -> tsunami::core::Result<ConservedVariables2D>
    {
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure<ConservedVariables2D>(policy_validation.error());
        }
        if (!finite(state.depth)) {
            return tsunami::core::failure<ConservedVariables2D>(detail::r2d_error(
                "r2d.state.depth_nonfinite",
                "state depth must be finite",
                "validate_and_canonicalise_state",
                "SWE-R2D-STA",
                nullptr,
                cell_id,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                state.depth));
        }
        if (!finite(state.momentum_x) || !finite(state.momentum_y)) {
            return tsunami::core::failure<ConservedVariables2D>(detail::r2d_error(
                "r2d.state.momentum_nonfinite",
                "state momenta must be finite",
                "validate_and_canonicalise_state",
                "SWE-R2D-STA",
                nullptr,
                cell_id,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                state.depth));
        }
        if (state.depth < -policy.depth_tolerance) {
            return tsunami::core::failure<ConservedVariables2D>(detail::r2d_error(
                "r2d.state.depth_negative",
                "state depth is below the accepted negative tolerance",
                "validate_and_canonicalise_state",
                "SWE-R2D-STA",
                nullptr,
                cell_id,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                state.depth));
        }
        if (state.depth <= policy.dry_depth) {
            return tsunami::core::success(ConservedVariables2D{});
        }
        return tsunami::core::success(state);
    }

    auto recover_primitive_variables(
        ConservedVariables2D state,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<PrimitiveVariables2D>
    {
        auto canonical = validate_and_canonicalise_state(state, policy);
        if (!canonical) {
            return tsunami::core::failure<PrimitiveVariables2D>(canonical.error());
        }
        const auto accepted = canonical.value();
        if (is_dry(accepted, policy)) {
            return tsunami::core::success(PrimitiveVariables2D{});
        }
        return tsunami::core::success(PrimitiveVariables2D{
            .depth = accepted.depth,
            .velocity_x = accepted.momentum_x / accepted.depth,
            .velocity_y = accepted.momentum_y / accepted.depth});
    }

} // namespace tsunami::r2d
