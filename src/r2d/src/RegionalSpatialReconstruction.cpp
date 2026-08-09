#include <tsunami/r2d/RegionalSpatialReconstruction.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include <tsunami/r2d/RegionalDiagnostics.hpp>

namespace tsunami::r2d
{
    namespace
    {
        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto finite_boundary_value(tsunami::core::Real value) -> bool
        {
            return finite(value);
        }

        [[nodiscard]] auto mesh_id(const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::fvm::MeshId
        {
            return mesh.summary().id;
        }

        [[nodiscard]] auto reconstruction_error(
            std::string message,
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt) -> tsunami::core::Error
        {
            const auto id = mesh_id(mesh);
            return detail::r2d_error(
                "r2d.reconstruction.invalid",
                std::move(message),
                "RegionalSpatialReconstruction",
                "SWE-R2D-RECON",
                &id,
                cell_id);
        }

        [[nodiscard]] auto other_cell(const tsunami::fvm::FaceRecord &face, tsunami::fvm::CellId cell_id)
            -> std::optional<tsunami::fvm::CellId>
        {
            if (face.owner == cell_id) {
                return face.neighbour;
            }
            if (face.neighbour && *face.neighbour == cell_id) {
                return face.owner;
            }
            return std::nullopt;
        }

        struct ScalarSample
        {
            tsunami::core::Real x{};
            tsunami::core::Real y{};
            tsunami::core::Real value{};
        };

        [[nodiscard]] auto face_sample(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::fvm::FaceRecord &face,
            tsunami::fvm::CellId cell_id,
            std::span<const tsunami::core::Real> cell_values,
            std::span<const tsunami::core::Real> boundary_face_values) -> std::optional<ScalarSample>
        {
            if (auto neighbour = other_cell(face, cell_id)) {
                const auto centroid = mesh.cell_geometry(*neighbour).centroid;
                return ScalarSample{.x = centroid.x, .y = centroid.y, .value = cell_values[neighbour->value]};
            }
            const auto boundary_value = boundary_face_values[face.id.value];
            if (!finite_boundary_value(boundary_value)) {
                return std::nullopt;
            }
            const auto centroid = mesh.face_geometry(face.id).centroid;
            return ScalarSample{.x = centroid.x, .y = centroid.y, .value = boundary_value};
        }

        [[nodiscard]] auto safe_limiter_ratio(tsunami::core::Real bound_delta, tsunami::core::Real face_delta) -> tsunami::core::Real
        {
            if (face_delta == 0.0) {
                return 1.0;
            }
            return std::clamp(bound_delta / face_delta, tsunami::core::Real{0.0}, tsunami::core::Real{1.0});
        }
    } // namespace

