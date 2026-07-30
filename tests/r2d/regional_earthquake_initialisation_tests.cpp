#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#include <tsunami/r2d/RegionalEarthquakeInitialisation.hpp>
#include <tsunami/r2d/RegionalSolveLoop.hpp>
#include <tsunami/r2d_benchmarks/RegionalBenchmarkCases.hpp>
#include <tsunami/r2d_io/RegionalCsvOutputWriter.hpp>

using Catch::Approx;

namespace
{
    using Real = tsunami::core::Real;

    [[nodiscard]] auto metadata(std::string id = "synthetic-test")
    {
        return tsunami::r2d::RegionalEarthquakeSourceMetadata{
            .source_kind = tsunami::r2d::RegionalEarthquakeSourceKind::synthetic,
            .event_id = std::move(id),
            .model_id = "model",
            .source_format = "programmatic",
            .coordinate_reference = "mesh-cartesian",
            .subfault_count = 0U};
    }

    [[nodiscard]] auto make_problem(std::string_view id = "lake_at_rest_flat")
    {
        auto benchmark = tsunami::r2d_benchmarks::make_regional_benchmark_case(id);
        REQUIRE(benchmark.has_value());
        return std::move(benchmark).value();
    }

    [[nodiscard]] auto initialise(
        tsunami::r2d_benchmarks::RegionalBenchmarkCase &problem,
        tsunami::r2d::RegionalSeabedDisplacement &displacement,
        tsunami::r2d::RegionalBedDeformationMappingKind mapping,
        tsunami::r2d::RegionalSurfaceTransferKind transfer,
        const tsunami::fvm::CellScalarField *prescribed = nullptr)
    {
        auto workspace = tsunami::r2d::make_regional_earthquake_initialisation_workspace(problem.mesh);
        REQUIRE(workspace.has_value());
        auto request = tsunami::r2d::RegionalEarthquakeInitialisationRequest{
            .mesh = &problem.mesh,
            .pre_event_bathymetry = &problem.bathymetry,
            .pre_event_state = &problem.simulation_state.conserved_state(),
            .seabed_displacement = &displacement,
            .bed_mapping = mapping,
            .surface_transfer = transfer,
            .bathymetry_boundaries = &problem.bathymetry_boundaries,
            .prescribed_surface_perturbation = prescribed,
            .state_policy = problem.state_policy,
            .zero_momentum_tolerance = 1.0e-12,
            .metadata = metadata()};
        return tsunami::r2d::initialise_regional_earthquake_state(request, workspace.value());
    }

    [[nodiscard]] auto solve_benchmark(tsunami::r2d_benchmarks::RegionalBenchmarkCase &problem)
    {
        auto workspace = tsunami::r2d::make_regional_time_integration_workspace(
            problem.mesh,
            problem.simulation_state.conserved_state());
        REQUIRE(workspace.has_value());
        auto request = tsunami::r2d::RegionalSolveRequest{
            .mesh = &problem.mesh,
            .bathymetry = &problem.bathymetry,
            .regional_boundaries = &problem.regional_boundaries,
            .relaxation_zones = &problem.relaxation_zones,
            .state_policy = problem.state_policy,
            .time_policy = problem.time_policy,
            .output_policy = tsunami::r2d::RegionalSnapshotOutputPolicy{false, false, std::nullopt},
            .final_time = problem.default_final_time,
            .maximum_steps = 1000U,
            .diagnostics_sink = {},
            .snapshot_sink = {},
            .local_sources = &problem.local_sources};
        return tsunami::r2d::solve_regional_model(request, problem.simulation_state, workspace.value());
    }

    [[nodiscard]] auto radial_outflow_metric(
        const tsunami::r2d_benchmarks::RegionalBenchmarkCase &problem,
        Real centre_x,
        Real centre_y) -> Real
    {
        auto total = Real{0.0};
        const auto &state = problem.simulation_state.conserved_state();
        for (std::size_t index = 0; index < problem.mesh.summary().cell_count; ++index) {
            const auto cell = tsunami::fvm::CellId{index};
            const auto centroid = problem.mesh.cell_geometry(cell).centroid;
            const auto dx = centroid.x - centre_x;
            const auto dy = centroid.y - centre_y;
            const auto radius = std::hypot(dx, dy);
            if (radius <= 1.0e-12) {
                continue;
            }
            const auto projection = (state.momentum_x().at(index) * dx + state.momentum_y().at(index) * dy) / radius;
            total += projection * problem.mesh.cell_geometry(cell).measure;
        }
        return total;
    }

} // namespace

