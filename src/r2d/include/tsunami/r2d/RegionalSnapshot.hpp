#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <tsunami/r2d/FreeSurfaceElevation.hpp>
#include <tsunami/r2d/RegionalDiagnostics.hpp>

namespace tsunami::r2d
{
    struct RegionalSnapshot
    {
        std::size_t step_index{};
        tsunami::core::Time time{};
        std::vector<tsunami::core::Real> depth;
        std::vector<tsunami::core::Real> momentum_x;
        std::vector<tsunami::core::Real> momentum_y;
        std::vector<tsunami::core::Real> bed_elevation;
        std::vector<tsunami::core::Real> free_surface_elevation;
    };

    struct RegionalSnapshotOutputPolicy
    {
        bool emit_initial_snapshot{true};
        bool emit_final_snapshot{true};
        std::optional<tsunami::core::Real> interval;
    };

    using RegionalStepDiagnosticsSink = std::function<tsunami::core::Result<void>(const RegionalStepDiagnostics &)>;
    using RegionalSnapshotSink = std::function<tsunami::core::Result<void>(const RegionalSnapshot &)>;

    [[nodiscard]] auto make_regional_snapshot(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        tsunami::core::Time time,
        std::size_t step_index,
        tsunami::fvm::CellScalarField &free_surface_workspace) -> tsunami::core::Result<RegionalSnapshot>;

} // namespace tsunami::r2d
