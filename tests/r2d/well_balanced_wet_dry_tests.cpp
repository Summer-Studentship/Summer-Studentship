#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <tsunami/fvm/BoundarySpecification.hpp>
#include <tsunami/r2d/FreeSurfaceElevation.hpp>
#include <tsunami/r2d/PositivityTimestep.hpp>
#include <tsunami/r2d/RegionalBathymetry.hpp>
#include <tsunami/r2d/WellBalancedResidualEvaluation.hpp>
#include <tsunami/r2d/WetDryUpdate.hpp>

#include "../fvm/reference_boundaries.hpp"

namespace
{
    using Catch::Approx;
    using tsunami::core::Real;
    using tsunami::fvm::BoundaryConditionId;
    using tsunami::fvm::BoundaryPatchId;
    using tsunami::fvm::BoundarySpecification;
    using tsunami::fvm::CellId;
    using tsunami::fvm::FaceId;
    using tsunami::fvm::FieldId;
    using tsunami::fvm::NamedBoundarySpecification;
    using tsunami::fvm::ZeroGradientSpecification;
    using tsunami::r2d::ConservedVariables2D;

    constexpr auto tol = 1.0e-12;

    [[nodiscard]] auto policy()
    {
        return tsunami::r2d::make_shallow_water_state_policy(9.81, 1.0e-6, 1.0e-8, 1.0e-12).value();
    }

    [[nodiscard]] auto mesh()
    {
        return tsunami::tests::fvm::reference_mesh();
    }

    [[nodiscard]] auto state(
        const tsunami::fvm::FiniteVolumeMesh &finite_volume_mesh,
        std::vector<Real> depth,
        std::vector<Real> qx,
        std::vector<Real> qy)
    {
        return tsunami::r2d::make_regional_conserved_state(
            finite_volume_mesh,
            FieldId{"h"},
            FieldId{"qx"},
            FieldId{"qy"},
            std::move(depth),
            std::move(qx),
            std::move(qy),
            policy()).value();
    }

    [[nodiscard]] auto bathymetry(const tsunami::fvm::FiniteVolumeMesh &finite_volume_mesh, std::vector<Real> values)
    {
        return tsunami::r2d::make_regional_bathymetry(
            finite_volume_mesh,
            FieldId{"zb"},
            "bed elevation",
            std::move(values)).value();
    }

    [[nodiscard]] auto cell_field(const tsunami::fvm::FiniteVolumeMesh &finite_volume_mesh, std::string unit, Real value = 0.0)
    {
        return tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(
            finite_volume_mesh,
            FieldId{"cell-field-" + unit},
            "cell field",
            std::move(unit),
            value).value();
    }

    [[nodiscard]] auto zero_gradient_specs(const tsunami::fvm::FiniteVolumeMesh &finite_volume_mesh, std::string unit)
        -> std::vector<BoundarySpecification<Real>>
    {
        std::vector<BoundarySpecification<Real>> specs;
        for (std::size_t index = 0; index < finite_volume_mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = BoundaryPatchId{index};
            const auto &patch = finite_volume_mesh.boundary_patch(patch_id);
            specs.push_back(BoundarySpecification<Real>{
                BoundaryConditionId{"zero-" + patch.name + "-" + unit},
                patch.name + " zero",
                patch.name,
                unit,
                ZeroGradientSpecification{}});
        }
        return specs;
    }

    [[nodiscard]] auto boundary_set(const tsunami::fvm::FiniteVolumeMesh &finite_volume_mesh, std::vector<BoundarySpecification<Real>> specs)
    {
        return tsunami::fvm::make_boundary_condition_set(finite_volume_mesh, std::move(specs)).value();
    }

    [[nodiscard]] auto max_abs(const tsunami::r2d::RegionalResidual &residual) -> Real
    {
        auto result = Real{0.0};
        for (std::size_t index = 0; index < residual.size(); ++index) {
            result = std::max(result, std::abs(residual.mass().at(index)));
            result = std::max(result, std::abs(residual.momentum_x().at(index)));
            result = std::max(result, std::abs(residual.momentum_y().at(index)));
        }
        return result;
    }
} // namespace

