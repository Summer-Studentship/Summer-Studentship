#pragma once

#include <vector>

#include <tsunami/fvm/MeshField.hpp>
#include <tsunami/r2d/ShallowWaterState.hpp>

namespace tsunami::r2d
{
    struct RegionalSeabedDisplacementVector
    {
        tsunami::core::Real eastward{};
        tsunami::core::Real northward{};
        tsunami::core::Real upward{};
    };

    class RegionalSeabedDisplacement
    {
    public:
        RegionalSeabedDisplacement(const RegionalSeabedDisplacement &) = delete;
        auto operator=(const RegionalSeabedDisplacement &) -> RegionalSeabedDisplacement & = delete;
        RegionalSeabedDisplacement(RegionalSeabedDisplacement &&) noexcept = default;
        auto operator=(RegionalSeabedDisplacement &&) noexcept -> RegionalSeabedDisplacement & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return upward_.binding(); }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return upward_.size(); }
        [[nodiscard]] auto eastward() const noexcept -> const tsunami::fvm::CellScalarField & { return eastward_; }
        [[nodiscard]] auto northward() const noexcept -> const tsunami::fvm::CellScalarField & { return northward_; }
        [[nodiscard]] auto upward() const noexcept -> const tsunami::fvm::CellScalarField & { return upward_; }
        [[nodiscard]] auto local_displacement(tsunami::fvm::CellId cell_id) const -> RegionalSeabedDisplacementVector;
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;
        [[nodiscard]] auto clone() const -> RegionalSeabedDisplacement;

    private:
        friend auto make_regional_seabed_displacement(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::vector<tsunami::core::Real> eastward,
            std::vector<tsunami::core::Real> northward,
            std::vector<tsunami::core::Real> upward) -> tsunami::core::Result<RegionalSeabedDisplacement>;

        RegionalSeabedDisplacement(
            tsunami::fvm::CellScalarField eastward,
            tsunami::fvm::CellScalarField northward,
            tsunami::fvm::CellScalarField upward);

        tsunami::fvm::CellScalarField eastward_;
        tsunami::fvm::CellScalarField northward_;
        tsunami::fvm::CellScalarField upward_;
    };

    [[nodiscard]] auto make_regional_seabed_displacement(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::vector<tsunami::core::Real> eastward,
        std::vector<tsunami::core::Real> northward,
        std::vector<tsunami::core::Real> upward) -> tsunami::core::Result<RegionalSeabedDisplacement>;

    [[nodiscard]] auto make_vertical_regional_seabed_displacement(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::vector<tsunami::core::Real> upward) -> tsunami::core::Result<RegionalSeabedDisplacement>;

    [[nodiscard]] auto make_filled_regional_seabed_displacement(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        tsunami::core::Real eastward,
        tsunami::core::Real northward,
        tsunami::core::Real upward) -> tsunami::core::Result<RegionalSeabedDisplacement>;

    [[nodiscard]] auto make_zero_regional_seabed_displacement(
        const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::core::Result<RegionalSeabedDisplacement>;

} // namespace tsunami::r2d