TEST_CASE("Regional seabed displacement validates three immutable components", "[r2d][earthquake][seabed displacement]")
{
    static_assert(!std::is_copy_constructible_v<tsunami::r2d::RegionalSeabedDisplacement>);
    auto problem = make_problem();
    const auto count = problem.mesh.summary().cell_count;

    auto displacement = tsunami::r2d::make_regional_seabed_displacement(
        problem.mesh,
        std::vector<Real>(count, 0.1),
        std::vector<Real>(count, -0.2),
        std::vector<Real>(count, 0.3));
    REQUIRE(displacement.has_value());
    REQUIRE(displacement.value().is_bound_to(problem.mesh));
    REQUIRE(displacement.value().local_displacement(tsunami::fvm::CellId{0U}).eastward == Approx(0.1));
    REQUIRE(displacement.value().local_displacement(tsunami::fvm::CellId{0U}).northward == Approx(-0.2));
    REQUIRE(displacement.value().local_displacement(tsunami::fvm::CellId{0U}).upward == Approx(0.3));

    auto clone = displacement.value().clone();
    REQUIRE(clone.local_displacement(tsunami::fvm::CellId{0U}).upward == Approx(0.3));
    REQUIRE(tsunami::r2d::make_zero_regional_seabed_displacement(problem.mesh).has_value());
    REQUIRE_FALSE(tsunami::r2d::make_regional_seabed_displacement(
                      problem.mesh,
                      std::vector<Real>(count - 1U, 0.0),
                      std::vector<Real>(count, 0.0),
                      std::vector<Real>(count, 0.0))
                      .has_value());
    auto bad = std::vector<Real>(count, 0.0);
    bad[0] = std::numeric_limits<Real>::quiet_NaN();
    REQUIRE_FALSE(tsunami::r2d::make_vertical_regional_seabed_displacement(problem.mesh, std::move(bad)).has_value());
}

TEST_CASE("Earthquake metadata validation preserves stable provenance semantics", "[r2d][earthquake][metadata]")
{
    REQUIRE(tsunami::r2d::to_string(tsunami::r2d::RegionalEarthquakeSourceKind::synthetic) == "synthetic");
    REQUIRE(tsunami::r2d::to_string(tsunami::r2d::RegionalBedDeformationMappingKind::horizontal_slope_corrected) == "horizontal_slope_corrected");
    REQUIRE(tsunami::r2d::to_string(tsunami::r2d::RegionalSurfaceTransferKind::prescribed) == "prescribed");
    REQUIRE(tsunami::r2d::validate_regional_earthquake_source_metadata(metadata()).has_value());

    auto finite_fault = metadata("finite");
    finite_fault.source_kind = tsunami::r2d::RegionalEarthquakeSourceKind::finite_fault;
    REQUIRE_FALSE(tsunami::r2d::validate_regional_earthquake_source_metadata(finite_fault).has_value());
    finite_fault.subfault_count = 1U;
    REQUIRE(tsunami::r2d::validate_regional_earthquake_source_metadata(finite_fault).has_value());

    auto invalid = metadata();
    invalid.event_id.clear();
    REQUIRE_FALSE(tsunami::r2d::validate_regional_earthquake_source_metadata(invalid).has_value());
    invalid = metadata();
    invalid.model_id.clear();
    REQUIRE_FALSE(tsunami::r2d::validate_regional_earthquake_source_metadata(invalid).has_value());
    invalid = metadata();
    invalid.source_format.clear();
    REQUIRE_FALSE(tsunami::r2d::validate_regional_earthquake_source_metadata(invalid).has_value());
    invalid = metadata();
    invalid.coordinate_reference.clear();
    REQUIRE_FALSE(tsunami::r2d::validate_regional_earthquake_source_metadata(invalid).has_value());
}