TEST_CASE("RegionalBathymetry owns signed finite cell elevations", "[r2d][bathymetry]")
{
    const auto m = mesh();
    auto bed = bathymetry(m, {-2.0, 3.0});

    static_assert(!std::is_copy_constructible_v<tsunami::r2d::RegionalBathymetry>);
    REQUIRE(bed.size() == 2);
    REQUIRE(bed.is_bound_to(m));
    REQUIRE(bed.bed_elevation().descriptor().unit_id == "m");
    REQUIRE(bed.local_bed_elevation(CellId{0}) == Approx(-2.0));
    REQUIRE(bed.local_bed_elevation(CellId{1}) == Approx(3.0));

    auto filled = tsunami::r2d::make_filled_regional_bathymetry(m, FieldId{"zb-filled"}, "filled bed", 0.0).value();
    REQUIRE(filled.local_bed_elevation(CellId{0}) == Approx(0.0));
    REQUIRE(filled.local_bed_elevation(CellId{1}) == Approx(0.0));

    auto clone = bed.clone();
    clone.set_local_bed_elevation(CellId{0}, 7.0);
    REQUIRE(bed.local_bed_elevation(CellId{0}) == Approx(-2.0));
    REQUIRE(clone.local_bed_elevation(CellId{0}) == Approx(7.0));

    REQUIRE_FALSE(tsunami::r2d::make_regional_bathymetry(m, FieldId{"bad-count"}, "bad", {1.0}).has_value());
    REQUIRE_FALSE(tsunami::r2d::make_regional_bathymetry(m, FieldId{"bad-finite"}, "bad", {1.0, std::numeric_limits<Real>::quiet_NaN()}).has_value());
}

TEST_CASE("Free surface elevation is derived transactionally from depth and bed", "[r2d][free-surface]")
{
    const auto m = mesh();
    const auto regional = state(m, {2.0, 0.0}, {0.0, 0.0}, {0.0, 0.0});
    const auto bed = bathymetry(m, {-1.0, 3.0});
    auto eta = cell_field(m, "m", -99.0);

    REQUIRE(tsunami::r2d::calculate_free_surface_elevation(m, regional, bed, eta).has_value());
    REQUIRE(eta.at(0) == Approx(1.0).margin(tol));
    REQUIRE(eta.at(1) == Approx(3.0).margin(tol));

    auto wrong_unit = cell_field(m, "s", 4.0);
    REQUIRE_FALSE(tsunami::r2d::calculate_free_surface_elevation(m, regional, bed, wrong_unit).has_value());
    REQUIRE(wrong_unit.at(0) == Approx(4.0));
}

TEST_CASE("Hydrostatic reconstruction preserves flat beds and applies bed-step corrections", "[r2d][hydrostatic]")
{
    const auto p = policy();
    const auto normal = tsunami::r2d::FaceNormal2D{1.0, 0.0, 1.0};
    const auto left = ConservedVariables2D{2.0, 4.0, -2.0};
    const auto right = ConservedVariables2D{1.0, -1.0, 0.5};

    auto flat = tsunami::r2d::hydrostatic_reconstruction(left, right, 0.5, 0.5, normal, p).value();
    REQUIRE(flat.left.depth == Approx(left.depth).margin(tol));
    REQUIRE(flat.right.depth == Approx(right.depth).margin(tol));
    REQUIRE(flat.left_pressure_correction.mass == 0.0);
    REQUIRE(flat.right_pressure_correction.mass == 0.0);
    REQUIRE(flat.left_pressure_correction.momentum_x == Approx(0.0).margin(tol));

    auto flat_flux = tsunami::r2d::rusanov_flux(flat.left, flat.right, normal, p).value();
    auto homogeneous = tsunami::r2d::rusanov_flux(left, right, normal, p).value();
    REQUIRE(flat_flux.flux.mass == Approx(homogeneous.flux.mass).margin(tol));
    REQUIRE(flat_flux.flux.momentum_x == Approx(homogeneous.flux.momentum_x).margin(tol));

    auto step = tsunami::r2d::hydrostatic_reconstruction(left, right, 0.0, 1.5, normal, p).value();
    REQUIRE(step.interface_bed_elevation == Approx(1.5));
    REQUIRE(step.left.depth == Approx(0.5).margin(tol));
    REQUIRE(step.right.depth == Approx(1.0).margin(tol));
    REQUIRE(step.left.momentum_x == Approx(0.5 * (left.momentum_x / left.depth)).margin(tol));
    REQUIRE(step.left_pressure_correction.mass == 0.0);
    REQUIRE(step.left_pressure_correction.momentum_x == Approx(0.5 * p.gravity * (4.0 - 0.25)).margin(tol));
}

