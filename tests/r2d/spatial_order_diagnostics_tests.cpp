#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <tsunami/fvm/BoundarySpecification.hpp>
#include <tsunami/r2d/RegionalBathymetry.hpp>
#include <tsunami/r2d/WellBalancedResidualEvaluation.hpp>
#include <tsunami/r2d_benchmarks/StructuredTriangularMesh.hpp>

namespace
{
    using Catch::Approx;
    using tsunami::core::Real;

    constexpr auto pi = Real{3.141592653589793238462643383279502884};

    struct ResidualErrors
    {
        Real h{};
        Real eta_l2{};
        Real qx_l2{};
        Real qy_l2{};
    };

    [[nodiscard]] auto policy()
    {
        return tsunami::r2d::make_shallow_water_state_policy(9.81, 1.0e-8, 1.0e-10, 1.0e-12).value();
    }

    [[nodiscard]] auto fixed_boundary_specs(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::string unit,
        const auto &value_at_face)
        -> std::vector<tsunami::fvm::BoundarySpecification<Real>>
    {
        std::vector<tsunami::fvm::BoundarySpecification<Real>> specs;
        specs.reserve(mesh.summary().boundary_patch_count);
        for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
            const auto &patch = mesh.boundary_patch(patch_id);
            std::vector<Real> values;
            values.reserve(patch.faces.size());
            for (const auto face_id : patch.faces) {
                values.push_back(value_at_face(mesh.face_geometry(face_id).centroid));
            }
            specs.push_back(tsunami::fvm::BoundarySpecification<Real>{
                tsunami::fvm::BoundaryConditionId{"fixed-" + patch.name + "-" + unit},
                patch.name + " fixed " + unit,
                patch.name,
                unit,
                tsunami::fvm::FixedValueSpecification<Real>{std::move(values)}});
        }
        return specs;
    }

    [[nodiscard]] auto boundary_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::string unit,
        const auto &value_at_face)
    {
        return tsunami::fvm::make_boundary_condition_set(mesh, fixed_boundary_specs(mesh, std::move(unit), value_at_face)).value();
    }

    [[nodiscard]] auto smooth_residual_error(std::size_t columns) -> ResidualErrors
    {
        constexpr auto mean_depth = Real{1.0};
        constexpr auto amplitude = Real{1.0e-4};
        constexpr auto length_x = Real{1.0};
        constexpr auto length_y = Real{0.25};
        const auto p = policy();
        const auto c = std::sqrt(p.gravity * mean_depth);
        const auto k = 2.0 * pi / length_x;
        auto mesh = tsunami::r2d_benchmarks::make_structured_triangular_mesh(
            tsunami::r2d_benchmarks::StructuredTriangularMeshSpec{
                "r6-smooth-residual-" + std::to_string(columns),
                columns,
                1U,
                length_x,
                length_y})
                        .value();

        auto eta = [&](Real x) { return amplitude * std::sin(k * x); };
        auto eta_x = [&](Real x) { return amplitude * k * std::cos(k * x); };

        std::vector<Real> depth;
        std::vector<Real> qx;
        std::vector<Real> qy(mesh.summary().cell_count, 0.0);
        std::vector<Real> bed(mesh.summary().cell_count, 0.0);
        depth.reserve(mesh.summary().cell_count);
        qx.reserve(mesh.summary().cell_count);
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto centroid = mesh.cell_geometry(tsunami::fvm::CellId{index}).centroid;
            depth.push_back(mean_depth + eta(centroid.x));
            qx.push_back(c * eta(centroid.x));
        }

        auto state = tsunami::r2d::make_regional_conserved_state(
            mesh,
            tsunami::fvm::FieldId{"h"},
            tsunami::fvm::FieldId{"qx"},
            tsunami::fvm::FieldId{"qy"},
            std::move(depth),
            std::move(qx),
            std::move(qy),
            p)
                         .value();
        auto bathymetry = tsunami::r2d::make_regional_bathymetry(mesh, tsunami::fvm::FieldId{"zb"}, "flat bed", std::move(bed)).value();
        auto h_bc = boundary_set(mesh, "m", [&](const auto &point) { return mean_depth + eta(point.x); });
        auto qx_bc = boundary_set(mesh, "m2/s", [&](const auto &point) { return c * eta(point.x); });
        auto qy_bc = boundary_set(mesh, "m2/s", [](const auto &) { return Real{0.0}; });
        auto zb_bc = boundary_set(mesh, "m", [](const auto &) { return Real{0.0}; });
        auto residual = tsunami::r2d::make_regional_residual(mesh).value();
        auto spectral = tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(
            mesh, tsunami::fvm::FieldId{"spectral"}, "spectral", "m2/s", 0.0)
                            .value();
        auto outgoing = tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(
            mesh, tsunami::fvm::FieldId{"outgoing"}, "outgoing", "m3/s", 0.0)
                            .value();
        auto workspace = tsunami::r2d::make_well_balanced_residual_workspace(mesh).value();
        auto max_speed = Real{};
        REQUIRE(tsunami::r2d::evaluate_well_balanced_rusanov_residual(
                    mesh, state, bathymetry, h_bc, qx_bc, qy_bc, zb_bc, p, residual, spectral, outgoing, max_speed, workspace)
                    .has_value());

        auto eta_sum = Real{0.0};
        auto qx_sum = Real{0.0};
        auto qy_sum = Real{0.0};
        auto area_sum = Real{0.0};
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto cell = mesh.cell_geometry(cell_id);
            if (cell.centroid.x < 0.15 || cell.centroid.x > 0.85) {
                continue;
            }
            const auto exact_mass_residual_density = c * eta_x(cell.centroid.x);
            const auto exact_qx_residual_density = p.gravity * mean_depth * eta_x(cell.centroid.x);
            const auto mass_error = (residual.mass().at(index) / cell.measure) - exact_mass_residual_density;
            const auto qx_error = (residual.momentum_x().at(index) / cell.measure) - exact_qx_residual_density;
            const auto qy_error = residual.momentum_y().at(index) / cell.measure;
            eta_sum += cell.measure * mass_error * mass_error;
            qx_sum += cell.measure * qx_error * qx_error;
            qy_sum += cell.measure * qy_error * qy_error;
            area_sum += cell.measure;
        }
        REQUIRE(area_sum > 0.0);
        return ResidualErrors{
            .h = std::sqrt((length_x * length_y) / static_cast<Real>(mesh.summary().cell_count)),
            .eta_l2 = std::sqrt(eta_sum / area_sum),
            .qx_l2 = std::sqrt(qx_sum / area_sum),
            .qy_l2 = std::sqrt(qy_sum / area_sum)};
    }

    [[nodiscard]] auto order(Real coarse_error, Real fine_error, Real coarse_h, Real fine_h) -> Real
    {
        return std::log(coarse_error / fine_error) / std::log(coarse_h / fine_h);
    }
} // namespace

