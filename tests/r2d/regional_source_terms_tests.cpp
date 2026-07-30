#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <vector>

#include <tsunami/r2d/PositivityTimestep.hpp>
#include <tsunami/r2d/RegionalSolveLoop.hpp>
#include <tsunami/r2d/RegionalSourceTimestep.hpp>
#include <tsunami/r2d/RegionalSourceUpdate.hpp>
#include <tsunami/r2d_benchmarks/RegionalBenchmarkCases.hpp>

using Catch::Approx;

TEST_CASE("Regional source term sets validate mesh-bound Manning and Coriolis fields", "[r2d][sources]")
{
    auto benchmark = tsunami::r2d_benchmarks::make_regional_benchmark_case("lake_at_rest_flat");
    REQUIRE(benchmark.has_value());
    auto problem = std::move(benchmark).value();
    const auto cell_count = problem.mesh.summary().cell_count;

    auto sources = tsunami::r2d::make_regional_source_term_set(
        problem.mesh,
        std::vector<tsunami::core::Real>(cell_count, 0.025),
        std::vector<tsunami::core::Real>(cell_count, -1.0e-4));
    REQUIRE(sources.has_value());
    REQUIRE(sources.value().is_bound_to(problem.mesh));
    REQUIRE(sources.value().has_manning());
    REQUIRE(sources.value().has_coriolis());
    REQUIRE(sources.value().manning_coefficient()->at(0U) == Approx(0.025));
    REQUIRE(sources.value().coriolis_parameter()->at(0U) == Approx(-1.0e-4));

    auto bad_count = tsunami::r2d::make_regional_source_term_set(
        problem.mesh,
        std::vector<tsunami::core::Real>(cell_count - 1U, 0.025),
        std::nullopt);
    REQUIRE_FALSE(bad_count.has_value());

    auto negative_manning = tsunami::r2d::make_regional_source_term_set(
        problem.mesh,
        std::vector<tsunami::core::Real>(cell_count, -0.01),
        std::nullopt);
    REQUIRE_FALSE(negative_manning.has_value());
}

TEST_CASE("Exact source update damps Manning momentum and rotates Coriolis momentum", "[r2d][sources]")
{
    auto manning_benchmark = tsunami::r2d_benchmarks::make_regional_benchmark_case("uniform_manning_decay");
    REQUIRE(manning_benchmark.has_value());
    auto manning_problem = std::move(manning_benchmark).value();
    auto destination = manning_problem.simulation_state.conserved_state().clone();
    auto workspace = tsunami::r2d::make_regional_source_update_workspace(
        manning_problem.mesh,
        manning_problem.simulation_state.conserved_state());
    REQUIRE(workspace.has_value());

    auto diagnostics = tsunami::r2d::RegionalSourceUpdateDiagnostics{};
    REQUIRE(tsunami::r2d::apply_regional_local_sources(
                manning_problem.mesh,
                manning_problem.simulation_state.conserved_state(),
                manning_problem.local_sources,
                manning_problem.state_policy,
                0.5,
                destination,
                diagnostics,
                workspace.value())
                .has_value());
    const auto initial = manning_problem.simulation_state.conserved_state().local_state(tsunami::fvm::CellId{0U});
    const auto damped = destination.local_state(tsunami::fvm::CellId{0U});
    const auto speed = std::hypot(initial.momentum_x, initial.momentum_y);
    const auto k = manning_problem.state_policy.gravity * 0.035 * 0.035 /
                   std::pow(initial.depth, 7.0 / 3.0);
    REQUIRE(damped.depth == Approx(initial.depth));
    REQUIRE(damped.momentum_x == Approx(initial.momentum_x / (1.0 + k * speed * 0.5)));
    REQUIRE(damped.momentum_y == Approx(0.0).margin(1.0e-14));
    REQUIRE(diagnostics.manning_active_cell_count == manning_problem.mesh.summary().cell_count);
    REQUIRE(diagnostics.friction_kinetic_energy_removed > 0.0);

    auto coriolis_benchmark = tsunami::r2d_benchmarks::make_regional_benchmark_case("uniform_coriolis_oscillation");
    REQUIRE(coriolis_benchmark.has_value());
    auto coriolis_problem = std::move(coriolis_benchmark).value();
    auto coriolis_destination = coriolis_problem.simulation_state.conserved_state().clone();
    auto coriolis_workspace = tsunami::r2d::make_regional_source_update_workspace(
        coriolis_problem.mesh,
        coriolis_problem.simulation_state.conserved_state());
    REQUIRE(coriolis_workspace.has_value());
    auto coriolis_diagnostics = tsunami::r2d::RegionalSourceUpdateDiagnostics{};
    REQUIRE(tsunami::r2d::apply_regional_local_sources(
                coriolis_problem.mesh,
                coriolis_problem.simulation_state.conserved_state(),
                coriolis_problem.local_sources,
                coriolis_problem.state_policy,
                10.0,
                coriolis_destination,
                coriolis_diagnostics,
                coriolis_workspace.value())
                .has_value());
    const auto before = coriolis_problem.simulation_state.conserved_state().local_state(tsunami::fvm::CellId{0U});
    const auto after = coriolis_destination.local_state(tsunami::fvm::CellId{0U});
    const auto theta = 1.0e-3 * 10.0;
    REQUIRE(after.momentum_x == Approx((std::cos(theta) * before.momentum_x) + (std::sin(theta) * before.momentum_y)));
    REQUIRE(after.momentum_y == Approx((-std::sin(theta) * before.momentum_x) + (std::cos(theta) * before.momentum_y)));
    REQUIRE(coriolis_diagnostics.coriolis_active_cell_count == coriolis_problem.mesh.summary().cell_count);
    REQUIRE(coriolis_diagnostics.coriolis_kinetic_energy_error == Approx(0.0).margin(1.0e-12));
}

