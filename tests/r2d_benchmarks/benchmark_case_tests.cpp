#include <catch2/catch_test_macros.hpp>

#include <tsunami/r2d/RegionalSolveLoop.hpp>
#include <tsunami/r2d_benchmarks/RegionalBenchmarkCases.hpp>

TEST_CASE("Regional benchmark cases build complete standalone solve inputs", "[r2d][benchmarks]")
{
    for (const auto case_id : tsunami::r2d_benchmarks::regional_benchmark_case_ids()) {
        auto benchmark = tsunami::r2d_benchmarks::make_regional_benchmark_case(case_id);
        REQUIRE(benchmark.has_value());
        auto problem = std::move(benchmark).value();
        REQUIRE(problem.mesh.summary().cell_count > 0U);
        REQUIRE(problem.depth_boundaries.is_complete_for(problem.mesh));
        REQUIRE(problem.momentum_x_boundaries.is_complete_for(problem.mesh));
        REQUIRE(problem.momentum_y_boundaries.is_complete_for(problem.mesh));
        REQUIRE(problem.bathymetry_boundaries.is_complete_for(problem.mesh));
    }
}

TEST_CASE("Standalone regional solve loop advances every benchmark case", "[r2d][solve][benchmarks]")
{
    for (const auto case_id : tsunami::r2d_benchmarks::regional_benchmark_case_ids()) {
        auto benchmark = tsunami::r2d_benchmarks::make_regional_benchmark_case(case_id);
        REQUIRE(benchmark.has_value());
        auto problem = std::move(benchmark).value();
        auto workspace = tsunami::r2d::make_regional_time_integration_workspace(problem.mesh, problem.simulation_state.conserved_state());
        REQUIRE(workspace.has_value());

        auto snapshots = std::size_t{0U};
        auto diagnostics = std::size_t{0U};
        auto request = tsunami::r2d::RegionalSolveRequest{
            &problem.mesh,
            &problem.bathymetry,
            &problem.depth_boundaries,
            &problem.momentum_x_boundaries,
            &problem.momentum_y_boundaries,
            &problem.bathymetry_boundaries,
            problem.state_policy,
            problem.time_policy,
            tsunami::r2d::RegionalSnapshotOutputPolicy{true, true, std::nullopt},
            problem.default_final_time,
            1000U,
            [&](const auto &) {
                ++diagnostics;
                return tsunami::core::success();
            },
            [&](const auto &snapshot) {
                ++snapshots;
                REQUIRE(snapshot.depth.size() == problem.mesh.summary().cell_count);
                return tsunami::core::success();
            }};
        auto summary = tsunami::r2d::solve_regional_model(request, problem.simulation_state, workspace.value());
        REQUIRE(summary.has_value());
        REQUIRE(summary.value().termination_reason == tsunami::r2d::RegionalSolveTerminationReason::final_time_reached);
        REQUIRE(summary.value().accepted_step_count > 0U);
        REQUIRE(diagnostics == summary.value().accepted_step_count);
        REQUIRE(snapshots == 2U);
        REQUIRE(summary.value().final_integrals.water_volume >= 0.0);
    }
}
