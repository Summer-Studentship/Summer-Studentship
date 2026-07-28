#include <tsunami/r2d/HydrostaticReconstruction.hpp>

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

        [[nodiscard]] auto correction(
            tsunami::core::Real gravity,
            tsunami::core::Real original_depth,
            tsunami::core::Real reconstructed_depth,
            FaceNormal2D normal) -> ShallowWaterFlux2D
        {
            const auto pressure = 0.5 * gravity * ((original_depth * original_depth) - (reconstructed_depth * reconstructed_depth));
            return ShallowWaterFlux2D{.mass = 0.0, .momentum_x = pressure * normal.x, .momentum_y = pressure * normal.y};
        }

        [[nodiscard]] auto valid_flux(ShallowWaterFlux2D flux) -> bool
        {
            return finite(flux.mass) && finite(flux.momentum_x) && finite(flux.momentum_y);
        }
    } // namespace

    auto hydrostatic_reconstruction(
        ConservedVariables2D left,
        ConservedVariables2D right,
        tsunami::core::Real left_bed_elevation,
        tsunami::core::Real right_bed_elevation,
        FaceNormal2D normal,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<HydrostaticReconstructionResult>
    {
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure<HydrostaticReconstructionResult>(policy_validation.error());
        }
        auto left_state = validate_and_canonicalise_state(left, policy);
        if (!left_state) {
            return tsunami::core::failure<HydrostaticReconstructionResult>(detail::r2d_error(
                "r2d.hydrostatic.state_invalid",
                "left state is invalid for hydrostatic reconstruction",
                "hydrostatic_reconstruction",
                "SWE-R2D-WB")
                    .with_cause_code(left_state.error().code()));
        }
        auto right_state = validate_and_canonicalise_state(right, policy);
        if (!right_state) {
            return tsunami::core::failure<HydrostaticReconstructionResult>(detail::r2d_error(
                "r2d.hydrostatic.state_invalid",
                "right state is invalid for hydrostatic reconstruction",
                "hydrostatic_reconstruction",
                "SWE-R2D-WB")
                    .with_cause_code(right_state.error().code()));
        }
        if (!finite(left_bed_elevation) || !finite(right_bed_elevation)) {
            return tsunami::core::failure<HydrostaticReconstructionResult>(detail::r2d_error(
                "r2d.hydrostatic.bed_nonfinite",
                "bed elevations must be finite",
                "hydrostatic_reconstruction",
                "SWE-R2D-WB"));
        }
        if (!finite(normal.x) || !finite(normal.y) || !finite(normal.length) || normal.length <= 0.0) {
            return tsunami::core::failure<HydrostaticReconstructionResult>(detail::r2d_error(
                "r2d.hydrostatic.normal_invalid",
                "normal must be finite and carry a positive length",
                "hydrostatic_reconstruction",
                "SWE-R2D-WB"));
        }

        const auto accepted_left = left_state.value();
        const auto accepted_right = right_state.value();
        const auto left_eta = accepted_left.depth + left_bed_elevation;
        const auto right_eta = accepted_right.depth + right_bed_elevation;
        const auto interface_bed = std::max(left_bed_elevation, right_bed_elevation);
        const auto reconstructed_left_depth = std::max(tsunami::core::Real{0.0}, left_eta - interface_bed);
        const auto reconstructed_right_depth = std::max(tsunami::core::Real{0.0}, right_eta - interface_bed);

        auto left_primitive = recover_primitive_variables(accepted_left, policy);
        auto right_primitive = recover_primitive_variables(accepted_right, policy);
        if (!left_primitive || !right_primitive) {
            return tsunami::core::failure<HydrostaticReconstructionResult>(detail::r2d_error(
                "r2d.hydrostatic.state_invalid",
                "primitive recovery failed",
                "hydrostatic_reconstruction",
                "SWE-R2D-WB"));
        }
        auto result = HydrostaticReconstructionResult{
            .left = ConservedVariables2D{
                .depth = reconstructed_left_depth,
                .momentum_x = reconstructed_left_depth > policy.dry_depth ? reconstructed_left_depth * left_primitive.value().velocity_x : 0.0,
                .momentum_y = reconstructed_left_depth > policy.dry_depth ? reconstructed_left_depth * left_primitive.value().velocity_y : 0.0},
            .right = ConservedVariables2D{
                .depth = reconstructed_right_depth,
                .momentum_x = reconstructed_right_depth > policy.dry_depth ? reconstructed_right_depth * right_primitive.value().velocity_x : 0.0,
                .momentum_y = reconstructed_right_depth > policy.dry_depth ? reconstructed_right_depth * right_primitive.value().velocity_y : 0.0},
            .interface_bed_elevation = interface_bed,
            .left_pressure_correction = correction(policy.gravity, accepted_left.depth, reconstructed_left_depth, normal),
            .right_pressure_correction = correction(policy.gravity, accepted_right.depth, reconstructed_right_depth, normal)};

        auto left_reconstructed = validate_and_canonicalise_state(result.left, policy);
        auto right_reconstructed = validate_and_canonicalise_state(result.right, policy);
        if (!left_reconstructed || !right_reconstructed || !finite(result.interface_bed_elevation) ||
            !valid_flux(result.left_pressure_correction) || !valid_flux(result.right_pressure_correction)) {
            return tsunami::core::failure<HydrostaticReconstructionResult>(detail::r2d_error(
                "r2d.hydrostatic.result_nonfinite",
                "hydrostatic reconstruction result must be finite and admissible",
                "hydrostatic_reconstruction",
                "SWE-R2D-WB"));
        }
        result.left = left_reconstructed.value();
        result.right = right_reconstructed.value();
        return tsunami::core::success(result);
    }

} // namespace tsunami::r2d
