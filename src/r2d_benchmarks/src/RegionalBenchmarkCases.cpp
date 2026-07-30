#include <tsunami/r2d_benchmarks/RegionalBenchmarkCases.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

#include <tsunami/fvm/BoundarySpecification.hpp>
#include <tsunami/r2d/RegionalEarthquakeInitialisation.hpp>
#include <tsunami/r2d_benchmarks/StructuredTriangularMesh.hpp>

namespace tsunami::r2d_benchmarks
{
    namespace
    {
        using tsunami::core::Real;

        [[nodiscard]] auto error(std::string code, std::string message) -> tsunami::core::Error
        {
            return tsunami::core::Error{std::move(code), std::move(message)};
        }

        [[nodiscard]] auto zero_gradient_specs(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::string unit) -> std::vector<tsunami::fvm::BoundarySpecification<Real>>
        {
            std::vector<tsunami::fvm::BoundarySpecification<Real>> specs;
            specs.reserve(mesh.summary().boundary_patch_count);
            for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
                const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
                const auto &patch = mesh.boundary_patch(patch_id);
                specs.push_back(tsunami::fvm::BoundarySpecification<Real>{
                    tsunami::fvm::BoundaryConditionId{"zero-" + patch.name + "-" + unit},
                    patch.name + " zero-gradient",
                    patch.name,
                    unit,
                    tsunami::fvm::ZeroGradientSpecification{}});
            }
            return specs;
        }

        [[nodiscard]] auto make_boundary_set(const tsunami::fvm::FiniteVolumeMesh &mesh, std::string unit)
        {
            return tsunami::fvm::make_boundary_condition_set(mesh, zero_gradient_specs(mesh, std::move(unit)));
        }

