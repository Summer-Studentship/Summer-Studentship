#include <tsunami/r2d_benchmarks/RegionalBenchmarkCases.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include <tsunami/fvm/BoundarySpecification.hpp>
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
                final_time});
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
            "frictional_wet_dry_dam_break"};
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
        return tsunami::core::failure<RegionalBenchmarkCase>(error(
            "r2d.benchmark.case_unknown",
            "regional benchmark case id is unknown"));
    }

} // namespace tsunami::r2d_benchmarks