TEST_CASE("Hydrostatic reconstruction blocks and permits first-order inundation", "[r2d][wet-dry]")
{
    const auto p = policy();
    const auto normal = tsunami::r2d::FaceNormal2D{1.0, 0.0, 1.0};

    auto blocked = tsunami::r2d::hydrostatic_reconstruction(
        ConservedVariables2D{1.0, 0.0, 0.0},
        ConservedVariables2D{},
        0.0,
        1.0,
        normal,
        p).value();
    auto blocked_flux = tsunami::r2d::rusanov_flux(blocked.left, blocked.right, normal, p).value();
    REQUIRE(blocked.left.depth == 0.0);
    REQUIRE(blocked.right.depth == 0.0);
    REQUIRE(blocked_flux.flux.mass == Approx(0.0).margin(tol));

    auto permitted = tsunami::r2d::hydrostatic_reconstruction(
        ConservedVariables2D{2.0, 0.0, 0.0},
        ConservedVariables2D{},
        0.0,
        1.0,
        normal,
        p).value();
    auto permitted_flux = tsunami::r2d::rusanov_flux(permitted.left, permitted.right, normal, p).value();
    REQUIRE(permitted.left.depth > 0.0);
    REQUIRE(permitted_flux.flux.mass > 0.0);
}

TEST_CASE("Well-balanced residual matches homogeneous residual on flat beds", "[r2d][well-balanced]")
{
    const auto m = mesh();
    const auto p = policy();
    auto regional = state(m, {2.0, 1.0}, {1.0, -0.5}, {0.25, 0.75});
    auto bed = tsunami::r2d::make_filled_regional_bathymetry(m, FieldId{"flat-zb"}, "flat bed", -2.0).value();
    auto h_bc = boundary_set(m, zero_gradient_specs(m, "m"));
    auto qx_bc = boundary_set(m, zero_gradient_specs(m, "m2/s"));
    auto qy_bc = boundary_set(m, zero_gradient_specs(m, "m2/s"));
    auto zb_bc = boundary_set(m, zero_gradient_specs(m, "m"));

    auto homogeneous = tsunami::r2d::make_regional_residual(m).value();
    auto well_balanced = tsunami::r2d::make_regional_residual(m).value();
    auto homogeneous_spectral = cell_field(m, "m2/s");
    auto well_spectral = cell_field(m, "m2/s");
    auto outgoing = cell_field(m, "m3/s", -9.0);
    auto homogeneous_workspace = tsunami::r2d::make_regional_residual_workspace(m).value();
    auto well_workspace = tsunami::r2d::make_well_balanced_residual_workspace(m).value();
    auto homogeneous_speed = Real{};
    auto well_speed = Real{};

    REQUIRE(tsunami::r2d::evaluate_rusanov_residual(m, regional, h_bc, qx_bc, qy_bc, p, homogeneous, homogeneous_spectral, homogeneous_speed, homogeneous_workspace).has_value());
    REQUIRE(tsunami::r2d::evaluate_well_balanced_rusanov_residual(m, regional, bed, h_bc, qx_bc, qy_bc, zb_bc, p, well_balanced, well_spectral, outgoing, well_speed, well_workspace).has_value());

    for (std::size_t index = 0; index < m.summary().cell_count; ++index) {
        REQUIRE(well_balanced.mass().at(index) == Approx(homogeneous.mass().at(index)).margin(tol));
        REQUIRE(well_balanced.momentum_x().at(index) == Approx(homogeneous.momentum_x().at(index)).margin(tol));
        REQUIRE(well_balanced.momentum_y().at(index) == Approx(homogeneous.momentum_y().at(index)).margin(tol));
        REQUIRE(well_spectral.at(index) == Approx(homogeneous_spectral.at(index)).margin(tol));
        REQUIRE(outgoing.at(index) >= 0.0);
    }
    REQUIRE(well_speed == Approx(homogeneous_speed).margin(tol));
}

