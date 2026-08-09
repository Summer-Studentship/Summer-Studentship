#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <tsunami/fvm/BoundarySpecification.hpp>
#include <tsunami/r2d/RegionalBathymetry.hpp>
#include <tsunami/r2d/RegionalSpatialReconstruction.hpp>
#include <tsunami/r2d/WellBalancedResidualEvaluation.hpp>
#include <tsunami/r2d_benchmarks/StructuredTriangularMesh.hpp>

namespace
{
    using Catch::Approx;
    using tsunami::core::Real;

    constexpr auto pi = Real{3.141592653589793238462643383279502884};

    [[nodiscard]] auto policy()
    {
        return tsunami::r2d::make_shallow_water_state_policy(9.81, 1.0e-8, 1.0e-10, 1.0e-12).value();
    }

    [[nodiscard]] auto mesh(std::size_t columns = 6U)
    {
        return tsunami::r2d_benchmarks::make_structured_triangular_mesh(
            tsunami::r2d_benchmarks::StructuredTriangularMeshSpec{"r9-reconstruction", columns, columns, 1.0, 1.0})
            .value();
    }

    [[nodiscard]] auto boundary_values(const tsunami::fvm::FiniteVolumeMesh &m, const auto &fn)
    {
        auto values = std::vector<Real>(m.summary().face_count, std::numeric_limits<Real>::quiet_NaN());
        for (const auto &face : m.topology().faces()) {
            if (face.is_boundary()) {
                const auto centroid = m.face_geometry(face.id).centroid;
                values[face.id.value] = fn(centroid.x, centroid.y);
            }
        }
        return values;
    }

    [[nodiscard]] auto cell_values(const tsunami::fvm::FiniteVolumeMesh &m, const auto &fn)
    {
        auto values = std::vector<Real>{};
        values.reserve(m.summary().cell_count);
        for (std::size_t index = 0; index < m.summary().cell_count; ++index) {
            const auto centroid = m.cell_geometry(tsunami::fvm::CellId{index}).centroid;
            values.push_back(fn(centroid.x, centroid.y));
        }
        return values;
    }

