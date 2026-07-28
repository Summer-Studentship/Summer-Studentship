#include <tsunami/r2d/FreeSurfaceElevation.hpp>

#include <cmath>
#include <vector>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto free_surface_unit = "m";

        [[nodiscard]] auto error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt,
            std::string field_id = {},
            std::string expected_unit = {},
            std::string actual_unit = {}) -> tsunami::core::Error
        {
            const auto mesh_id = mesh.summary().id;
            return detail::r2d_error(
                std::move(code),
                std::move(message),
                std::move(operation),
                "SWE-R2D-SRC",
                &mesh_id,
                cell_id,
                std::nullopt,
                std::nullopt,
                std::move(field_id),
                std::move(expected_unit),
                std::move(actual_unit));
        }
    } // namespace

    auto calculate_free_surface_elevation(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        tsunami::fvm::CellScalarField &destination) -> tsunami::core::Result<void>
    {
        if (!state.is_bound_to(mesh) || state.size() != mesh.summary().cell_count) {
            return tsunami::core::failure(error("r2d.free_surface.state_incompatible", "state is not compatible with mesh", "calculate_free_surface_elevation", mesh));
        }
        if (!bathymetry.is_bound_to(mesh) || bathymetry.size() != mesh.summary().cell_count) {
            return tsunami::core::failure(error("r2d.free_surface.bathymetry_incompatible", "bathymetry is not compatible with mesh", "calculate_free_surface_elevation", mesh));
        }
        const auto descriptor = destination.descriptor();
        if (!destination.is_bound_to(mesh) || destination.size() != mesh.summary().cell_count || descriptor.unit_id != free_surface_unit) {
            return tsunami::core::failure(error(
                "r2d.free_surface.destination_incompatible",
                "destination free-surface field is incompatible",
                "calculate_free_surface_elevation",
                mesh,
                std::nullopt,
                descriptor.id.value,
                free_surface_unit,
                descriptor.unit_id));
        }
        std::vector<tsunami::core::Real> values(mesh.summary().cell_count);
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index] = state.depth().at(index) + bathymetry.bed_elevation().at(index);
            if (!std::isfinite(values[index])) {
                return tsunami::core::failure(error(
                    "r2d.free_surface.result_nonfinite",
                    "derived free-surface elevation must be finite",
                    "calculate_free_surface_elevation",
                    mesh,
                    tsunami::fvm::CellId{index}));
            }
        }
        for (std::size_t index = 0; index < values.size(); ++index) {
            destination.at(index) = values[index];
        }
        return tsunami::core::success();
    }

} // namespace tsunami::r2d