    auto validate_reconstruction_policy(const RegionalReconstructionPolicy &policy) -> tsunami::core::Result<void>
    {
        if (!finite(policy.conditioning_tolerance) || policy.conditioning_tolerance < 0.0 ||
            !finite(policy.minimum_distance_weight_denominator) || policy.minimum_distance_weight_denominator <= 0.0) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.reconstruction.policy_invalid",
                "reconstruction policy tolerances must be finite and non-negative",
                "validate_reconstruction_policy",
                "SWE-R2D-RECON"));
        }
        return tsunami::core::success();
    }

    auto compute_weighted_least_squares_gradients(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::span<const tsunami::core::Real> cell_values,
        std::span<const tsunami::core::Real> boundary_face_values,
        const RegionalReconstructionPolicy &policy) -> tsunami::core::Result<std::vector<RegionalGradient2D>>
    {
        auto policy_validation = validate_reconstruction_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure<std::vector<RegionalGradient2D>>(policy_validation.error());
        }
        if (cell_values.size() != mesh.summary().cell_count || boundary_face_values.size() != mesh.summary().face_count) {
            return tsunami::core::failure<std::vector<RegionalGradient2D>>(
                reconstruction_error("reconstruction inputs are not sized for the supplied mesh", mesh));
        }
        auto gradients = std::vector<RegionalGradient2D>(mesh.summary().cell_count);
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto center = mesh.cell_geometry(cell_id).centroid;
            const auto center_value = cell_values[index];
            if (!finite(center.x) || !finite(center.y) || !finite(center_value)) {
                return tsunami::core::failure<std::vector<RegionalGradient2D>>(
                    reconstruction_error("cell centroid and scalar value must be finite", mesh, cell_id));
            }

            auto a00 = tsunami::core::Real{0.0};
            auto a01 = tsunami::core::Real{0.0};
            auto a11 = tsunami::core::Real{0.0};
            auto b0 = tsunami::core::Real{0.0};
            auto b1 = tsunami::core::Real{0.0};
            auto sample_count = std::size_t{0U};
            for (const auto face_id : mesh.cell(cell_id).faces) {
                const auto &face = mesh.face(face_id);
                const auto sample = face_sample(mesh, face, cell_id, cell_values, boundary_face_values);
                if (!sample || !finite(sample->value)) {
                    continue;
                }
                const auto dx = sample->x - center.x;
                const auto dy = sample->y - center.y;
                const auto distance2 = (dx * dx) + (dy * dy);
                if (!finite(distance2) || distance2 <= 0.0) {
                    continue;
                }
                const auto weight = 1.0 / std::max(distance2, policy.minimum_distance_weight_denominator);
                const auto delta = sample->value - center_value;
                a00 += weight * dx * dx;
                a01 += weight * dx * dy;
                a11 += weight * dy * dy;
                b0 += weight * dx * delta;
                b1 += weight * dy * delta;
                ++sample_count;
            }
            if (sample_count < 2U) {
                gradients[index] = RegionalGradient2D{};
                continue;
            }
            const auto determinant = (a00 * a11) - (a01 * a01);
            const auto scale = std::max({std::abs(a00), std::abs(a01), std::abs(a11), tsunami::core::Real{1.0}});
            if (!finite(determinant) || std::abs(determinant) <= policy.conditioning_tolerance * scale * scale) {
                gradients[index] = RegionalGradient2D{};
                continue;
            }
            gradients[index] = RegionalGradient2D{
                .x = ((a11 * b0) - (a01 * b1)) / determinant,
                .y = ((a00 * b1) - (a01 * b0)) / determinant,
                .valid = true};
            if (!finite(gradients[index].x) || !finite(gradients[index].y)) {
                return tsunami::core::failure<std::vector<RegionalGradient2D>>(
                    reconstruction_error("weighted least-squares gradient must be finite", mesh, cell_id));
            }
        }
        return tsunami::core::success(std::move(gradients));
    }

    auto compute_barth_jespersen_limiters(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::span<const tsunami::core::Real> cell_values,
        std::span<const tsunami::core::Real> boundary_face_values,
        std::span<const RegionalGradient2D> gradients) -> tsunami::core::Result<std::vector<tsunami::core::Real>>
    {
        if (cell_values.size() != mesh.summary().cell_count || gradients.size() != mesh.summary().cell_count ||
            boundary_face_values.size() != mesh.summary().face_count) {
            return tsunami::core::failure<std::vector<tsunami::core::Real>>(
                reconstruction_error("limiter inputs are not sized for the supplied mesh", mesh));
        }
        auto limiter = std::vector<tsunami::core::Real>(mesh.summary().cell_count, 1.0);
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto center = mesh.cell_geometry(cell_id).centroid;
            const auto center_value = cell_values[index];
            auto local_min = center_value;
            auto local_max = center_value;
            for (const auto face_id : mesh.cell(cell_id).faces) {
                const auto sample = face_sample(mesh, mesh.face(face_id), cell_id, cell_values, boundary_face_values);
                if (!sample || !finite(sample->value)) {
                    continue;
                }
                local_min = std::min(local_min, sample->value);
                local_max = std::max(local_max, sample->value);
            }
            if (!gradients[index].valid) {
                limiter[index] = 0.0;
                continue;
            }
            auto phi = tsunami::core::Real{1.0};
            for (const auto face_id : mesh.cell(cell_id).faces) {
                const auto face_centroid = mesh.face_geometry(face_id).centroid;
                const auto delta =
                    gradients[index].x * (face_centroid.x - center.x) +
                    gradients[index].y * (face_centroid.y - center.y);
                if (delta > 0.0) {
                    phi = std::min(phi, safe_limiter_ratio(local_max - center_value, delta));
                } else if (delta < 0.0) {
                    phi = std::min(phi, safe_limiter_ratio(local_min - center_value, delta));
                }
            }
            limiter[index] = std::clamp(phi, tsunami::core::Real{0.0}, tsunami::core::Real{1.0});
        }
        return tsunami::core::success(std::move(limiter));
    }

    auto make_regional_scalar_reconstruction(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::span<const tsunami::core::Real> cell_values,
        std::span<const tsunami::core::Real> boundary_face_values,
        const RegionalReconstructionPolicy &policy) -> tsunami::core::Result<RegionalScalarReconstruction>
    {
        if (policy.scheme == RegionalReconstructionScheme::first_order) {
            return tsunami::core::success(RegionalScalarReconstruction{
                .gradients = std::vector<RegionalGradient2D>(mesh.summary().cell_count),
                .limiter = std::vector<tsunami::core::Real>(mesh.summary().cell_count, 0.0)});
        }
        auto gradients = compute_weighted_least_squares_gradients(mesh, cell_values, boundary_face_values, policy);
        if (!gradients) {
            return tsunami::core::failure<RegionalScalarReconstruction>(gradients.error());
        }
        auto limiter = std::vector<tsunami::core::Real>(mesh.summary().cell_count, 1.0);
        if (policy.scheme == RegionalReconstructionScheme::limited_linear) {
            auto limited = compute_barth_jespersen_limiters(mesh, cell_values, boundary_face_values, gradients.value());
            if (!limited) {
                return tsunami::core::failure<RegionalScalarReconstruction>(limited.error());
            }
            limiter = std::move(limited).value();
        }
        return tsunami::core::success(RegionalScalarReconstruction{.gradients = std::move(gradients).value(), .limiter = std::move(limiter)});
    }

    auto reconstruct_cell_scalar_to_face(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::CellId cell_id,
        tsunami::fvm::FaceId face_id,
        std::span<const tsunami::core::Real> cell_values,
        const RegionalScalarReconstruction &reconstruction) -> tsunami::core::Real
    {
        const auto center = mesh.cell_geometry(cell_id).centroid;
        const auto face = mesh.face_geometry(face_id).centroid;
        const auto gradient = reconstruction.gradients[cell_id.value];
        if (!gradient.valid) {
            return cell_values[cell_id.value];
        }
        const auto phi = reconstruction.limiter[cell_id.value];
        return cell_values[cell_id.value] + phi * (gradient.x * (face.x - center.x) + gradient.y * (face.y - center.y));
    }

} // namespace tsunami::r2d