TEST_CASE("Regional2D smooth residual diagnostic remains below formal first-order behaviour", "[r2d][convergence][r6]")
{
    const auto coarse = smooth_residual_error(16U);
    const auto medium = smooth_residual_error(32U);
    const auto fine = smooth_residual_error(64U);

    const auto eta_order_1 = order(coarse.eta_l2, medium.eta_l2, coarse.h, medium.h);
    const auto eta_order_2 = order(medium.eta_l2, fine.eta_l2, medium.h, fine.h);
    const auto qx_order_1 = order(coarse.qx_l2, medium.qx_l2, coarse.h, medium.h);
    const auto qx_order_2 = order(medium.qx_l2, fine.qx_l2, medium.h, fine.h);

    CAPTURE(coarse.h, medium.h, fine.h);
    CAPTURE(coarse.eta_l2, medium.eta_l2, fine.eta_l2, eta_order_1, eta_order_2);
    CAPTURE(coarse.qx_l2, medium.qx_l2, fine.qx_l2, qx_order_1, qx_order_2);
    CAPTURE(coarse.qy_l2, medium.qy_l2, fine.qy_l2);

    REQUIRE(std::isfinite(eta_order_1));
    REQUIRE(std::isfinite(eta_order_2));
    REQUIRE(std::isfinite(qx_order_1));
    REQUIRE(std::isfinite(qx_order_2));
    REQUIRE(eta_order_2 < 0.65);
    REQUIRE(qx_order_2 < 0.65);
    REQUIRE(fine.qy_l2 < fine.qx_l2);
}

