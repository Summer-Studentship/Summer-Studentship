#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <tsunami/r2d/RegionalSolveLoop.hpp>
#include <tsunami/r2d/WellBalancedResidualEvaluation.hpp>
#include <tsunami/r2d_benchmarks/RegionalBenchmarkCases.hpp>

namespace
{
    using Catch::Approx;
    using tsunami::core::Real;
    using tsunami::fvm::BoundaryPatchId;
    using tsunami::fvm::CellId;
    using tsunami::r2d::ConservedVariables2D;

    constexpr auto tol = 1.0e-12;

    [[nodiscard]] auto lake_case()
    {
        auto result = tsunami::r2d_benchmarks::make_regional_benchmark_case("lake_at_rest_flat");
        return std::move(result).value();
    }

    [[nodiscard]] auto cell_field(const tsunami::fvm::FiniteVolumeMesh &mesh, std::string unit)
    {
        return tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(
            mesh,
            tsunami::fvm::FieldId{"boundary-test-" + unit},
            "boundary test field",
            std::move(unit),
            0.0).value();
    }
}

TEST_CASE("Regional far-field state derives depth from local bed", "[r2d][boundary]")
{
    const auto policy = tsunami::r2d::make_shallow_water_state_policy(9.81, 1.0e-6, 1.0e-8, 1.0e-12).value();
    const auto reference = tsunami::r2d::RegionalFarFieldState{.free_surface_elevation = 2.0, .velocity_x = 0.5, .velocity_y = -0.25};

    auto wet = tsunami::r2d::regional_reference_conserved_state(reference, 0.5, policy).value();
    REQUIRE(wet.depth == Approx(1.5).margin(tol));
    REQUIRE(wet.momentum_x == Approx(0.75).margin(tol));
    REQUIRE(wet.momentum_y == Approx(-0.375).margin(tol));

    auto dry = tsunami::r2d::regional_reference_conserved_state(reference, 3.0, policy).value();
    REQUIRE(dry.depth == Approx(0.0).margin(tol));
    REQUIRE(dry.momentum_x == Approx(0.0).margin(tol));
    REQUIRE_FALSE(tsunami::r2d::validate_regional_far_field_state(
                      tsunami::r2d::RegionalFarFieldState{.free_surface_elevation = std::numeric_limits<Real>::quiet_NaN()})
                      .has_value());
}

TEST_CASE("Regional boundary set mixes componentwise and physical patches", "[r2d][boundary]")
{
    auto problem = lake_case();
    auto mixed = tsunami::r2d::make_regional_boundary_condition_set(
        problem.mesh,
        problem.depth_boundaries,
        problem.momentum_x_boundaries,
        problem.momentum_y_boundaries,
        problem.bathymetry_boundaries,
        {tsunami::r2d::RegionalBoundaryOverrideSpecification{
            .patch_tag = "right",
            .operation = tsunami::r2d::RegionalRadiationSpecification{
                tsunami::r2d::RegionalFarFieldState{.free_surface_elevation = 1.0}}},
         tsunami::r2d::RegionalBoundaryOverrideSpecification{
             .patch_tag = "top",
             .operation = tsunami::r2d::RegionalTransmissiveSpecification{}}});
    REQUIRE(mixed.has_value());
    REQUIRE(mixed.value().is_complete_for(problem.mesh));

    const auto *left = mixed.value().condition(BoundaryPatchId{0});
    const auto *right = mixed.value().condition(BoundaryPatchId{1});
    const auto *top = mixed.value().condition(BoundaryPatchId{3});
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    REQUIRE(top != nullptr);
    REQUIRE(std::holds_alternative<tsunami::r2d::RegionalComponentwiseBoundary>(*left));
    REQUIRE(std::holds_alternative<tsunami::r2d::RegionalRadiationBoundary>(*right));
    REQUIRE(std::holds_alternative<tsunami::r2d::RegionalTransmissiveBoundary>(*top));

    REQUIRE_FALSE(tsunami::r2d::make_regional_boundary_condition_set(
                      problem.mesh,
                      problem.depth_boundaries,
                      problem.momentum_x_boundaries,
                      problem.momentum_y_boundaries,
                      problem.bathymetry_boundaries,
                      {tsunami::r2d::RegionalBoundaryOverrideSpecification{
                           .patch_tag = "right",
                           .operation = tsunami::r2d::RegionalTransmissiveSpecification{}},
                       tsunami::r2d::RegionalBoundaryOverrideSpecification{
                           .patch_tag = "right",
                           .operation = tsunami::r2d::RegionalTransmissiveSpecification{}}})
                      .has_value());
}

