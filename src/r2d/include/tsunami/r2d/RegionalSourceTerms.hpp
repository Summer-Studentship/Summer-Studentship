#pragma once

#include <optional>
#include <memory>
#include <vector>

#include <tsunami/fvm/MeshField.hpp>
#include <tsunami/r2d/ShallowWaterState.hpp>

namespace tsunami::r2d
{
    class RegionalSourceTermSet
    {
    public:
        RegionalSourceTermSet(const RegionalSourceTermSet &) = delete;
        auto operator=(const RegionalSourceTermSet &) -> RegionalSourceTermSet & = delete;
        RegionalSourceTermSet(RegionalSourceTermSet &&) noexcept = default;
        auto operator=(RegionalSourceTermSet &&) noexcept -> RegionalSourceTermSet & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto has_manning() const noexcept -> bool { return manning_coefficient_.has_value(); }
        [[nodiscard]] auto has_coriolis() const noexcept -> bool { return coriolis_parameter_.has_value(); }
        [[nodiscard]] auto empty() const noexcept -> bool { return !has_manning() && !has_coriolis(); }
        [[nodiscard]] auto manning_coefficient() const noexcept -> const tsunami::fvm::CellScalarField * { return manning_coefficient_ ? std::addressof(*manning_coefficient_) : nullptr; }
        [[nodiscard]] auto coriolis_parameter() const noexcept -> const tsunami::fvm::CellScalarField * { return coriolis_parameter_ ? std::addressof(*coriolis_parameter_) : nullptr; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;
        [[nodiscard]] auto clone() const -> RegionalSourceTermSet;

    private:
        friend auto make_regional_source_term_set(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::optional<std::vector<tsunami::core::Real>> manning_coefficients,
            std::optional<std::vector<tsunami::core::Real>> coriolis_parameters) -> tsunami::core::Result<RegionalSourceTermSet>;

        RegionalSourceTermSet(
            tsunami::fvm::MeshBinding binding,
            std::optional<tsunami::fvm::CellScalarField> manning_coefficient,
            std::optional<tsunami::fvm::CellScalarField> coriolis_parameter);

        tsunami::fvm::MeshBinding binding_;
        std::optional<tsunami::fvm::CellScalarField> manning_coefficient_;
        std::optional<tsunami::fvm::CellScalarField> coriolis_parameter_;
    };

    [[nodiscard]] auto make_regional_source_term_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::optional<std::vector<tsunami::core::Real>> manning_coefficients,
        std::optional<std::vector<tsunami::core::Real>> coriolis_parameters) -> tsunami::core::Result<RegionalSourceTermSet>;

    [[nodiscard]] auto make_uniform_manning_source_term_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::core::Real manning_coefficient) -> tsunami::core::Result<RegionalSourceTermSet>;

    [[nodiscard]] auto make_uniform_coriolis_source_term_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::core::Real coriolis_parameter) -> tsunami::core::Result<RegionalSourceTermSet>;

    [[nodiscard]] auto make_uniform_manning_coriolis_source_term_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::core::Real manning_coefficient,
        tsunami::core::Real coriolis_parameter) -> tsunami::core::Result<RegionalSourceTermSet>;

    [[nodiscard]] auto make_empty_regional_source_term_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::core::Result<RegionalSourceTermSet>;

} // namespace tsunami::r2d
