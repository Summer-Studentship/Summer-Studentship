#include <tsunami/r2d/WellBalancedResidualEvaluation.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <string>

#ifdef TSUNAMI_ENABLE_OPENMP
#include <omp.h>
#endif

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto depth_unit = "m";
        constexpr auto momentum_unit = "m2/s";
        constexpr auto bed_unit = "m";
        constexpr auto spectral_unit = "m2/s";
        constexpr auto outgoing_unit = "m3/s";

        [[nodiscard]] auto finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto mesh_id(const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::fvm::MeshId
        {
            return mesh.summary().id;
        }

        auto add_flux(RegionalResidual &residual, tsunami::fvm::CellId cell_id, ShallowWaterFlux2D flux, tsunami::core::Real factor) -> void
        {
            residual.mass().at(cell_id.value) += factor * flux.mass;
            residual.momentum_x().at(cell_id.value) += factor * flux.momentum_x;
            residual.momentum_y().at(cell_id.value) += factor * flux.momentum_y;
        }

        struct ThreadLocalResidualBuffers
        {
            std::size_t thread_count{};
            std::size_t cell_count{};
            std::vector<tsunami::core::Real> mass;
            std::vector<tsunami::core::Real> momentum_x;
            std::vector<tsunami::core::Real> momentum_y;
            std::vector<tsunami::core::Real> spectral_sum;
            std::vector<tsunami::core::Real> outgoing_mass_rate;

            ThreadLocalResidualBuffers(std::size_t threads, std::size_t cells)
                : thread_count{threads}
                , cell_count{cells}
                , mass(threads * cells, 0.0)
                , momentum_x(threads * cells, 0.0)
                , momentum_y(threads * cells, 0.0)
                , spectral_sum(threads * cells, 0.0)
                , outgoing_mass_rate(threads * cells, 0.0)
            {
            }
        };

        [[nodiscard]] auto regional_openmp_thread_count() -> std::size_t
        {
#ifdef TSUNAMI_ENABLE_OPENMP
            return static_cast<std::size_t>(std::max(1, omp_get_max_threads()));
#else
            return 1U;
#endif
        }

        [[nodiscard]] auto use_openmp_face_path(std::size_t face_count, std::size_t cell_count) -> bool
        {
#ifdef TSUNAMI_ENABLE_OPENMP
            return regional_openmp_thread_count() > 1U && face_count >= 512U && cell_count >= 256U;
#else
            (void)face_count;
            (void)cell_count;
            return false;
#endif
        }

        auto add_thread_flux(
            ThreadLocalResidualBuffers &buffers,
            std::size_t thread_id,
            tsunami::fvm::CellId cell_id,
            ShallowWaterFlux2D flux,
            tsunami::core::Real factor) -> void
        {
            const auto offset = (thread_id * buffers.cell_count) + cell_id.value;
            buffers.mass[offset] += factor * flux.mass;
            buffers.momentum_x[offset] += factor * flux.momentum_x;
            buffers.momentum_y[offset] += factor * flux.momentum_y;
        }

        auto record_parallel_failure(std::atomic<std::size_t> &failed_face, std::size_t index) -> void
        {
            auto current = failed_face.load(std::memory_order_relaxed);
            while (index < current &&
                   !failed_face.compare_exchange_weak(current, index, std::memory_order_relaxed, std::memory_order_relaxed)) {
            }
        }

        auto reduce_thread_buffers(
            const ThreadLocalResidualBuffers &buffers,
            RegionalResidual &residual,
            tsunami::fvm::CellScalarField &spectral_sum,
            tsunami::fvm::CellScalarField &outgoing_mass_rate) -> void
        {
            for (std::size_t cell = 0; cell < buffers.cell_count; ++cell) {
                auto mass = tsunami::core::Real{0.0};
                auto qx = tsunami::core::Real{0.0};
                auto qy = tsunami::core::Real{0.0};
                auto spectral = tsunami::core::Real{0.0};
                auto outgoing = tsunami::core::Real{0.0};
                for (std::size_t thread = 0; thread < buffers.thread_count; ++thread) {
                    const auto offset = (thread * buffers.cell_count) + cell;
                    mass += buffers.mass[offset];
                    qx += buffers.momentum_x[offset];
                    qy += buffers.momentum_y[offset];
                    spectral += buffers.spectral_sum[offset];
                    outgoing += buffers.outgoing_mass_rate[offset];
                }
                residual.mass().at(cell) = mass;
                residual.momentum_x().at(cell) = qx;
                residual.momentum_y().at(cell) = qy;
                spectral_sum.at(cell) = spectral;
                outgoing_mass_rate.at(cell) = outgoing;
            }
        }

        [[nodiscard]] auto sum_flux(ShallowWaterFlux2D left, ShallowWaterFlux2D right) -> ShallowWaterFlux2D
        {
            return ShallowWaterFlux2D{
                .mass = left.mass + right.mass,
                .momentum_x = left.momentum_x + right.momentum_x,
                .momentum_y = left.momentum_y + right.momentum_y};
        }

        [[nodiscard]] auto validate_boundary_set(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const ScalarBoundaryConditionSet &boundaries,
            std::string_view expected_unit,
            std::string operation) -> tsunami::core::Result<void>
        {
            const auto id = mesh_id(mesh);
            if (!boundaries.is_bound_to(mesh) || !boundaries.is_complete_for(mesh)) {
                return tsunami::core::failure(detail::r2d_error(
                    "r2d.well_balanced.boundary_incompatible",
                    "boundary set is not complete for the supplied mesh",
                    std::move(operation),
                    "SWE-R2D-WB",
                    &id));
            }
            for (const auto &condition : boundaries.conditions()) {
                const auto descriptor = condition.descriptor();
                if (descriptor.unit_id != expected_unit) {
                    return tsunami::core::failure(detail::r2d_error(
                        "r2d.well_balanced.boundary_incompatible",
                        "boundary unit is incompatible",
                        std::move(operation),
                        "SWE-R2D-WB",
                        &id,
                        std::nullopt,
                        std::nullopt,
                        descriptor.patch_id,
                        descriptor.id.value,
                        std::string{expected_unit},
                        descriptor.unit_id));
                }
                if (!condition.is_executable()) {
                    return tsunami::core::failure(detail::r2d_error(
                        "r2d.well_balanced.boundary_not_executable",
                        "boundary condition is not executable",
                        std::move(operation),
                        "SWE-R2D-WB",
                        &id,
                        std::nullopt,
                        std::nullopt,
                        descriptor.patch_id,
                        descriptor.id.value));
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto make_patch_fields(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string_view component,
            std::string_view unit) -> tsunami::core::Result<std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>>>
        {
            std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> patches;
            patches.reserve(mesh.summary().boundary_patch_count);
            for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
                const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
                auto patch = tsunami::fvm::make_filled_boundary_patch_field<tsunami::core::Real>(
                    mesh,
                    patch_id,
                    tsunami::fvm::FieldId{"regional.well_balanced.boundary." + std::string{component} + "." + std::to_string(index)},
                    "regional well-balanced boundary " + std::string{component},
                    std::string{unit},
                    0.0);
                if (!patch) {
                    return tsunami::core::failure<std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>>>(patch.error());
                }
                patches.push_back(std::move(patch).value());
            }
            return tsunami::core::success(std::move(patches));
        }

        auto apply_boundaries(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const ScalarBoundaryConditionSet &boundaries,
            const tsunami::fvm::CellScalarField &source,
            std::span<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> patches) -> tsunami::core::Result<void>
        {
            const auto id = mesh_id(mesh);
            for (const auto &condition : boundaries.conditions()) {
                const auto descriptor = condition.descriptor();
                auto applied = condition.apply(mesh, source, patches[descriptor.patch_id.value]);
                if (!applied) {
                    return tsunami::core::failure(detail::r2d_error(
                        "r2d.well_balanced.boundary_not_executable",
                        "boundary condition application failed",
                        "evaluate_well_balanced_rusanov_residual",
                        "SWE-R2D-WB",
                        &id,
                        std::nullopt,
                        std::nullopt,
                        descriptor.patch_id,
                        descriptor.id.value));
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto patch_local_index(
            const tsunami::fvm::BoundaryPatchRecord &patch,
            tsunami::fvm::FaceId face_id) -> std::size_t
        {
            const auto found = std::ranges::find(patch.faces, face_id);
            return static_cast<std::size_t>(found - patch.faces.begin());
        }
    } // namespace

    WellBalancedResidualWorkspace::WellBalancedResidualWorkspace(
        tsunami::fvm::MeshBinding binding,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> depth_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_x_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> momentum_y_patches,
        std::vector<tsunami::fvm::BoundaryPatchField<tsunami::core::Real>> bed_elevation_patches,
        RegionalResidual residual,
        tsunami::fvm::CellScalarField spectral_sum,
        tsunami::fvm::CellScalarField outgoing_mass_rate)
        : binding_{std::move(binding)}
        , depth_patches_{std::move(depth_patches)}
        , momentum_x_patches_{std::move(momentum_x_patches)}
        , momentum_y_patches_{std::move(momentum_y_patches)}
        , bed_elevation_patches_{std::move(bed_elevation_patches)}
        , residual_{std::move(residual)}
        , spectral_sum_{std::move(spectral_sum)}
        , outgoing_mass_rate_{std::move(outgoing_mass_rate)}
    {
    }

    auto WellBalancedResidualWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
               residual_.is_bound_to(mesh) &&
               spectral_sum_.is_bound_to(mesh) &&
               outgoing_mass_rate_.is_bound_to(mesh) &&
               depth_patches_.size() == mesh.summary().boundary_patch_count &&
               momentum_x_patches_.size() == mesh.summary().boundary_patch_count &&
               momentum_y_patches_.size() == mesh.summary().boundary_patch_count &&
               bed_elevation_patches_.size() == mesh.summary().boundary_patch_count;
    }

    auto make_well_balanced_residual_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<WellBalancedResidualWorkspace>
    {
        auto depth = make_patch_fields(mesh, "depth", depth_unit);
        auto momentum_x = make_patch_fields(mesh, "momentum_x", momentum_unit);
        auto momentum_y = make_patch_fields(mesh, "momentum_y", momentum_unit);
        auto bed = make_patch_fields(mesh, "bed_elevation", bed_unit);
        auto residual = make_regional_residual(mesh);
        auto spectral = tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh, tsunami::fvm::FieldId{"regional.well_balanced.spectral_sum"}, "regional well-balanced spectral sum", spectral_unit, 0.0);
        auto outgoing = tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh, tsunami::fvm::FieldId{"regional.outgoing_mass_rate"}, "regional outgoing mass rate", outgoing_unit, 0.0);
        if (!depth) {
            return tsunami::core::failure<WellBalancedResidualWorkspace>(depth.error());
        }
        if (!momentum_x) {
            return tsunami::core::failure<WellBalancedResidualWorkspace>(momentum_x.error());
        }
        if (!momentum_y) {
            return tsunami::core::failure<WellBalancedResidualWorkspace>(momentum_y.error());
        }
        if (!bed) {
            return tsunami::core::failure<WellBalancedResidualWorkspace>(bed.error());
        }
        if (!residual) {
            return tsunami::core::failure<WellBalancedResidualWorkspace>(residual.error());
        }
        if (!spectral) {
            return tsunami::core::failure<WellBalancedResidualWorkspace>(spectral.error());
        }
        if (!outgoing) {
            return tsunami::core::failure<WellBalancedResidualWorkspace>(outgoing.error());
        }
        return tsunami::core::success(WellBalancedResidualWorkspace{
            tsunami::fvm::make_mesh_binding(mesh),
            std::move(depth).value(),
            std::move(momentum_x).value(),
            std::move(momentum_y).value(),
            std::move(bed).value(),
            std::move(residual).value(),
            std::move(spectral).value(),
            std::move(outgoing).value()});
    }

    PhysicalBoundaryResidualWorkspace::PhysicalBoundaryResidualWorkspace(
        WellBalancedResidualWorkspace residual,
        RegionalExteriorStateWorkspace exterior)
        : residual_{std::move(residual)}
        , exterior_{std::move(exterior)}
    {
    }

    auto PhysicalBoundaryResidualWorkspace::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return residual_.is_bound_to(mesh) && exterior_.is_bound_to(mesh);
    }

    auto make_physical_boundary_residual_workspace(const tsunami::fvm::FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<PhysicalBoundaryResidualWorkspace>
    {
        auto residual = make_well_balanced_residual_workspace(mesh);
        auto exterior = make_regional_exterior_state_workspace(mesh);
        if (!residual) {
            return tsunami::core::failure<PhysicalBoundaryResidualWorkspace>(residual.error());
        }
        if (!exterior) {
            return tsunami::core::failure<PhysicalBoundaryResidualWorkspace>(exterior.error());
        }
        return tsunami::core::success(PhysicalBoundaryResidualWorkspace{
            std::move(residual).value(),
            std::move(exterior).value()});
    }

    auto evaluate_well_balanced_rusanov_residual(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        const ScalarBoundaryConditionSet &depth_boundaries,
        const ScalarBoundaryConditionSet &momentum_x_boundaries,
        const ScalarBoundaryConditionSet &momentum_y_boundaries,
        const ScalarBoundaryConditionSet &bathymetry_boundaries,
        const ShallowWaterStatePolicy &policy,
        RegionalResidual &destination_residual,
        tsunami::fvm::CellScalarField &destination_spectral_sum,
        tsunami::fvm::CellScalarField &destination_outgoing_mass_rate,
        tsunami::core::Real &destination_maximum_signal_speed,
        WellBalancedResidualWorkspace &workspace) -> tsunami::core::Result<void>
    {
        const auto id = mesh_id(mesh);
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure(policy_validation.error());
        }
        if (!state.is_bound_to(mesh) || state.size() != mesh.summary().cell_count) {
            return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.state_incompatible", "state is not bound to the mesh", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
        }
        if (!bathymetry.is_bound_to(mesh) || bathymetry.size() != mesh.summary().cell_count ||
            bathymetry.bed_elevation().descriptor().unit_id != bed_unit) {
            return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.bathymetry_incompatible", "bathymetry is not bound to the mesh", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
        }
        if (!destination_residual.is_bound_to(mesh) || destination_residual.size() != mesh.summary().cell_count ||
            !destination_spectral_sum.is_bound_to(mesh) || destination_spectral_sum.size() != mesh.summary().cell_count ||
            destination_spectral_sum.descriptor().unit_id != spectral_unit ||
            !destination_outgoing_mass_rate.is_bound_to(mesh) || destination_outgoing_mass_rate.size() != mesh.summary().cell_count ||
            destination_outgoing_mass_rate.descriptor().unit_id != outgoing_unit) {
            return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.destination_incompatible", "well-balanced destinations are incompatible", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
        }
        if (!workspace.is_bound_to(mesh)) {
            return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.workspace_incompatible", "well-balanced residual workspace is incompatible", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
        }
        for (std::size_t index = 0; index < mesh.geometry().cells().size(); ++index) {
            const auto &cell = mesh.geometry().cells()[index];
            if (!finite(cell.measure) || cell.measure <= 0.0) {
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.mesh_incompatible", "cell area must be finite and positive", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, tsunami::fvm::CellId{index}));
            }
        }
        for (const auto &face_geometry : mesh.geometry().faces()) {
            if (!finite(face_geometry.area_vector.x) || !finite(face_geometry.area_vector.y) || !finite(face_geometry.area_vector.z)) {
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.mesh_incompatible", "face geometry must be finite", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
            }
        }
        for (std::size_t index = 0; index < state.size(); ++index) {
            auto local = validate_and_canonicalise_state(state.local_state(tsunami::fvm::CellId{index}), policy, tsunami::fvm::CellId{index});
            if (!local || !finite(bathymetry.bed_elevation().at(index))) {
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.state_incompatible", "interior state or bed is invalid", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, tsunami::fvm::CellId{index}));
            }
        }

        auto depth_validation = validate_boundary_set(mesh, depth_boundaries, depth_unit, "evaluate_well_balanced_rusanov_residual");
        auto qx_validation = validate_boundary_set(mesh, momentum_x_boundaries, momentum_unit, "evaluate_well_balanced_rusanov_residual");
        auto qy_validation = validate_boundary_set(mesh, momentum_y_boundaries, momentum_unit, "evaluate_well_balanced_rusanov_residual");
        auto bed_validation = validate_boundary_set(mesh, bathymetry_boundaries, bed_unit, "evaluate_well_balanced_rusanov_residual");
        if (!depth_validation) {
            return tsunami::core::failure(depth_validation.error());
        }
        if (!qx_validation) {
            return tsunami::core::failure(qx_validation.error());
        }
        if (!qy_validation) {
            return tsunami::core::failure(qy_validation.error());
        }
        if (!bed_validation) {
            return tsunami::core::failure(bed_validation.error());
        }

        auto depth_apply = apply_boundaries(mesh, depth_boundaries, state.depth(), workspace.depth_patches());
        auto qx_apply = apply_boundaries(mesh, momentum_x_boundaries, state.momentum_x(), workspace.momentum_x_patches());
        auto qy_apply = apply_boundaries(mesh, momentum_y_boundaries, state.momentum_y(), workspace.momentum_y_patches());
        auto bed_apply = apply_boundaries(mesh, bathymetry_boundaries, bathymetry.bed_elevation(), workspace.bed_elevation_patches());
        if (!depth_apply) {
            return tsunami::core::failure(depth_apply.error());
        }
        if (!qx_apply) {
            return tsunami::core::failure(qx_apply.error());
        }
        if (!qy_apply) {
            return tsunami::core::failure(qy_apply.error());
        }
        if (!bed_apply) {
            return tsunami::core::failure(bed_apply.error());
        }

        workspace.residual().fill(ConservedVariables2D{});
        workspace.spectral_sum().fill(0.0);
        workspace.outgoing_mass_rate().fill(0.0);
        auto maximum_speed = tsunami::core::Real{0.0};

        const auto faces = mesh.topology().faces();
        if (!use_openmp_face_path(faces.size(), mesh.summary().cell_count)) {
            for (const auto &face : faces) {
                auto normal = make_face_normal(mesh.face_geometry(face.id).area_vector, policy, face.id);
                if (!normal) {
                    return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.mesh_incompatible", "face normal is invalid", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id).with_cause_code(normal.error().code()));
                }

                auto left = state.local_state(face.owner);
                auto right = ConservedVariables2D{};
                auto left_bed = bathymetry.local_bed_elevation(face.owner);
                auto right_bed = tsunami::core::Real{};
                if (face.neighbour) {
                    right = state.local_state(*face.neighbour);
                    right_bed = bathymetry.local_bed_elevation(*face.neighbour);
                } else {
                    const auto patch_id = *face.boundary_patch;
                    const auto &patch = mesh.boundary_patch(patch_id);
                    const auto local_it = std::ranges::find(patch.faces, face.id);
                    const auto local_index = static_cast<std::size_t>(local_it - patch.faces.begin());
                    right = ConservedVariables2D{
                        .depth = workspace.depth_patches()[patch_id.value].at(local_index),
                        .momentum_x = workspace.momentum_x_patches()[patch_id.value].at(local_index),
                        .momentum_y = workspace.momentum_y_patches()[patch_id.value].at(local_index)};
                    right_bed = workspace.bed_elevation_patches()[patch_id.value].at(local_index);
                    auto exterior = validate_and_canonicalise_state(right, policy, std::nullopt);
                    if (!exterior || !finite(right_bed)) {
                        return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.boundary_state_invalid", "exterior boundary state or bed is invalid", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id, patch_id));
                    }
                    right = exterior.value();
                }

                auto reconstructed = hydrostatic_reconstruction(left, right, left_bed, right_bed, normal.value(), policy);
                if (!reconstructed) {
                    return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.result_nonfinite", "hydrostatic reconstruction failed", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id).with_cause_code(reconstructed.error().code()));
                }
                auto flux = rusanov_flux(reconstructed.value().left, reconstructed.value().right, normal.value(), policy);
                if (!flux) {
                    return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.result_nonfinite", "Rusanov flux evaluation failed", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id).with_cause_code(flux.error().code()));
                }

                const auto owner_flux = sum_flux(flux.value().flux, reconstructed.value().left_pressure_correction);
                add_flux(workspace.residual(), face.owner, owner_flux, normal.value().length);
                workspace.spectral_sum().at(face.owner.value) += flux.value().maximum_signal_speed * normal.value().length;
                const auto integrated_mass_flux = flux.value().flux.mass * normal.value().length;
                workspace.outgoing_mass_rate().at(face.owner.value) += std::max(integrated_mass_flux, 0.0);

                if (face.neighbour) {
                    const auto neighbour_flux = sum_flux(flux.value().flux, reconstructed.value().right_pressure_correction);
                    add_flux(workspace.residual(), *face.neighbour, neighbour_flux, -normal.value().length);
                    workspace.spectral_sum().at(face.neighbour->value) += flux.value().maximum_signal_speed * normal.value().length;
                    workspace.outgoing_mass_rate().at(face.neighbour->value) += std::max(-integrated_mass_flux, 0.0);
                }
                maximum_speed = std::max(maximum_speed, flux.value().maximum_signal_speed);
            }
        } else {
            auto normals = std::vector<FaceNormal2D>(faces.size());
            auto boundary_local_indices = std::vector<std::size_t>(faces.size(), 0U);
            for (std::size_t index = 0; index < faces.size(); ++index) {
                const auto &face = faces[index];
                auto normal = make_face_normal(mesh.face_geometry(face.id).area_vector, policy, face.id);
                if (!normal) {
                    return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.mesh_incompatible", "face normal is invalid", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id).with_cause_code(normal.error().code()));
                }
                normals[index] = normal.value();
                if (!face.neighbour) {
                    const auto &patch = mesh.boundary_patch(*face.boundary_patch);
                    const auto local_it = std::ranges::find(patch.faces, face.id);
                    boundary_local_indices[index] = static_cast<std::size_t>(local_it - patch.faces.begin());
                }
            }
            auto buffers = ThreadLocalResidualBuffers{regional_openmp_thread_count(), mesh.summary().cell_count};
            constexpr auto no_failed_face = std::numeric_limits<std::size_t>::max();
            auto boundary_failed_face = std::atomic<std::size_t>{no_failed_face};
            auto result_failed_face = std::atomic<std::size_t>{no_failed_face};
#ifdef TSUNAMI_ENABLE_OPENMP
#pragma omp parallel for schedule(static) reduction(max : maximum_speed)
#endif
            for (std::ptrdiff_t signed_index = 0; signed_index < static_cast<std::ptrdiff_t>(faces.size()); ++signed_index) {
                const auto index = static_cast<std::size_t>(signed_index);
                const auto &face = faces[index];
#ifdef TSUNAMI_ENABLE_OPENMP
                const auto thread_id = static_cast<std::size_t>(omp_get_thread_num());
#else
                const auto thread_id = std::size_t{0U};
#endif
                auto left = state.local_state(face.owner);
                auto right = ConservedVariables2D{};
                auto left_bed = bathymetry.local_bed_elevation(face.owner);
                auto right_bed = tsunami::core::Real{};
                if (face.neighbour) {
                    right = state.local_state(*face.neighbour);
                    right_bed = bathymetry.local_bed_elevation(*face.neighbour);
                } else {
                    const auto patch_id = *face.boundary_patch;
                    const auto local_index = boundary_local_indices[index];
                    right = ConservedVariables2D{
                        .depth = workspace.depth_patches()[patch_id.value].at(local_index),
                        .momentum_x = workspace.momentum_x_patches()[patch_id.value].at(local_index),
                        .momentum_y = workspace.momentum_y_patches()[patch_id.value].at(local_index)};
                    right_bed = workspace.bed_elevation_patches()[patch_id.value].at(local_index);
                    auto exterior = validate_and_canonicalise_state(right, policy, std::nullopt);
                    if (!exterior || !finite(right_bed)) {
                        record_parallel_failure(boundary_failed_face, index);
                        continue;
                    }
                    right = exterior.value();
                }
                const auto reconstructed = hydrostatic_reconstruction(left, right, left_bed, right_bed, normals[index], policy);
                if (!reconstructed) {
                    record_parallel_failure(result_failed_face, index);
                    continue;
                }
                const auto flux = rusanov_flux(reconstructed.value().left, reconstructed.value().right, normals[index], policy);
                if (!flux) {
                    record_parallel_failure(result_failed_face, index);
                    continue;
                }

                const auto owner_flux = sum_flux(flux.value().flux, reconstructed.value().left_pressure_correction);
                add_thread_flux(buffers, thread_id, face.owner, owner_flux, normals[index].length);
                const auto owner_offset = (thread_id * buffers.cell_count) + face.owner.value;
                buffers.spectral_sum[owner_offset] += flux.value().maximum_signal_speed * normals[index].length;
                const auto integrated_mass_flux = flux.value().flux.mass * normals[index].length;
                buffers.outgoing_mass_rate[owner_offset] += std::max(integrated_mass_flux, 0.0);

                if (face.neighbour) {
                    const auto neighbour_flux = sum_flux(flux.value().flux, reconstructed.value().right_pressure_correction);
                    add_thread_flux(buffers, thread_id, *face.neighbour, neighbour_flux, -normals[index].length);
                    const auto neighbour_offset = (thread_id * buffers.cell_count) + face.neighbour->value;
                    buffers.spectral_sum[neighbour_offset] += flux.value().maximum_signal_speed * normals[index].length;
                    buffers.outgoing_mass_rate[neighbour_offset] += std::max(-integrated_mass_flux, 0.0);
                }
                maximum_speed = std::max(maximum_speed, flux.value().maximum_signal_speed);
            }
            const auto boundary_face_index = boundary_failed_face.load(std::memory_order_relaxed);
            if (boundary_face_index != no_failed_face) {
                const auto &face = faces[boundary_face_index];
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.boundary_state_invalid", "exterior boundary state or bed is invalid", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id, *face.boundary_patch));
            }
            const auto result_face_index = result_failed_face.load(std::memory_order_relaxed);
            if (result_face_index != no_failed_face) {
                const auto face_id = faces[result_face_index].id;
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.result_nonfinite", "parallel hydrostatic reconstruction or Rusanov flux evaluation failed", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face_id));
            }
            reduce_thread_buffers(buffers, workspace.residual(), workspace.spectral_sum(), workspace.outgoing_mass_rate());
        }

        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            if (!finite(workspace.residual().mass().at(index)) || !finite(workspace.residual().momentum_x().at(index)) ||
                !finite(workspace.residual().momentum_y().at(index)) || !finite(workspace.spectral_sum().at(index)) ||
                workspace.spectral_sum().at(index) < 0.0 || !finite(workspace.outgoing_mass_rate().at(index)) ||
                workspace.outgoing_mass_rate().at(index) < 0.0) {
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.result_nonfinite", "well-balanced residual outputs must be finite", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, tsunami::fvm::CellId{index}));
            }
        }

        auto residual_copy = destination_residual.copy_values_from(workspace.residual());
        if (!residual_copy) {
            return tsunami::core::failure(residual_copy.error());
        }
        auto spectral_copy = destination_spectral_sum.copy_values_from(workspace.spectral_sum());
        if (!spectral_copy) {
            return tsunami::core::failure(spectral_copy.error());
        }
        auto outgoing_copy = destination_outgoing_mass_rate.copy_values_from(workspace.outgoing_mass_rate());
        if (!outgoing_copy) {
            return tsunami::core::failure(outgoing_copy.error());
        }
        destination_maximum_signal_speed = maximum_speed;
        return tsunami::core::success();
    }

    auto evaluate_well_balanced_rusanov_residual(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const RegionalConservedState &state,
        const RegionalBathymetry &bathymetry,
        const RegionalBoundaryConditionSet &boundaries,
        const RegionalRelaxationZoneSet &relaxation_zones,
        const ShallowWaterStatePolicy &policy,
        tsunami::core::Time time,
        RegionalResidual &destination_residual,
        tsunami::fvm::CellScalarField &destination_spectral_sum,
        tsunami::fvm::CellScalarField &destination_outgoing_mass_rate,
        tsunami::core::Real &destination_maximum_signal_speed,
        PhysicalBoundaryResidualWorkspace &workspace) -> tsunami::core::Result<void>
    {
        const auto id = mesh_id(mesh);
        auto policy_validation = validate_policy(policy);
        if (!policy_validation) {
            return tsunami::core::failure(policy_validation.error());
        }
        if (!finite(time) || !state.is_bound_to(mesh) || state.size() != mesh.summary().cell_count) {
            return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.state_incompatible", "state is not bound to the mesh", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
        }
        if (!bathymetry.is_bound_to(mesh) || bathymetry.size() != mesh.summary().cell_count ||
            bathymetry.bed_elevation().descriptor().unit_id != bed_unit) {
            return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.bathymetry_incompatible", "bathymetry is not bound to the mesh", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
        }
        if (!boundaries.is_complete_for(mesh) || !relaxation_zones.is_bound_to(mesh)) {
            return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.boundary_incompatible", "physical boundary or relaxation set is not complete for the mesh", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
        }
        if (!destination_residual.is_bound_to(mesh) || destination_residual.size() != mesh.summary().cell_count ||
            !destination_spectral_sum.is_bound_to(mesh) || destination_spectral_sum.size() != mesh.summary().cell_count ||
            destination_spectral_sum.descriptor().unit_id != spectral_unit ||
            !destination_outgoing_mass_rate.is_bound_to(mesh) || destination_outgoing_mass_rate.size() != mesh.summary().cell_count ||
            destination_outgoing_mass_rate.descriptor().unit_id != outgoing_unit) {
            return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.destination_incompatible", "well-balanced destinations are incompatible", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
        }
        if (!workspace.is_bound_to(mesh)) {
            return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.workspace_incompatible", "physical boundary residual workspace is incompatible", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
        }
        for (std::size_t index = 0; index < mesh.geometry().cells().size(); ++index) {
            const auto &cell = mesh.geometry().cells()[index];
            if (!finite(cell.measure) || cell.measure <= 0.0) {
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.mesh_incompatible", "cell area must be finite and positive", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, tsunami::fvm::CellId{index}));
            }
        }
        for (const auto &face_geometry : mesh.geometry().faces()) {
            if (!finite(face_geometry.area_vector.x) || !finite(face_geometry.area_vector.y) || !finite(face_geometry.area_vector.z)) {
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.mesh_incompatible", "face geometry must be finite", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id));
            }
        }
        for (std::size_t index = 0; index < state.size(); ++index) {
            auto local = validate_and_canonicalise_state(state.local_state(tsunami::fvm::CellId{index}), policy, tsunami::fvm::CellId{index});
            if (!local || !finite(bathymetry.bed_elevation().at(index))) {
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.state_incompatible", "interior state or bed is invalid", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, tsunami::fvm::CellId{index}));
            }
        }

        auto exterior = populate_regional_exterior_states(
            mesh,
            state,
            bathymetry,
            boundaries,
            policy,
            time,
            workspace.exterior_workspace());
        if (!exterior) {
            return tsunami::core::failure(exterior.error());
        }

        workspace.residual().fill(ConservedVariables2D{});
        workspace.spectral_sum().fill(0.0);
        workspace.outgoing_mass_rate().fill(0.0);
        workspace.relaxation_diagnostics() = RegionalRelaxationDiagnostics{};
        auto maximum_speed = tsunami::core::Real{0.0};

        const auto faces = mesh.topology().faces();
        if (!use_openmp_face_path(faces.size(), mesh.summary().cell_count)) {
            for (const auto &face : faces) {
                auto normal = make_face_normal(mesh.face_geometry(face.id).area_vector, policy, face.id);
                if (!normal) {
                    return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.mesh_incompatible", "face normal is invalid", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id).with_cause_code(normal.error().code()));
                }

                auto left = state.local_state(face.owner);
                auto right = ConservedVariables2D{};
                auto left_bed = bathymetry.local_bed_elevation(face.owner);
                auto right_bed = tsunami::core::Real{};
                if (face.neighbour) {
                    right = state.local_state(*face.neighbour);
                    right_bed = bathymetry.local_bed_elevation(*face.neighbour);
                } else {
                    const auto patch_id = *face.boundary_patch;
                    const auto &patch = mesh.boundary_patch(patch_id);
                    const auto local_index = patch_local_index(patch, face.id);
                    right = ConservedVariables2D{
                        .depth = workspace.exterior_workspace().depth_patches()[patch_id.value].at(local_index),
                        .momentum_x = workspace.exterior_workspace().momentum_x_patches()[patch_id.value].at(local_index),
                        .momentum_y = workspace.exterior_workspace().momentum_y_patches()[patch_id.value].at(local_index)};
                    right_bed = workspace.exterior_workspace().bed_elevation_patches()[patch_id.value].at(local_index);
                    auto exterior_state = validate_and_canonicalise_state(right, policy, std::nullopt);
                    if (!exterior_state || !finite(right_bed)) {
                        return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.boundary_state_invalid", "exterior boundary state or bed is invalid", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id, patch_id));
                    }
                    right = exterior_state.value();
                }

                auto reconstructed = hydrostatic_reconstruction(left, right, left_bed, right_bed, normal.value(), policy);
                if (!reconstructed) {
                    return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.result_nonfinite", "hydrostatic reconstruction failed", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id).with_cause_code(reconstructed.error().code()));
                }
                auto flux = rusanov_flux(reconstructed.value().left, reconstructed.value().right, normal.value(), policy);
                if (!flux) {
                    return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.result_nonfinite", "Rusanov flux evaluation failed", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id).with_cause_code(flux.error().code()));
                }

                const auto owner_flux = sum_flux(flux.value().flux, reconstructed.value().left_pressure_correction);
                add_flux(workspace.residual(), face.owner, owner_flux, normal.value().length);
                workspace.spectral_sum().at(face.owner.value) += flux.value().maximum_signal_speed * normal.value().length;
                const auto integrated_mass_flux = flux.value().flux.mass * normal.value().length;
                workspace.outgoing_mass_rate().at(face.owner.value) += std::max(integrated_mass_flux, 0.0);

                if (face.neighbour) {
                    const auto neighbour_flux = sum_flux(flux.value().flux, reconstructed.value().right_pressure_correction);
                    add_flux(workspace.residual(), *face.neighbour, neighbour_flux, -normal.value().length);
                    workspace.spectral_sum().at(face.neighbour->value) += flux.value().maximum_signal_speed * normal.value().length;
                    workspace.outgoing_mass_rate().at(face.neighbour->value) += std::max(-integrated_mass_flux, 0.0);
                }
                maximum_speed = std::max(maximum_speed, flux.value().maximum_signal_speed);
            }
        } else {
            auto normals = std::vector<FaceNormal2D>(faces.size());
            auto boundary_local_indices = std::vector<std::size_t>(faces.size(), 0U);
            for (std::size_t index = 0; index < faces.size(); ++index) {
                const auto &face = faces[index];
                auto normal = make_face_normal(mesh.face_geometry(face.id).area_vector, policy, face.id);
                if (!normal) {
                    return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.mesh_incompatible", "face normal is invalid", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id).with_cause_code(normal.error().code()));
                }
                normals[index] = normal.value();
                if (!face.neighbour) {
                    boundary_local_indices[index] = patch_local_index(mesh.boundary_patch(*face.boundary_patch), face.id);
                }
            }
            auto buffers = ThreadLocalResidualBuffers{regional_openmp_thread_count(), mesh.summary().cell_count};
            constexpr auto no_failed_face = std::numeric_limits<std::size_t>::max();
            auto boundary_failed_face = std::atomic<std::size_t>{no_failed_face};
            auto result_failed_face = std::atomic<std::size_t>{no_failed_face};
#ifdef TSUNAMI_ENABLE_OPENMP
#pragma omp parallel for schedule(static) reduction(max : maximum_speed)
#endif
            for (std::ptrdiff_t signed_index = 0; signed_index < static_cast<std::ptrdiff_t>(faces.size()); ++signed_index) {
                const auto index = static_cast<std::size_t>(signed_index);
                const auto &face = faces[index];
#ifdef TSUNAMI_ENABLE_OPENMP
                const auto thread_id = static_cast<std::size_t>(omp_get_thread_num());
#else
                const auto thread_id = std::size_t{0U};
#endif
                auto left = state.local_state(face.owner);
                auto right = ConservedVariables2D{};
                auto left_bed = bathymetry.local_bed_elevation(face.owner);
                auto right_bed = tsunami::core::Real{};
                if (face.neighbour) {
                    right = state.local_state(*face.neighbour);
                    right_bed = bathymetry.local_bed_elevation(*face.neighbour);
                } else {
                    const auto patch_id = *face.boundary_patch;
                    const auto local_index = boundary_local_indices[index];
                    right = ConservedVariables2D{
                        .depth = workspace.exterior_workspace().depth_patches()[patch_id.value].at(local_index),
                        .momentum_x = workspace.exterior_workspace().momentum_x_patches()[patch_id.value].at(local_index),
                        .momentum_y = workspace.exterior_workspace().momentum_y_patches()[patch_id.value].at(local_index)};
                    right_bed = workspace.exterior_workspace().bed_elevation_patches()[patch_id.value].at(local_index);
                    auto exterior_state = validate_and_canonicalise_state(right, policy, std::nullopt);
                    if (!exterior_state || !finite(right_bed)) {
                        record_parallel_failure(boundary_failed_face, index);
                        continue;
                    }
                    right = exterior_state.value();
                }
                const auto reconstructed = hydrostatic_reconstruction(left, right, left_bed, right_bed, normals[index], policy);
                if (!reconstructed) {
                    record_parallel_failure(result_failed_face, index);
                    continue;
                }
                const auto flux = rusanov_flux(reconstructed.value().left, reconstructed.value().right, normals[index], policy);
                if (!flux) {
                    record_parallel_failure(result_failed_face, index);
                    continue;
                }

                const auto owner_flux = sum_flux(flux.value().flux, reconstructed.value().left_pressure_correction);
                add_thread_flux(buffers, thread_id, face.owner, owner_flux, normals[index].length);
                const auto owner_offset = (thread_id * buffers.cell_count) + face.owner.value;
                buffers.spectral_sum[owner_offset] += flux.value().maximum_signal_speed * normals[index].length;
                const auto integrated_mass_flux = flux.value().flux.mass * normals[index].length;
                buffers.outgoing_mass_rate[owner_offset] += std::max(integrated_mass_flux, 0.0);

                if (face.neighbour) {
                    const auto neighbour_flux = sum_flux(flux.value().flux, reconstructed.value().right_pressure_correction);
                    add_thread_flux(buffers, thread_id, *face.neighbour, neighbour_flux, -normals[index].length);
                    const auto neighbour_offset = (thread_id * buffers.cell_count) + face.neighbour->value;
                    buffers.spectral_sum[neighbour_offset] += flux.value().maximum_signal_speed * normals[index].length;
                    buffers.outgoing_mass_rate[neighbour_offset] += std::max(-integrated_mass_flux, 0.0);
                }
                maximum_speed = std::max(maximum_speed, flux.value().maximum_signal_speed);
            }
            const auto boundary_face_index = boundary_failed_face.load(std::memory_order_relaxed);
            if (boundary_face_index != no_failed_face) {
                const auto &face = faces[boundary_face_index];
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.boundary_state_invalid", "exterior boundary state or bed is invalid", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face.id, *face.boundary_patch));
            }
            const auto result_face_index = result_failed_face.load(std::memory_order_relaxed);
            if (result_face_index != no_failed_face) {
                const auto face_id = faces[result_face_index].id;
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.result_nonfinite", "parallel hydrostatic reconstruction or Rusanov flux evaluation failed", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, std::nullopt, face_id));
            }
            reduce_thread_buffers(buffers, workspace.residual(), workspace.spectral_sum(), workspace.outgoing_mass_rate());
        }

        auto relaxation = apply_regional_relaxation_source(
            mesh,
            state,
            bathymetry,
            relaxation_zones,
            policy,
            workspace.residual(),
            workspace.outgoing_mass_rate(),
            workspace.relaxation_diagnostics());
        if (!relaxation) {
            return tsunami::core::failure(relaxation.error());
        }

        for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
            if (!finite(workspace.residual().mass().at(index)) || !finite(workspace.residual().momentum_x().at(index)) ||
                !finite(workspace.residual().momentum_y().at(index)) || !finite(workspace.spectral_sum().at(index)) ||
                workspace.spectral_sum().at(index) < 0.0 || !finite(workspace.outgoing_mass_rate().at(index)) ||
                workspace.outgoing_mass_rate().at(index) < 0.0) {
                return tsunami::core::failure(detail::r2d_error("r2d.well_balanced.result_nonfinite", "well-balanced residual outputs must be finite", "evaluate_well_balanced_rusanov_residual", "SWE-R2D-WB", &id, tsunami::fvm::CellId{index}));
            }
        }

        auto residual_copy = destination_residual.copy_values_from(workspace.residual());
        if (!residual_copy) {
            return tsunami::core::failure(residual_copy.error());
        }
        auto spectral_copy = destination_spectral_sum.copy_values_from(workspace.spectral_sum());
        if (!spectral_copy) {
            return tsunami::core::failure(spectral_copy.error());
        }
        auto outgoing_copy = destination_outgoing_mass_rate.copy_values_from(workspace.outgoing_mass_rate());
        if (!outgoing_copy) {
            return tsunami::core::failure(outgoing_copy.error());
        }
        destination_maximum_signal_speed = maximum_speed;
        return tsunami::core::success();
    }

} // namespace tsunami::r2d