TEST_CASE("Transmissive and radiation boundaries populate exterior states", "[r2d][boundary]")
{
    auto problem = lake_case();
    auto boundaries = tsunami::r2d::make_regional_boundary_condition_set(
        problem.mesh,
        problem.depth_boundaries,
        problem.momentum_x_boundaries,
        problem.momentum_y_boundaries,
        problem.bathymetry_boundaries,
        {tsunami::r2d::RegionalBoundaryOverrideSpecification{
            .patch_tag = "right",
            .operation = tsunami::r2d::RegionalTransmissiveSpecification{}}})
                          .value();
    auto workspace = tsunami::r2d::make_regional_exterior_state_workspace(problem.mesh).value();
    REQUIRE(tsunami::r2d::populate_regional_exterior_states(
                problem.mesh,
                problem.simulation_state.conserved_state(),
                problem.bathymetry,
                boundaries,
                problem.state_policy,
                0.0,
                workspace)
                .has_value());

    const auto right = BoundaryPatchId{1};
    const auto &patch = problem.mesh.boundary_patch(right);
    for (std::size_t local = 0; local < patch.faces.size(); ++local) {
        const auto owner = problem.mesh.topology().face(patch.faces[local]).owner;
        REQUIRE(workspace.depth_patches()[right.value].at(local) == Approx(problem.simulation_state.conserved_state().depth().at(owner.value)).margin(tol));
        REQUIRE(workspace.bed_elevation_patches()[right.value].at(local) == Approx(problem.bathymetry.bed_elevation().at(owner.value)).margin(tol));
    }
}

TEST_CASE("Radiation exterior state follows characteristic regimes", "[r2d][boundary]")
{
    auto problem = lake_case();
    auto boundary = tsunami::r2d::RegionalRadiationBoundary{
        tsunami::fvm::make_mesh_binding(problem.mesh),
        BoundaryPatchId{1},
        tsunami::r2d::RegionalFarFieldState{.free_surface_elevation = 1.0}};
    const auto normal = tsunami::r2d::FaceNormal2D{1.0, 0.0, 1.0};

    auto super_out = boundary.exterior_state(ConservedVariables2D{1.0, 4.0, 0.2}, 0.0, normal, problem.state_policy, 0.0).value();
    REQUIRE(super_out.depth == Approx(1.0).margin(tol));
    REQUIRE(super_out.momentum_x == Approx(4.0).margin(tol));

    auto super_in = boundary.exterior_state(ConservedVariables2D{1.0, -4.0, 0.0}, 0.0, normal, problem.state_policy, 0.0).value();
    REQUIRE(super_in.depth == Approx(1.0).margin(tol));
    REQUIRE(super_in.momentum_x == Approx(0.0).margin(tol));

    auto subcritical = boundary.exterior_state(ConservedVariables2D{1.0, 0.1, 0.3}, 0.0, normal, problem.state_policy, 0.0).value();
    REQUIRE(subcritical.depth > 0.0);
    REQUIRE(std::isfinite(subcritical.momentum_x));
    REQUIRE(std::isfinite(subcritical.momentum_y));
}

