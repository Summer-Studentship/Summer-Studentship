#pragma once

#include <span>
#include <optional>
#include <string>
#include <vector>

#include <tsunami/r2d/RegionalBoundaryCondition.hpp>
#include <tsunami/r2d/RegionalResidual.hpp>

namespace tsunami::r2d
{
    struct PatchRelaxationZoneSpecification
    {
        std::string patch_tag;
        tsunami::core::Real width{};
        tsunami::core::Real maximum_rate{};
        tsunami::core::Real profile_exponent{1.0};
        RegionalFarFieldState reference_state;
    };

    struct RegionalRelaxationDiagnostics
    {
        std::size_t zone_count{};
        std::size_t active_cell_count{};
        tsunami::core::Real maximum_rate{};
        tsunami::core::Real integrated_mass_source_rate{};
        tsunami::core::Real outgoing_mass_rate{};
    };

    struct RelaxationTimestepEstimate
    {
        std::optional<tsunami::core::Real> stable_timestep;
        std::optional<tsunami::fvm::CellId> limiting_cell;
        tsunami::core::Real maximum_rate{};
    };

    class RegionalRelaxationZone
    {
    public:
        RegionalRelaxationZone(const RegionalRelaxationZone &) = delete;
        auto operator=(const RegionalRelaxationZone &) -> RegionalRelaxationZone & = delete;
        RegionalRelaxationZone(RegionalRelaxationZone &&) noexcept = default;
        auto operator=(RegionalRelaxationZone &&) noexcept -> RegionalRelaxationZone & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return damping_rate_.binding(); }
        [[nodiscard]] auto patch_id() const noexcept -> tsunami::fvm::BoundaryPatchId { return patch_id_; }
        [[nodiscard]] auto reference_state() const noexcept -> RegionalFarFieldState { return reference_state_; }
        [[nodiscard]] auto damping_rate() const noexcept -> const tsunami::fvm::CellScalarField & { return damping_rate_; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;
        [[nodiscard]] auto clone() const -> RegionalRelaxationZone;

    private:
        friend auto make_regional_relaxation_zone_set(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::vector<PatchRelaxationZoneSpecification> specifications) -> tsunami::core::Result<class RegionalRelaxationZoneSet>;

        RegionalRelaxationZone(
            tsunami::fvm::BoundaryPatchId patch_id,
            RegionalFarFieldState reference_state,
            tsunami::fvm::CellScalarField damping_rate);

        tsunami::fvm::BoundaryPatchId patch_id_;
        RegionalFarFieldState reference_state_;
        tsunami::fvm::CellScalarField damping_rate_;
    };

    class RegionalRelaxationZoneSet
    {
    public:
        RegionalRelaxationZoneSet(const RegionalRelaxationZoneSet &) = delete;
        auto operator=(const RegionalRelaxationZoneSet &) -> RegionalRelaxationZoneSet & = delete;
        RegionalRelaxationZoneSet(RegionalRelaxationZoneSet &&) noexcept = default;
        auto operator=(RegionalRelaxationZoneSet &&) noexcept -> RegionalRelaxationZoneSet & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto zones() const noexcept -> std::span<const RegionalRelaxationZone> { return zones_; }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return zones_.size(); }
        [[nodiscard]] auto empty() const noexcept -> bool { return zones_.empty(); }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;
        [[nodiscard]] auto clone() const -> RegionalRelaxationZoneSet;

    private:
        friend auto make_regional_relaxation_zone_set(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::vector<PatchRelaxationZoneSpecification> specifications) -> tsunami::core::Result<RegionalRelaxationZoneSet>;

        RegionalRelaxationZoneSet(tsunami::fvm::MeshBinding binding, std::vector<RegionalRelaxationZone> zones);

        tsunami::fvm::MeshBinding binding_;
        std::vector<RegionalRelaxationZone> zones_;
    };

    [[nodiscard]] auto make_regional_relaxation_zone_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::vector<PatchRelaxationZoneSpecification> specifications) -> tsunami::core::Result<RegionalRelaxationZoneSet>;

    auto apply_regional_relaxation_source(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        const RegionalRelaxationZoneSet &zones,
        const ShallowWaterStatePolicy &policy,
        RegionalResidual &residual,
        tsunami::fvm::CellScalarField &outgoing_mass_rate,
        RegionalRelaxationDiagnostics &diagnostics) -> tsunami::core::Result<void>;

    [[nodiscard]] auto estimate_relaxation_timestep(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalRelaxationZoneSet &zones,
        tsunami::core::Real safety_factor) -> tsunami::core::Result<RelaxationTimestepEstimate>;

} // namespace tsunami::r2d
