#pragma once

#include <span>
#include <string>
#include <variant>
#include <vector>

#include <tsunami/fvm/BoundaryConditionSet.hpp>
#include <tsunami/r2d/RegionalBathymetry.hpp>
#include <tsunami/r2d/RegionalConservedState.hpp>
#include <tsunami/r2d/ShallowWaterFlux.hpp>

namespace tsunami::r2d
{
    struct RegionalFarFieldState
    {
        tsunami::core::Real free_surface_elevation{};
        tsunami::core::Real velocity_x{};
        tsunami::core::Real velocity_y{};
    };

    enum class RegionalBoundaryConditionKind
    {
        componentwise,
        transmissive,
        radiation
    };

    struct RegionalTransmissiveSpecification
    {
    };

    struct RegionalRadiationSpecification
    {
        RegionalFarFieldState reference_state;
    };

    struct RegionalBoundaryOverrideSpecification
    {
        std::string patch_tag;
        std::variant<RegionalTransmissiveSpecification, RegionalRadiationSpecification> operation;
    };

    [[nodiscard]] auto validate_regional_far_field_state(RegionalFarFieldState state) -> tsunami::core::Result<void>;

    [[nodiscard]] auto regional_reference_conserved_state(
        RegionalFarFieldState reference_state,
        tsunami::core::Real bed_elevation,
        const ShallowWaterStatePolicy &policy,
        std::optional<tsunami::fvm::CellId> cell_id = std::nullopt) -> tsunami::core::Result<ConservedVariables2D>;

    class RegionalComponentwiseBoundary
    {
    public:
        RegionalComponentwiseBoundary(const RegionalComponentwiseBoundary &) = delete;
        auto operator=(const RegionalComponentwiseBoundary &) -> RegionalComponentwiseBoundary & = delete;
        RegionalComponentwiseBoundary(RegionalComponentwiseBoundary &&) noexcept = default;
        auto operator=(RegionalComponentwiseBoundary &&) noexcept -> RegionalComponentwiseBoundary & = default;

        [[nodiscard]] auto kind() const noexcept -> RegionalBoundaryConditionKind { return RegionalBoundaryConditionKind::componentwise; }
        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto patch_id() const noexcept -> tsunami::fvm::BoundaryPatchId { return patch_id_; }
        [[nodiscard]] auto entity_count() const noexcept -> std::size_t { return entity_count_; }
        [[nodiscard]] auto clone() const -> RegionalComponentwiseBoundary;
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

        [[nodiscard]] auto populate(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalConservedState &state,
            const RegionalBathymetry &bathymetry,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &depth,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_x,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_y,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &bed_elevation) const -> tsunami::core::Result<void>;

    private:
        friend auto make_regional_componentwise_boundary(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::fvm::ScalarBoundaryCondition &depth,
            const tsunami::fvm::ScalarBoundaryCondition &momentum_x,
            const tsunami::fvm::ScalarBoundaryCondition &momentum_y,
            const tsunami::fvm::ScalarBoundaryCondition &bed_elevation,
            bool require_executable) -> tsunami::core::Result<RegionalComponentwiseBoundary>;

        RegionalComponentwiseBoundary(
            tsunami::fvm::MeshBinding binding,
            tsunami::fvm::BoundaryPatchId patch_id,
            std::size_t entity_count,
            tsunami::fvm::ScalarBoundaryCondition depth,
            tsunami::fvm::ScalarBoundaryCondition momentum_x,
            tsunami::fvm::ScalarBoundaryCondition momentum_y,
            tsunami::fvm::ScalarBoundaryCondition bed_elevation);

        tsunami::fvm::MeshBinding binding_;
        tsunami::fvm::BoundaryPatchId patch_id_;
        std::size_t entity_count_{};
        tsunami::fvm::ScalarBoundaryCondition depth_;
        tsunami::fvm::ScalarBoundaryCondition momentum_x_;
        tsunami::fvm::ScalarBoundaryCondition momentum_y_;
        tsunami::fvm::ScalarBoundaryCondition bed_elevation_;
    };

    class RegionalTransmissiveBoundary
    {
    public:
        RegionalTransmissiveBoundary(tsunami::fvm::MeshBinding binding, tsunami::fvm::BoundaryPatchId patch_id);

        [[nodiscard]] auto kind() const noexcept -> RegionalBoundaryConditionKind { return RegionalBoundaryConditionKind::transmissive; }
        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto patch_id() const noexcept -> tsunami::fvm::BoundaryPatchId { return patch_id_; }
        [[nodiscard]] auto clone() const -> RegionalTransmissiveBoundary { return RegionalTransmissiveBoundary{binding_, patch_id_}; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

        [[nodiscard]] auto populate(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalConservedState &state,
            const RegionalBathymetry &bathymetry,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &depth,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_x,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_y,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &bed_elevation) const -> tsunami::core::Result<void>;

    private:
        tsunami::fvm::MeshBinding binding_;
        tsunami::fvm::BoundaryPatchId patch_id_;
    };