TEST_CASE("Relaxation zones add source residual and timestep restrictions", "[r2d][relaxation]")
{
    auto problem = lake_case();
    auto zones = tsunami::r2d::make_regional_relaxation_zone_set(
        problem.mesh,
        {tsunami::r2d::PatchRelaxationZoneSpecification{
            .patch_tag = "right",
            .width = 0.4,
            .maximum_rate = 5.0,
            .profile_exponent = 2.0,
            .reference_state = tsunami::r2d::RegionalFarFieldState{.free_surface_elevation = 0.9}}})
                     .value();
    REQUIRE(zones.size() == 1U);

    auto residual = tsunami::r2d::make_regional_residual(problem.mesh).value();
    auto outgoing = cell_field(problem.mesh, "m3/s");
    auto diagnostics = tsunami::r2d::RegionalRelaxationDiagnostics{};
    REQUIRE(tsunami::r2d::apply_regional_relaxation_source(
                problem.mesh,
                problem.simulation_state.conserved_state(),
                problem.bathymetry,
                zones,
                problem.state_policy,
                residual,
                outgoing,
                diagnostics)
                .has_value());
    REQUIRE(diagnostics.zone_count == 1U);
    REQUIRE(diagnostics.active_cell_count > 0U);
    REQUIRE(diagnostics.maximum_rate > 0.0);
    REQUIRE(diagnostics.outgoing_mass_rate > 0.0);

    auto relaxation = tsunami::r2d::estimate_relaxation_timestep(problem.mesh, zones, 1.0).value();
    REQUIRE(relaxation.stable_timestep.has_value());
    auto selected = tsunami::r2d::select_stable_explicit_timestep(
                        tsunami::r2d::CflTimestepEstimate{10.0, CellId{0}},
                        tsunami::r2d::PositivityTimestepEstimate{9.0, CellId{0}},
                        relaxation,
                        1.0e-12)
                        .value();
    REQUIRE(selected.restriction == tsunami::r2d::TimestepRestrictionKind::relaxation);
}

TEST_CASE("Physical residual and solve loop preserve a flat lake at rest", "[r2d][boundary][solve]")
{
    auto problem = lake_case();
    auto relaxation = tsunami::r2d::make_regional_relaxation_zone_set(problem.mesh, {}).value();
    auto residual = tsunami::r2d::make_regional_residual(problem.mesh).value();
    auto spectral = cell_field(problem.mesh, "m2/s");
    auto outgoing = cell_field(problem.mesh, "m3/s");
    auto workspace = tsunami::r2d::make_physical_boundary_residual_workspace(problem.mesh).value();
    auto maximum_speed = Real{};

    REQUIRE(tsunami::r2d::evaluate_well_balanced_rusanov_residual(
                problem.mesh,
                problem.simulation_state.conserved_state(),
                problem.bathymetry,
                problem.regional_boundaries,
                relaxation,
                problem.state_policy,
                0.0,
                residual,
                spectral,
                outgoing,
                maximum_speed,
                workspace)
                .has_value());
    for (std::size_t index = 0; index < residual.size(); ++index) {
        REQUIRE(residual.mass().at(index) == Approx(0.0).margin(tol));
        REQUIRE(residual.momentum_x().at(index) == Approx(0.0).margin(tol));
        REQUIRE(residual.momentum_y().at(index) == Approx(0.0).margin(tol));
    }

    auto time_workspace = tsunami::r2d::make_regional_time_integration_workspace(problem.mesh, problem.simulation_state.conserved_state()).value();
    auto request = tsunami::r2d::RegionalSolveRequest{
        .mesh = &problem.mesh,
        .bathymetry = &problem.bathymetry,
        .regional_boundaries = &problem.regional_boundaries,
        .relaxation_zones = &relaxation,
        .state_policy = problem.state_policy,
        .time_policy = problem.time_policy,
        .output_policy = tsunami::r2d::RegionalSnapshotOutputPolicy{},
        .final_time = 0.01,
        .maximum_steps = 100U,
        .diagnostics_sink = {},
        .snapshot_sink = {},
        .stop_token = {},
        .local_sources = &problem.local_sources};
    auto summary = tsunami::r2d::solve_regional_model(request, problem.simulation_state, time_workspace);
    REQUIRE(summary.has_value());
    REQUIRE(summary.value().termination_reason == tsunami::r2d::RegionalSolveTerminationReason::final_time_reached);

    request.depth_boundaries = &problem.depth_boundaries;
    REQUIRE_FALSE(tsunami::r2d::solve_regional_model(request, problem.simulation_state, time_workspace).has_value());
}