TEST_CASE("Regional2D lake-at-rest update has negligible drift on a structured mesh", "[r2d][well-balanced][r6]")
{
    auto mesh = tsunami::r2d_benchmarks::make_structured_triangular_mesh(
        tsunami::r2d_benchmarks::StructuredTriangularMeshSpec{"r6-lake-at-rest", 12U, 6U, 1.0, 0.5})
                    .value();
    const auto p = policy();
    constexpr auto eta0 = Real{2.0};
    std::vector<Real> bed;
    std::vector<Real> depth;
    std::vector<Real> q(mesh.summary().cell_count, 0.0);
    bed.reserve(mesh.summary().cell_count);
    depth.reserve(mesh.summary().cell_count);
    for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
        const auto centroid = mesh.cell_geometry(tsunami::fvm::CellId{index}).centroid;
        const auto local_bed = 0.15 * std::sin(2.0 * pi * centroid.x) + 0.05 * std::cos(2.0 * pi * centroid.y);
        bed.push_back(local_bed);
        depth.push_back(eta0 - local_bed);
    }
    auto state = tsunami::r2d::make_regional_conserved_state(
        mesh, tsunami::fvm::FieldId{"h"}, tsunami::fvm::FieldId{"qx"}, tsunami::fvm::FieldId{"qy"}, std::move(depth), q, q, p)
                     .value();
    auto bathymetry = tsunami::r2d::make_regional_bathymetry(mesh, tsunami::fvm::FieldId{"zb"}, "smooth bed", std::move(bed)).value();
    auto h_bc = boundary_set(mesh, "m", [&](const auto &point) {
        const auto local_bed = 0.15 * std::sin(2.0 * pi * point.x) + 0.05 * std::cos(2.0 * pi * point.y);
        return eta0 - local_bed;
    });
    auto q_bc = boundary_set(mesh, "m2/s", [](const auto &) { return Real{0.0}; });
    auto zb_bc = boundary_set(mesh, "m", [](const auto &point) {
        return 0.15 * std::sin(2.0 * pi * point.x) + 0.05 * std::cos(2.0 * pi * point.y);
    });
    auto residual = tsunami::r2d::make_regional_residual(mesh).value();
    auto spectral = tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(
        mesh, tsunami::fvm::FieldId{"spectral"}, "spectral", "m2/s", 0.0)
                        .value();
    auto outgoing = tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(
        mesh, tsunami::fvm::FieldId{"outgoing"}, "outgoing", "m3/s", 0.0)
                        .value();
    auto workspace = tsunami::r2d::make_well_balanced_residual_workspace(mesh).value();
    auto max_speed = Real{};
    REQUIRE(tsunami::r2d::evaluate_well_balanced_rusanov_residual(
                mesh, state, bathymetry, h_bc, q_bc, q_bc, zb_bc, p, residual, spectral, outgoing, max_speed, workspace)
                .has_value());

    auto max_mass = Real{0.0};
    auto max_qx = Real{0.0};
    auto max_qy = Real{0.0};
    for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
        max_mass = std::max(max_mass, std::abs(residual.mass().at(index)));
        max_qx = std::max(max_qx, std::abs(residual.momentum_x().at(index)));
        max_qy = std::max(max_qy, std::abs(residual.momentum_y().at(index)));
    }
    CAPTURE(max_mass, max_qx, max_qy);
    REQUIRE(max_mass == Approx(0.0).margin(1.0e-12));
    REQUIRE(max_qx == Approx(0.0).margin(1.0e-12));
    REQUIRE(max_qy == Approx(0.0).margin(1.0e-12));
}
