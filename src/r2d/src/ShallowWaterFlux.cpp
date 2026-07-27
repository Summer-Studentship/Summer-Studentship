#include <tsunami/r2d/ShallowWaterFlux.hpp>

#include <algorithm>
#include <cmath>

namespace tsunami::r2d
{
    namespace
    {
        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }
    } // namespace

    auto make_face_normal(
        tsunami::fvm::Vector3 area_vector,
        const ShallowWaterStatePolicy &policy,
        std::optional<tsunami::fvm::FaceId> face_id) -> tsunami::core::Result<FaceNormal2D>
    {
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure<FaceNormal2D>(policy_validation.error());
        }
        if (!finite(area_vector.x) || !finite(area_vector.y) || !finite(area_vector.z)) {
            return tsunami::core::failure<FaceNormal2D>(detail::r2d_error(
                "r2d.flux.normal_invalid",
                "face area vector components must be finite",
                "make_face_normal",
                "SWE-R2D-FLX",
                nullptr,
                std::nullopt,
                face_id));
        }
        if (std::abs(area_vector.z) > policy.normal_tolerance) {
            return tsunami::core::failure<FaceNormal2D>(detail::r2d_error(
                "r2d.flux.normal_invalid",
                "face area vector must be planar",
                "make_face_normal",
                "SWE-R2D-FLX",
                nullptr,
                std::nullopt,
                face_id));
        }
        const auto length = std::sqrt((area_vector.x * area_vector.x) + (area_vector.y * area_vector.y));
        if (!finite(length) || length <= 0.0) {
            return tsunami::core::failure<FaceNormal2D>(detail::r2d_error(
                "r2d.flux.normal_invalid",
                "face area vector magnitude must be finite and positive",
                "make_face_normal",
                "SWE-R2D-FLX",
                nullptr,
                std::nullopt,
                face_id));
        }
        const auto normal = FaceNormal2D{.x = area_vector.x / length, .y = area_vector.y / length, .length = length};
        const auto magnitude = std::sqrt((normal.x * normal.x) + (normal.y * normal.y));
        if (!finite(magnitude) || std::abs(magnitude - 1.0) > policy.normal_tolerance) {
            return tsunami::core::failure<FaceNormal2D>(detail::r2d_error(
                "r2d.flux.normal_invalid",
                "normalised face normal is not unit length",
                "make_face_normal",
                "SWE-R2D-FLX",
                nullptr,
                std::nullopt,
                face_id));
        }
        return tsunami::core::success(normal);
    }

    auto negated(FaceNormal2D normal) -> FaceNormal2D
    {
        return FaceNormal2D{.x = -normal.x, .y = -normal.y, .length = normal.length};
    }

    auto physical_normal_flux(
        ConservedVariables2D state,
        FaceNormal2D normal,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<ShallowWaterFlux2D>
    {
        auto canonical = validate_and_canonicalise_state(state, policy);
        if (!canonical) {
            return tsunami::core::failure<ShallowWaterFlux2D>(detail::r2d_error(
                "r2d.flux.state_invalid",
                "state is invalid for physical flux evaluation",
                "physical_normal_flux",
                "SWE-R2D-FLX")
                    .with_cause_code(canonical.error().code()));
        }
        if (!finite(normal.x) || !finite(normal.y) || !finite(normal.length) || normal.length <= 0.0) {
            return tsunami::core::failure<ShallowWaterFlux2D>(detail::r2d_error(
                "r2d.flux.normal_invalid",
                "normal must be finite and carry a positive face length",
                "physical_normal_flux",
                "SWE-R2D-FLX"));
        }
        const auto accepted = canonical.value();
        if (is_dry(accepted, policy)) {
            return tsunami::core::success(ShallowWaterFlux2D{});
        }
        auto primitive = recover_primitive_variables(accepted, policy);
        if (!primitive) {
            return tsunami::core::failure<ShallowWaterFlux2D>(primitive.error());
        }
        const auto un = (primitive.value().velocity_x * normal.x) + (primitive.value().velocity_y * normal.y);
        const auto pressure = 0.5 * policy.gravity * accepted.depth * accepted.depth;
        const auto flux = ShallowWaterFlux2D{
            .mass = accepted.depth * un,
            .momentum_x = (accepted.momentum_x * un) + (pressure * normal.x),
            .momentum_y = (accepted.momentum_y * un) + (pressure * normal.y)};
        if (!finite(flux.mass) || !finite(flux.momentum_x) || !finite(flux.momentum_y)) {
            return tsunami::core::failure<ShallowWaterFlux2D>(detail::r2d_error(
                "r2d.flux.result_nonfinite",
                "physical flux result must be finite",
                "physical_normal_flux",
                "SWE-R2D-FLX",
                nullptr,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                accepted.depth,
                policy.gravity));
        }
        return tsunami::core::success(flux);
    }

    auto characteristic_signal_speed(
        ConservedVariables2D state,
        FaceNormal2D normal,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<tsunami::core::Real>
    {
        auto canonical = validate_and_canonicalise_state(state, policy);
        if (!canonical) {
            return tsunami::core::failure<tsunami::core::Real>(canonical.error());
        }
        const auto accepted = canonical.value();
        if (is_dry(accepted, policy)) {
            return tsunami::core::success(0.0);
        }
        auto primitive = recover_primitive_variables(accepted, policy);
        if (!primitive) {
            return tsunami::core::failure<tsunami::core::Real>(primitive.error());
        }
        const auto un = (primitive.value().velocity_x * normal.x) + (primitive.value().velocity_y * normal.y);
        const auto wave_speed = std::sqrt(policy.gravity * accepted.depth);
        const auto speed = std::abs(un) + wave_speed;
        if (!finite(speed)) {
            return tsunami::core::failure<tsunami::core::Real>(detail::r2d_error(
                "r2d.flux.signal_speed_nonfinite",
                "characteristic signal speed must be finite",
                "characteristic_signal_speed",
                "SWE-R2D-FLX",
                nullptr,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                {},
                {},
                {},
                accepted.depth,
                policy.gravity));
        }
        return tsunami::core::success(speed);
    }

    auto maximum_characteristic_signal_speed(
        ConservedVariables2D left,
        ConservedVariables2D right,
        FaceNormal2D normal,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<tsunami::core::Real>
    {
        auto left_speed = characteristic_signal_speed(left, normal, policy);
        if (!left_speed) {
            return tsunami::core::failure<tsunami::core::Real>(left_speed.error());
        }
        auto right_speed = characteristic_signal_speed(right, normal, policy);
        if (!right_speed) {
            return tsunami::core::failure<tsunami::core::Real>(right_speed.error());
        }
        return tsunami::core::success(std::max(left_speed.value(), right_speed.value()));
    }

} // namespace tsunami::r2d
