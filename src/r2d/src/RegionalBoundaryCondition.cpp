#include <tsunami/r2d/RegionalBoundaryCondition.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

#include <tsunami/r2d/ShallowWaterFlux.hpp>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto depth_unit = "m";
        constexpr auto momentum_unit = "m2/s";
        constexpr auto bed_unit = "m";
        constexpr auto rule_id = "SWE-R2D-BC";

        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto boundary_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh *mesh = nullptr,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt,
            std::optional<tsunami::fvm::FaceId> face_id = std::nullopt,
            std::optional<tsunami::fvm::BoundaryPatchId> patch_id = std::nullopt,
            std::string field_id = {},
            std::string expected_unit = {},
            std::string actual_unit = {}) -> tsunami::core::Error
        {
            const auto mesh_id = mesh ? mesh->summary().id : tsunami::fvm::MeshId{};
            return detail::r2d_error(
                std::move(code),
                std::move(message),
                std::move(operation),
                rule_id,
                mesh ? &mesh_id : nullptr,
                cell_id,
                face_id,
                patch_id,
                std::move(field_id),
                std::move(expected_unit),
                std::move(actual_unit));
        }

        [[nodiscard]] auto validate_scalar_condition(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::fvm::ScalarBoundaryCondition &condition,
            tsunami::fvm::BoundaryPatchId patch_id,
            std::string_view expected_unit,
            bool require_executable,
            std::string operation) -> tsunami::core::Result<void>
        {
            const auto descriptor = condition.descriptor();
            if (!condition.is_bound_to(mesh) || descriptor.patch_id != patch_id ||
                descriptor.entity_count != mesh.boundary_patch(patch_id).faces.size()) {
                return tsunami::core::failure(boundary_error(
                    "r2d.boundary.scalar_incompatible",
                    "component boundary condition is not compatible with the requested patch",
                    std::move(operation),
                    &mesh,
                    std::nullopt,
                    std::nullopt,
                    patch_id,
                    descriptor.id.value));
            }
            if (descriptor.unit_id != expected_unit) {
                return tsunami::core::failure(boundary_error(
                    "r2d.boundary.scalar_unit_incompatible",
                    "component boundary condition has an incompatible unit",
                    std::move(operation),
                    &mesh,
                    std::nullopt,
                    std::nullopt,
                    patch_id,
                    descriptor.id.value,
                    std::string{expected_unit},
                    descriptor.unit_id));
            }
            if (require_executable && !condition.is_executable()) {
                return tsunami::core::failure(boundary_error(
                    "r2d.boundary.scalar_not_executable",
                    "component boundary condition must be executable for componentwise mode",
                    std::move(operation),
                    &mesh,
                    std::nullopt,
                    std::nullopt,
                    patch_id,
                    descriptor.id.value));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto normal_magnitude(FaceNormal2D normal) -> tsunami::core::Real
        {
            return std::sqrt((normal.x * normal.x) + (normal.y * normal.y));
        }

        struct BoundaryVelocities
        {
            tsunami::core::Real normal{};
            tsunami::core::Real tangent{};
        };

        [[nodiscard]] auto project_velocity(PrimitiveVariables2D primitive, FaceNormal2D normal) -> BoundaryVelocities
        {
            return BoundaryVelocities{
                .normal = (primitive.velocity_x * normal.x) + (primitive.velocity_y * normal.y),
                .tangent = (-primitive.velocity_x * normal.y) + (primitive.velocity_y * normal.x)};
        }

        [[nodiscard]] auto cartesian_velocity(
            tsunami::core::Real normal_velocity,
            tsunami::core::Real tangential_velocity,
            FaceNormal2D normal) -> std::pair<tsunami::core::Real, tsunami::core::Real>
        {
            return {
                (normal_velocity * normal.x) - (tangential_velocity * normal.y),
                (normal_velocity * normal.y) + (tangential_velocity * normal.x)};
        }
    } // namespace

    auto validate_regional_far_field_state(RegionalFarFieldState state) -> tsunami::core::Result<void>
    {
        if (!finite(state.free_surface_elevation) || !finite(state.velocity_x) || !finite(state.velocity_y)) {
            return tsunami::core::failure(boundary_error(
                "r2d.boundary.reference_state_invalid",
                "regional far-field reference values must be finite",
                "validate_regional_far_field_state"));
        }
        return tsunami::core::success();
    }

    auto regional_reference_conserved_state(
        RegionalFarFieldState reference_state,
        tsunami::core::Real bed_elevation,
        const ShallowWaterStatePolicy &policy,
        std::optional<tsunami::fvm::CellId> cell_id) -> tsunami::core::Result<ConservedVariables2D>
    {
        auto reference_validation = validate_regional_far_field_state(reference_state);
        if (!reference_validation) {
            return tsunami::core::failure<ConservedVariables2D>(reference_validation.error());
        }
        if (!finite(bed_elevation)) {
            return tsunami::core::failure<ConservedVariables2D>(detail::r2d_error(
                "r2d.boundary.reference_bed_invalid",
                "bed elevation used for a regional reference state must be finite",
                "regional_reference_conserved_state",
                rule_id,
                nullptr,
                cell_id));
        }
        const auto depth = std::max(tsunami::core::Real{0.0}, reference_state.free_surface_elevation - bed_elevation);
        return validate_and_canonicalise_state(
            ConservedVariables2D{
                .depth = depth,
                .momentum_x = depth * reference_state.velocity_x,
                .momentum_y = depth * reference_state.velocity_y},
            policy,
            cell_id);
    }

    RegionalComponentwiseBoundary::RegionalComponentwiseBoundary(
        tsunami::fvm::MeshBinding binding,
        tsunami::fvm::BoundaryPatchId patch_id,
        std::size_t entity_count,
        tsunami::fvm::ScalarBoundaryCondition depth,
        tsunami::fvm::ScalarBoundaryCondition momentum_x,
        tsunami::fvm::ScalarBoundaryCondition momentum_y,
        tsunami::fvm::ScalarBoundaryCondition bed_elevation)
        : binding_{std::move(binding)}
        , patch_id_{patch_id}
        , entity_count_{entity_count}
        , depth_{std::move(depth)}
        , momentum_x_{std::move(momentum_x)}
        , momentum_y_{std::move(momentum_y)}
        , bed_elevation_{std::move(bed_elevation)}
    {
    }

    auto RegionalComponentwiseBoundary::clone() const -> RegionalComponentwiseBoundary
    {
        return RegionalComponentwiseBoundary{
            binding_,
            patch_id_,
            entity_count_,
            depth_.clone(),
            momentum_x_.clone(),
            momentum_y_.clone(),
            bed_elevation_.clone()};
    }

    auto RegionalComponentwiseBoundary::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               patch_id_.value < mesh.summary().boundary_patch_count &&
               entity_count_ == mesh.boundary_patch(patch_id_).faces.size() &&
               depth_.is_bound_to(mesh) &&
               momentum_x_.is_bound_to(mesh) &&
               momentum_y_.is_bound_to(mesh) &&
               bed_elevation_.is_bound_to(mesh);
    }

    auto RegionalComponentwiseBoundary::populate(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &depth,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_x,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_y,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &bed_elevation) const -> tsunami::core::Result<void>
    {
        auto depth_apply = depth_.apply(mesh, state.depth(), depth);
        if (!depth_apply) {
            return tsunami::core::failure(depth_apply.error());
        }
        auto qx_apply = momentum_x_.apply(mesh, state.momentum_x(), momentum_x);
        if (!qx_apply) {
            return tsunami::core::failure(qx_apply.error());
        }
        auto qy_apply = momentum_y_.apply(mesh, state.momentum_y(), momentum_y);
        if (!qy_apply) {
            return tsunami::core::failure(qy_apply.error());
        }
        return bed_elevation_.apply(mesh, bathymetry.bed_elevation(), bed_elevation);
    }

    RegionalTransmissiveBoundary::RegionalTransmissiveBoundary(
        tsunami::fvm::MeshBinding binding,
        tsunami::fvm::BoundaryPatchId patch_id)
        : binding_{std::move(binding)}
        , patch_id_{patch_id}
    {
    }

    auto RegionalTransmissiveBoundary::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) && patch_id_.value < mesh.summary().boundary_patch_count;
    }

    auto RegionalTransmissiveBoundary::populate(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &depth,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_x,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_y,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &bed_elevation) const -> tsunami::core::Result<void>
    {
        if (!is_bound_to(mesh) || !state.is_bound_to(mesh) || !bathymetry.is_bound_to(mesh)) {
            return tsunami::core::failure(boundary_error(
                "r2d.boundary.transmissive_incompatible",
                "transmissive boundary inputs are incompatible",
                "RegionalTransmissiveBoundary::populate",
                &mesh,
                std::nullopt,
                std::nullopt,
                patch_id_));
        }
        const auto &patch = mesh.boundary_patch(patch_id_);
        for (std::size_t local = 0; local < patch.faces.size(); ++local) {
            const auto &face = mesh.topology().face(patch.faces[local]);
            const auto owner = face.owner;
            const auto owner_state = state.local_state(owner);
            depth.at(local) = owner_state.depth;
            momentum_x.at(local) = owner_state.momentum_x;
            momentum_y.at(local) = owner_state.momentum_y;
            bed_elevation.at(local) = bathymetry.local_bed_elevation(owner);
            if (!finite(depth.at(local)) || !finite(momentum_x.at(local)) || !finite(momentum_y.at(local)) ||
                !finite(bed_elevation.at(local))) {
                return tsunami::core::failure(boundary_error(
                    "r2d.boundary.transmissive_state_invalid",
                    "transmissive exterior state must be finite",
                    "RegionalTransmissiveBoundary::populate",
                    &mesh,
                    owner,
                    face.id,
                    patch_id_));
            }
        }
        return tsunami::core::success();
    }

    RegionalRadiationBoundary::RegionalRadiationBoundary(
        tsunami::fvm::MeshBinding binding,
        tsunami::fvm::BoundaryPatchId patch_id,
        RegionalFarFieldState reference_state)
        : binding_{std::move(binding)}
        , patch_id_{patch_id}
        , reference_state_{reference_state}
    {
    }

    auto RegionalRadiationBoundary::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) && patch_id_.value < mesh.summary().boundary_patch_count;
    }

    auto RegionalRadiationBoundary::exterior_state(
        ConservedVariables2D interior,
        tsunami::core::Real bed_elevation,
        FaceNormal2D outward_normal,
        const ShallowWaterStatePolicy &policy,
        tsunami::core::Time time,
        std::optional<tsunami::fvm::FaceId> face_id) const -> tsunami::core::Result<ConservedVariables2D>
    {
        static_cast<void>(time);
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure<ConservedVariables2D>(policy_validation.error());
        }
        if (!finite(outward_normal.x) || !finite(outward_normal.y) || !finite(outward_normal.length) ||
            outward_normal.length <= 0.0 ||
            std::abs(normal_magnitude(outward_normal) - 1.0) > policy.normal_tolerance) {
            return tsunami::core::failure<ConservedVariables2D>(boundary_error(
                "r2d.boundary.normal_invalid",
                "radiation boundary requires a finite unit outward normal",
                "RegionalRadiationBoundary::exterior_state",
                nullptr,
                std::nullopt,
                face_id,
                patch_id_));
        }

        auto canonical_interior = validate_and_canonicalise_state(interior, policy);
        if (!canonical_interior) {
            return tsunami::core::failure<ConservedVariables2D>(canonical_interior.error());
        }
        auto reference = regional_reference_conserved_state(reference_state_, bed_elevation, policy);
        if (!reference) {
            return tsunami::core::failure<ConservedVariables2D>(reference.error());
        }

        const auto left = canonical_interior.value();
        const auto ref = reference.value();
        const auto left_dry = is_dry(left, policy);
        const auto ref_dry = is_dry(ref, policy);
        if (left_dry && ref_dry) {
            return tsunami::core::success(ConservedVariables2D{});
        }
        if (left_dry && !ref_dry) {
            return tsunami::core::success(ref);
        }

        auto left_primitive = recover_primitive_variables(left, policy);
        if (!left_primitive) {
            return tsunami::core::failure<ConservedVariables2D>(left_primitive.error());
        }
        const auto left_velocity = project_velocity(left_primitive.value(), outward_normal);
        const auto left_wave_speed = std::sqrt(policy.gravity * left.depth);

        if (left_velocity.normal - left_wave_speed >= 0.0) {
            return tsunami::core::success(left);
        }
        if (!ref_dry) {
            auto ref_primitive = recover_primitive_variables(ref, policy);
            if (!ref_primitive) {
                return tsunami::core::failure<ConservedVariables2D>(ref_primitive.error());
            }
            const auto ref_velocity = project_velocity(ref_primitive.value(), outward_normal);
            const auto ref_wave_speed = std::sqrt(policy.gravity * ref.depth);
            if (left_velocity.normal + left_wave_speed <= 0.0) {
                return tsunami::core::success(ref);
            }

            const auto outgoing = left_velocity.normal + (2.0 * left_wave_speed);
            const auto incoming = ref_velocity.normal - (2.0 * ref_wave_speed);
            const auto normal_velocity = 0.5 * (outgoing + incoming);
            const auto reconstructed_wave_speed = 0.25 * (outgoing - incoming);
            if (!finite(reconstructed_wave_speed) || reconstructed_wave_speed < -policy.depth_tolerance) {
                return tsunami::core::failure<ConservedVariables2D>(boundary_error(
                    "r2d.boundary.characteristic_invalid",
                    "radiation characteristic reconstruction produced an invalid wave speed",
                    "RegionalRadiationBoundary::exterior_state",
                    nullptr,
                    std::nullopt,
                    face_id,
                    patch_id_));
            }
            const auto clipped_wave_speed = std::max(tsunami::core::Real{0.0}, reconstructed_wave_speed);
            const auto depth = (clipped_wave_speed * clipped_wave_speed) / policy.gravity;
            const auto tangent_velocity = normal_velocity >= 0.0 ? left_velocity.tangent : ref_velocity.tangent;
            const auto [u, v] = cartesian_velocity(normal_velocity, tangent_velocity, outward_normal);
            if (!finite(depth) || !finite(u) || !finite(v)) {
                return tsunami::core::failure<ConservedVariables2D>(boundary_error(
                    "r2d.boundary.characteristic_invalid",
                    "radiation characteristic reconstruction produced nonfinite values",
                    "RegionalRadiationBoundary::exterior_state",
                    nullptr,
                    std::nullopt,
                    face_id,
                    patch_id_));
            }
            return validate_and_canonicalise_state(
                ConservedVariables2D{.depth = depth, .momentum_x = depth * u, .momentum_y = depth * v},
                policy);
        }

        const auto outgoing = left_velocity.normal + (2.0 * left_wave_speed);
        const auto incoming = 0.0;
        const auto normal_velocity = 0.5 * (outgoing + incoming);
        const auto reconstructed_wave_speed = 0.25 * (outgoing - incoming);
        if (!finite(reconstructed_wave_speed) || reconstructed_wave_speed < -policy.depth_tolerance) {
            return tsunami::core::failure<ConservedVariables2D>(boundary_error(
                "r2d.boundary.characteristic_invalid",
                "radiation characteristic reconstruction with dry reference is invalid",
                "RegionalRadiationBoundary::exterior_state",
                nullptr,
                std::nullopt,
                face_id,
                patch_id_));
        }
        const auto depth = (std::max(tsunami::core::Real{0.0}, reconstructed_wave_speed) *
                            std::max(tsunami::core::Real{0.0}, reconstructed_wave_speed)) /
                           policy.gravity;
        const auto [u, v] = cartesian_velocity(normal_velocity, left_velocity.tangent, outward_normal);
        return validate_and_canonicalise_state(
            ConservedVariables2D{.depth = depth, .momentum_x = depth * u, .momentum_y = depth * v},
            policy);
    }

    auto RegionalRadiationBoundary::populate(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        const ShallowWaterStatePolicy &policy,
        tsunami::core::Time time,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &depth,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_x,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &momentum_y,
        tsunami::fvm::BoundaryPatchField<tsunami::core::Real> &bed_elevation) const -> tsunami::core::Result<void>
    {
        if (!is_bound_to(mesh) || !state.is_bound_to(mesh) || !bathymetry.is_bound_to(mesh) || !finite(time)) {
            return tsunami::core::failure(boundary_error(
                "r2d.boundary.radiation_incompatible",
                "radiation boundary inputs are incompatible",
                "RegionalRadiationBoundary::populate",
                &mesh,
                std::nullopt,
                std::nullopt,
                patch_id_));
        }
        const auto &patch = mesh.boundary_patch(patch_id_);
        for (std::size_t local = 0; local < patch.faces.size(); ++local) {
            const auto &face = mesh.topology().face(patch.faces[local]);
            auto normal = make_face_normal(mesh.face_geometry(face.id).area_vector, policy, face.id);
            if (!normal) {
                return tsunami::core::failure(normal.error());
            }
            const auto owner = face.owner;
            auto exterior = exterior_state(
                state.local_state(owner),
                bathymetry.local_bed_elevation(owner),
                normal.value(),
                policy,
                time,
                face.id);
            if (!exterior) {
                return tsunami::core::failure(exterior.error());
            }
            depth.at(local) = exterior.value().depth;
            momentum_x.at(local) = exterior.value().momentum_x;
            momentum_y.at(local) = exterior.value().momentum_y;
            bed_elevation.at(local) = bathymetry.local_bed_elevation(owner);
        }
        return tsunami::core::success();
    }

    auto make_regional_componentwise_boundary(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::fvm::ScalarBoundaryCondition &depth,
        const tsunami::fvm::ScalarBoundaryCondition &momentum_x,
        const tsunami::fvm::ScalarBoundaryCondition &momentum_y,
        const tsunami::fvm::ScalarBoundaryCondition &bed_elevation,
        bool require_executable) -> tsunami::core::Result<RegionalComponentwiseBoundary>
    {
        const auto patch_id = depth.patch_id();
        if (momentum_x.patch_id() != patch_id || momentum_y.patch_id() != patch_id || bed_elevation.patch_id() != patch_id ||
            patch_id.value >= mesh.summary().boundary_patch_count) {
            return tsunami::core::failure<RegionalComponentwiseBoundary>(boundary_error(
                "r2d.boundary.componentwise_patch_mismatch",
                "componentwise boundary conditions must target the same patch",
                "make_regional_componentwise_boundary",
                &mesh,
                std::nullopt,
                std::nullopt,
                patch_id));
        }
        auto depth_validation = validate_scalar_condition(mesh, depth, patch_id, depth_unit, require_executable, "make_regional_componentwise_boundary");
        auto qx_validation = validate_scalar_condition(mesh, momentum_x, patch_id, momentum_unit, require_executable, "make_regional_componentwise_boundary");
        auto qy_validation = validate_scalar_condition(mesh, momentum_y, patch_id, momentum_unit, require_executable, "make_regional_componentwise_boundary");
        auto bed_validation = validate_scalar_condition(mesh, bed_elevation, patch_id, bed_unit, require_executable, "make_regional_componentwise_boundary");
        if (!depth_validation) {
            return tsunami::core::failure<RegionalComponentwiseBoundary>(depth_validation.error());
        }
        if (!qx_validation) {
            return tsunami::core::failure<RegionalComponentwiseBoundary>(qx_validation.error());
        }
        if (!qy_validation) {
            return tsunami::core::failure<RegionalComponentwiseBoundary>(qy_validation.error());
        }
        if (!bed_validation) {
            return tsunami::core::failure<RegionalComponentwiseBoundary>(bed_validation.error());
        }
        return tsunami::core::success(RegionalComponentwiseBoundary{
            tsunami::fvm::make_mesh_binding(mesh),
            patch_id,
            mesh.boundary_patch(patch_id).faces.size(),
            depth.clone(),
            momentum_x.clone(),
            momentum_y.clone(),
            bed_elevation.clone()});
    }

    RegionalBoundaryConditionSet::RegionalBoundaryConditionSet(
        tsunami::fvm::MeshBinding binding,
        std::vector<RegionalBoundaryCondition> conditions)
        : binding_{std::move(binding)}
        , conditions_{std::move(conditions)}
    {
    }

    auto RegionalBoundaryConditionSet::condition(tsunami::fvm::BoundaryPatchId patch_id) const -> const RegionalBoundaryCondition *
    {
        const auto found = std::ranges::find_if(conditions_, [patch_id](const auto &condition) {
            return std::visit([patch_id](const auto &operation) { return operation.patch_id() == patch_id; }, condition);
        });
        if (found == conditions_.end()) {
            return nullptr;
        }
        return std::addressof(*found);
    }

    auto RegionalBoundaryConditionSet::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               std::ranges::all_of(conditions_, [&mesh](const auto &condition) {
                   return std::visit([&mesh](const auto &operation) { return operation.is_bound_to(mesh); }, condition);
               });
    }

    auto RegionalBoundaryConditionSet::is_complete_for(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        if (!is_bound_to(mesh) || conditions_.size() != mesh.summary().boundary_patch_count) {
            return false;
        }
        for (std::size_t index = 0; index < conditions_.size(); ++index) {
            const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
            const auto matches = std::visit([patch_id](const auto &operation) { return operation.patch_id() == patch_id; }, conditions_[index]);
            if (!matches) {
                return false;
            }
        }
        return true;
    }

    auto RegionalBoundaryConditionSet::clone() const -> RegionalBoundaryConditionSet
    {
        std::vector<RegionalBoundaryCondition> copies;
        copies.reserve(conditions_.size());
        for (const auto &condition : conditions_) {
            copies.push_back(std::visit(
                [](const auto &operation) -> RegionalBoundaryCondition {
                    return RegionalBoundaryCondition{operation.clone()};
                },
                condition));
        }
        return RegionalBoundaryConditionSet{binding_, std::move(copies)};
    }

    auto make_regional_boundary_condition_set(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::fvm::ScalarBoundaryConditionSet &depth_conditions,
        const tsunami::fvm::ScalarBoundaryConditionSet &momentum_x_conditions,
        const tsunami::fvm::ScalarBoundaryConditionSet &momentum_y_conditions,
        const tsunami::fvm::ScalarBoundaryConditionSet &bed_conditions,
        std::vector<RegionalBoundaryOverrideSpecification> overrides) -> tsunami::core::Result<RegionalBoundaryConditionSet>
    {
        if (!depth_conditions.is_complete_for(mesh) || !momentum_x_conditions.is_complete_for(mesh) ||
            !momentum_y_conditions.is_complete_for(mesh) || !bed_conditions.is_complete_for(mesh)) {
            return tsunami::core::failure<RegionalBoundaryConditionSet>(boundary_error(
                "r2d.boundary.scalar_set_incomplete",
                "regional boundary factory requires complete scalar boundary sets",
                "make_regional_boundary_condition_set",
                &mesh));
        }

        std::map<std::string, tsunami::fvm::BoundaryPatchId, std::less<>> patches_by_name;
        for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
            patches_by_name.emplace(mesh.boundary_patch(patch_id).name, patch_id);
        }
        std::map<tsunami::fvm::BoundaryPatchId, RegionalBoundaryOverrideSpecification> overrides_by_patch;
        std::set<std::string, std::less<>> seen_tags;
        for (auto &override : overrides) {
            if (override.patch_tag.empty()) {
                return tsunami::core::failure<RegionalBoundaryConditionSet>(boundary_error(
                    "r2d.boundary.patch_tag_required",
                    "regional boundary override requires a patch tag",
                    "make_regional_boundary_condition_set",
                    &mesh));
            }
            if (!seen_tags.insert(override.patch_tag).second) {
                return tsunami::core::failure<RegionalBoundaryConditionSet>(boundary_error(
                    "r2d.boundary.patch_duplicate",
                    "regional boundary override patch tags must be unique",
                    "make_regional_boundary_condition_set",
                    &mesh));
            }
            const auto patch = patches_by_name.find(override.patch_tag);
            if (patch == patches_by_name.end()) {
                return tsunami::core::failure<RegionalBoundaryConditionSet>(boundary_error(
                    "r2d.boundary.patch_unknown",
                    "regional boundary override patch tag does not match the mesh",
                    "make_regional_boundary_condition_set",
                    &mesh));
            }
            if (std::holds_alternative<RegionalRadiationSpecification>(override.operation)) {
                auto validation = validate_regional_far_field_state(std::get<RegionalRadiationSpecification>(override.operation).reference_state);
                if (!validation) {
                    return tsunami::core::failure<RegionalBoundaryConditionSet>(validation.error());
                }
            }
            overrides_by_patch.emplace(patch->second, std::move(override));
        }

        std::vector<RegionalBoundaryCondition> conditions;
        conditions.reserve(mesh.summary().boundary_patch_count);
        for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
            const auto *depth = depth_conditions.condition(patch_id);
            const auto *qx = momentum_x_conditions.condition(patch_id);
            const auto *qy = momentum_y_conditions.condition(patch_id);
            const auto *bed = bed_conditions.condition(patch_id);
            if (depth == nullptr || qx == nullptr || qy == nullptr || bed == nullptr) {
                return tsunami::core::failure<RegionalBoundaryConditionSet>(boundary_error(
                    "r2d.boundary.scalar_set_incomplete",
                    "regional boundary factory could not resolve a scalar condition",
                    "make_regional_boundary_condition_set",
                    &mesh,
                    std::nullopt,
                    std::nullopt,
                    patch_id));
            }

            const auto override = overrides_by_patch.find(patch_id);
            if (override == overrides_by_patch.end()) {
                auto componentwise = make_regional_componentwise_boundary(mesh, *depth, *qx, *qy, *bed, true);
                if (!componentwise) {
                    return tsunami::core::failure<RegionalBoundaryConditionSet>(componentwise.error());
                }
                conditions.push_back(RegionalBoundaryCondition{std::move(componentwise).value()});
                continue;
            }

            auto component_validation = make_regional_componentwise_boundary(mesh, *depth, *qx, *qy, *bed, false);
            if (!component_validation) {
                return tsunami::core::failure<RegionalBoundaryConditionSet>(component_validation.error());
            }
            if (std::holds_alternative<RegionalTransmissiveSpecification>(override->second.operation)) {
                conditions.push_back(RegionalBoundaryCondition{RegionalTransmissiveBoundary{tsunami::fvm::make_mesh_binding(mesh), patch_id}});
            } else {
                const auto &spec = std::get<RegionalRadiationSpecification>(override->second.operation);
                conditions.push_back(RegionalBoundaryCondition{RegionalRadiationBoundary{tsunami::fvm::make_mesh_binding(mesh), patch_id, spec.reference_state}});
            }
        }

        auto set = RegionalBoundaryConditionSet{tsunami::fvm::make_mesh_binding(mesh), std::move(conditions)};
        if (!set.is_complete_for(mesh)) {
            return tsunami::core::failure<RegionalBoundaryConditionSet>(boundary_error(
                "r2d.boundary.set_incomplete",
                "regional boundary set is not complete for the mesh",
                "make_regional_boundary_condition_set",
                &mesh));
        }
        return tsunami::core::success(std::move(set));
    }

} // namespace tsunami::r2d