TEST_CASE("Well-balanced residual preserves wet and partially dry lakes at rest", "[r2d][well-balanced]")
{
    const auto m = mesh();
    const auto p = policy();
    auto h_bc = boundary_set(m, zero_gradient_specs(m, "m"));
    auto q_bc = boundary_set(m, zero_gradient_specs(m, "m2/s"));
    auto zb_bc = boundary_set(m, zero_gradient_specs(m, "m"));

    for (const auto eta0 : {3.0, 1.0}) {
        auto bed = bathymetry(m, {0.25, 1.5});
        auto regional = state(
            m,
            {std::max(Real{0.0}, eta0 - bed.local_bed_elevation(CellId{0})),
             std::max(Real{0.0}, eta0 - bed.local_bed_elevation(CellId{1}))},
            {0.0, 0.0},
            {0.0, 0.0});
        auto residual = tsunami::r2d::make_regional_residual(m).value();
        auto spectral = cell_field(m, "m2/s");
        auto outgoing = cell_field(m, "m3/s");
        auto workspace = tsunami::r2d::make_well_balanced_residual_workspace(m).value();
        auto max_speed = Real{-1.0};

        REQUIRE(tsunami::r2d::evaluate_well_balanced_rusanov_residual(m, regional, bed, h_bc, q_bc, q_bc, zb_bc, p, residual, spectral, outgoing, max_speed, workspace).has_value());
        REQUIRE(max_abs(residual) == Approx(0.0).margin(tol));
        for (const auto value : outgoing.values()) {
            REQUIRE(value == Approx(0.0).margin(tol));
        }

        auto stable = tsunami::r2d::StableExplicitTimestepEstimate{std::nullopt, std::nullopt, tsunami::r2d::TimestepRestrictionKind::none};
        auto destination = regional.clone();
        auto update_workspace = tsunami::r2d::make_wet_dry_update_workspace(m).value();
        auto diagnostics = tsunami::r2d::WetDryUpdateDiagnostics{};
        REQUIRE(tsunami::r2d::wet_dry_forward_euler_update(m, regional, residual, 0.1, stable, p, destination, diagnostics, update_workspace).has_value());
        REQUIRE(destination.local_state(CellId{0}).depth == Approx(regional.local_state(CellId{0}).depth).margin(tol));
        REQUIRE(destination.local_state(CellId{1}).depth == Approx(regional.local_state(CellId{1}).depth).margin(tol));
        REQUIRE(diagnostics.dry_threshold_removed_water_volume == Approx(0.0).margin(tol));
        REQUIRE(diagnostics.negative_tolerance_correction_volume == Approx(0.0).margin(tol));
    }
}

TEST_CASE("Positivity timestep and combined explicit bound are deterministic", "[r2d][positivity]")
{
    const auto m = mesh();
    auto regional = state(m, {2.0, 1.0}, {0.0, 0.0}, {0.0, 0.0});
    auto outgoing = cell_field(m, "m3/s");
    outgoing.at(0) = 2.0;
    outgoing.at(1) = 1.0;

    auto estimate = tsunami::r2d::estimate_positivity_timestep(m, regional, outgoing, 0.5).value();
    REQUIRE(*estimate.stable_timestep == Approx(0.25).margin(tol));
    REQUIRE(estimate.limiting_cell->value == 0);

    outgoing.fill(0.0);
    auto none = tsunami::r2d::estimate_positivity_timestep(m, regional, outgoing, 1.0).value();
    REQUIRE_FALSE(none.stable_timestep.has_value());
    REQUIRE_FALSE(tsunami::r2d::estimate_positivity_timestep(m, regional, outgoing, 0.0).has_value());
    outgoing.at(1) = -1.0;
    REQUIRE_FALSE(tsunami::r2d::estimate_positivity_timestep(m, regional, outgoing, 1.0).has_value());
    outgoing.at(1) = std::numeric_limits<Real>::quiet_NaN();
    REQUIRE_FALSE(tsunami::r2d::estimate_positivity_timestep(m, regional, outgoing, 1.0).has_value());

    const auto cfl = tsunami::r2d::CflTimestepEstimate{0.2, CellId{1}};
    const auto pos = tsunami::r2d::PositivityTimestepEstimate{0.25, CellId{0}};
    auto selected = tsunami::r2d::select_stable_explicit_timestep(cfl, pos, 1.0e-14).value();
    REQUIRE(*selected.stable_timestep == Approx(0.2));
    REQUIRE(selected.restriction == tsunami::r2d::TimestepRestrictionKind::cfl);
    auto equal = tsunami::r2d::select_stable_explicit_timestep(cfl, tsunami::r2d::PositivityTimestepEstimate{0.2 + 1.0e-15, CellId{0}}, 1.0e-12).value();
    REQUIRE(equal.restriction == tsunami::r2d::TimestepRestrictionKind::multiple);
    REQUIRE(tsunami::r2d::select_stable_explicit_timestep({}, {}, 0.0).value().restriction == tsunami::r2d::TimestepRestrictionKind::none);
    REQUIRE_FALSE(tsunami::r2d::select_stable_explicit_timestep(cfl, pos, -1.0).has_value());
}