        [[nodiscard]] auto fixed_bathymetry_specs(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const std::function<Real(const tsunami::fvm::Point3 &)> &bed_function)
            -> std::vector<tsunami::fvm::BoundarySpecification<Real>>
        {
            std::vector<tsunami::fvm::BoundarySpecification<Real>> specs;
            specs.reserve(mesh.summary().boundary_patch_count);
            for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
                const auto patch_id = tsunami::fvm::BoundaryPatchId{index};
                const auto &patch = mesh.boundary_patch(patch_id);
                std::vector<Real> values;
                values.reserve(patch.faces.size());
                for (const auto face_id : patch.faces) {
                    values.push_back(bed_function(mesh.face_geometry(face_id).centroid));
                }
                specs.push_back(tsunami::fvm::BoundarySpecification<Real>{
                    tsunami::fvm::BoundaryConditionId{"fixed-" + patch.name + "-bed"},
                    patch.name + " fixed bed",
                    patch.name,
                    "m",
                    tsunami::fvm::FixedValueSpecification<Real>{std::move(values)}});
            }
            return specs;
        }

        [[nodiscard]] auto make_case(
            std::string id,
            StructuredTriangularMeshSpec mesh_spec,
            Real final_time,
            const std::function<Real(const tsunami::fvm::Point3 &)> &bed_function,
            const std::function<Real(const tsunami::fvm::Point3 &, Real)> &depth_function,
            const std::function<Real(const tsunami::fvm::Point3 &, Real)> &velocity_x_function,
            const std::function<Real(const tsunami::fvm::Point3 &, Real)> &velocity_y_function)
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            auto mesh_result = make_structured_triangular_mesh(std::move(mesh_spec));
            if (!mesh_result) {
                return tsunami::core::failure<RegionalBenchmarkCase>(mesh_result.error());
            }
            auto mesh = std::move(mesh_result).value();
            auto state_policy = tsunami::r2d::make_shallow_water_state_policy(9.81, 1.0e-6, 1.0e-8, 1.0e-12);
            if (!state_policy) {
                return tsunami::core::failure<RegionalBenchmarkCase>(state_policy.error());
            }
            auto time_policy = tsunami::r2d::make_regional_time_integration_policy(
                tsunami::r2d::ExplicitIntegrationScheme::ssprk3,
                0.45,
                0.95,
                1.0e-10,
                0.01);
            if (!time_policy) {
                return tsunami::core::failure<RegionalBenchmarkCase>(time_policy.error());
            }

            std::vector<Real> bed_values;
            std::vector<Real> depth;
            std::vector<Real> qx(mesh.summary().cell_count, 0.0);
            std::vector<Real> qy(mesh.summary().cell_count, 0.0);
            bed_values.reserve(mesh.summary().cell_count);
            depth.reserve(mesh.summary().cell_count);
            for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
                const auto centroid = mesh.cell_geometry(tsunami::fvm::CellId{index}).centroid;
                const auto bed = bed_function(centroid);
                bed_values.push_back(bed);
                const auto h = std::max(0.0, depth_function(centroid, bed));
                depth.push_back(h);
                qx[index] = h * velocity_x_function(centroid, bed);
                qy[index] = h * velocity_y_function(centroid, bed);
            }

            auto bathymetry = tsunami::r2d::make_regional_bathymetry(mesh, tsunami::fvm::FieldId{"zb"}, id + " bed elevation", std::move(bed_values));
            auto conserved = tsunami::r2d::make_regional_conserved_state(
                mesh,
                tsunami::fvm::FieldId{"h"},
                tsunami::fvm::FieldId{"qx"},
                tsunami::fvm::FieldId{"qy"},
                std::move(depth),
                std::move(qx),
                std::move(qy),
                state_policy.value());
            auto h_bc = make_boundary_set(mesh, "m");
            auto qx_bc = make_boundary_set(mesh, "m2/s");
            auto qy_bc = make_boundary_set(mesh, "m2/s");
            auto zb_bc = make_boundary_set(mesh, "m");
            if (!bathymetry) {
                return tsunami::core::failure<RegionalBenchmarkCase>(bathymetry.error());
            }
            if (!conserved) {
                return tsunami::core::failure<RegionalBenchmarkCase>(conserved.error());
            }
            if (!h_bc) {
                return tsunami::core::failure<RegionalBenchmarkCase>(h_bc.error());
            }
            if (!qx_bc) {
                return tsunami::core::failure<RegionalBenchmarkCase>(qx_bc.error());
            }
            if (!qy_bc) {
                return tsunami::core::failure<RegionalBenchmarkCase>(qy_bc.error());
            }
            if (!zb_bc) {
                return tsunami::core::failure<RegionalBenchmarkCase>(zb_bc.error());
            }
            auto regional_bc = tsunami::r2d::make_regional_boundary_condition_set(
                mesh,
                h_bc.value(),
                qx_bc.value(),
                qy_bc.value(),
                zb_bc.value(),
                {});
            auto relaxation = tsunami::r2d::make_regional_relaxation_zone_set(mesh, {});
            auto local_sources = tsunami::r2d::make_empty_regional_source_term_set(mesh);
            if (!regional_bc) {
                return tsunami::core::failure<RegionalBenchmarkCase>(regional_bc.error());
            }
            if (!relaxation) {
                return tsunami::core::failure<RegionalBenchmarkCase>(relaxation.error());
            }
            if (!local_sources) {
                return tsunami::core::failure<RegionalBenchmarkCase>(local_sources.error());
            }

            return tsunami::core::success(RegionalBenchmarkCase{
                std::move(id),
                std::move(mesh),
                std::move(bathymetry).value(),
                tsunami::r2d::RegionalSimulationState{std::move(conserved).value(), 0.0, 0U},
                std::move(h_bc).value(),
                std::move(qx_bc).value(),
                std::move(qy_bc).value(),
                std::move(zb_bc).value(),
                std::move(regional_bc).value(),
                std::move(relaxation).value(),
                std::move(local_sources).value(),
                state_policy.value(),
                time_policy.value(),
                final_time,
                std::nullopt});
        }

        [[nodiscard]] auto make_outgoing_wave_case(std::string id, bool sponge)
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            constexpr auto mean_depth = Real{1.0};
            const auto wave_speed = std::sqrt(9.81 * mean_depth);
            auto elevation = [](const tsunami::fvm::Point3 &point) {
                constexpr auto amp = Real{0.01};
                constexpr auto x0 = Real{0.25};
                constexpr auto sigma = Real{0.055};
                const auto dx = point.x - x0;
                return amp * std::exp(-(dx * dx) / (2.0 * sigma * sigma));
            };
            auto result = make_case(
                std::move(id),
                StructuredTriangularMeshSpec{"outgoing-linear-wave", 48U, 4U, 1.0, 0.2},
                sponge ? 0.24 : 0.22,
                [](const auto &) { return 0.0; },
                [&](const auto &point, Real bed) { return (mean_depth + elevation(point)) - bed; },
                [&](const auto &point, Real) { return (wave_speed / mean_depth) * elevation(point); },
                [](const auto &, Real) { return 0.0; });
            if (!result) {
                return result;
            }
            auto problem = std::move(result).value();
            problem.time_policy.maximum_timestep = 0.002;
            auto regional = tsunami::r2d::make_regional_boundary_condition_set(
                problem.mesh,
                problem.depth_boundaries,
                problem.momentum_x_boundaries,
                problem.momentum_y_boundaries,
                problem.bathymetry_boundaries,
                {tsunami::r2d::RegionalBoundaryOverrideSpecification{
                    "right",
                    tsunami::r2d::RegionalRadiationSpecification{
                        tsunami::r2d::RegionalFarFieldState{.free_surface_elevation = mean_depth}}}});
            if (!regional) {
                return tsunami::core::failure<RegionalBenchmarkCase>(regional.error());
            }
            problem.regional_boundaries = std::move(regional).value();
            std::vector<tsunami::r2d::PatchRelaxationZoneSpecification> relaxation_specs;
            if (sponge) {
                relaxation_specs.push_back(tsunami::r2d::PatchRelaxationZoneSpecification{
                    .patch_tag = "right",
                    .width = 0.25,
                    .maximum_rate = 6.0,
                    .profile_exponent = 2.0,
                    .reference_state = tsunami::r2d::RegionalFarFieldState{.free_surface_elevation = mean_depth}});
            }
            auto relaxation = tsunami::r2d::make_regional_relaxation_zone_set(
                problem.mesh,
                std::move(relaxation_specs));
            if (!relaxation) {
                return tsunami::core::failure<RegionalBenchmarkCase>(relaxation.error());
            }
            problem.relaxation_zones = std::move(relaxation).value();
            return tsunami::core::success(std::move(problem));
        }

        [[nodiscard]] auto make_uniform_manning_decay_case()
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            auto result = make_case(
                "uniform_manning_decay",
                StructuredTriangularMeshSpec{"uniform-manning-decay", 6U, 2U, 1.0, 0.4},
                0.08,
                [](const auto &) { return 0.0; },
                [](const auto &, Real bed) { return 1.0 - bed; },
                [](const auto &, Real) { return 0.7; },
                [](const auto &, Real) { return 0.0; });
            if (!result) {
                return result;
            }
            auto problem = std::move(result).value();
            auto sources = tsunami::r2d::make_uniform_manning_source_term_set(problem.mesh, 0.035);
            if (!sources) {
                return tsunami::core::failure<RegionalBenchmarkCase>(sources.error());
            }
            problem.local_sources = std::move(sources).value();
            problem.time_policy.maximum_timestep = 0.002;
            problem.time_policy.source_safety_factor = 0.75;
            return tsunami::core::success(std::move(problem));
        }

        [[nodiscard]] auto make_uniform_coriolis_oscillation_case()
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            auto result = make_case(
                "uniform_coriolis_oscillation",
                StructuredTriangularMeshSpec{"uniform-coriolis-oscillation", 6U, 2U, 1.0, 0.4},
                0.12,
                [](const auto &) { return 0.0; },
                [](const auto &, Real bed) { return 1.0 - bed; },
                [](const auto &, Real) { return 0.6; },
                [](const auto &, Real) { return 0.15; });
            if (!result) {
                return result;
            }
            auto problem = std::move(result).value();
            auto sources = tsunami::r2d::make_uniform_coriolis_source_term_set(problem.mesh, 1.0e-3);
            if (!sources) {
                return tsunami::core::failure<RegionalBenchmarkCase>(sources.error());
            }
            problem.local_sources = std::move(sources).value();
            problem.time_policy.maximum_timestep = 0.002;
            return tsunami::core::success(std::move(problem));
        }

        [[nodiscard]] auto make_uniform_manning_coriolis_case()
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            auto result = make_case(
                "uniform_manning_coriolis",
                StructuredTriangularMeshSpec{"uniform-manning-coriolis", 8U, 2U, 1.0, 0.4},
                0.08,
                [](const auto &) { return 0.0; },
                [](const auto &, Real bed) { return 1.0 - bed; },
                [](const auto &, Real) { return 0.55; },
                [](const auto &, Real) { return -0.2; });
            if (!result) {
                return result;
            }
            auto problem = std::move(result).value();
            auto sources = tsunami::r2d::make_uniform_manning_coriolis_source_term_set(problem.mesh, 0.03, 8.0e-4);
            if (!sources) {
                return tsunami::core::failure<RegionalBenchmarkCase>(sources.error());
            }
            problem.local_sources = std::move(sources).value();
            problem.time_policy.maximum_timestep = 0.002;
            problem.time_policy.source_safety_factor = 0.75;
            return tsunami::core::success(std::move(problem));
        }

        [[nodiscard]] auto make_frictional_wet_dry_dam_break_case()
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            auto result = make_case(
                "frictional_wet_dry_dam_break",
                StructuredTriangularMeshSpec{"frictional-wet-dry-dam-break", 8U, 2U, 1.0, 0.25},
                0.02,
                [](const auto &) { return 0.0; },
                [](const auto &point, Real) { return point.x < 0.5 ? 1.5 : 0.05; },
                [](const auto &, Real) { return 0.0; },
                [](const auto &, Real) { return 0.0; });
            if (!result) {
                return result;
            }
            auto problem = std::move(result).value();
            auto sources = tsunami::r2d::make_uniform_manning_source_term_set(problem.mesh, 0.04);
            if (!sources) {
                return tsunami::core::failure<RegionalBenchmarkCase>(sources.error());
            }
            problem.local_sources = std::move(sources).value();
            problem.time_policy.maximum_timestep = 0.001;
            problem.time_policy.source_safety_factor = 0.75;
            return tsunami::core::success(std::move(problem));
        }

        [[nodiscard]] auto initialise_earthquake_case(
            RegionalBenchmarkCase problem,
            tsunami::r2d::RegionalSeabedDisplacement displacement,
            tsunami::r2d::RegionalBedDeformationMappingKind bed_mapping,
            tsunami::r2d::RegionalSurfaceTransferKind surface_transfer,
            tsunami::r2d::RegionalEarthquakeSourceMetadata metadata,
            const tsunami::fvm::CellScalarField *prescribed = nullptr)
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            auto workspace = tsunami::r2d::make_regional_earthquake_initialisation_workspace(problem.mesh);
            if (!workspace) {
                return tsunami::core::failure<RegionalBenchmarkCase>(workspace.error());
            }
            auto request = tsunami::r2d::RegionalEarthquakeInitialisationRequest{
                .mesh = &problem.mesh,
                .pre_event_bathymetry = &problem.bathymetry,
                .pre_event_state = &problem.simulation_state.conserved_state(),
                .seabed_displacement = &displacement,
                .bed_mapping = bed_mapping,
                .surface_transfer = surface_transfer,
                .bathymetry_boundaries = &problem.bathymetry_boundaries,
                .prescribed_surface_perturbation = prescribed,
                .state_policy = problem.state_policy,
                .zero_momentum_tolerance = 1.0e-12,
                .metadata = std::move(metadata)};
            auto initialised = tsunami::r2d::initialise_regional_earthquake_state(request, workspace.value());
            if (!initialised) {
                return tsunami::core::failure<RegionalBenchmarkCase>(initialised.error());
            }
            auto result = std::move(initialised).value();
            problem.bathymetry = std::move(result.post_event_bathymetry);
            problem.simulation_state = std::move(result.simulation_state);
            problem.earthquake_initialisation = std::move(result.diagnostics);
            return tsunami::core::success(std::move(problem));
        }

        [[nodiscard]] auto metadata_for(std::string id) -> tsunami::r2d::RegionalEarthquakeSourceMetadata
        {
            return tsunami::r2d::RegionalEarthquakeSourceMetadata{
                .source_kind = tsunami::r2d::RegionalEarthquakeSourceKind::synthetic,
                .event_id = std::move(id),
                .model_id = "manufactured-v0.1",
                .source_format = "programmatic",
                .coordinate_reference = "mesh-cartesian-east-north-up",
                .subfault_count = 0U};
        }

        [[nodiscard]] auto make_earthquake_uniform_vertical_translation_case()
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            auto result = make_case(
                "earthquake_uniform_vertical_translation",
                StructuredTriangularMeshSpec{"earthquake-uniform-vertical-translation", 6U, 2U, 1.0, 0.4},
                0.04,
                [](const auto &point) { return 0.1 * point.x; },
                [](const auto &, Real bed) { return 1.0 - bed; },
                [](const auto &, Real) { return 0.0; },
                [](const auto &, Real) { return 0.0; });
            if (!result) {
                return result;
            }
            auto problem = std::move(result).value();
            auto displacement = tsunami::r2d::make_filled_regional_seabed_displacement(problem.mesh, 0.0, 0.0, 0.05);
            if (!displacement) {
                return tsunami::core::failure<RegionalBenchmarkCase>(displacement.error());
            }
            problem.time_policy.maximum_timestep = 0.001;
            return initialise_earthquake_case(
                std::move(problem),
                std::move(displacement).value(),
                tsunami::r2d::RegionalBedDeformationMappingKind::vertical_only,
                tsunami::r2d::RegionalSurfaceTransferKind::passive_equal_to_effective_bed,
                metadata_for("earthquake_uniform_vertical_translation"));
        }

        [[nodiscard]] auto make_earthquake_localised_vertical_uplift_case()
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            auto result = make_case(
                "earthquake_localised_vertical_uplift",
                StructuredTriangularMeshSpec{"earthquake-localised-vertical-uplift", 16U, 8U, 1.0, 0.5},
                0.025,
                [](const auto &) { return 0.0; },
                [](const auto &, Real bed) { return 1.0 - bed; },
                [](const auto &, Real) { return 0.0; },
                [](const auto &, Real) { return 0.0; });
            if (!result) {
                return result;
            }
            auto problem = std::move(result).value();
            std::vector<Real> uplift;
            uplift.reserve(problem.mesh.summary().cell_count);
            for (std::size_t index = 0; index < problem.mesh.summary().cell_count; ++index) {
                const auto centroid = problem.mesh.cell_geometry(tsunami::fvm::CellId{index}).centroid;
                const auto dx = centroid.x - 0.5;
                const auto dy = centroid.y - 0.25;
                uplift.push_back(0.04 * std::exp(-((dx * dx) + (dy * dy)) / (2.0 * 0.08 * 0.08)));
            }
            auto displacement = tsunami::r2d::make_vertical_regional_seabed_displacement(problem.mesh, std::move(uplift));
            if (!displacement) {
                return tsunami::core::failure<RegionalBenchmarkCase>(displacement.error());
            }
            problem.time_policy.maximum_timestep = 0.0008;
            return initialise_earthquake_case(
                std::move(problem),
                std::move(displacement).value(),
                tsunami::r2d::RegionalBedDeformationMappingKind::vertical_only,
                tsunami::r2d::RegionalSurfaceTransferKind::passive_equal_to_effective_bed,
                metadata_for("earthquake_localised_vertical_uplift"));
        }

        [[nodiscard]] auto make_earthquake_uplift_subsidence_dipole_case()
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            auto result = make_case(
                "earthquake_uplift_subsidence_dipole",
                StructuredTriangularMeshSpec{"earthquake-uplift-subsidence-dipole", 16U, 8U, 1.0, 0.5},
                0.025,
                [](const auto &) { return 0.0; },
                [](const auto &, Real bed) { return 1.0 - bed; },
                [](const auto &, Real) { return 0.0; },
                [](const auto &, Real) { return 0.0; });
            if (!result) {
                return result;
            }
            auto problem = std::move(result).value();
            std::vector<Real> displacement_values;
            displacement_values.reserve(problem.mesh.summary().cell_count);
            for (std::size_t index = 0; index < problem.mesh.summary().cell_count; ++index) {
                const auto centroid = problem.mesh.cell_geometry(tsunami::fvm::CellId{index}).centroid;
                const auto left_dx = centroid.x - 0.35;
                const auto right_dx = centroid.x - 0.65;
                const auto dy = centroid.y - 0.25;
                const auto left = std::exp(-((left_dx * left_dx) + (dy * dy)) / (2.0 * 0.08 * 0.08));
                const auto right = std::exp(-((right_dx * right_dx) + (dy * dy)) / (2.0 * 0.08 * 0.08));
                displacement_values.push_back(0.035 * (left - right));
            }
            auto displacement = tsunami::r2d::make_vertical_regional_seabed_displacement(problem.mesh, std::move(displacement_values));
            if (!displacement) {
                return tsunami::core::failure<RegionalBenchmarkCase>(displacement.error());
            }
            problem.time_policy.maximum_timestep = 0.0008;
            return initialise_earthquake_case(
                std::move(problem),
                std::move(displacement).value(),
                tsunami::r2d::RegionalBedDeformationMappingKind::vertical_only,
                tsunami::r2d::RegionalSurfaceTransferKind::passive_equal_to_effective_bed,
                metadata_for("earthquake_uplift_subsidence_dipole"));
        }

        [[nodiscard]] auto make_earthquake_horizontal_slope_correction_case()
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            constexpr auto a = Real{0.12};
            constexpr auto b = Real{-0.04};
            constexpr auto c = Real{0.05};
            auto bed = [](const tsunami::fvm::Point3 &point) { return a * point.x + b * point.y + c; };
            auto result = make_case(
                "earthquake_horizontal_slope_correction",
                StructuredTriangularMeshSpec{"earthquake-horizontal-slope-correction", 8U, 4U, 1.0, 0.5},
                0.04,
                bed,
                [](const auto &, Real bed_value) { return 1.0 - bed_value; },
                [](const auto &, Real) { return 0.0; },
                [](const auto &, Real) { return 0.0; });
            if (!result) {
                return result;
            }
            auto problem = std::move(result).value();
            auto fixed_bed = tsunami::fvm::make_boundary_condition_set(problem.mesh, fixed_bathymetry_specs(problem.mesh, bed));
            if (!fixed_bed) {
                return tsunami::core::failure<RegionalBenchmarkCase>(fixed_bed.error());
            }
            problem.bathymetry_boundaries = std::move(fixed_bed).value();
            auto regional = tsunami::r2d::make_regional_boundary_condition_set(
                problem.mesh,
                problem.depth_boundaries,
                problem.momentum_x_boundaries,
                problem.momentum_y_boundaries,
                problem.bathymetry_boundaries,
                {});
            if (!regional) {
                return tsunami::core::failure<RegionalBenchmarkCase>(regional.error());
            }
            problem.regional_boundaries = std::move(regional).value();
            auto displacement = tsunami::r2d::make_filled_regional_seabed_displacement(problem.mesh, 0.2, -0.1, 0.03);
            if (!displacement) {
                return tsunami::core::failure<RegionalBenchmarkCase>(displacement.error());
            }
            problem.time_policy.maximum_timestep = 0.001;
            return initialise_earthquake_case(
                std::move(problem),
                std::move(displacement).value(),
                tsunami::r2d::RegionalBedDeformationMappingKind::horizontal_slope_corrected,
                tsunami::r2d::RegionalSurfaceTransferKind::passive_equal_to_effective_bed,
                metadata_for("earthquake_horizontal_slope_correction"));
        }

        [[nodiscard]] auto make_earthquake_prescribed_surface_perturbation_case()
            -> tsunami::core::Result<RegionalBenchmarkCase>
        {
            auto result = make_case(
                "earthquake_prescribed_surface_perturbation",
                StructuredTriangularMeshSpec{"earthquake-prescribed-surface-perturbation", 12U, 4U, 1.0, 0.35},
                0.02,
                [](const auto &point) { return point.x < 0.2 ? 0.18 : 0.0; },
                [](const auto &point, Real bed) { return point.x < 0.18 ? 0.0 : std::max(0.0, 0.08 - bed); },
                [](const auto &, Real) { return 0.0; },
                [](const auto &, Real) { return 0.0; });
            if (!result) {
                return result;
            }
            auto problem = std::move(result).value();
            std::vector<Real> bed_displacement;
            std::vector<Real> surface_values;
            bed_displacement.reserve(problem.mesh.summary().cell_count);
            surface_values.reserve(problem.mesh.summary().cell_count);
            for (std::size_t index = 0; index < problem.mesh.summary().cell_count; ++index) {
                const auto centroid = problem.mesh.cell_geometry(tsunami::fvm::CellId{index}).centroid;
                const auto dx = centroid.x - 0.45;
                const auto dry_dx = centroid.x - 0.1;
                const auto dy = centroid.y - 0.175;
                bed_displacement.push_back(0.02 * std::exp(-((dx * dx) + (dy * dy)) / (2.0 * 0.1 * 0.1)));
                surface_values.push_back(0.16 * std::exp(-((dry_dx * dry_dx) + (dy * dy)) / (2.0 * 0.09 * 0.09)) - 0.06);
            }
            auto displacement = tsunami::r2d::make_vertical_regional_seabed_displacement(problem.mesh, std::move(bed_displacement));
            if (!displacement) {
                return tsunami::core::failure<RegionalBenchmarkCase>(displacement.error());
            }
            auto prescribed = tsunami::fvm::make_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(
                problem.mesh,
                tsunami::fvm::FieldId{"earthquake.prescribed-surface"},
                "earthquake prescribed surface perturbation",
                "m",
                std::move(surface_values));
            if (!prescribed) {
                return tsunami::core::failure<RegionalBenchmarkCase>(prescribed.error());
            }
            problem.time_policy.maximum_timestep = 0.0008;
            return initialise_earthquake_case(
                std::move(problem),
                std::move(displacement).value(),
                tsunami::r2d::RegionalBedDeformationMappingKind::vertical_only,
                tsunami::r2d::RegionalSurfaceTransferKind::prescribed,
                metadata_for("earthquake_prescribed_surface_perturbation"),
                &prescribed.value());
        }
    } // namespace

    auto regional_benchmark_case_ids() -> std::vector<std::string_view>
    {
        return {
            "lake_at_rest_flat",
            "lake_at_rest_bed_step",
            "partially_dry_lake_at_rest",
            "wet_dry_dam_break",
            "outgoing_linear_wave_radiation",
            "outgoing_linear_wave_radiation_sponge",
            "uniform_manning_decay",
            "uniform_coriolis_oscillation",
            "uniform_manning_coriolis",
            "frictional_wet_dry_dam_break",
            "earthquake_uniform_vertical_translation",
            "earthquake_localised_vertical_uplift",
            "earthquake_uplift_subsidence_dipole",
            "earthquake_horizontal_slope_correction",
            "earthquake_prescribed_surface_perturbation"};
    }

    auto make_regional_benchmark_case(std::string_view id)
        -> tsunami::core::Result<RegionalBenchmarkCase>
    {
        if (id == "lake_at_rest_flat") {
            return make_case(
                "lake_at_rest_flat",
                StructuredTriangularMeshSpec{"lake-at-rest-flat", 4U, 2U, 1.0, 0.5},
                0.05,
                [](const auto &) { return 0.0; },
                [](const auto &, Real bed) { return 1.0 - bed; },
                [](const auto &, Real) { return 0.0; },
                [](const auto &, Real) { return 0.0; });
        }
        if (id == "lake_at_rest_bed_step") {
            return make_case(
                "lake_at_rest_bed_step",
                StructuredTriangularMeshSpec{"lake-at-rest-bed-step", 6U, 2U, 1.0, 0.5},
                0.05,
                [](const auto &point) { return point.x < 0.5 ? 0.0 : 0.25; },
                [](const auto &, Real bed) { return 1.0 - bed; },
                [](const auto &, Real) { return 0.0; },
                [](const auto &, Real) { return 0.0; });
        }
        if (id == "partially_dry_lake_at_rest") {
            return make_case(
                "partially_dry_lake_at_rest",
                StructuredTriangularMeshSpec{"partially-dry-lake-at-rest", 6U, 2U, 1.0, 0.5},
                0.05,
                [](const auto &point) { return point.x < 0.55 ? 0.0 : 1.2; },
                [](const auto &, Real bed) { return std::max(0.0, 1.0 - bed); },
                [](const auto &, Real) { return 0.0; },
                [](const auto &, Real) { return 0.0; });
        }
        if (id == "wet_dry_dam_break") {
            return make_case(
                "wet_dry_dam_break",
                StructuredTriangularMeshSpec{"wet-dry-dam-break", 8U, 2U, 1.0, 0.25},
                0.02,
                [](const auto &) { return 0.0; },
                [](const auto &point, Real) { return point.x < 0.5 ? 1.5 : 0.05; },
                [](const auto &, Real) { return 0.0; },
                [](const auto &, Real) { return 0.0; });
        }
        if (id == "outgoing_linear_wave_radiation") {
            return make_outgoing_wave_case("outgoing_linear_wave_radiation", false);
        }
        if (id == "outgoing_linear_wave_radiation_sponge") {
            return make_outgoing_wave_case("outgoing_linear_wave_radiation_sponge", true);
        }
        if (id == "uniform_manning_decay") {
            return make_uniform_manning_decay_case();
        }
        if (id == "uniform_coriolis_oscillation") {
            return make_uniform_coriolis_oscillation_case();
        }
        if (id == "uniform_manning_coriolis") {
            return make_uniform_manning_coriolis_case();
        }
        if (id == "frictional_wet_dry_dam_break") {
            return make_frictional_wet_dry_dam_break_case();
        }
        if (id == "earthquake_uniform_vertical_translation") {
            return make_earthquake_uniform_vertical_translation_case();
        }
        if (id == "earthquake_localised_vertical_uplift") {
            return make_earthquake_localised_vertical_uplift_case();
        }
        if (id == "earthquake_uplift_subsidence_dipole") {
            return make_earthquake_uplift_subsidence_dipole_case();
        }
        if (id == "earthquake_horizontal_slope_correction") {
            return make_earthquake_horizontal_slope_correction_case();
        }
        if (id == "earthquake_prescribed_surface_perturbation") {
            return make_earthquake_prescribed_surface_perturbation_case();
        }
        return tsunami::core::failure<RegionalBenchmarkCase>(error(
            "r2d.benchmark.case_unknown",
            "regional benchmark case id is unknown"));
    }

} // namespace tsunami::r2d_benchmarks
