#pragma once

#include <string>
#include <vector>

#include <tsunami/fvm/MeshField.hpp>
#include <tsunami/r2d/ShallowWaterState.hpp>

namespace tsunami::r2d
{
    class RegionalBathymetry
    {
    public:
        RegionalBathymetry(const RegionalBathymetry &) = delete;
        auto operator=(const RegionalBathymetry &) -> RegionalBathymetry & = delete;
        RegionalBathymetry(RegionalBathymetry &&) noexcept = default;
        auto operator=(RegionalBathymetry &&) noexcept -> RegionalBathymetry & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return bed_elevation_.binding(); }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return bed_elevation_.size(); }

        [[nodiscard]] auto bed_elevation() noexcept -> tsunami::fvm::CellScalarField & { return bed_elevation_; }
        [[nodiscard]] auto bed_elevation() const noexcept -> const tsunami::fvm::CellScalarField & { return bed_elevation_; }
        [[nodiscard]] auto local_bed_elevation(tsunami::fvm::CellId cell_id) const -> tsunami::core::Real;
        auto set_local_bed_elevation(tsunami::fvm::CellId cell_id, tsunami::core::Real value) -> void;

        [[nodiscard]] auto clone() const -> RegionalBathymetry;
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;
        [[nodiscard]] auto is_layout_compatible_with(const RegionalBathymetry &other) const -> bool;
        auto copy_values_from(const RegionalBathymetry &other) -> tsunami::core::Result<void>;

    private:
        friend auto make_regional_bathymetry(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            tsunami::fvm::FieldId field_id,
            std::string name,
            std::vector<tsunami::core::Real> bed_elevation) -> tsunami::core::Result<RegionalBathymetry>;

        explicit RegionalBathymetry(tsunami::fvm::CellScalarField bed_elevation);

        tsunami::fvm::CellScalarField bed_elevation_;
    };

    [[nodiscard]] auto make_regional_bathymetry(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::FieldId field_id,
        std::string name,
        std::vector<tsunami::core::Real> bed_elevation) -> tsunami::core::Result<RegionalBathymetry>;

    [[nodiscard]] auto make_filled_regional_bathymetry(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::fvm::FieldId field_id,
        std::string name,
        tsunami::core::Real bed_elevation) -> tsunami::core::Result<RegionalBathymetry>;

} // namespace tsunami::r2d
