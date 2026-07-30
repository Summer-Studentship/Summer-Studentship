#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <tsunami/fvm/BoundaryConditionSet.hpp>
#include <tsunami/r2d/RegionalBathymetry.hpp>
#include <tsunami/r2d/RegionalSimulationState.hpp>
#include <tsunami/r2d/RegionalSourceTerms.hpp>
#include <tsunami/r2d/RegionalTimeIntegration.hpp>

namespace tsunami::r2d_benchmarks
{
    struct RegionalBenchmarkCase
    {
        std::string id;
        tsunami::fvm::FiniteVolumeMesh mesh;
        tsunami::r2d::RegionalBathymetry bathymetry;
        tsunami::r2d::RegionalSimulationState simulation_state;
        tsunami::fvm::ScalarBoundaryConditionSet depth_boundaries;
        tsunami::fvm::ScalarBoundaryConditionSet momentum_x_boundaries;
        tsunami::fvm::ScalarBoundaryConditionSet momentum_y_boundaries;
        tsunami::fvm::ScalarBoundaryConditionSet bathymetry_boundaries;
        tsunami::r2d::RegionalBoundaryConditionSet regional_boundaries;
        tsunami::r2d::RegionalRelaxationZoneSet relaxation_zones;
        tsunami::r2d::RegionalSourceTermSet local_sources;
        tsunami::r2d::ShallowWaterStatePolicy state_policy;
        tsunami::r2d::RegionalTimeIntegrationPolicy time_policy;
        tsunami::core::Time default_final_time{0.05};
    };

    [[nodiscard]] auto make_regional_benchmark_case(std::string_view id)
        -> tsunami::core::Result<RegionalBenchmarkCase>;

    [[nodiscard]] auto regional_benchmark_case_ids() -> std::vector<std::string_view>;

} // namespace tsunami::r2d_benchmarks
