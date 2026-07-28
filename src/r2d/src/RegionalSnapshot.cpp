#include <tsunami/r2d/RegionalSnapshot.hpp>

namespace tsunami::r2d
{
    auto make_regional_snapshot(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        tsunami::core::Time time,
        std::size_t step_index,
        tsunami::fvm::CellScalarField &free_surface_workspace) -> tsunami::core::Result<RegionalSnapshot>
    {
        auto eta = calculate_free_surface_elevation(mesh, state, bathymetry, free_surface_workspace);
        if (!eta) {
            return tsunami::core::failure<RegionalSnapshot>(eta.error());
        }

        RegionalSnapshot snapshot;
        snapshot.step_index = step_index;
        snapshot.time = time;
        snapshot.depth.reserve(mesh.summary().cell_count);
        snapshot.momentum_x.reserve(mesh.summary().cell_count);
        snapshot.momentum_y.reserve(mesh.summary().cell_count);
        snapshot.bed_elevation.reserve(mesh.summary().cell_count);
        snapshot.free_surface_elevation.reserve(mesh.summary().cell_count);
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            snapshot.depth.push_back(state.depth().at(index));
            snapshot.momentum_x.push_back(state.momentum_x().at(index));
            snapshot.momentum_y.push_back(state.momentum_y().at(index));
            snapshot.bed_elevation.push_back(bathymetry.bed_elevation().at(index));
            snapshot.free_surface_elevation.push_back(free_surface_workspace.at(index));
        }
        return tsunami::core::success(std::move(snapshot));
    }

} // namespace tsunami::r2d
