#include <tsunami/r2d/RegionalBathymetry.hpp>

#include <algorithm>
#include <cmath>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto bed_unit = "m";

        [[nodiscard]] auto bathymetry_error(
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

    RegionalBathymetry::RegionalBathymetry(tsunami::fvm::CellScalarField bed_elevation)
        : bed_elevation_{std::move(bed_elevation)}
    {
    }

    auto RegionalBathymetry::local_bed_elevation(tsunami::fvm::CellId cell_id) const -> tsunami::core::Real
    {
        return bed_elevation_.at(cell_id.value);
    }

    auto RegionalBathymetry::set_local_bed_elevation(tsunami::fvm::CellId cell_id, tsunami::core::Real value) -> void
    {
        bed_elevation_.at(cell_id.value) = value;
    }

    auto RegionalBathymetry::clone() const -> RegionalBathymetry
    {
        return RegionalBathymetry{bed_elevation_.clone()};
    }

    auto RegionalBathymetry::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return bed_elevation_.is_bound_to(mesh);
    }

    auto RegionalBathymetry::is_layout_compatible_with(const RegionalBathymetry &other) const -> bool
    {
        return bed_elevation_.is_layout_compatible_with(other.bed_elevation_);
    }

    auto RegionalBathymetry::copy_values_from(const RegionalBathymetry &other) -> tsunami::core::Result<void>
    {
        if (!is_layout_compatible_with(other)) {
            return tsunami::core::failure(detail::r2d_error(
                "r2d.bathymetry.mesh_incompatible",
                "bathymetry fields are not layout compatible",
                "copy_values_from",
                "SWE-R2D-SRC"));
        }
        return bed_elevation_.copy_values_from(other.bed_elevation_);
    }

    auto make_regional_bathymetry(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::FieldId field_id,
        std::string name,
        std::vector<tsunami::core::Real> bed_elevation) -> tsunami::core::Result<RegionalBathymetry>
    {
        const auto expected = mesh.summary().cell_count;
        if (bed_elevation.size() != expected) {
            return tsunami::core::failure<RegionalBathymetry>(bathymetry_error(
                "r2d.bathymetry.entity_count_mismatch",
                "bathymetry value count must match mesh cell count",
                "make_regional_bathymetry",
                mesh));
        }
        for (std::size_t index = 0; index < bed_elevation.size(); ++index) {
            if (!std::isfinite(bed_elevation[index])) {
                return tsunami::core::failure<RegionalBathymetry>(bathymetry_error(
                    "r2d.bathymetry.value_nonfinite",
                    "bathymetry elevations must be finite",
                    "make_regional_bathymetry",
                    mesh,
                    tsunami::fvm::CellId{index}));
            }
        }
        auto field = tsunami::fvm::make_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh,
            std::move(field_id),
            std::move(name),
            bed_unit,
            std::move(bed_elevation));
        if (!field) {
            return tsunami::core::failure<RegionalBathymetry>(field.error());
        }
        auto bathymetry = RegionalBathymetry{std::move(field).value()};
        if (bathymetry.bed_elevation().descriptor().unit_id != bed_unit) {
            return tsunami::core::failure<RegionalBathymetry>(bathymetry_error(
                "r2d.bathymetry.unit_incompatible",
                "bathymetry unit must be metres",
                "make_regional_bathymetry",
                mesh,
                std::nullopt,
                bathymetry.bed_elevation().descriptor().id.value,
                bed_unit,
                bathymetry.bed_elevation().descriptor().unit_id));
        }
        return tsunami::core::success(std::move(bathymetry));
    }

    auto make_filled_regional_bathymetry(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::FieldId field_id,
        std::string name,
        tsunami::core::Real bed_elevation) -> tsunami::core::Result<RegionalBathymetry>
    {
        return make_regional_bathymetry(
            mesh,
            std::move(field_id),
            std::move(name),
            std::vector<tsunami::core::Real>(mesh.summary().cell_count, bed_elevation));
    }

} // namespace tsunami::r2d
