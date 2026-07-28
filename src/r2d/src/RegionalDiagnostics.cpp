#include <tsunami/r2d/RegionalDiagnostics.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace tsunami::r2d
{
    auto calculate_regional_integrals(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const ShallowWaterStatePolicy &policy) -> tsunami::core::Result<RegionalIntegralDiagnostics>
    {
        const auto mesh_id = mesh.summary().id;
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure<RegionalIntegralDiagnostics>(policy_validation.error());
        }
        if (!state.is_bound_to(mesh) || state.size() != mesh.summary().cell_count) {
            return tsunami::core::failure<RegionalIntegralDiagnostics>(detail::r2d_error(
                "r2d.diagnostics.state_incompatible",
                "diagnostic state is not compatible with mesh",
                "calculate_regional_integrals",
                "SWE-R2D-SOL",
                &mesh_id));
        }

        RegionalIntegralDiagnostics diagnostics;
        diagnostics.minimum_depth = std::numeric_limits<tsunami::core::Real>::infinity();
        diagnostics.maximum_depth = -std::numeric_limits<tsunami::core::Real>::infinity();
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto area = mesh.cell_geometry(cell_id).measure;
            const auto local = state.local_state(cell_id);
            if (!std::isfinite(area) || area <= 0.0 || !std::isfinite(local.depth) ||
                !std::isfinite(local.momentum_x) || !std::isfinite(local.momentum_y)) {
                return tsunami::core::failure<RegionalIntegralDiagnostics>(detail::r2d_error(
                    "r2d.diagnostics.value_invalid",
                    "diagnostic input contains nonfinite or invalid values",
                    "calculate_regional_integrals",
                    "SWE-R2D-SOL",
                    &mesh_id,
                    cell_id));
            }
            diagnostics.water_volume += area * local.depth;
            diagnostics.momentum_x += area * local.momentum_x;
            diagnostics.momentum_y += area * local.momentum_y;
            diagnostics.minimum_depth = std::min(diagnostics.minimum_depth, local.depth);
            diagnostics.maximum_depth = std::max(diagnostics.maximum_depth, local.depth);
            if (is_wet(local, policy)) {
                ++diagnostics.wet_cell_count;
            } else {
                ++diagnostics.dry_cell_count;
            }
        }
        if (mesh.summary().cell_count == 0U) {
            diagnostics.minimum_depth = 0.0;
            diagnostics.maximum_depth = 0.0;
        }
        return tsunami::core::success(diagnostics);
    }

} // namespace tsunami::r2d
