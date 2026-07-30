#include <tsunami/r2d/RegionalSourceTerms.hpp>

#include <cmath>
#include <string>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto manning_unit = "s m^(-1/3)";
        constexpr auto coriolis_unit = "1/s";

        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto source_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh *mesh = nullptr,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt) -> tsunami::core::Error
        {
            const auto mesh_id = mesh ? mesh->summary().id : tsunami::fvm::MeshId{};
            return detail::r2d_error(
                std::move(code),
                std::move(message),
                std::move(operation),
                "SWE-R2D-SRC",
                mesh ? &mesh_id : nullptr,
                cell_id);
        }

        [[nodiscard]] auto validate_values(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const std::vector<tsunami::core::Real> &values,
            bool manning) -> tsunami::core::Result<void>
        {
            if (values.size() != mesh.summary().cell_count) {
                return tsunami::core::failure(source_error(
                    manning ? "r2d.source.manning_count_invalid" : "r2d.source.coriolis_count_invalid",
                    "source field value count must match mesh cells",
                    "make_regional_source_term_set",
                    &mesh));
            }
            for (std::size_t index = 0; index < values.size(); ++index) {
                const auto value = values[index];
                if (!finite(value) || (manning && value < 0.0)) {
                    return tsunami::core::failure(source_error(
                        manning ? "r2d.source.manning_value_invalid" : "r2d.source.coriolis_value_invalid",
                        "source field value is invalid",
                        "make_regional_source_term_set",
                        &mesh,
                        tsunami::fvm::CellId{index}));
                }
            }
            return tsunami::core::success();
        }
    } // namespace

    RegionalSourceTermSet::RegionalSourceTermSet(
        tsunami::fvm::MeshBinding binding,
        std::optional<tsunami::fvm::CellScalarField> manning_coefficient,
        std::optional<tsunami::fvm::CellScalarField> coriolis_parameter)
        : binding_{std::move(binding)}
        , manning_coefficient_{std::move(manning_coefficient)}
        , coriolis_parameter_{std::move(coriolis_parameter)}
    {
    }

    auto RegionalSourceTermSet::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        const auto cell_count = mesh.summary().cell_count;
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               (!manning_coefficient_ || (manning_coefficient_->is_bound_to(mesh) &&
                                          manning_coefficient_->size() == cell_count &&
                                          manning_coefficient_->descriptor().unit_id == manning_unit)) &&
               (!coriolis_parameter_ || (coriolis_parameter_->is_bound_to(mesh) &&
                                         coriolis_parameter_->size() == cell_count &&
                                         coriolis_parameter_->descriptor().unit_id == coriolis_unit));
    }

    auto RegionalSourceTermSet::clone() const -> RegionalSourceTermSet
    {
        std::optional<tsunami::fvm::CellScalarField> manning;
        std::optional<tsunami::fvm::CellScalarField> coriolis;
        if (manning_coefficient_) {
            manning = manning_coefficient_->clone();
        }
        if (coriolis_parameter_) {
            coriolis = coriolis_parameter_->clone();
        }
        return RegionalSourceTermSet{binding_, std::move(manning), std::move(coriolis)};
    }

    auto make_regional_source_term_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::optional<std::vector<tsunami::core::Real>> manning_coefficients,
        std::optional<std::vector<tsunami::core::Real>> coriolis_parameters) -> tsunami::core::Result<RegionalSourceTermSet>
    {
        std::optional<tsunami::fvm::CellScalarField> manning;
        std::optional<tsunami::fvm::CellScalarField> coriolis;

        if (manning_coefficients) {
            auto validation = validate_values(mesh, *manning_coefficients, true);
            if (!validation) {
                return tsunami::core::failure<RegionalSourceTermSet>(validation.error());
            }
            auto field = tsunami::fvm::make_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
                mesh,
                tsunami::fvm::FieldId{"regional.source.manning"},
                "regional Manning coefficient",
                manning_unit,
                std::move(*manning_coefficients));
            if (!field) {
                return tsunami::core::failure<RegionalSourceTermSet>(field.error());
            }
            manning = std::move(field).value();
        }
        if (coriolis_parameters) {
            auto validation = validate_values(mesh, *coriolis_parameters, false);
            if (!validation) {
                return tsunami::core::failure<RegionalSourceTermSet>(validation.error());
            }
            auto field = tsunami::fvm::make_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
                mesh,
                tsunami::fvm::FieldId{"regional.source.coriolis"},
                "regional Coriolis parameter",
                coriolis_unit,
                std::move(*coriolis_parameters));
            if (!field) {
                return tsunami::core::failure<RegionalSourceTermSet>(field.error());
            }
            coriolis = std::move(field).value();
        }

        auto set = RegionalSourceTermSet{tsunami::fvm::make_mesh_binding(mesh), std::move(manning), std::move(coriolis)};
        if (!set.is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalSourceTermSet>(source_error(
                "r2d.source.set_invalid",
                "regional source term set is invalid",
                "make_regional_source_term_set",
                &mesh));
        }
        return tsunami::core::success(std::move(set));
    }

    auto make_uniform_manning_source_term_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::core::Real manning_coefficient) -> tsunami::core::Result<RegionalSourceTermSet>
    {
        return make_regional_source_term_set(
            mesh,
            std::vector<tsunami::core::Real>(mesh.summary().cell_count, manning_coefficient),
            std::nullopt);
    }

    auto make_uniform_coriolis_source_term_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::core::Real coriolis_parameter) -> tsunami::core::Result<RegionalSourceTermSet>
    {
        return make_regional_source_term_set(
            mesh,
            std::nullopt,
            std::vector<tsunami::core::Real>(mesh.summary().cell_count, coriolis_parameter));
    }

    auto make_uniform_manning_coriolis_source_term_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::core::Real manning_coefficient,
        tsunami::core::Real coriolis_parameter) -> tsunami::core::Result<RegionalSourceTermSet>
    {
        return make_regional_source_term_set(
            mesh,
            std::vector<tsunami::core::Real>(mesh.summary().cell_count, manning_coefficient),
            std::vector<tsunami::core::Real>(mesh.summary().cell_count, coriolis_parameter));
    }

    auto make_empty_regional_source_term_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::core::Result<RegionalSourceTermSet>
    {
        return make_regional_source_term_set(mesh, std::nullopt, std::nullopt);
    }

} // namespace tsunami::r2d
