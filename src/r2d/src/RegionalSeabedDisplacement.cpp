#include <tsunami/r2d/RegionalSeabedDisplacement.hpp>

#include <cmath>
#include <string>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto displacement_unit = "m";

        [[nodiscard]] auto displacement_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt,
            std::string component = {}) -> tsunami::core::Error
        {
            const auto mesh_id = mesh.summary().id;
            auto error = detail::r2d_error(
                std::move(code),
                std::move(message),
                std::move(operation),
                "SWE-R2D-EQK",
                &mesh_id,
                cell_id);
            if (!component.empty()) {
                error.add_context("component", std::move(component));
            }
            return error;
        }

        [[nodiscard]] auto validate_component(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const std::vector<tsunami::core::Real> &values,
            std::string_view component) -> tsunami::core::Result<void>
        {
            if (values.size() != mesh.summary().cell_count) {
                auto error = displacement_error(
                    "r2d.earthquake.displacement_count_invalid",
                    "seabed displacement component count must match mesh cells",
                    "make_regional_seabed_displacement",
                    mesh,
                    std::nullopt,
                    std::string{component});
                error.add_context("expected_count", std::to_string(mesh.summary().cell_count))
                    .add_context("actual_count", std::to_string(values.size()));
                return tsunami::core::failure(error);
            }
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (!std::isfinite(values[index])) {
                    auto error = displacement_error(
                        "r2d.earthquake.displacement_value_invalid",
                        "seabed displacement values must be finite",
                        "make_regional_seabed_displacement",
                        mesh,
                        tsunami::fvm::CellId{index},
                        std::string{component});
                    error.add_context("displacement", std::to_string(values[index]));
                    return tsunami::core::failure(error);
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto component_field(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string component,
            std::vector<tsunami::core::Real> values) -> tsunami::core::Result<tsunami::fvm::CellScalarField>
        {
            return tsunami::fvm::make_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
                mesh,
                tsunami::fvm::FieldId{"regional.earthquake.displacement." + component},
                "regional earthquake " + component + " seabed displacement",
                displacement_unit,
                std::move(values));
        }
    } // namespace

    RegionalSeabedDisplacement::RegionalSeabedDisplacement(
        tsunami::fvm::CellScalarField eastward,
        tsunami::fvm::CellScalarField northward,
        tsunami::fvm::CellScalarField upward)
        : eastward_{std::move(eastward)}
        , northward_{std::move(northward)}
        , upward_{std::move(upward)}
    {
    }

    auto RegionalSeabedDisplacement::local_displacement(tsunami::fvm::CellId cell_id) const -> RegionalSeabedDisplacementVector
    {
        return RegionalSeabedDisplacementVector{
            .eastward = eastward_.at(cell_id.value),
            .northward = northward_.at(cell_id.value),
            .upward = upward_.at(cell_id.value)};
    }

    auto RegionalSeabedDisplacement::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return eastward_.is_bound_to(mesh) && northward_.is_bound_to(mesh) && upward_.is_bound_to(mesh) &&
               eastward_.size() == mesh.summary().cell_count && northward_.size() == mesh.summary().cell_count &&
               upward_.size() == mesh.summary().cell_count &&
               eastward_.descriptor().unit_id == displacement_unit &&
               northward_.descriptor().unit_id == displacement_unit &&
               upward_.descriptor().unit_id == displacement_unit &&
               eastward_.binding() == upward_.binding() && northward_.binding() == upward_.binding();
    }

    auto RegionalSeabedDisplacement::clone() const -> RegionalSeabedDisplacement
    {
        return RegionalSeabedDisplacement{eastward_.clone(), northward_.clone(), upward_.clone()};
    }

    auto make_regional_seabed_displacement(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::vector<tsunami::core::Real> eastward,
        std::vector<tsunami::core::Real> northward,
        std::vector<tsunami::core::Real> upward) -> tsunami::core::Result<RegionalSeabedDisplacement>
    {
        auto east_validation = validate_component(mesh, eastward, "eastward");
        if (!east_validation) {
            return tsunami::core::failure<RegionalSeabedDisplacement>(east_validation.error());
        }
        auto north_validation = validate_component(mesh, northward, "northward");
        if (!north_validation) {
            return tsunami::core::failure<RegionalSeabedDisplacement>(north_validation.error());
        }
        auto up_validation = validate_component(mesh, upward, "upward");
        if (!up_validation) {
            return tsunami::core::failure<RegionalSeabedDisplacement>(up_validation.error());
        }

        auto east = component_field(mesh, "eastward", std::move(eastward));
        auto north = component_field(mesh, "northward", std::move(northward));
        auto up = component_field(mesh, "upward", std::move(upward));
        if (!east) {
            return tsunami::core::failure<RegionalSeabedDisplacement>(east.error());
        }
        if (!north) {
            return tsunami::core::failure<RegionalSeabedDisplacement>(north.error());
        }
        if (!up) {
            return tsunami::core::failure<RegionalSeabedDisplacement>(up.error());
        }
        auto displacement = RegionalSeabedDisplacement{
            std::move(east).value(),
            std::move(north).value(),
            std::move(up).value()};
        if (!displacement.is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalSeabedDisplacement>(displacement_error(
                "r2d.earthquake.displacement_mesh_incompatible",
                "seabed displacement fields are not mesh compatible",
                "make_regional_seabed_displacement",
                mesh));
        }
        return tsunami::core::success(std::move(displacement));
    }

    auto make_vertical_regional_seabed_displacement(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::vector<tsunami::core::Real> upward) -> tsunami::core::Result<RegionalSeabedDisplacement>
    {
        return make_regional_seabed_displacement(
            mesh,
            std::vector<tsunami::core::Real>(mesh.summary().cell_count, 0.0),
            std::vector<tsunami::core::Real>(mesh.summary().cell_count, 0.0),
            std::move(upward));
    }

    auto make_filled_regional_seabed_displacement(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::core::Real eastward,
        tsunami::core::Real northward,
        tsunami::core::Real upward) -> tsunami::core::Result<RegionalSeabedDisplacement>
    {
        const auto count = mesh.summary().cell_count;
        return make_regional_seabed_displacement(
            mesh,
            std::vector<tsunami::core::Real>(count, eastward),
            std::vector<tsunami::core::Real>(count, northward),
            std::vector<tsunami::core::Real>(count, upward));
    }

    auto make_zero_regional_seabed_displacement(
        const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::core::Result<RegionalSeabedDisplacement>
    {
        return make_filled_regional_seabed_displacement(mesh, 0.0, 0.0, 0.0);
    }

} // namespace tsunami::r2d
