#include <tsunami/r2d/RegionalCasePreparation.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <tsunami/data/CaseConfigurationValidation.hpp>
#include <tsunami/fvm/BoundarySpecification.hpp>
#include <tsunami/geo/CoordinateTransformation.hpp>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto rule_id = "r2d.case_preparation";
        constexpr auto prepare_operation = "prepare_regional_case";
        constexpr auto request_operation = "make_regional_solve_request";

        [[nodiscard]] auto finite(tsunami::core::Real value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto prep_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh *mesh = nullptr) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", std::move(operation))
                .add_context("rule_id", rule_id)
                .add_context("state_changed", "false");
            if (mesh != nullptr) {
                error.add_context("mesh_id", mesh->summary().id.value);
            }
            return error;
        }

        [[nodiscard]] auto wrap_failure(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::core::Error &cause,
            const tsunami::fvm::FiniteVolumeMesh *mesh = nullptr) -> tsunami::core::Error
        {
            auto error = prep_error(std::move(code), std::move(message), std::move(operation), mesh);
            error.with_cause_code(cause.code());
            return error;
        }

        [[nodiscard]] auto policy_valid(const RegionalCasePreparationPolicy &policy) noexcept -> bool
        {
            return finite(policy.pre_event_free_surface_elevation_m) &&
                finite(policy.dry_depth_m) && policy.dry_depth_m > 0.0 &&
                finite(policy.depth_tolerance_m) && policy.depth_tolerance_m >= 0.0 &&
                finite(policy.normal_tolerance) && policy.normal_tolerance > 0.0 &&
                finite(policy.zero_momentum_tolerance) && policy.zero_momentum_tolerance >= 0.0;
        }

        [[nodiscard]] auto close(
            tsunami::core::Real left,
            tsunami::core::Real right,
            tsunami::core::Real absolute,
            tsunami::core::Real relative) noexcept -> bool
        {
            return std::abs(left - right) <= absolute + relative * std::max({1.0, std::abs(left), std::abs(right)});
        }

        [[nodiscard]] auto bearing_delta(tsunami::core::Real left, tsunami::core::Real right) noexcept
            -> tsunami::core::Real
        {
            auto delta = std::abs(left - right);
            while (delta >= 360.0) {
                delta -= 360.0;
            }
            return std::min(delta, 360.0 - delta);
        }

        [[nodiscard]] auto machine_tolerance(tsunami::core::Real scale) noexcept -> tsunami::core::Real
        {
            return 128.0 * std::numeric_limits<tsunami::core::Real>::epsilon() * std::max(tsunami::core::Real{1.0}, std::abs(scale));
        }

        [[nodiscard]] auto elevation_close(
            tsunami::core::Real left,
            tsunami::core::Real right,
            tsunami::core::Real depth_tolerance) noexcept -> bool
        {
            return std::abs(left - right) <= depth_tolerance + machine_tolerance(std::max(std::abs(left), std::abs(right)));
        }

        struct BathymetryEvidence
        {
            tsunami::core::Real minimum_bed{};
            tsunami::core::Real maximum_bed{};
            tsunami::core::Real total_mesh_area{};
        };

        [[nodiscard]] auto actual_bathymetry_evidence(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalBathymetry &bathymetry) -> tsunami::core::Result<BathymetryEvidence>
        {
            auto evidence = BathymetryEvidence{
                std::numeric_limits<tsunami::core::Real>::infinity(),
                -std::numeric_limits<tsunami::core::Real>::infinity(),
                0.0};
            for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
                const auto cell_id = tsunami::fvm::CellId{index};
                const auto bed = bathymetry.local_bed_elevation(cell_id);
                const auto area = mesh.cell_geometry(cell_id).measure;
                if (!finite(bed) || !finite(area) || area <= 0.0) {
                    return tsunami::core::failure<BathymetryEvidence>(prep_error(
                        "r2d.case_preparation.bathymetry_invalid",
                        "pre-event bathymetry and mesh cell areas must be finite",
                        prepare_operation,
                        &mesh));
                }
                evidence.minimum_bed = std::min(evidence.minimum_bed, bed);
                evidence.maximum_bed = std::max(evidence.maximum_bed, bed);
                evidence.total_mesh_area += area;
            }
            if (!finite(evidence.total_mesh_area) || evidence.total_mesh_area <= 0.0 ||
                evidence.minimum_bed > evidence.maximum_bed) {
                return tsunami::core::failure<BathymetryEvidence>(prep_error(
                    "r2d.case_preparation.bathymetry_invalid",
                    "pre-event bathymetry evidence is invalid",
                    prepare_operation,
                    &mesh));
            }
            return tsunami::core::success(evidence);
        }

        [[nodiscard]] auto validate_terrain_transfer_evidence(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalBathymetry &bathymetry,
            const RegionalTerrainTransferDiagnostics &transfer,
            const RegionalCasePreparationPolicy &preparation_policy) -> tsunami::core::Result<void>
        {
            auto actual = actual_bathymetry_evidence(mesh, bathymetry);
            if (!actual) {
                return tsunami::core::failure(actual.error());
            }
            const auto cell_count = mesh.summary().cell_count;
            if (transfer.cell_count != cell_count ||
                transfer.total_contributor_count < cell_count ||
                transfer.minimum_contributors_per_cell == 0U ||
                transfer.minimum_contributors_per_cell > transfer.maximum_contributors_per_cell ||
                !finite(transfer.total_mesh_area_m2) || transfer.total_mesh_area_m2 <= 0.0 ||
                !finite(transfer.total_mapped_terrain_area_m2) || transfer.total_mapped_terrain_area_m2 <= 0.0 ||
                !finite(transfer.maximum_cell_area_residual_m2) || transfer.maximum_cell_area_residual_m2 < 0.0 ||
                !finite(transfer.minimum_bed_elevation_m) || !finite(transfer.maximum_bed_elevation_m) ||
                transfer.minimum_bed_elevation_m > transfer.maximum_bed_elevation_m) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.terrain_transfer_mismatch",
                    "terrain transfer diagnostics are internally invalid",
                    prepare_operation,
                    &mesh));
            }
            if (!elevation_close(actual.value().minimum_bed, transfer.minimum_bed_elevation_m, preparation_policy.depth_tolerance_m) ||
                !elevation_close(actual.value().maximum_bed, transfer.maximum_bed_elevation_m, preparation_policy.depth_tolerance_m) ||
                !close(actual.value().total_mesh_area, transfer.total_mesh_area_m2, machine_tolerance(actual.value().total_mesh_area), 0.0)) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.terrain_transfer_mismatch",
                    "terrain transfer diagnostics do not match the supplied bathymetry",
                    prepare_operation,
                    &mesh));
            }
            const auto mapped_discrepancy = std::abs(transfer.total_mapped_terrain_area_m2 - transfer.total_mesh_area_m2);
            const auto mapped_allowance = machine_tolerance(std::max(transfer.total_mapped_terrain_area_m2, transfer.total_mesh_area_m2)) +
                static_cast<tsunami::core::Real>(cell_count) * transfer.maximum_cell_area_residual_m2;
            if (mapped_discrepancy > mapped_allowance) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.terrain_transfer_mismatch",
                    "terrain transfer mapped area evidence is inconsistent",
                    prepare_operation,
                    &mesh));
            }
            return tsunami::core::success();
        }

        auto calculate_canonical_still_water_diagnostics(
            RegionalCasePreparationDiagnostics &diagnostics,
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalConservedState &state,
            const ShallowWaterStatePolicy &policy) -> void
        {
            diagnostics.wet_cell_count = 0U;
            diagnostics.dry_cell_count = 0U;
            diagnostics.minimum_depth_m = std::numeric_limits<tsunami::core::Real>::infinity();
            diagnostics.maximum_depth_m = -std::numeric_limits<tsunami::core::Real>::infinity();
            diagnostics.total_water_volume_m3 = 0.0;
            diagnostics.maximum_initial_momentum_m2_per_s = 0.0;
            for (std::size_t index = 0; index < state.size(); ++index) {
                const auto cell_id = tsunami::fvm::CellId{index};
                const auto local = state.local_state(cell_id);
                if (is_dry(local, policy)) {
                    ++diagnostics.dry_cell_count;
                } else {
                    ++diagnostics.wet_cell_count;
                }
                diagnostics.minimum_depth_m = std::min(diagnostics.minimum_depth_m, local.depth);
                diagnostics.maximum_depth_m = std::max(diagnostics.maximum_depth_m, local.depth);
                diagnostics.total_water_volume_m3 += local.depth * mesh.cell_geometry(cell_id).measure;
                diagnostics.maximum_initial_momentum_m2_per_s = std::max(
                    diagnostics.maximum_initial_momentum_m2_per_s,
                    std::hypot(local.momentum_x, local.momentum_y));
            }
            if (state.size() == 0U) {
                diagnostics.minimum_depth_m = 0.0;
                diagnostics.maximum_depth_m = 0.0;
            }
        }

        [[nodiscard]] auto validate_cross_contracts(const RegionalCasePreparationRequest &request)
            -> tsunami::core::Result<void>
        {
            if (request.configuration == nullptr || request.corridor_record == nullptr ||
                request.preflight == nullptr || request.terrain_transfer == nullptr ||
                request.mesh == nullptr || request.pre_event_bathymetry == nullptr ||
                !policy_valid(request.policy)) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.request_invalid",
                    "case preparation request is incomplete or policy values are invalid",
                    prepare_operation,
                    request.mesh));
            }

            const auto &configuration = *request.configuration;
            const auto &corridor_record = *request.corridor_record;
            const auto &preflight = *request.preflight;
            const auto &transfer = *request.terrain_transfer;
            const auto &mesh = *request.mesh;
            const auto summary = mesh.summary();

            if (auto valid = tsunami::data::validate_case_configuration(configuration); !valid) {
                return tsunami::core::failure(wrap_failure(
                    "r2d.case_preparation.case_invalid",
                    "case configuration must be valid before preparation",
                    prepare_operation,
                    valid.error(),
                    &mesh));
            }
            if (auto valid = tsunami::geo::validate_corridor_construction_record(corridor_record); !valid) {
                return tsunami::core::failure(wrap_failure(
                    "r2d.case_preparation.corridor_record_invalid",
                    "corridor construction record must be valid before preparation",
                    prepare_operation,
                    valid.error(),
                    &mesh));
            }
            if (configuration.scenario().model_family != tsunami::data::CaseModelFamily::regional_2d) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.model_family_invalid",
                    "case preparation supports only regional_2d cases",
                    prepare_operation,
                    &mesh));
            }
            if (auto target = tsunami::geo::validate_transformation_target_for_case(corridor_record.target_reference, configuration); !target) {
                return tsunami::core::failure(wrap_failure(
                    "r2d.case_preparation.coordinate_frame_mismatch",
                    "case coordinate frame must match the accepted corridor target reference",
                    prepare_operation,
                    target.error(),
                    &mesh));
            }
            if (corridor_record.epicentre.target_reference != corridor_record.target_reference ||
                corridor_record.target.target_reference != corridor_record.target_reference) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.coordinate_frame_mismatch",
                    "corridor reference-point evidence must use the accepted corridor target reference",
                    prepare_operation,
                    &mesh));
            }

            const auto &case_id = configuration.identity().case_id;
            const auto &record_case = corridor_record.identity.case_revision;
            if (!case_id || record_case.case_id != case_id ||
                record_case.revision != configuration.identity().revision ||
                corridor_record.scenario_id != configuration.scenario().scenario_id ||
                corridor_record.target_site != configuration.scenario().target_site ||
                corridor_record.identity.trajectory_id != configuration.regional_2d().corridor.trajectory_id) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.identity_mismatch",
                    "case, scenario, target site, and corridor identities must agree",
                    prepare_operation,
                    &mesh));
            }

            const auto &corridor = configuration.regional_2d().corridor;
            const auto origin_dx = corridor.origin.x - corridor_record.configured_origin.x;
            const auto origin_dy = corridor.origin.y - corridor_record.configured_origin.y;
            const auto origin_residual = std::sqrt(origin_dx * origin_dx + origin_dy * origin_dy);
            const auto expected_inland_width = corridor.narrowing.enabled
                ? *corridor.narrowing.inland_width_m
                : corridor.width_m;
            const auto &policy = corridor_record.policy;
            const auto recorded_offshore_sponge_width = corridor_record.sponge_limits.offshore_end_xi_m -
                corridor_record.sponge_limits.offshore_start_xi_m;
            if (origin_residual > policy.origin_tolerance_m ||
                bearing_delta(corridor.bearing_degrees_clockwise_from_north, corridor_record.configured_bearing_degrees) >
                    policy.bearing_tolerance_degrees ||
                corridor.narrowing.enabled != corridor_record.narrowing_enabled ||
                !close(corridor.width_m, corridor_record.offshore_width_m, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance) ||
                !close(expected_inland_width, corridor_record.inland_width_m, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance) ||
                !close(corridor.offshore_extent_m, corridor_record.offshore_extent_m, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance) ||
                !close(corridor.inland_extent_m, corridor_record.inland_extent_m, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance) ||
                !close(recorded_offshore_sponge_width, corridor.sponge.offshore_width_m, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance) ||
                !close(corridor_record.sponge_limits.offshore_start_xi_m, corridor_record.stations.offshore_xi_m, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance) ||
                !close(corridor.sponge.side_width_m, corridor_record.sponge_limits.side_width_m, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance)) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.corridor_mismatch",
                    "case corridor request and construction record geometry must agree within accepted tolerances",
                    prepare_operation,
                    &mesh));
            }

            if (preflight.validation_status != "accepted" ||
                preflight.corridor_id != corridor_record.identity.corridor_id ||
                preflight.mesh_id != summary.id.value ||
                preflight.vertex_count != summary.vertex_count ||
                preflight.face_count != summary.face_count ||
                preflight.cell_count != summary.cell_count) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.preflight_mismatch",
                    "accepted preflight report must match corridor and mesh inputs",
                    prepare_operation,
                    &mesh));
            }
            if (transfer.method_id != regional_terrain_transfer_method_id ||
                transfer.mesh_id != preflight.mesh_id ||
                transfer.terrain_id != preflight.terrain_id ||
                transfer.cell_count != preflight.cell_count ||
                !request.pre_event_bathymetry->is_bound_to(mesh) ||
                request.pre_event_bathymetry->size() != summary.cell_count) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.terrain_transfer_mismatch",
                    "terrain transfer diagnostics and pre-event bathymetry must match the accepted preflight",
                    prepare_operation,
                    &mesh));
            }
            if (auto evidence = validate_terrain_transfer_evidence(mesh, *request.pre_event_bathymetry, transfer, request.policy); !evidence) {
                return tsunami::core::failure(evidence.error());
            }
            if ((request.seabed_displacement != nullptr && !request.seabed_displacement->is_bound_to(mesh)) ||
                (request.prescribed_surface_perturbation != nullptr &&
                 !request.prescribed_surface_perturbation->is_bound_to(mesh))) {
                return tsunami::core::failure(prep_error(
                    "r2d.case_preparation.field_mismatch",
                    "optional runtime fields must be bound to the supplied mesh",
                    prepare_operation,
                    &mesh));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto zero_gradient_specs(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string unit) -> std::vector<tsunami::fvm::BoundarySpecification<tsunami::core::Real>>
        {
            auto specs = std::vector<tsunami::fvm::BoundarySpecification<tsunami::core::Real>>{};
            specs.reserve(mesh.summary().boundary_patch_count);
            for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
                const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
                const auto &patch = mesh.boundary_patch(patch_id);
                specs.push_back(tsunami::fvm::BoundarySpecification<tsunami::core::Real>{
                    tsunami::fvm::BoundaryConditionId{"case-prep-zero-" + patch.name + "-" + unit},
                    patch.name + " zero-gradient",
                    patch.name,
                    unit,
                    tsunami::fvm::ZeroGradientSpecification{}});
            }
            return specs;
        }

        [[nodiscard]] auto make_scalar_boundary_set(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string unit) -> tsunami::core::Result<tsunami::fvm::ScalarBoundaryConditionSet>
        {
            return tsunami::fvm::make_boundary_condition_set(mesh, zero_gradient_specs(mesh, std::move(unit)));
        }

        [[nodiscard]] auto boundary_kind_for(
            const tsunami::data::CorridorBoundaryConfiguration &boundaries,
            std::string_view patch) -> tsunami::data::RegionalBoundaryKind
        {
            if (patch == "boundary.offshore") {
                return boundaries.offshore;
            }
            if (patch == "boundary.inland") {
                return boundaries.inland;
            }
            if (patch == "boundary.left_side") {
                return boundaries.left_side;
            }
            return boundaries.right_side;
        }

        [[nodiscard]] auto map_boundary_kind(
            tsunami::data::RegionalBoundaryKind kind,
            RegionalFarFieldState still_water) -> std::variant<RegionalTransmissiveSpecification, RegionalRadiationSpecification>
        {
            if (kind == tsunami::data::RegionalBoundaryKind::transmissive) {
                return RegionalTransmissiveSpecification{};
            }
            return RegionalRadiationSpecification{still_water};
        }

        [[nodiscard]] auto make_boundaries(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::data::CaseConfiguration &configuration,
            RegionalFarFieldState still_water,
            const tsunami::fvm::ScalarBoundaryConditionSet &depth,
            const tsunami::fvm::ScalarBoundaryConditionSet &momentum_x,
            const tsunami::fvm::ScalarBoundaryConditionSet &momentum_y,
            const tsunami::fvm::ScalarBoundaryConditionSet &bed)
            -> tsunami::core::Result<RegionalBoundaryConditionSet>
        {
            static constexpr std::string_view required_patches[] = {
                "boundary.offshore",
                "boundary.inland",
                "boundary.left_side",
                "boundary.right_side"};
            auto overrides = std::vector<RegionalBoundaryOverrideSpecification>{};
            overrides.reserve(std::size(required_patches));
            for (const auto patch : required_patches) {
                overrides.push_back(RegionalBoundaryOverrideSpecification{
                    std::string{patch},
                    map_boundary_kind(boundary_kind_for(configuration.regional_2d().boundaries, patch), still_water)});
            }
            return make_regional_boundary_condition_set(mesh, depth, momentum_x, momentum_y, bed, std::move(overrides));
        }

        [[nodiscard]] auto make_relaxation(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::data::CaseConfiguration &configuration,
            RegionalFarFieldState still_water) -> tsunami::core::Result<RegionalRelaxationZoneSet>
        {
            const auto &relaxation = configuration.regional_2d().relaxation;
            if (!relaxation.enabled) {
                return make_regional_relaxation_zone_set(mesh, {});
            }
            const auto &corridor = configuration.regional_2d().corridor;
            auto specs = std::vector<PatchRelaxationZoneSpecification>{};
            const auto add = [&](std::string patch, tsunami::core::Real width) {
                if (width == 0.0) {
                    return;
                }
                specs.push_back(PatchRelaxationZoneSpecification{
                    std::move(patch),
                    width,
                    *relaxation.maximum_rate_per_s,
                    *relaxation.profile_exponent,
                    still_water});
            };
            add("boundary.offshore", corridor.sponge.offshore_width_m);
            add("boundary.left_side", corridor.sponge.side_width_m);
            add("boundary.right_side", corridor.sponge.side_width_m);
            return make_regional_relaxation_zone_set(mesh, std::move(specs));
        }

        [[nodiscard]] auto copy_dataset_values(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::optional<std::span<const tsunami::core::Real>> values,
            bool manning) -> tsunami::core::Result<std::vector<tsunami::core::Real>>
        {
            if (!values || values->size() != mesh.summary().cell_count) {
                return tsunami::core::failure<std::vector<tsunami::core::Real>>(prep_error(
                    manning ? "r2d.case_preparation.manning_dataset_invalid" : "r2d.case_preparation.coriolis_dataset_invalid",
                    "dataset source values must provide one value per mesh cell",
                    prepare_operation,
                    &mesh));
            }
            auto copied = std::vector<tsunami::core::Real>{values->begin(), values->end()};
            for (const auto value : copied) {
                if (!finite(value) || (manning && value < 0.0)) {
                    return tsunami::core::failure<std::vector<tsunami::core::Real>>(prep_error(
                        manning ? "r2d.case_preparation.manning_dataset_invalid" : "r2d.case_preparation.coriolis_dataset_invalid",
                        "dataset source values must be finite and physically admissible",
                        prepare_operation,
                        &mesh));
                }
            }
            return tsunami::core::success(std::move(copied));
        }

        [[nodiscard]] auto make_sources(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::data::CaseConfiguration &configuration,
            const RegionalCasePreparationRequest &request) -> tsunami::core::Result<RegionalSourceTermSet>
        {
            auto manning = std::optional<std::vector<tsunami::core::Real>>{};
            auto coriolis = std::optional<std::vector<tsunami::core::Real>>{};
            const auto cell_count = mesh.summary().cell_count;
            const auto &physics = configuration.regional_2d().physics;
            if ((request.manning_values && physics.manning.kind != tsunami::data::ManningConfigurationKind::dataset) ||
                (request.coriolis_values && physics.coriolis.kind != tsunami::data::CoriolisConfigurationKind::dataset)) {
                return tsunami::core::failure<RegionalSourceTermSet>(prep_error(
                    "r2d.case_preparation.source_input_invalid",
                    "source dataset spans are only accepted for dataset-configured sources",
                    prepare_operation,
                    &mesh));
            }
            if (physics.manning.kind == tsunami::data::ManningConfigurationKind::uniform) {
                manning = std::vector<tsunami::core::Real>(cell_count, *physics.manning.value_s_per_m_one_third);
            } else if (physics.manning.kind == tsunami::data::ManningConfigurationKind::dataset) {
                auto copied = copy_dataset_values(mesh, request.manning_values, true);
                if (!copied) {
                    return tsunami::core::failure<RegionalSourceTermSet>(copied.error());
                }
                manning = std::move(copied).value();
            }
            if (physics.coriolis.kind == tsunami::data::CoriolisConfigurationKind::constant) {
                coriolis = std::vector<tsunami::core::Real>(cell_count, *physics.coriolis.value_per_s);
            } else if (physics.coriolis.kind == tsunami::data::CoriolisConfigurationKind::dataset) {
                auto copied = copy_dataset_values(mesh, request.coriolis_values, false);
                if (!copied) {
                    return tsunami::core::failure<RegionalSourceTermSet>(copied.error());
                }
                coriolis = std::move(copied).value();
            }
            auto sources = make_regional_source_term_set(mesh, std::move(manning), std::move(coriolis));
            if (!sources) {
                return tsunami::core::failure<RegionalSourceTermSet>(wrap_failure(
                    "r2d.case_preparation.source_invalid",
                    "regional source term set could not be constructed",
                    prepare_operation,
                    sources.error(),
                    &mesh));
            }
            return sources;
        }

        [[nodiscard]] auto map_scheme(tsunami::data::RegionalTimeScheme scheme) noexcept -> ExplicitIntegrationScheme
        {
            switch (scheme) {
            case tsunami::data::RegionalTimeScheme::forward_euler:
                return ExplicitIntegrationScheme::forward_euler;
            case tsunami::data::RegionalTimeScheme::ssprk2:
                return ExplicitIntegrationScheme::ssprk2;
            case tsunami::data::RegionalTimeScheme::ssprk3:
                return ExplicitIntegrationScheme::ssprk3;
            }
            return ExplicitIntegrationScheme::ssprk3;
        }

        [[nodiscard]] auto make_time_policy(const tsunami::data::CaseConfiguration &configuration)
            -> tsunami::core::Result<RegionalTimeIntegrationPolicy>
        {
            const auto &numerics = configuration.regional_2d().numerics;
            auto policy = make_regional_time_integration_policy(
                map_scheme(numerics.scheme),
                numerics.courant_number,
                numerics.positivity_safety_factor,
                numerics.minimum_timestep_s,
                numerics.maximum_timestep_s);
            if (!policy) {
                return policy;
            }
            auto value = policy.value();
            value.relaxation_safety_factor = numerics.relaxation_safety_factor;
            value.source_safety_factor = numerics.source_safety_factor;
            auto valid = validate_regional_time_integration_policy(value);
            if (!valid) {
                return tsunami::core::failure<RegionalTimeIntegrationPolicy>(valid.error());
            }
            return tsunami::core::success(value);
        }

        [[nodiscard]] auto map_bed_mapping(tsunami::data::BedDeformationMapping mapping) noexcept
            -> RegionalBedDeformationMappingKind
        {
            if (mapping == tsunami::data::BedDeformationMapping::horizontal_slope_corrected) {
                return RegionalBedDeformationMappingKind::horizontal_slope_corrected;
            }
            return RegionalBedDeformationMappingKind::vertical_only;
        }

        [[nodiscard]] auto map_surface_transfer(tsunami::data::SurfaceTransfer transfer) noexcept
            -> RegionalSurfaceTransferKind
        {
            if (transfer == tsunami::data::SurfaceTransfer::prescribed) {
                return RegionalSurfaceTransferKind::prescribed;
            }
            return RegionalSurfaceTransferKind::passive_equal_to_effective_bed;
        }
    } // namespace

    RegionalPreparedCase::RegionalPreparedCase(
        tsunami::fvm::MeshBinding binding,
        RegionalBathymetry bathymetry,
        RegionalSimulationState simulation_state,
        RegionalBoundaryConditionSet regional_boundaries,
        RegionalRelaxationZoneSet relaxation_zones,
        RegionalSourceTermSet local_sources,
        ShallowWaterStatePolicy state_policy,
        RegionalTimeIntegrationPolicy time_policy,
        RegionalSnapshotOutputPolicy output_policy,
        RegionalTimeIntegrationWorkspace workspace,
        tsunami::core::Time final_time,
        std::size_t maximum_steps,
        std::optional<RegionalEarthquakeInitialisationDiagnostics> earthquake_diagnostics,
        RegionalCasePreparationDiagnostics diagnostics)
        : binding_{std::move(binding)}
        , bathymetry_{std::move(bathymetry)}
        , simulation_state_{std::move(simulation_state)}
        , regional_boundaries_{std::move(regional_boundaries)}
        , relaxation_zones_{std::move(relaxation_zones)}
        , local_sources_{std::move(local_sources)}
        , state_policy_{state_policy}
        , time_policy_{time_policy}
        , output_policy_{output_policy}
        , workspace_{std::move(workspace)}
        , final_time_{final_time}
        , maximum_steps_{maximum_steps}
        , earthquake_diagnostics_{std::move(earthquake_diagnostics)}
        , diagnostics_{std::move(diagnostics)}
    {
    }

    auto RegionalPreparedCase::is_bound_to(const tsunami::fvm::FiniteVolumeMesh &mesh) const -> bool
    {
        return binding_ == tsunami::fvm::make_mesh_binding(mesh) &&
            bathymetry_.is_bound_to(mesh) &&
            simulation_state_.conserved_state().is_bound_to(mesh) &&
            regional_boundaries_.is_complete_for(mesh) &&
            relaxation_zones_.is_bound_to(mesh) &&
            local_sources_.is_bound_to(mesh) &&
            workspace_.is_bound_to(mesh);
    }

    auto prepare_regional_case(const RegionalCasePreparationRequest &request)
        -> tsunami::core::Result<RegionalPreparedCase>
    {
        auto cross_contracts = validate_cross_contracts(request);
        if (!cross_contracts) {
            return tsunami::core::failure<RegionalPreparedCase>(cross_contracts.error());
        }

        const auto &configuration = *request.configuration;
        const auto &mesh = *request.mesh;
        const auto summary = mesh.summary();
        auto state_policy = make_shallow_water_state_policy(
            configuration.regional_2d().physics.gravity_m_per_s2,
            request.policy.dry_depth_m,
            request.policy.depth_tolerance_m,
            request.policy.normal_tolerance);
        if (!state_policy) {
            return tsunami::core::failure<RegionalPreparedCase>(wrap_failure(
                "r2d.case_preparation.state_policy_invalid",
                "shallow-water state policy could not be constructed",
                prepare_operation,
                state_policy.error(),
                &mesh));
        }

        auto depth = std::vector<tsunami::core::Real>{};
        auto qx = std::vector<tsunami::core::Real>(summary.cell_count, 0.0);
        auto qy = std::vector<tsunami::core::Real>(summary.cell_count, 0.0);
        depth.reserve(summary.cell_count);
        auto diagnostics = RegionalCasePreparationDiagnostics{};
        diagnostics.case_id = configuration.identity().case_id.str();
        diagnostics.case_revision = configuration.identity().revision;
        diagnostics.scenario_id = configuration.scenario().scenario_id;
        diagnostics.target_site = configuration.scenario().target_site;
        diagnostics.corridor_id = request.corridor_record->identity.corridor_id;
        diagnostics.mesh_id = summary.id.value;
        diagnostics.terrain_id = request.terrain_transfer->terrain_id;
        diagnostics.cell_count = summary.cell_count;
        for (std::size_t index = 0; index < summary.cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            const auto bed = request.pre_event_bathymetry->local_bed_elevation(cell_id);
            if (!finite(bed)) {
                return tsunami::core::failure<RegionalPreparedCase>(prep_error(
                    "r2d.case_preparation.bathymetry_invalid",
                    "pre-event bathymetry values must be finite",
                    prepare_operation,
                    &mesh));
            }
            const auto h = std::max(tsunami::core::Real{0.0}, request.policy.pre_event_free_surface_elevation_m - bed);
            depth.push_back(h);
        }

        auto conserved = make_regional_conserved_state(
            mesh,
            tsunami::fvm::FieldId{"regional.case.depth"},
            tsunami::fvm::FieldId{"regional.case.momentum_x"},
            tsunami::fvm::FieldId{"regional.case.momentum_y"},
            std::move(depth),
            std::move(qx),
            std::move(qy),
            state_policy.value());
        if (!conserved) {
            return tsunami::core::failure<RegionalPreparedCase>(wrap_failure(
                "r2d.case_preparation.state_invalid",
                "pre-event conserved state could not be constructed",
                prepare_operation,
                conserved.error(),
                &mesh));
        }
        auto simulation_state = RegionalSimulationState{std::move(conserved).value(), 0.0, 0U};
        calculate_canonical_still_water_diagnostics(
            diagnostics,
            mesh,
            simulation_state.conserved_state(),
            state_policy.value());

        auto depth_boundaries = make_scalar_boundary_set(mesh, "m");
        auto qx_boundaries = make_scalar_boundary_set(mesh, "m2/s");
        auto qy_boundaries = make_scalar_boundary_set(mesh, "m2/s");
        auto bed_boundaries = make_scalar_boundary_set(mesh, "m");
        if (!depth_boundaries || !qx_boundaries || !qy_boundaries || !bed_boundaries) {
            const auto &cause = !depth_boundaries ? depth_boundaries.error() :
                !qx_boundaries ? qx_boundaries.error() :
                !qy_boundaries ? qy_boundaries.error() :
                bed_boundaries.error();
            return tsunami::core::failure<RegionalPreparedCase>(wrap_failure(
                "r2d.case_preparation.boundary_invalid",
                "zero-gradient scalar boundary sets could not be constructed",
                prepare_operation,
                cause,
                &mesh));
        }

        auto bathymetry = request.pre_event_bathymetry->clone();
        auto earthquake_diagnostics = std::optional<RegionalEarthquakeInitialisationDiagnostics>{};
        const auto &earthquake = configuration.regional_2d().physics.earthquake;
        if (!earthquake.enabled) {
            if (request.seabed_displacement != nullptr || request.prescribed_surface_perturbation != nullptr ||
                request.earthquake_metadata != nullptr) {
                return tsunami::core::failure<RegionalPreparedCase>(prep_error(
                    "r2d.case_preparation.earthquake_input_invalid",
                    "earthquake runtime inputs are not allowed when earthquake initialisation is disabled",
                    prepare_operation,
                    &mesh));
            }
        } else {
            if (!earthquake.displacement_binding || request.seabed_displacement == nullptr ||
                request.earthquake_metadata == nullptr) {
                return tsunami::core::failure<RegionalPreparedCase>(prep_error(
                    "r2d.case_preparation.earthquake_input_invalid",
                    "enabled earthquake initialisation requires displacement and metadata",
                    prepare_operation,
                    &mesh));
            }
            if (auto valid = validate_regional_earthquake_source_metadata(*request.earthquake_metadata); !valid) {
                return tsunami::core::failure<RegionalPreparedCase>(wrap_failure(
                    "r2d.case_preparation.earthquake_metadata_invalid",
                    "earthquake source metadata must be valid",
                    prepare_operation,
                    valid.error(),
                    &mesh));
            }
            if (request.earthquake_metadata->event_id != configuration.scenario().event_id) {
                return tsunami::core::failure<RegionalPreparedCase>(prep_error(
                    "r2d.case_preparation.earthquake_metadata_mismatch",
                    "earthquake metadata event id must match the case event",
                    prepare_operation,
                    &mesh));
            }
            const auto needs_prescribed = earthquake.surface_transfer == tsunami::data::SurfaceTransfer::prescribed;
            if (needs_prescribed != (request.prescribed_surface_perturbation != nullptr)) {
                return tsunami::core::failure<RegionalPreparedCase>(prep_error(
                    "r2d.case_preparation.earthquake_input_invalid",
                    "prescribed surface input must match the configured earthquake surface transfer",
                    prepare_operation,
                    &mesh));
            }
            auto workspace = make_regional_earthquake_initialisation_workspace(mesh);
            if (!workspace) {
                return tsunami::core::failure<RegionalPreparedCase>(wrap_failure(
                    "r2d.case_preparation.earthquake_workspace_invalid",
                    "earthquake initialisation workspace could not be constructed",
                    prepare_operation,
                    workspace.error(),
                    &mesh));
            }
            auto earthquake_request = RegionalEarthquakeInitialisationRequest{
                .mesh = &mesh,
                .pre_event_bathymetry = request.pre_event_bathymetry,
                .pre_event_state = &simulation_state.conserved_state(),
                .seabed_displacement = request.seabed_displacement,
                .bed_mapping = map_bed_mapping(earthquake.bed_mapping),
                .surface_transfer = map_surface_transfer(earthquake.surface_transfer),
                .bathymetry_boundaries = &bed_boundaries.value(),
                .prescribed_surface_perturbation = request.prescribed_surface_perturbation,
                .state_policy = state_policy.value(),
                .zero_momentum_tolerance = request.policy.zero_momentum_tolerance,
                .metadata = *request.earthquake_metadata};
            auto initialised = initialise_regional_earthquake_state(earthquake_request, workspace.value());
            if (!initialised) {
                return tsunami::core::failure<RegionalPreparedCase>(wrap_failure(
                    "r2d.case_preparation.earthquake_initialisation_failed",
                    "existing earthquake initialisation failed",
                    prepare_operation,
                    initialised.error(),
                    &mesh));
            }
            auto result = std::move(initialised).value();
            bathymetry = std::move(result.post_event_bathymetry);
            simulation_state = std::move(result.simulation_state);
            earthquake_diagnostics = std::move(result.diagnostics);
            diagnostics.earthquake_initialised = true;
        }

        const auto still_water = RegionalFarFieldState{
            request.policy.pre_event_free_surface_elevation_m,
            0.0,
            0.0};
        auto regional_boundaries = make_boundaries(
            mesh,
            configuration,
            still_water,
            depth_boundaries.value(),
            qx_boundaries.value(),
            qy_boundaries.value(),
            bed_boundaries.value());
        if (!regional_boundaries) {
            return tsunami::core::failure<RegionalPreparedCase>(wrap_failure(
                "r2d.case_preparation.boundary_invalid",
                "physical regional boundary set could not be constructed",
                prepare_operation,
                regional_boundaries.error(),
                &mesh));
        }
        auto relaxation = make_relaxation(mesh, configuration, still_water);
        if (!relaxation) {
            return tsunami::core::failure<RegionalPreparedCase>(wrap_failure(
                "r2d.case_preparation.relaxation_invalid",
                "regional relaxation zones could not be constructed",
                prepare_operation,
                relaxation.error(),
                &mesh));
        }
        auto sources = make_sources(mesh, configuration, request);
        if (!sources) {
            return tsunami::core::failure<RegionalPreparedCase>(sources.error());
        }
        auto time_policy = make_time_policy(configuration);
        if (!time_policy) {
            return tsunami::core::failure<RegionalPreparedCase>(wrap_failure(
                "r2d.case_preparation.time_policy_invalid",
                "regional time policy could not be constructed",
                prepare_operation,
                time_policy.error(),
                &mesh));
        }
        auto workspace = make_regional_time_integration_workspace(mesh, simulation_state.conserved_state());
        if (!workspace) {
            return tsunami::core::failure<RegionalPreparedCase>(wrap_failure(
                "r2d.case_preparation.workspace_invalid",
                "regional time integration workspace could not be constructed",
                prepare_operation,
                workspace.error(),
                &mesh));
        }

        diagnostics.physical_boundary_count = regional_boundaries.value().size();
        diagnostics.relaxation_zone_count = relaxation.value().size();
        diagnostics.has_manning_source = sources.value().has_manning();
        diagnostics.has_coriolis_source = sources.value().has_coriolis();
        diagnostics.retry_factor = time_policy.value().retry_factor;
        diagnostics.maximum_stage_retries = time_policy.value().maximum_stage_retries;
        diagnostics.timestep_comparison_tolerance = time_policy.value().timestep_comparison_tolerance;

        auto output_policy = RegionalSnapshotOutputPolicy{
            true,
            true,
            configuration.outputs().snapshot_interval_s};
        auto prepared = RegionalPreparedCase{
            tsunami::fvm::make_mesh_binding(mesh),
            std::move(bathymetry),
            std::move(simulation_state),
            std::move(regional_boundaries).value(),
            std::move(relaxation).value(),
            std::move(sources).value(),
            state_policy.value(),
            time_policy.value(),
            output_policy,
            std::move(workspace).value(),
            configuration.regional_2d().numerics.final_time_s,
            static_cast<std::size_t>(configuration.regional_2d().numerics.maximum_steps),
            std::move(earthquake_diagnostics),
            std::move(diagnostics)};
        if (!prepared.is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalPreparedCase>(prep_error(
                "r2d.case_preparation.prepared_case_invalid",
                "prepared Regional2D runtime components are not bound to the same mesh",
                prepare_operation,
                &mesh));
        }
        return tsunami::core::success(std::move(prepared));
    }

    auto make_regional_solve_request(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        RegionalPreparedCase &prepared,
        RegionalStepDiagnosticsSink diagnostics_sink,
        RegionalSnapshotSink snapshot_sink,
        std::stop_token stop_token) -> tsunami::core::Result<RegionalSolveRequest>
    {
        if (!prepared.is_bound_to(mesh)) {
            return tsunami::core::failure<RegionalSolveRequest>(prep_error(
                "r2d.case_preparation.mesh_mismatch",
                "prepared case mesh binding does not match the supplied mesh",
                request_operation,
                &mesh));
        }
        return tsunami::core::success(RegionalSolveRequest{
            .mesh = &mesh,
            .bathymetry = &prepared.bathymetry(),
            .depth_boundaries = nullptr,
            .momentum_x_boundaries = nullptr,
            .momentum_y_boundaries = nullptr,
            .bathymetry_boundaries = nullptr,
            .regional_boundaries = &prepared.regional_boundaries(),
            .relaxation_zones = &prepared.relaxation_zones(),
            .state_policy = prepared.state_policy(),
            .time_policy = prepared.time_policy(),
            .output_policy = prepared.output_policy(),
            .final_time = prepared.final_time(),
            .maximum_steps = prepared.maximum_steps(),
            .diagnostics_sink = std::move(diagnostics_sink),
            .snapshot_sink = std::move(snapshot_sink),
            .stop_token = stop_token,
            .local_sources = &prepared.local_sources()});
    }

} // namespace tsunami::r2d