    [[nodiscard]] auto fixed_boundary_specs(
        const tsunami::fvm::FiniteVolumeMesh &m,
        std::string unit,
        const auto &value_at_face)
        -> std::vector<tsunami::fvm::BoundarySpecification<Real>>
    {
        std::vector<tsunami::fvm::BoundarySpecification<Real>> specs;
        specs.reserve(m.summary().boundary_patch_count);
        for (std::size_t index = 0; index < m.summary().boundary_patch_count; ++index) {
            const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
            const auto &patch = m.boundary_patch(patch_id);
            std::vector<Real> values;
            values.reserve(patch.faces.size());
            for (const auto face_id : patch.faces) {
                values.push_back(value_at_face(m.face_geometry(face_id).centroid));
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
        const tsunami::fvm::FiniteVolumeMesh &m,
        std::string unit,
        const auto &value_at_face)
    {
        return tsunami::fvm::make_boundary_condition_set(m, fixed_boundary_specs(m, std::move(unit), value_at_face)).value();
    }
} // namespace

TEST_CASE("Regional2D weighted least-squares gradients recover exact linear fields", "[r2d][reconstruction][r9]")
{
    const auto m = mesh();
    const auto reconstruction_policy = tsunami::r2d::RegionalReconstructionPolicy{
        .scheme = tsunami::r2d::RegionalReconstructionScheme::unlimited_linear_for_verification};

    const auto cases = std::vector<std::tuple<Real, Real, Real>>{
        {0.0, 0.0, 3.5},
        {1.0, 0.0, -0.25},
        {0.0, 1.0, 0.75},
        {1.7, -0.4, 0.2}};

    for (const auto [a, b, c] : cases) {
        auto fn = [=](Real x, Real y) { return (a * x) + (b * y) + c; };
        const auto values = cell_values(m, fn);
        const auto boundary = boundary_values(m, fn);
        const auto gradients = tsunami::r2d::compute_weighted_least_squares_gradients(m, values, boundary, reconstruction_policy).value();
        for (const auto gradient : gradients) {
            REQUIRE(gradient.valid);
            REQUIRE(gradient.x == Approx(a).margin(2.0e-13));
            REQUIRE(gradient.y == Approx(b).margin(2.0e-13));
        }
    }
}

TEST_CASE("Regional2D nonlinear gradients remain finite and approximate smooth derivatives", "[r2d][reconstruction][r9]")
{
    const auto m = mesh(16U);
    const auto reconstruction_policy = tsunami::r2d::RegionalReconstructionPolicy{
        .scheme = tsunami::r2d::RegionalReconstructionScheme::unlimited_linear_for_verification};
    auto fn = [](Real x, Real y) { return std::sin(2.0 * pi * x) * std::cos(2.0 * pi * y); };
    const auto values = cell_values(m, fn);
    const auto boundary = boundary_values(m, fn);
    const auto gradients = tsunami::r2d::compute_weighted_least_squares_gradients(m, values, boundary, reconstruction_policy).value();
    auto l2 = Real{0.0};
    auto count = std::size_t{0U};
    for (std::size_t index = 0; index < m.summary().cell_count; ++index) {
        const auto centroid = m.cell_geometry(tsunami::fvm::CellId{index}).centroid;
        if (centroid.x < 0.2 || centroid.x > 0.8 || centroid.y < 0.2 || centroid.y > 0.8) {
            continue;
        }
        REQUIRE(gradients[index].valid);
        const auto exact_x = 2.0 * pi * std::cos(2.0 * pi * centroid.x) * std::cos(2.0 * pi * centroid.y);
        const auto exact_y = -2.0 * pi * std::sin(2.0 * pi * centroid.x) * std::sin(2.0 * pi * centroid.y);
        l2 += (gradients[index].x - exact_x) * (gradients[index].x - exact_x);
        l2 += (gradients[index].y - exact_y) * (gradients[index].y - exact_y);
        ++count;
    }
    REQUIRE(count > 0U);
    REQUIRE(std::sqrt(l2 / static_cast<Real>(2U * count)) < 0.7);
}

TEST_CASE("Regional2D Barth-Jespersen limiter preserves constants and bounds sharp gradients", "[r2d][reconstruction][r9]")
{
    const auto m = mesh();
    const auto reconstruction_policy = tsunami::r2d::RegionalReconstructionPolicy{
        .scheme = tsunami::r2d::RegionalReconstructionScheme::limited_linear};

    const auto constants = cell_values(m, [](Real, Real) { return Real{2.0}; });
    const auto constant_boundary = boundary_values(m, [](Real, Real) { return Real{2.0}; });
    const auto constant_reconstruction =
        tsunami::r2d::make_regional_scalar_reconstruction(m, constants, constant_boundary, reconstruction_policy).value();
    REQUIRE(std::ranges::all_of(constant_reconstruction.limiter, [](Real phi) { return phi == Approx(1.0).margin(1.0e-14); }));

    auto sharp = cell_values(m, [](Real x, Real) { return x < 0.5 ? Real{0.0} : Real{1.0}; });
    const auto sharp_boundary = boundary_values(m, [](Real x, Real) { return x < 0.5 ? Real{0.0} : Real{1.0}; });
    const auto sharp_reconstruction =
        tsunami::r2d::make_regional_scalar_reconstruction(m, sharp, sharp_boundary, reconstruction_policy).value();
    REQUIRE(std::ranges::any_of(sharp_reconstruction.limiter, [](Real phi) { return phi < 1.0; }));
    for (std::size_t index = 0; index < m.summary().cell_count; ++index) {
        for (const auto face_id : m.cell(tsunami::fvm::CellId{index}).faces) {
            const auto reconstructed =
                tsunami::r2d::reconstruct_cell_scalar_to_face(m, tsunami::fvm::CellId{index}, face_id, sharp, sharp_reconstruction);
            REQUIRE(reconstructed >= -1.0e-14);
            REQUIRE(reconstructed <= 1.0 + 1.0e-14);
        }
    }
}

TEST_CASE("Regional2D first-order residual is preserved when reconstruction policy is default", "[r2d][reconstruction][r9]")
{
    const auto m = mesh();
    const auto p = policy();
    auto h = cell_values(m, [](Real x, Real y) { return 1.0 + 0.01 * std::sin(2.0 * pi * x) * std::cos(2.0 * pi * y); });
    auto qx = cell_values(m, [](Real x, Real) { return 0.03 * std::cos(2.0 * pi * x); });
    auto qy = cell_values(m, [](Real, Real y) { return -0.02 * std::sin(2.0 * pi * y); });
    auto bed = cell_values(m, [](Real x, Real y) { return 0.03 * x - 0.02 * y; });
    auto state = tsunami::r2d::make_regional_conserved_state(
        m, tsunami::fvm::FieldId{"h"}, tsunami::fvm::FieldId{"qx"}, tsunami::fvm::FieldId{"qy"}, h, qx, qy, p)
                     .value();
    auto bathymetry = tsunami::r2d::make_regional_bathymetry(m, tsunami::fvm::FieldId{"zb"}, "bed", bed).value();
    auto h_bc = boundary_set(m, "m", [](const auto &point) { return 1.0 + 0.01 * std::sin(2.0 * pi * point.x) * std::cos(2.0 * pi * point.y); });
    auto qx_bc = boundary_set(m, "m2/s", [](const auto &point) { return 0.03 * std::cos(2.0 * pi * point.x); });
    auto qy_bc = boundary_set(m, "m2/s", [](const auto &point) { return -0.02 * std::sin(2.0 * pi * point.y); });
    auto bed_bc = boundary_set(m, "m", [](const auto &point) { return 0.03 * point.x - 0.02 * point.y; });
    auto first = tsunami::r2d::make_regional_residual(m).value();
    auto defaulted = tsunami::r2d::make_regional_residual(m).value();
    auto spectral = tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(m, tsunami::fvm::FieldId{"s1"}, "s1", "m2/s", 0.0).value();
    auto outgoing = tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(m, tsunami::fvm::FieldId{"o1"}, "o1", "m3/s", 0.0).value();
    auto spectral_2 = tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(m, tsunami::fvm::FieldId{"s2"}, "s2", "m2/s", 0.0).value();
    auto outgoing_2 = tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(m, tsunami::fvm::FieldId{"o2"}, "o2", "m3/s", 0.0).value();
    auto workspace = tsunami::r2d::make_well_balanced_residual_workspace(m).value();
    auto workspace_2 = tsunami::r2d::make_well_balanced_residual_workspace(m).value();
    auto max_speed = Real{};
    auto max_speed_2 = Real{};
    REQUIRE(tsunami::r2d::evaluate_well_balanced_rusanov_residual(m, state, bathymetry, h_bc, qx_bc, qy_bc, bed_bc, p, first, spectral, outgoing, max_speed, workspace).has_value());
    REQUIRE(tsunami::r2d::evaluate_well_balanced_rusanov_residual(
                m,
                state,
                bathymetry,
                h_bc,
                qx_bc,
                qy_bc,
                bed_bc,
                p,
                defaulted,
                spectral_2,
                outgoing_2,
                max_speed_2,
                workspace_2,
                tsunami::r2d::RegionalReconstructionPolicy{})
                .has_value());
    for (std::size_t index = 0; index < m.summary().cell_count; ++index) {
        REQUIRE(defaulted.mass().at(index) == Approx(first.mass().at(index)).margin(1.0e-14));
        REQUIRE(defaulted.momentum_x().at(index) == Approx(first.momentum_x().at(index)).margin(1.0e-14));
        REQUIRE(defaulted.momentum_y().at(index) == Approx(first.momentum_y().at(index)).margin(1.0e-14));
    }
}

TEST_CASE("Regional2D limited-linear residual preserves lake at rest and internal conservation", "[r2d][reconstruction][r9]")
{
    const auto m = mesh(8U);
    const auto p = policy();
    constexpr auto eta0 = Real{2.0};
    auto bed = cell_values(m, [](Real x, Real y) { return 0.10 * std::sin(2.0 * pi * x) + 0.04 * std::cos(2.0 * pi * y); });
    auto depth = std::vector<Real>{};
    depth.reserve(m.summary().cell_count);
    for (const auto local_bed : bed) {
        depth.push_back(eta0 - local_bed);
    }
    auto q = std::vector<Real>(m.summary().cell_count, 0.0);
    auto state = tsunami::r2d::make_regional_conserved_state(
        m, tsunami::fvm::FieldId{"h"}, tsunami::fvm::FieldId{"qx"}, tsunami::fvm::FieldId{"qy"}, depth, q, q, p)
                     .value();
    auto bathymetry = tsunami::r2d::make_regional_bathymetry(m, tsunami::fvm::FieldId{"zb"}, "bed", bed).value();
    auto h_bc = boundary_set(m, "m", [](const auto &point) {
        return eta0 - (0.10 * std::sin(2.0 * pi * point.x) + 0.04 * std::cos(2.0 * pi * point.y));
    });
    auto q_bc = boundary_set(m, "m2/s", [](const auto &) { return Real{0.0}; });
    auto bed_bc = boundary_set(m, "m", [](const auto &point) {
        return 0.10 * std::sin(2.0 * pi * point.x) + 0.04 * std::cos(2.0 * pi * point.y);
    });
    auto residual = tsunami::r2d::make_regional_residual(m).value();
    auto spectral = tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(m, tsunami::fvm::FieldId{"s"}, "s", "m2/s", 0.0).value();
    auto outgoing = tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(m, tsunami::fvm::FieldId{"o"}, "o", "m3/s", 0.0).value();
    auto workspace = tsunami::r2d::make_well_balanced_residual_workspace(m).value();
    auto max_speed = Real{};
    const auto reconstruction_policy = tsunami::r2d::RegionalReconstructionPolicy{
        .scheme = tsunami::r2d::RegionalReconstructionScheme::limited_linear};
    REQUIRE(tsunami::r2d::evaluate_well_balanced_rusanov_residual(
                m, state, bathymetry, h_bc, q_bc, q_bc, bed_bc, p, residual, spectral, outgoing, max_speed, workspace, reconstruction_policy)
                .has_value());

    auto total_mass = Real{0.0};
    auto total_qx = Real{0.0};
    auto total_qy = Real{0.0};
    auto max_component = Real{0.0};
    for (std::size_t index = 0; index < m.summary().cell_count; ++index) {
        total_mass += residual.mass().at(index);
        total_qx += residual.momentum_x().at(index);
        total_qy += residual.momentum_y().at(index);
        max_component = std::max({max_component, std::abs(residual.mass().at(index)), std::abs(residual.momentum_x().at(index)), std::abs(residual.momentum_y().at(index))});
    }
    CAPTURE(max_component, total_mass, total_qx, total_qy);
    REQUIRE(max_component < 5.0e-12);
    REQUIRE(total_mass == Approx(0.0).margin(1.0e-12));
    REQUIRE(total_qx == Approx(0.0).margin(1.0e-12));
    REQUIRE(total_qy == Approx(0.0).margin(1.0e-12));
}
