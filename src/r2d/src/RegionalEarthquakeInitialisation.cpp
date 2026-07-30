#include <tsunami/r2d/RegionalEarthquakeInitialisation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <tsunami/r2d/RegionalDiagnostics.hpp>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto metre_unit = "m";
        constexpr auto passive_tolerance = 1.0e-10;

        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto earthquake_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh *mesh = nullptr,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt,
            std::optional<tsunami::fvm::BoundaryPatchId> patch_id = std::nullopt,
            const RegionalEarthquakeSourceMetadata *metadata = nullptr,
            RegionalBedDeformationMappingKind mapping = RegionalBedDeformationMappingKind::vertical_only,
            RegionalSurfaceTransferKind transfer = RegionalSurfaceTransferKind::passive_equal_to_effective_bed) -> tsunami::core::Error
        {
            const auto mesh_id = mesh ? mesh->summary().id : tsunami::fvm::MeshId{};
            auto error = detail::r2d_error(
                std::move(code),
                std::move(message),
                std::move(operation),
                "SWE-R2D-EQK",
                mesh ? &mesh_id : nullptr,
                cell_id,
                std::nullopt,
                patch_id);
            error.add_context("bed_mapping", std::string{to_string(mapping)})
                .add_context("surface_transfer", std::string{to_string(transfer)})
                .add_context("state_changed", "false");
            if (metadata != nullptr) {
                error.add_context("event_id", metadata->event_id)
                    .add_context("model_id", metadata->model_id)
                    .add_context("source_kind", std::string{to_string(metadata->source_kind)});
            }
            return error;
        }

        [[nodiscard]] auto make_cell_scalar(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string id,
            std::string name,
            tsunami::core::Real initial = 0.0) -> tsunami::core::Result<tsunami::fvm::CellScalarField>
        {
            return tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
                mesh,
                tsunami::fvm::FieldId{std::move(id)},
                std::move(name),
                metre_unit,
                initial);
        }

        [[nodiscard]] auto make_face_scalar(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string id,
            std::string name) -> tsunami::core::Result<tsunami::fvm::FaceScalarField>
        {
            return tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::face>(
                mesh,
                tsunami::fvm::FieldId{std::move(id)},
                std::move(name),
                metre_unit,
                0.0);
        }

        [[nodiscard]] auto make_cell_vector(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string id,
            std::string name) -> tsunami::core::Result<tsunami::fvm::CellVectorField>
        {
            return tsunami::fvm::make_filled_mesh_field<tsunami::fvm::Vector3, tsunami::fvm::FieldLocation::cell>(
                mesh,
                tsunami::fvm::FieldId{std::move(id)},
                std::move(name),
                "m/m",
                tsunami::fvm::Vector3{});
        }

        [[nodiscard]] auto scratch_for(
            tsunami::fvm::CellScalarField &destination,
            RegionalEarthquakeInitialisationWorkspace &workspace) -> tsunami::fvm::CellScalarField *
        {
            auto *scratch = std::addressof(workspace.raw_post_event_depth());
            if (scratch == std::addressof(destination)) {
                scratch = std::addressof(workspace.surface_perturbation());
            }
            if (scratch == std::addressof(destination)) {
                scratch = std::addressof(workspace.pre_event_free_surface());
            }
            return scratch;
        }

        [[nodiscard]] auto validate_prescribed_surface(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::fvm::CellScalarField *field,
            const RegionalEarthquakeInitialisationRequest &request) -> tsunami::core::Result<void>
        {
            if (field == nullptr) {
                return tsunami::core::failure(earthquake_error(
                    "r2d.earthquake.prescribed_surface_required",
                    "prescribed surface transfer requires a surface perturbation field",
                    "initialise_regional_earthquake_state",
                    &mesh,
                    std::nullopt,
                    std::nullopt,
                    &request.metadata,
                    request.bed_mapping,
                    request.surface_transfer));
            }
            const auto descriptor = field->descriptor();
            if (!field->is_bound_to(mesh) || field->size() != mesh.summary().cell_count || descriptor.unit_id != metre_unit) {
                return tsunami::core::failure(earthquake_error(
                    "r2d.earthquake.prescribed_surface_invalid",
                    "prescribed surface perturbation is not mesh-bound in metres",
                    "initialise_regional_earthquake_state",
                    &mesh,
                    std::nullopt,
                    std::nullopt,
                    &request.metadata,
                    request.bed_mapping,
                    request.surface_transfer));
            }
            for (std::size_t index = 0; index < field->size(); ++index) {
                if (!finite(field->at(index))) {
                    return tsunami::core::failure(earthquake_error(
                        "r2d.earthquake.prescribed_surface_invalid",
                        "prescribed surface perturbation contains a nonfinite value",
                        "initialise_regional_earthquake_state",
                        &mesh,
                        tsunami::fvm::CellId{index},
                        std::nullopt,
                        &request.metadata,
                        request.bed_mapping,
                        request.surface_transfer));
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto maximum_momentum(
            const RegionalConservedState &state,
            const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::core::Real
        {
            auto maximum = tsunami::core::Real{0.0};
            for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
                const auto local = state.local_state(tsunami::fvm::CellId{index});
                maximum = std::max(maximum, std::hypot(local.momentum_x, local.momentum_y));
            }
            return maximum;
        }

        auto update_minmax(tsunami::core::Real value, tsunami::core::Real &minimum, tsunami::core::Real &maximum) -> void
        {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }

        [[nodiscard]] auto make_diagnostics(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalBathymetry &pre_bathymetry,
            const RegionalBathymetry &post_bathymetry,
            const RegionalConservedState &pre_state,
            const RegionalConservedState &post_state,
            const RegionalSeabedDisplacement &displacement,
            const tsunami::fvm::CellScalarField &effective,
            const tsunami::fvm::CellScalarField &surface,
            const RegionalEarthquakeInitialisationRequest &request) -> tsunami::core::Result<RegionalEarthquakeInitialisationDiagnostics>
        {
            auto pre_integrals = calculate_regional_integrals(mesh, pre_state, request.state_policy);
            auto post_integrals = calculate_regional_integrals(mesh, post_state, request.state_policy);
            if (!pre_integrals) {
                return tsunami::core::failure<RegionalEarthquakeInitialisationDiagnostics>(pre_integrals.error());
            }
            if (!post_integrals) {
                return tsunami::core::failure<RegionalEarthquakeInitialisationDiagnostics>(post_integrals.error());
            }

            auto diagnostics = RegionalEarthquakeInitialisationDiagnostics{};
            diagnostics.metadata = request.metadata;
            diagnostics.bed_mapping = request.bed_mapping;
            diagnostics.surface_transfer = request.surface_transfer;
            diagnostics.cell_count = mesh.summary().cell_count;
            diagnostics.minimum_eastward_displacement = std::numeric_limits<tsunami::core::Real>::infinity();
            diagnostics.minimum_northward_displacement = diagnostics.minimum_eastward_displacement;
            diagnostics.minimum_upward_displacement = diagnostics.minimum_eastward_displacement;
            diagnostics.minimum_effective_bed_displacement = diagnostics.minimum_eastward_displacement;
            diagnostics.minimum_surface_perturbation = diagnostics.minimum_eastward_displacement;
            diagnostics.maximum_eastward_displacement = -diagnostics.minimum_eastward_displacement;
            diagnostics.maximum_northward_displacement = diagnostics.maximum_eastward_displacement;
            diagnostics.maximum_upward_displacement = diagnostics.maximum_eastward_displacement;
            diagnostics.maximum_effective_bed_displacement = diagnostics.maximum_eastward_displacement;
            diagnostics.maximum_surface_perturbation = diagnostics.maximum_eastward_displacement;
            diagnostics.pre_event_water_volume = pre_integrals.value().water_volume;
            diagnostics.post_event_water_volume = post_integrals.value().water_volume;
            diagnostics.water_volume_change = diagnostics.post_event_water_volume - diagnostics.pre_event_water_volume;
            diagnostics.pre_event_maximum_momentum = maximum_momentum(pre_state, mesh);
            diagnostics.post_event_maximum_momentum = maximum_momentum(post_state, mesh);

            for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
                const auto cell_id = tsunami::fvm::CellId{index};
                const auto area = mesh.cell_geometry(cell_id).measure;
                const auto local = displacement.local_displacement(cell_id);
                const auto eff = effective.at(index);
                const auto eta = surface.at(index);
                const auto pre = pre_state.local_state(cell_id);
                const auto post = post_state.local_state(cell_id);
                update_minmax(local.eastward, diagnostics.minimum_eastward_displacement, diagnostics.maximum_eastward_displacement);
                update_minmax(local.northward, diagnostics.minimum_northward_displacement, diagnostics.maximum_northward_displacement);
                update_minmax(local.upward, diagnostics.minimum_upward_displacement, diagnostics.maximum_upward_displacement);
                update_minmax(eff, diagnostics.minimum_effective_bed_displacement, diagnostics.maximum_effective_bed_displacement);
                update_minmax(eta, diagnostics.minimum_surface_perturbation, diagnostics.maximum_surface_perturbation);
                diagnostics.integrated_upward_displacement += area * local.upward;
                diagnostics.integrated_effective_bed_displacement += area * eff;
                diagnostics.integrated_surface_perturbation += area * eta;
                diagnostics.maximum_absolute_bathymetry_change = std::max(
                    diagnostics.maximum_absolute_bathymetry_change,
                    std::abs(post_bathymetry.local_bed_elevation(cell_id) - pre_bathymetry.local_bed_elevation(cell_id)));
                diagnostics.maximum_absolute_surface_perturbation = std::max(diagnostics.maximum_absolute_surface_perturbation, std::abs(eta));
                diagnostics.maximum_absolute_depth_change = std::max(diagnostics.maximum_absolute_depth_change, std::abs(post.depth - pre.depth));
                if (is_dry(pre, request.state_policy) && is_wet(post, request.state_policy)) {
                    ++diagnostics.newly_wet_cell_count;
                }
                if (is_wet(pre, request.state_policy) && is_dry(post, request.state_policy)) {
                    ++diagnostics.newly_dry_cell_count;
                }
            }
            return tsunami::core::success(std::move(diagnostics));
        }
    } // namespace

    RegionalEarthquakeInitialisationWorkspace::RegionalEarthquakeInitialisationWorkspace(
        tsunami::fvm::MeshBinding binding,
        tsunami::fvm::LinearInterpolationStencil stencil,
        tsunami::fvm::ScalarLinearInterpolationWorkspace interpolation,
        tsunami::fvm::GreenGaussGradientWorkspace gradient,
        tsunami::fvm::FaceScalarField face_bathymetry,
        tsunami::fvm::CellVectorField bathymetry_gradient,
        tsunami::fvm::CellScalarField effective_bed_displacement,
        tsunami::fvm::CellScalarField surface_perturbation,
        tsunami::fvm::CellScalarField pre_event_free_surface,
        tsunami::fvm::CellScalarField post_event_free_surface,
        tsunami::fvm::CellScalarField raw_post_event_depth)
        : binding_{std::move(binding)}
        , stencil_{std::move(stencil)}
        , interpolation_{std::move(interpolation)}
        , gradient_{std::move(gradient)}
        , face_bathymetry_{std::move(face_bathymetry)}
        , bathymetry_gradient_{std::move(bathymetry_gradient)}
        , effective_bed_displacement_{std::move(effective_bed_displacement)}
        , surface_perturbation_{std::move(surface_perturbation)}
        , pre_event_free_surface_{std::move(pre_event_free_surface)}
        , post_event_free_surface_{std::move(post_event_free_surface)}
        , raw_post_event_depth_{std::move(raw_post_event_depth)}
    {
    }

    auto RegionalEarthquakeInitialisationWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               stencil_.is_bound_to(mesh) && interpolation_.is_bound_to(mesh) &&
               gradient_.is_bound_to(mesh) && face_bathymetry_.is_bound_to(mesh) &&
               bathymetry_gradient_.is_bound_to(mesh) && effective_bed_displacement_.is_bound_to(mesh) &&
               surface_perturbation_.is_bound_to(mesh) && pre_event_free_surface_.is_bound_to(mesh) &&
               post_event_free_surface_.is_bound_to(mesh) && raw_post_event_depth_.is_bound_to(mesh);
    }

    auto make_regional_earthquake_initialisation_workspace(
        const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::core::Result<RegionalEarthquakeInitialisationWorkspace>
    {
        auto stencil = tsunami::fvm::make_linear_interpolation_stencil(mesh);
        auto interpolation = tsunami::fvm::make_linear_interpolation_workspace<tsunami::core::Real>(mesh, metre_unit);
        auto gradient = tsunami::fvm::make_green_gauss_gradient_workspace(mesh);
        auto face_bathymetry = make_face_scalar(mesh, "regional.earthquake.face-bathymetry", "earthquake face bathymetry");
        auto bathymetry_gradient = make_cell_vector(mesh, "regional.earthquake.bathymetry-gradient", "earthquake bathymetry gradient");
        auto effective = make_cell_scalar(mesh, "regional.earthquake.effective-bed-displacement", "effective earthquake bed displacement");
        auto surface = make_cell_scalar(mesh, "regional.earthquake.surface-perturbation", "earthquake surface perturbation");
        auto pre_surface = make_cell_scalar(mesh, "regional.earthquake.pre-event-free-surface", "pre-event free surface");
        auto post_surface = make_cell_scalar(mesh, "regional.earthquake.post-event-free-surface", "post-event free surface");
        auto raw_depth = make_cell_scalar(mesh, "regional.earthquake.raw-post-event-depth", "raw post-event depth");
        if (!stencil) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationWorkspace>(stencil.error());
        }
        if (!interpolation) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationWorkspace>(interpolation.error());
        }
        if (!gradient) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationWorkspace>(gradient.error());
        }
        if (!face_bathymetry) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationWorkspace>(face_bathymetry.error());
        }
        if (!bathymetry_gradient) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationWorkspace>(bathymetry_gradient.error());
        }
        if (!effective) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationWorkspace>(effective.error());
        }
        if (!surface) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationWorkspace>(surface.error());
        }
        if (!pre_surface) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationWorkspace>(pre_surface.error());
        }
        if (!post_surface) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationWorkspace>(post_surface.error());
        }
        if (!raw_depth) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationWorkspace>(raw_depth.error());
        }
        return tsunami::core::success(RegionalEarthquakeInitialisationWorkspace{
            tsunami::fvm::make_mesh_binding(mesh),
            std::move(stencil).value(),
            std::move(interpolation).value(),
            std::move(gradient).value(),
            std::move(face_bathymetry).value(),
            std::move(bathymetry_gradient).value(),
            std::move(effective).value(),
            std::move(surface).value(),
            std::move(pre_surface).value(),
            std::move(post_surface).value(),
            std::move(raw_depth).value()});
    }

    auto calculate_effective_seabed_displacement(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalBathymetry &bathymetry,
        const RegionalSeabedDisplacement &displacement,
        RegionalBedDeformationMappingKind mapping,
        const ScalarBoundaryConditionSet *bathymetry_boundaries,
        tsunami::fvm::CellScalarField &destination,
        RegionalEarthquakeInitialisationWorkspace &workspace) -> tsunami::core::Result<void>
    {
        if (!bathymetry.is_bound_to(mesh) || !displacement.is_bound_to(mesh) || !destination.is_bound_to(mesh) ||
            destination.size() != mesh.summary().cell_count || destination.descriptor().unit_id != metre_unit ||
            !workspace.is_bound_to(mesh)) {
            return tsunami::core::failure(earthquake_error(
                "r2d.earthquake.workspace_incompatible",
                "earthquake effective-displacement inputs are mesh incompatible",
                "calculate_effective_seabed_displacement",
                &mesh,
                std::nullopt,
                std::nullopt,
                nullptr,
                mapping));
        }
        auto *scratch = scratch_for(destination, workspace);
        if (mapping == RegionalBedDeformationMappingKind::vertical_only) {
            for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
                scratch->at(index) = displacement.upward().at(index);
            }
            return destination.copy_values_from(*scratch);
        }
        if (bathymetry_boundaries == nullptr) {
            return tsunami::core::failure(earthquake_error(
                "r2d.earthquake.bathymetry_boundary_required",
                "horizontal slope correction requires bathymetry boundary conditions",
                "calculate_effective_seabed_displacement",
                &mesh,
                std::nullopt,
                std::nullopt,
                nullptr,
                mapping));
        }
        if (!bathymetry_boundaries->is_bound_to(mesh) || !bathymetry_boundaries->is_complete_for(mesh)) {
            return tsunami::core::failure(earthquake_error(
                "r2d.earthquake.bathymetry_boundary_invalid",
                "bathymetry boundary set is incompatible or incomplete",
                "calculate_effective_seabed_displacement",
                &mesh,
                std::nullopt,
                std::nullopt,
                nullptr,
                mapping));
        }
        auto interpolated = tsunami::fvm::interpolate_cell_to_face(
            mesh,
            workspace.stencil(),
            bathymetry.bed_elevation(),
            *bathymetry_boundaries,
            workspace.face_bathymetry(),
            workspace.interpolation_workspace());
        if (!interpolated) {
            return tsunami::core::failure(earthquake_error(
                "r2d.earthquake.interpolation_failed",
                "bathymetry interpolation failed during earthquake slope correction",
                "calculate_effective_seabed_displacement",
                &mesh,
                std::nullopt,
                std::nullopt,
                nullptr,
                mapping)
                    .with_cause_code(interpolated.error().code()));
        }
        auto gradient = tsunami::fvm::green_gauss_gradient(
            mesh,
            workspace.face_bathymetry(),
            workspace.bathymetry_gradient(),
            workspace.gradient_workspace());
        if (!gradient) {
            return tsunami::core::failure(earthquake_error(
                "r2d.earthquake.gradient_failed",
                "bathymetry gradient failed during earthquake slope correction",
                "calculate_effective_seabed_displacement",
                &mesh,
                std::nullopt,
                std::nullopt,
                nullptr,
                mapping)
                    .with_cause_code(gradient.error().code()));
        }
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto local = displacement.local_displacement(cell_id);
            const auto slope = workspace.bathymetry_gradient().at(index);
            if (!finite(slope.x) || !finite(slope.y) || !finite(slope.z)) {
                return tsunami::core::failure(earthquake_error(
                    "r2d.earthquake.gradient_failed",
                    "bathymetry gradient contains a nonfinite component",
                    "calculate_effective_seabed_displacement",
                    &mesh,
                    cell_id,
                    std::nullopt,
                    nullptr,
                    mapping));
            }
            const auto effective = local.upward - (local.eastward * slope.x) - (local.northward * slope.y);
            if (!finite(effective)) {
                return tsunami::core::failure(earthquake_error(
                    "r2d.earthquake.effective_displacement_nonfinite",
                    "effective seabed displacement is nonfinite",
                    "calculate_effective_seabed_displacement",
                    &mesh,
                    cell_id,
                    std::nullopt,
                    nullptr,
                    mapping));
            }
            scratch->at(index) = effective;
        }
        return destination.copy_values_from(*scratch);
    }

    auto initialise_regional_earthquake_state(
        const RegionalEarthquakeInitialisationRequest &request,
        RegionalEarthquakeInitialisationWorkspace &workspace) -> tsunami::core::Result<RegionalEarthquakeInitialisationResult>
    {
        if (request.mesh == nullptr || request.pre_event_bathymetry == nullptr ||
            request.pre_event_state == nullptr || request.seabed_displacement == nullptr) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(earthquake_error(
                "r2d.earthquake.request_invalid",
                "earthquake initialisation request pointers are required",
                "initialise_regional_earthquake_state",
                request.mesh,
                std::nullopt,
                std::nullopt,
                &request.metadata,
                request.bed_mapping,
                request.surface_transfer));
        }
        const auto &mesh = *request.mesh;
        auto policy_validation = validate_policy(request.state_policy);
        if (!policy_validation) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(policy_validation.error());
        }
        if (!finite(request.zero_momentum_tolerance) || request.zero_momentum_tolerance < 0.0) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(earthquake_error(
                "r2d.earthquake.momentum_tolerance_invalid",
                "zero momentum tolerance must be finite and nonnegative",
                "initialise_regional_earthquake_state",
                &mesh,
                std::nullopt,
                std::nullopt,
                &request.metadata,
                request.bed_mapping,
                request.surface_transfer));
        }
        auto metadata_validation = validate_regional_earthquake_source_metadata(request.metadata);
        if (!metadata_validation) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(metadata_validation.error());
        }
        if (!request.pre_event_bathymetry->is_bound_to(mesh) || !request.pre_event_state->is_bound_to(mesh) ||
            !request.seabed_displacement->is_bound_to(mesh) || !workspace.is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(earthquake_error(
                "r2d.earthquake.request_invalid",
                "earthquake initialisation inputs are not bound to the mesh",
                "initialise_regional_earthquake_state",
                &mesh,
                std::nullopt,
                std::nullopt,
                &request.metadata,
                request.bed_mapping,
                request.surface_transfer));
        }
        auto pre_state = request.pre_event_state->clone();
        auto pre_validation = validate_and_canonicalise(pre_state, request.state_policy);
        if (!pre_validation) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(pre_validation.error());
        }
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            auto local = pre_state.local_state(tsunami::fvm::CellId{index});
            const auto momentum = std::hypot(local.momentum_x, local.momentum_y);
            if (!finite(momentum) || momentum > request.zero_momentum_tolerance) {
                auto error = earthquake_error(
                    "r2d.earthquake.pre_event_momentum_nonzero",
                    "pre-event state contains materially nonzero momentum",
                    "initialise_regional_earthquake_state",
                    &mesh,
                    tsunami::fvm::CellId{index},
                    std::nullopt,
                    &request.metadata,
                    request.bed_mapping,
                    request.surface_transfer);
                error.add_context("momentum_magnitude", std::to_string(momentum))
                    .add_context("tolerance", std::to_string(request.zero_momentum_tolerance));
                return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(error);
            }
            local.momentum_x = 0.0;
            local.momentum_y = 0.0;
            pre_state.set_local_state(tsunami::fvm::CellId{index}, local);
        }

        auto effective = calculate_effective_seabed_displacement(
            mesh,
            *request.pre_event_bathymetry,
            *request.seabed_displacement,
            request.bed_mapping,
            request.bathymetry_boundaries,
            workspace.effective_bed_displacement(),
            workspace);
        if (!effective) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(effective.error());
        }
        if (request.surface_transfer == RegionalSurfaceTransferKind::prescribed) {
            auto prescribed_validation = validate_prescribed_surface(mesh, request.prescribed_surface_perturbation, request);
            if (!prescribed_validation) {
                return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(prescribed_validation.error());
            }
            auto copy = workspace.surface_perturbation().copy_values_from(*request.prescribed_surface_perturbation);
            if (!copy) {
                return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(copy.error());
            }
        } else {
            auto copy = workspace.surface_perturbation().copy_values_from(workspace.effective_bed_displacement());
            if (!copy) {
                return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(copy.error());
            }
        }

        std::vector<tsunami::core::Real> post_bed;
        std::vector<tsunami::core::Real> post_depth;
        std::vector<tsunami::core::Real> post_qx(mesh.summary().cell_count, 0.0);
        std::vector<tsunami::core::Real> post_qy(mesh.summary().cell_count, 0.0);
        post_bed.reserve(mesh.summary().cell_count);
        post_depth.reserve(mesh.summary().cell_count);
        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto pre_bed = request.pre_event_bathymetry->local_bed_elevation(cell_id);
            const auto pre_local = pre_state.local_state(cell_id);
            const auto eta_minus = pre_local.depth + pre_bed;
            const auto bed_plus = pre_bed + workspace.effective_bed_displacement().at(index);
            const auto eta_plus = eta_minus + workspace.surface_perturbation().at(index);
            const auto raw_depth = eta_plus - bed_plus;
            if (!finite(bed_plus)) {
                return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(earthquake_error(
                    "r2d.earthquake.post_event_bathymetry_nonfinite",
                    "post-event bathymetry is nonfinite",
                    "initialise_regional_earthquake_state",
                    &mesh,
                    cell_id,
                    std::nullopt,
                    &request.metadata,
                    request.bed_mapping,
                    request.surface_transfer));
            }
            if (!finite(eta_plus)) {
                return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(earthquake_error(
                    "r2d.earthquake.post_event_surface_nonfinite",
                    "post-event free surface is nonfinite",
                    "initialise_regional_earthquake_state",
                    &mesh,
                    cell_id,
                    std::nullopt,
                    &request.metadata,
                    request.bed_mapping,
                    request.surface_transfer));
            }
            if (!finite(raw_depth)) {
                return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(earthquake_error(
                    "r2d.earthquake.post_event_depth_nonfinite",
                    "post-event raw depth is nonfinite",
                    "initialise_regional_earthquake_state",
                    &mesh,
                    cell_id,
                    std::nullopt,
                    &request.metadata,
                    request.bed_mapping,
                    request.surface_transfer));
            }
            workspace.pre_event_free_surface().at(index) = eta_minus;
            workspace.post_event_free_surface().at(index) = eta_plus;
            workspace.raw_post_event_depth().at(index) = raw_depth;
            post_bed.push_back(bed_plus);
            post_depth.push_back(std::max(0.0, raw_depth));
        }
        auto post_bathymetry = make_regional_bathymetry(
            mesh,
            request.pre_event_bathymetry->bed_elevation().descriptor().id,
            request.pre_event_bathymetry->bed_elevation().descriptor().name,
            std::move(post_bed));
        if (!post_bathymetry) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(post_bathymetry.error());
        }
        auto post_state = make_regional_conserved_state(
            mesh,
            request.pre_event_state->depth().descriptor().id,
            request.pre_event_state->momentum_x().descriptor().id,
            request.pre_event_state->momentum_y().descriptor().id,
            std::move(post_depth),
            std::move(post_qx),
            std::move(post_qy),
            request.state_policy);
        if (!post_state) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(earthquake_error(
                "r2d.earthquake.post_event_state_invalid",
                "post-event conserved state is invalid",
                "initialise_regional_earthquake_state",
                &mesh,
                std::nullopt,
                std::nullopt,
                &request.metadata,
                request.bed_mapping,
                request.surface_transfer)
                    .with_cause_code(post_state.error().code()));
        }
        auto diagnostics = make_diagnostics(
            mesh,
            *request.pre_event_bathymetry,
            post_bathymetry.value(),
            pre_state,
            post_state.value(),
            *request.seabed_displacement,
            workspace.effective_bed_displacement(),
            workspace.surface_perturbation(),
            request);
        if (!diagnostics) {
            return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(diagnostics.error());
        }
        if (request.surface_transfer == RegionalSurfaceTransferKind::passive_equal_to_effective_bed) {
            const auto volume_scale = std::max<tsunami::core::Real>(1.0, std::abs(diagnostics.value().pre_event_water_volume));
            if (std::abs(diagnostics.value().water_volume_change) > passive_tolerance * volume_scale ||
                diagnostics.value().newly_wet_cell_count != 0U || diagnostics.value().newly_dry_cell_count != 0U) {
                return tsunami::core::failure<RegionalEarthquakeInitialisationResult>(earthquake_error(
                    "r2d.earthquake.diagnostics_invalid",
                    "passive earthquake transfer changed depth, volume or wet/dry classification materially",
                    "initialise_regional_earthquake_state",
                    &mesh,
                    std::nullopt,
                    std::nullopt,
                    &request.metadata,
                    request.bed_mapping,
                    request.surface_transfer));
            }
        }
        return tsunami::core::success(RegionalEarthquakeInitialisationResult{
            std::move(post_bathymetry).value(),
            RegionalSimulationState{std::move(post_state).value(), 0.0, 0U},
            std::move(diagnostics).value()});
    }

} // namespace tsunami::r2d