TEST_CASE("Wet/dry forward Euler enforces bounds and reports transition volumes", "[r2d][wet-dry]")
{
    const auto m = mesh();
    const auto p = policy();
    auto current = state(m, {1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0});
    auto residual = tsunami::r2d::make_regional_residual(m).value();
    auto destination = current.clone();
    auto workspace = tsunami::r2d::make_wet_dry_update_workspace(m).value();
    auto diagnostics = tsunami::r2d::WetDryUpdateDiagnostics{};
    const auto stable = tsunami::r2d::StableExplicitTimestepEstimate{0.25, CellId{0}, tsunami::r2d::TimestepRestrictionKind::positivity};

    residual.mass().at(0) = 2.0;
    residual.mass().at(1) = -2.0;
    REQUIRE(tsunami::r2d::wet_dry_forward_euler_update(m, current, residual, 0.25, stable, p, destination, diagnostics, workspace).has_value());
    REQUIRE(destination.local_state(CellId{0}).depth == Approx(0.0).margin(tol));
    REQUIRE(destination.local_state(CellId{1}).depth == Approx(1.0).margin(tol));
    REQUIRE(diagnostics.cells_dried == 1);
    REQUIRE(diagnostics.cells_wetted == 1);
    REQUIRE(diagnostics.minimum_accepted_depth >= 0.0);

    const auto before = destination.local_state(CellId{1});
    REQUIRE_FALSE(tsunami::r2d::wet_dry_forward_euler_update(m, current, residual, 0.250001, stable, p, destination, diagnostics, workspace).has_value());
    REQUIRE(destination.local_state(CellId{1}).depth == Approx(before.depth).margin(tol));

    current = state(m, {2.0e-6, 2.0e-6}, {1.0, 1.0}, {1.0, 1.0});
    destination = current.clone();
    residual.fill(ConservedVariables2D{});
    residual.mass().at(0) = (2.0e-6 - 0.5e-6) * 0.5 / 0.25;
    residual.mass().at(1) = (2.0e-6 + 0.5e-8) * 0.5 / 0.25;
    REQUIRE(tsunami::r2d::wet_dry_forward_euler_update(m, current, residual, 0.25, stable, p, destination, diagnostics, workspace).has_value());
    REQUIRE(diagnostics.dry_threshold_removed_water_volume == Approx(0.5 * 0.5e-6).margin(tol));
    REQUIRE(diagnostics.negative_tolerance_correction_volume == Approx(0.5 * 0.5e-8).margin(tol));
    REQUIRE(diagnostics.cells_canonicalised == 2);
}

TEST_CASE("Well-balanced residual failure is transactional and workspace storage is reusable", "[r2d][well-balanced]")
{
    const auto m = mesh();
    const auto p = policy();
    auto regional = state(m, {2.0, 2.0}, {0.0, 0.0}, {0.0, 0.0});
    auto bed = bathymetry(m, {0.0, 0.0});
    auto h_bc_specs = zero_gradient_specs(m, "m");
    h_bc_specs[0].operation = NamedBoundarySpecification{"radiation"};
    auto h_bc = boundary_set(m, std::move(h_bc_specs));
    auto q_bc = boundary_set(m, zero_gradient_specs(m, "m2/s"));
    auto zb_bc = boundary_set(m, zero_gradient_specs(m, "m"));
    auto residual = tsunami::r2d::make_regional_residual(m).value();
    auto spectral = cell_field(m, "m2/s", 8.0);
    auto outgoing = cell_field(m, "m3/s", 9.0);
    auto workspace = tsunami::r2d::make_well_balanced_residual_workspace(m).value();
    auto max_speed = Real{7.0};
    const auto *residual_data = residual.mass().values().data();
    const auto *workspace_data = workspace.residual().mass().values().data();

    auto failed = tsunami::r2d::evaluate_well_balanced_rusanov_residual(m, regional, bed, h_bc, q_bc, q_bc, zb_bc, p, residual, spectral, outgoing, max_speed, workspace);
    REQUIRE_FALSE(failed.has_value());
    REQUIRE(failed.error().code() == "r2d.well_balanced.boundary_not_executable");
    for (const auto value : residual.mass().values()) {
        REQUIRE(value == Approx(0.0));
    }
    for (const auto value : spectral.values()) {
        REQUIRE(value == Approx(8.0));
    }
    for (const auto value : outgoing.values()) {
        REQUIRE(value == Approx(9.0));
    }
    REQUIRE(max_speed == Approx(7.0));
    REQUIRE(residual.mass().values().data() == residual_data);
    REQUIRE(workspace.residual().mass().values().data() == workspace_data);

    auto valid_h = boundary_set(m, zero_gradient_specs(m, "m"));
    REQUIRE(tsunami::r2d::evaluate_well_balanced_rusanov_residual(m, regional, bed, valid_h, q_bc, q_bc, zb_bc, p, residual, spectral, outgoing, max_speed, workspace).has_value());
}