TEST_CASE("Earthquake vertical passive initialisation preserves depth volume and wet dry state", "[r2d][earthquake][passive transfer]")
{
    auto problem = make_problem("partially_dry_lake_at_rest");
    auto displacement = tsunami::r2d::make_filled_regional_seabed_displacement(problem.mesh, 0.3, -0.2, 0.05);
    REQUIRE(displacement.has_value());

    const auto pre_depth = problem.simulation_state.conserved_state().depth().clone();
    auto result = initialise(
        problem,
        displacement.value(),
        tsunami::r2d::RegionalBedDeformationMappingKind::vertical_only,
        tsunami::r2d::RegionalSurfaceTransferKind::passive_equal_to_effective_bed);
    REQUIRE(result.has_value());
    REQUIRE(result.value().simulation_state.time() == Approx(0.0));
    REQUIRE(result.value().simulation_state.accepted_step_count() == 0U);
    REQUIRE(result.value().diagnostics.water_volume_change == Approx(0.0).margin(1.0e-12));
    REQUIRE(result.value().diagnostics.newly_wet_cell_count == 0U);
    REQUIRE(result.value().diagnostics.newly_dry_cell_count == 0U);
    for (std::size_t index = 0; index < problem.mesh.summary().cell_count; ++index) {
        const auto cell_id = tsunami::fvm::CellId{index};
        REQUIRE(result.value().post_event_bathymetry.local_bed_elevation(cell_id) ==
                Approx(problem.bathymetry.local_bed_elevation(cell_id) + 0.05));
        REQUIRE(result.value().simulation_state.conserved_state().depth().at(index) == Approx(pre_depth.at(index)).margin(1.0e-12));
        REQUIRE(result.value().simulation_state.conserved_state().momentum_x().at(index) == Approx(0.0).margin(1.0e-15));
        REQUIRE(result.value().simulation_state.conserved_state().momentum_y().at(index) == Approx(0.0).margin(1.0e-15));
    }
}

TEST_CASE("Horizontal slope correction uses Green-Gauss planar gradient with accepted boundary data", "[r2d][earthquake][horizontal slope correction]")
{
    constexpr auto a = Real{0.12};
    constexpr auto b = Real{-0.04};
    auto problem = make_problem("earthquake_horizontal_slope_correction");
    const auto expected = 0.03 - (0.2 * a) - (-0.1 * b);
    REQUIRE(problem.earthquake_initialisation.has_value());
    REQUIRE(problem.earthquake_initialisation->bed_mapping ==
            tsunami::r2d::RegionalBedDeformationMappingKind::horizontal_slope_corrected);
    REQUIRE(problem.earthquake_initialisation->minimum_effective_bed_displacement == Approx(expected).margin(1.0e-12));
    REQUIRE(problem.earthquake_initialisation->maximum_effective_bed_displacement == Approx(expected).margin(1.0e-12));

    auto workspace = tsunami::r2d::make_regional_earthquake_initialisation_workspace(problem.mesh);
    REQUIRE(workspace.has_value());
    auto destination = workspace.value().effective_bed_displacement().clone();
    auto preserved = destination.at(0U);
    auto displacement = tsunami::r2d::make_filled_regional_seabed_displacement(problem.mesh, 0.2, -0.1, 0.03);
    REQUIRE(displacement.has_value());
    REQUIRE_FALSE(tsunami::r2d::calculate_effective_seabed_displacement(
                      problem.mesh,
                      problem.bathymetry,
                      displacement.value(),
                      tsunami::r2d::RegionalBedDeformationMappingKind::horizontal_slope_corrected,
                      nullptr,
                      destination,
                      workspace.value())
                      .has_value());
    REQUIRE(destination.at(0U) == Approx(preserved));
}

TEST_CASE("Prescribed earthquake transfer updates bed and surface independently with wet dry diagnostics", "[r2d][earthquake][prescribed surface]")
{
    auto problem = make_problem("earthquake_prescribed_surface_perturbation");
    REQUIRE(problem.earthquake_initialisation.has_value());
    const auto &diagnostics = *problem.earthquake_initialisation;
    REQUIRE(diagnostics.surface_transfer == tsunami::r2d::RegionalSurfaceTransferKind::prescribed);
    REQUIRE(diagnostics.maximum_surface_perturbation != Approx(diagnostics.maximum_effective_bed_displacement));
    REQUIRE(diagnostics.newly_wet_cell_count > 0U);
    REQUIRE(diagnostics.post_event_maximum_momentum == Approx(0.0).margin(1.0e-15));
    for (std::size_t index = 0; index < problem.mesh.summary().cell_count; ++index) {
        REQUIRE(problem.simulation_state.conserved_state().depth().at(index) >= 0.0);
    }
}

