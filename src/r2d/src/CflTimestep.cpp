#include <tsunami/r2d/CflTimestep.hpp>

#include <cmath>

namespace tsunami::r2d
{
    auto estimate_cfl_timestep(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::fvm::CellScalarField &spectral_sum,
        tsunami::core::Real courant_number) -> tsunami::core::Result<CflTimestepEstimate>
    {
        const auto mesh_id = mesh.summary().id;
        if (!std::isfinite(courant_number) || courant_number <= 0.0 || courant_number > 1.0) {
            return tsunami::core::failure<CflTimestepEstimate>(detail::r2d_error(
                "r2d.cfl.courant_invalid",
                "Courant number must be finite and in (0, 1]",
                "estimate_cfl_timestep",
                "SWE-R2D-TIM",
                &mesh_id));
        }
        if (!spectral_sum.is_bound_to(mesh) || spectral_sum.size() != mesh.summary().cell_count) {
            return tsunami::core::failure<CflTimestepEstimate>(detail::r2d_error(
                "r2d.cfl.spectral_sum_invalid",
                "spectral sum field is not compatible with the mesh",
                "estimate_cfl_timestep",
                "SWE-R2D-TIM",
                &mesh_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                spectral_sum.descriptor().id.value));
        }
        CflTimestepEstimate estimate;
        for (std::size_t index = 0; index < spectral_sum.size(); ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto measure = mesh.cell_geometry(cell_id).measure;
            if (!std::isfinite(measure) || measure <= 0.0) {
                return tsunami::core::failure<CflTimestepEstimate>(detail::r2d_error(
                    "r2d.cfl.cell_measure_invalid",
                    "cell measure must be finite and positive",
                    "estimate_cfl_timestep",
                    "SWE-R2D-TIM",
                    &mesh_id,
                    cell_id));
            }
            const auto spectral = spectral_sum.at(index);
            if (!std::isfinite(spectral) || spectral < 0.0) {
                return tsunami::core::failure<CflTimestepEstimate>(detail::r2d_error(
                    "r2d.cfl.spectral_sum_invalid",
                    "spectral sum must be finite and nonnegative",
                    "estimate_cfl_timestep",
                    "SWE-R2D-TIM",
                    &mesh_id,
                    cell_id,
                    std::nullopt,
                    std::nullopt,
                    spectral_sum.descriptor().id.value,
                    {},
                    {},
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    spectral));
            }
            if (spectral == 0.0) {
                continue;
            }
            const auto candidate = courant_number * measure / spectral;
            if (!std::isfinite(candidate) || candidate <= 0.0) {
                return tsunami::core::failure<CflTimestepEstimate>(detail::r2d_error(
                    "r2d.cfl.spectral_sum_invalid",
                    "CFL candidate must be finite and positive",
                    "estimate_cfl_timestep",
                    "SWE-R2D-TIM",
                    &mesh_id,
                    cell_id,
                    std::nullopt,
                    std::nullopt,
                    spectral_sum.descriptor().id.value,
                    {},
                    {},
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    spectral));
            }
            if (!estimate.stable_timestep || candidate < *estimate.stable_timestep) {
                estimate.stable_timestep = candidate;
                estimate.limiting_cell = cell_id;
            }
        }
        return tsunami::core::success(estimate);
    }

} // namespace tsunami::r2d