TEST_CASE("Source timestep estimates and stable selector identify source restrictions", "[r2d][sources][timestep]")
{
    auto benchmark = tsunami::r2d_benchmarks::make_regional_benchmark_case("uniform_manning_decay");
    REQUIRE(benchmark.has_value());
    auto problem = std::move(benchmark).value();
    auto estimate = tsunami::r2d::estimate_regional_source_timestep(
        problem.mesh,
        problem.simulation_state.conserved_state(),
        problem.local_sources,
        problem.state_policy,
        0.5,
        1.0e-12);
    REQUIRE(estimate.has_value());
    REQUIRE(estimate.value().stable_timestep.has_value());
    REQUIRE(estimate.value().limiting_cell.has_value());
    REQUIRE(estimate.value().restriction == tsunami::r2d::RegionalSourceRestrictionKind::manning);
    REQUIRE(estimate.value().maximum_manning_rate > 0.0);
    REQUIRE(estimate.value().maximum_coriolis_rate == Approx(0.0));

    const auto selected = tsunami::r2d::select_stable_explicit_timestep(
        tsunami::r2d::CflTimestepEstimate{1.0, tsunami::fvm::CellId{0U}},
        tsunami::r2d::PositivityTimestepEstimate{2.0, tsunami::fvm::CellId{0U}},
        tsunami::r2d::RelaxationTimestepEstimate{3.0, tsunami::fvm::CellId{0U}, 0.1},
        tsunami::r2d::RegionalSourceTimestepEstimate{0.25, tsunami::fvm::CellId{0U}, tsunami::r2d::RegionalSourceRestrictionKind::manning, 4.0, 0.0},
        1.0e-12);
    REQUIRE(selected.has_value());
    REQUIRE(selected.value().restriction == tsunami::r2d::TimestepRestrictionKind::source);
    REQUIRE(*selected.value().stable_timestep == Approx(0.25));
}

TEST_CASE("Regional solve loop advances source-enabled benchmark cases", "[r2d][sources][solve]")
{
    auto benchmark = tsunami::r2d_benchmarks::make_regional_benchmark_case("uniform_manning_coriolis");
    REQUIRE(benchmark.has_value());
    auto problem = std::move(benchmark).value();
    auto workspace = tsunami::r2d::make_regional_time_integration_workspace(
        problem.mesh,
        problem.simulation_state.conserved_state());
    REQUIRE(workspace.has_value());

    auto diagnostics_seen = std::size_t{};
    auto source_diagnostics_seen = false;
    auto request = tsunami::r2d::RegionalSolveRequest{
        .mesh = &problem.mesh,
        .bathymetry = &problem.bathymetry,
        .regional_boundaries = &problem.regional_boundaries,
        .relaxation_zones = &problem.relaxation_zones,
        .state_policy = problem.state_policy,
        .time_policy = problem.time_policy,
        .output_policy = tsunami::r2d::RegionalSnapshotOutputPolicy{},
        .final_time = 0.006,
        .maximum_steps = 100U,
        .diagnostics_sink = [&](const auto &diagnostics) {
            ++diagnostics_seen;
            source_diagnostics_seen = source_diagnostics_seen ||
                                      diagnostics.sources.manning_active_cell_count > 0U ||
                                      diagnostics.sources.coriolis_active_cell_count > 0U;
            return tsunami::core::success();
        },
        .snapshot_sink = {},
        .stop_token = {},
        .local_sources = &problem.local_sources};

    auto summary = tsunami::r2d::solve_regional_model(request, problem.simulation_state, workspace.value());
    REQUIRE(summary.has_value());
    REQUIRE(summary.value().termination_reason == tsunami::r2d::RegionalSolveTerminationReason::final_time_reached);
    REQUIRE(summary.value().accepted_step_count > 0U);
    REQUIRE(diagnostics_seen == summary.value().accepted_step_count);
    REQUIRE(source_diagnostics_seen);
}