TEST_CASE("Earthquake benchmarks extend the catalogue and remain solver-ready", "[r2d][earthquake][benchmarks]")
{
    const auto ids = tsunami::r2d_benchmarks::regional_benchmark_case_ids();
    REQUIRE(ids.size() == 15U);
    for (const auto id : {
             std::string_view{"earthquake_uniform_vertical_translation"},
             std::string_view{"earthquake_localised_vertical_uplift"},
             std::string_view{"earthquake_uplift_subsidence_dipole"},
             std::string_view{"earthquake_horizontal_slope_correction"},
             std::string_view{"earthquake_prescribed_surface_perturbation"}}) {
        auto problem = make_problem(id);
        REQUIRE(problem.earthquake_initialisation.has_value());
        REQUIRE(problem.bathymetry.is_bound_to(problem.mesh));
        REQUIRE(problem.simulation_state.conserved_state().is_bound_to(problem.mesh));
        REQUIRE(problem.earthquake_initialisation->cell_count == problem.mesh.summary().cell_count);
    }
}

TEST_CASE("Earthquake propagation benchmarks retain deterministic verification metrics", "[r2d][earthquake][benchmarks]")
{
    auto uniform = make_problem("earthquake_uniform_vertical_translation");
    REQUIRE(uniform.earthquake_initialisation.has_value());
    const auto uniform_initial_volume = uniform.earthquake_initialisation->post_event_water_volume;
    auto uniform_summary = solve_benchmark(uniform);
    REQUIRE(uniform_summary.has_value());
    REQUIRE(uniform_summary.value().termination_reason == tsunami::r2d::RegionalSolveTerminationReason::final_time_reached);
    REQUIRE(uniform_summary.value().final_integrals.water_volume == Approx(uniform_initial_volume).margin(1.0e-12));
    REQUIRE(std::hypot(uniform_summary.value().final_integrals.momentum_x, uniform_summary.value().final_integrals.momentum_y) ==
            Approx(0.0).margin(1.0e-13));

    auto localised = make_problem("earthquake_localised_vertical_uplift");
    REQUIRE(localised.earthquake_initialisation.has_value());
    auto localised_summary = solve_benchmark(localised);
    REQUIRE(localised_summary.has_value());
    REQUIRE(localised_summary.value().termination_reason == tsunami::r2d::RegionalSolveTerminationReason::final_time_reached);
    REQUIRE(radial_outflow_metric(localised, 0.5, 0.25) > 1.0e-5);

    auto dipole = make_problem("earthquake_uplift_subsidence_dipole");
    REQUIRE(dipole.earthquake_initialisation.has_value());
    REQUIRE(dipole.earthquake_initialisation->integrated_surface_perturbation == Approx(0.0).margin(1.0e-3));
    auto dipole_summary = solve_benchmark(dipole);
    REQUIRE(dipole_summary.has_value());
    REQUIRE(dipole_summary.value().termination_reason == tsunami::r2d::RegionalSolveTerminationReason::final_time_reached);

    auto prescribed = make_problem("earthquake_prescribed_surface_perturbation");
    REQUIRE(prescribed.earthquake_initialisation.has_value());
    REQUIRE(prescribed.earthquake_initialisation->newly_wet_cell_count > 0U);
    REQUIRE(prescribed.earthquake_initialisation->water_volume_change < 0.0);
    auto prescribed_summary = solve_benchmark(prescribed);
    REQUIRE(prescribed_summary.has_value());
    REQUIRE(prescribed_summary.value().termination_reason == tsunami::r2d::RegionalSolveTerminationReason::final_time_reached);
    REQUIRE(prescribed_summary.value().final_integrals.water_volume >= 0.0);
}

TEST_CASE("Earthquake initialisation CSV is deterministic", "[r2d][earthquake CSV]")
{
    auto problem = make_problem("earthquake_uniform_vertical_translation");
    REQUIRE(problem.earthquake_initialisation.has_value());
    const auto output = std::filesystem::temp_directory_path() / "tsunami-r2d-earthquake-initialisation.csv";
    REQUIRE(tsunami::r2d_io::write_regional_earthquake_initialisation_csv(output, *problem.earthquake_initialisation).has_value());
    std::ifstream file(output);
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(text.find("source_kind,event_id,model_id") != std::string::npos);
    REQUIRE(text.find("synthetic,earthquake_uniform_vertical_translation") != std::string::npos);
}