    class RegionalRadiationBoundary
    {
    public:
        RegionalRadiationBoundary(
            tsunami::fvm::MeshBinding binding,
            tsunami::fvm::BoundaryPatchId patch_id,
            RegionalFarFieldState reference_state);

        [[nodiscard]] auto kind() const noexcept -> RegionalBoundaryConditionKind { return RegionalBoundaryConditionKind::radiation; }
        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto patch_id() const noexcept -> tsunami::fvm::BoundaryPatchId { return patch_id_; }
        [[nodiscard]] auto reference_state() const noexcept -> RegionalFarFieldState { return reference_state_; }
        [[nodiscard]] auto clone() const -> RegionalRadiationBoundary { return RegionalRadiationBoundary{binding_, patch_id_, reference_state_}; }
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;

        [[nodiscard]] auto exterior_state(
            ConservedVariables2D interior,
            tsunami::core::Real bed_elevation,
            FaceNormal2D outward_normal,
            const ShallowWaterStatePolicy &policy,
            tsunami::core::Time time,
            std::optional<tsunami::fvm::FaceId> face_id = std::nullopt) const -> tsunami::core::Result<ConservedVariables2D>;

        [[nodiscard]] auto populate(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalConservedState &state,
            const RegionalBathymetry &bathymetry,
            const ShallowWaterStatePolicy &policy,
            tsunami::core::Time time,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &depth,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_x,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_y,
            tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &bed_elevation) const -> tsunami::core::Result<void>;

    private:
        tsunami::fvm::MeshBinding binding_;
        tsunami::fvm::BoundaryPatchId patch_id_;
        RegionalFarFieldState reference_state_;
    };

    using RegionalBoundaryCondition = std::variant<
        RegionalComponentwiseBoundary,
        RegionalTransmissiveBoundary,
        RegionalRadiationBoundary>;

    class RegionalBoundaryConditionSet
    {
    public:
        RegionalBoundaryConditionSet(const RegionalBoundaryConditionSet &) = delete;
        auto operator=(const RegionalBoundaryConditionSet &) -> RegionalBoundaryConditionSet & = delete;
        RegionalBoundaryConditionSet(RegionalBoundaryConditionSet &&) noexcept = default;
        auto operator=(RegionalBoundaryConditionSet &&) noexcept -> RegionalBoundaryConditionSet & = default;

        [[nodiscard]] auto binding() const noexcept -> const tsunami::fvm::MeshBinding & { return binding_; }
        [[nodiscard]] auto conditions() const noexcept -> std::span<const RegionalBoundaryCondition> { return conditions_; }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return conditions_.size(); }
        [[nodiscard]] auto condition(tsunami::fvm::BoundaryPatchId patch_id) const -> const RegionalBoundaryCondition *;
        [[nodiscard]] auto is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;
        [[nodiscard]] auto is_complete_for(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool;
        [[nodiscard]] auto clone() const -> RegionalBoundaryConditionSet;

    private:
        friend auto make_regional_boundary_condition_set(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::fvm::ScalarBoundaryConditionSet &depth_conditions,
            const tsunami::fvm::ScalarBoundaryConditionSet &momentum_x_conditions,
            const tsunami::fvm::ScalarBoundaryConditionSet &momentum_y_conditions,
            const tsunami::fvm::ScalarBoundaryConditionSet &bed_conditions,
            std::vector<RegionalBoundaryOverrideSpecification> overrides) -> tsunami::core::Result<RegionalBoundaryConditionSet>;

        RegionalBoundaryConditionSet(
            tsunami::fvm::MeshBinding binding,
            std::vector<RegionalBoundaryCondition> conditions);

        tsunami::fvm::MeshBinding binding_;
        std::vector<RegionalBoundaryCondition> conditions_;
    };

    [[nodiscard]] auto make_regional_componentwise_boundary(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::fvm::ScalarBoundaryCondition &depth,
        const tsunami::fvm::ScalarBoundaryCondition &momentum_x,
        const tsunami::fvm::ScalarBoundaryCondition &momentum_y,
        const tsunami::fvm::ScalarBoundaryCondition &bed_elevation,
        bool require_executable) -> tsunami::core::Result<RegionalComponentwiseBoundary>;

    [[nodiscard]] auto make_regional_boundary_condition_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::fvm::ScalarBoundaryConditionSet &depth_conditions,
        const tsunami::fvm::ScalarBoundaryConditionSet &momentum_x_conditions,
        const tsunami::fvm::ScalarBoundaryConditionSet &momentum_y_conditions,
        const tsunami::fvm::ScalarBoundaryConditionSet &bed_conditions,
        std::vector<RegionalBoundaryOverrideSpecification> overrides) -> tsunami::core::Result<RegionalBoundaryConditionSet>;

} // namespace tsunami::r2d
