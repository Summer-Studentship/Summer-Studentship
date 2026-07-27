#include <tsunami/r2d/RegionalResidual.hpp>

namespace tsunami::r2d
{
    RegionalResidual::RegionalResidual(
        tsunami::fvm::CellScalarField mass,
        tsunami::fvm::CellScalarField momentum_x,
        tsunami::fvm::CellScalarField momentum_y)
        : mass_{std::move(mass)}
        , momentum_x_{std::move(momentum_x)}
        , momentum_y_{std::move(momentum_y)}
    {
    }

    auto RegionalResidual::fill(ConservedVariables2D value) -> void
    {
        mass_.fill(value.depth);
        momentum_x_.fill(value.momentum_x);
        momentum_y_.fill(value.momentum_y);
    }

    auto RegionalResidual::clone() const -> RegionalResidual
    {
        return RegionalResidual{mass_.clone(), momentum_x_.clone(), momentum_y_.clone()};
    }

    auto RegionalResidual::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return mass_.is_bound_to(mesh) && momentum_x_.is_bound_to(mesh) && momentum_y_.is_bound_to(mesh);
    }

    auto RegionalResidual::is_layout_compatible_with(const RegionalResidual &other) const -> bool
    {
        return mass_.is_layout_compatible_with(other.mass_) &&
               momentum_x_.is_layout_compatible_with(other.momentum_x_) &&
               momentum_y_.is_layout_compatible_with(other.momentum_y_);
    }

    auto RegionalResidual::copy_values_from(const RegionalResidual &other) -> tsunami::core::Result<void>
    {
        if (!is_layout_compatible_with(other)) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.residual.destination_incompatible",
                "residual destinations are not layout compatible",
                "copy_values_from",
                "SWE-R2D-SOL"));
        }
        auto mass_copy = mass_.copy_values_from(other.mass_);
        if (!mass_copy) {
            return tsunami::core::failure(mass_copy.error());
        }
        auto momentum_x_copy = momentum_x_.copy_values_from(other.momentum_x_);
        if (!momentum_x_copy) {
            return tsunami::core::failure(momentum_x_copy.error());
        }
        return momentum_y_.copy_values_from(other.momentum_y_);
    }

    auto make_regional_residual(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<RegionalResidual>
    {
        auto mass = tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh, tsunami::fvm::FieldId{"regional.residual.mass"}, "regional mass residual", "m3/s", 0.0);
        if (!mass) {
            return tsunami::core::failure<RegionalResidual>(mass.error());
        }
        auto momentum_x = tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh, tsunami::fvm::FieldId{"regional.residual.momentum_x"}, "regional x momentum residual", "m4/s2", 0.0);
        if (!momentum_x) {
            return tsunami::core::failure<RegionalResidual>(momentum_x.error());
        }
        auto momentum_y = tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh, tsunami::fvm::FieldId{"regional.residual.momentum_y"}, "regional y momentum residual", "m4/s2", 0.0);
        if (!momentum_y) {
            return tsunami::core::failure<RegionalResidual>(momentum_y.error());
        }
        return tsunami::core::success(RegionalResidual{std::move(mass).value(), std::move(momentum_x).value(), std::move(momentum_y).value()});
    }

} // namespace tsunami::r2d
