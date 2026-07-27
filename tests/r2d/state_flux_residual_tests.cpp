#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <tsunami/fvm/BoundarySpecification.hpp>
#include <tsunami/r2d/CflTimestep.hpp>
#include <tsunami/r2d/ForwardEulerUpdate.hpp>
#include <tsunami/r2d/RegionalResidualEvaluation.hpp>

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
    using tsunami::fvm::FixedValueSpecification;
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

    [[nodiscard]] auto filled_state(const tsunami::fvm::FiniteVolumeMesh &finite_volume_mesh, ConservedVariables2D value)
    {
        return tsunami::r2d::make_filled_regional_conserved_state(
            finite_volume_mesh,
            FieldId{"h"},
            FieldId{"qx"},
            FieldId{"qy"},
            value,
            policy()).value();
    }

    [[nodiscard]] auto fixed_specs(const tsunami::fvm::FiniteVolumeMesh &finite_volume_mesh, Real value, std::string unit)
        -> std::vector<BoundarySpecification<Real>>
    {
        std::vector<BoundarySpecification<Real>> specs;
        for (std::size_t index = 0; index < finite_volume_mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = BoundaryPatchId{index};
            const auto &patch = finite_volume_mesh.boundary_patch(patch_id);
            specs.push_back(BoundarySpecification<Real>{
                BoundaryConditionId{"fixed-" + patch.name},
                patch.name + " fixed",
                patch.name,
                unit,
                FixedValueSpecification<Real>{std::vector<Real>(patch.faces.size(), value)}});
        }
        return specs;
    }

    [[nodiscard]] auto boundary_set(const tsunami::fvm::FiniteVolumeMesh &finite_volume_mesh, std::vector<BoundarySpecification<Real>> specs)
    {
        return tsunami::fvm::make_boundary_condition_set(finite_volume_mesh, std::move(specs)).value();
    }

    [[nodiscard]] auto spectral_field(const tsunami::fvm::FiniteVolumeMesh &finite_volume_mesh, Real value = 0.0)
    {
        return tsunami::fvm::make_filled_mesh_field<Real, tsunami::fvm::FieldLocation::cell>(
            finite_volume_mesh,
            FieldId{"spectral"},
            "spectral",
            "m2/s",
            value).value();
    }

    [[nodiscard]] auto vector_max_abs(const tsunami::r2d::RegionalResidual &residual) -> Real
    {
        auto result = Real{0.0};
        for (std::size_t index = 0; index < residual.size(); ++index) {
            result = std::max(result, std::abs(residual.mass().at(index)));
            result = std::max(result, std::abs(residual.momentum_x().at(index)));
            result = std::max(result, std::abs(residual.momentum_y().at(index)));
        }
        return result;
    }

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        const auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }
} // namespace

TEST_CASE("Regional2D state policy validation is deterministic", "[r2d][state]")
{
    REQUIRE(tsunami::r2d::make_shallow_water_state_policy(9.81, 1.0e-6, 0.0, 1.0e-12).has_value());
    REQUIRE_FALSE(tsunami::r2d::make_shallow_water_state_policy(0.0, 1.0e-6, 0.0, 1.0e-12).has_value());
    REQUIRE_FALSE(tsunami::r2d::make_shallow_water_state_policy(-9.81, 1.0e-6, 0.0, 1.0e-12).has_value());
    REQUIRE_FALSE(tsunami::r2d::make_shallow_water_state_policy(std::numeric_limits<Real>::quiet_NaN(), 1.0e-6, 0.0, 1.0e-12).has_value());
    REQUIRE_FALSE(tsunami::r2d::make_shallow_water_state_policy(9.81, 0.0, 0.0, 1.0e-12).has_value());
    REQUIRE_FALSE(tsunami::r2d::make_shallow_water_state_policy(9.81, 1.0e-6, -1.0e-9, 1.0e-12).has_value());
    REQUIRE_FALSE(tsunami::r2d::make_shallow_water_state_policy(9.81, 1.0e-6, 2.0e-6, 1.0e-12).has_value());
    REQUIRE_FALSE(tsunami::r2d::make_shallow_water_state_policy(9.81, 1.0e-6, 0.0, 0.0).has_value());
}

TEST_CASE("Regional2D local state admissibility canonicalises dry states", "[r2d][state]")
{
    const auto p = policy();
    auto wet = tsunami::r2d::validate_and_canonicalise_state(ConservedVariables2D{2.0, 4.0, -2.0}, p).value();
    REQUIRE(wet.depth == Approx(2.0));
    REQUIRE(wet.momentum_x == Approx(4.0));

    for (const auto dry_depth : {0.0, 0.5e-6, -0.5e-8}) {
        auto dry = tsunami::r2d::validate_and_canonicalise_state(ConservedVariables2D{dry_depth, 99.0, -99.0}, p).value();
        REQUIRE(dry.depth == 0.0);
        REQUIRE(dry.momentum_x == 0.0);
        REQUIRE(dry.momentum_y == 0.0);
        auto primitive = tsunami::r2d::recover_primitive_variables(dry, p).value();
        REQUIRE(primitive.velocity_x == 0.0);
        REQUIRE(primitive.velocity_y == 0.0);
    }

    REQUIRE_FALSE(tsunami::r2d::validate_and_canonicalise_state(ConservedVariables2D{-2.0e-8, 0.0, 0.0}, p).has_value());
    REQUIRE_FALSE(tsunami::r2d::validate_and_canonicalise_state(ConservedVariables2D{std::numeric_limits<Real>::infinity(), 0.0, 0.0}, p).has_value());
    REQUIRE_FALSE(tsunami::r2d::validate_and_canonicalise_state(ConservedVariables2D{1.0, std::numeric_limits<Real>::quiet_NaN(), 0.0}, p).has_value());

    auto primitive = tsunami::r2d::recover_primitive_variables(ConservedVariables2D{2.0, 4.0, -6.0}, p).value();
    REQUIRE(primitive.depth == Approx(2.0));
    REQUIRE(primitive.velocity_x == Approx(2.0));
    REQUIRE(primitive.velocity_y == Approx(-3.0));
}

TEST_CASE("RegionalConservedState owns canonical fixed-size component fields", "[r2d][state]")
{
    const auto m = mesh();
    auto regional = state(m, {2.0, 5.0e-7}, {4.0, 99.0}, {-2.0, -99.0});

    static_assert(!std::is_copy_constructible_v<tsunami::r2d::RegionalConservedState>);
    REQUIRE(regional.size() == 2);
    REQUIRE(regional.is_bound_to(m));
    REQUIRE(regional.depth().descriptor().unit_id == "m");
    REQUIRE(regional.momentum_x().descriptor().unit_id == "m2/s");
    REQUIRE(regional.local_state(CellId{0}).momentum_x == Approx(4.0));
    REQUIRE(regional.local_state(CellId{1}).depth == 0.0);
    REQUIRE(regional.local_state(CellId{1}).momentum_y == 0.0);

    auto clone = regional.clone();
    clone.set_local_state(CellId{0}, ConservedVariables2D{3.0, 0.0, 0.0});
    REQUIRE(regional.local_state(CellId{0}).depth == Approx(2.0));
    REQUIRE(clone.local_state(CellId{0}).depth == Approx(3.0));

    const auto before = regional.local_state(CellId{0});
    regional.set_local_state(CellId{0}, ConservedVariables2D{-2.0e-8, 0.0, 0.0});
    REQUIRE_FALSE(tsunami::r2d::validate_and_canonicalise(regional, policy()).has_value());
    REQUIRE(regional.local_state(CellId{0}).depth == Approx(-2.0e-8));
    regional.set_local_state(CellId{0}, before);
}

TEST_CASE("Physical flux and signal speed match analytical shallow-water values", "[r2d][flux]")
{
    const auto p = policy();
    auto normal = tsunami::r2d::make_face_normal(tsunami::fvm::Vector3{1.0, 0.0, 0.0}, p).value();
    auto still = tsunami::r2d::physical_normal_flux(ConservedVariables2D{2.0, 0.0, 0.0}, normal, p).value();
    REQUIRE(still.mass == Approx(0.0).margin(tol));
    REQUIRE(still.momentum_x == Approx(0.5 * 9.81 * 4.0).margin(tol));
    REQUIRE(still.momentum_y == Approx(0.0).margin(tol));

    auto flow_x = tsunami::r2d::physical_normal_flux(ConservedVariables2D{2.0, 6.0, 0.0}, normal, p).value();
    REQUIRE(flow_x.mass == Approx(6.0).margin(tol));
    REQUIRE(flow_x.momentum_x == Approx(6.0 * 3.0 + 0.5 * 9.81 * 4.0).margin(tol));

    auto y_normal = tsunami::r2d::make_face_normal(tsunami::fvm::Vector3{0.0, 2.0, 0.0}, p).value();
    auto flow_y = tsunami::r2d::physical_normal_flux(ConservedVariables2D{2.0, 0.0, 8.0}, y_normal, p).value();
    REQUIRE(flow_y.mass == Approx(8.0).margin(tol));
    REQUIRE(flow_y.momentum_y == Approx(8.0 * 4.0 + 0.5 * 9.81 * 4.0).margin(tol));

    auto oblique = tsunami::r2d::make_face_normal(tsunami::fvm::Vector3{1.0, 1.0, 0.0}, p).value();
    auto speed = tsunami::r2d::characteristic_signal_speed(ConservedVariables2D{4.0, 8.0, 0.0}, oblique, p).value();
    REQUIRE(speed == Approx(std::abs(2.0 / std::sqrt(2.0)) + std::sqrt(9.81 * 4.0)).epsilon(1.0e-12));
    auto reversed = tsunami::r2d::characteristic_signal_speed(ConservedVariables2D{4.0, 8.0, 0.0}, tsunami::r2d::negated(oblique), p).value();
    REQUIRE(reversed == Approx(speed).margin(tol));
    REQUIRE(tsunami::r2d::characteristic_signal_speed(ConservedVariables2D{}, normal, p).value() == 0.0);
}

TEST_CASE("Rusanov flux is consistent, conservative under reversal and dry-state safe", "[r2d][flux]")
{
    const auto p = policy();
    auto normal = tsunami::r2d::make_face_normal(tsunami::fvm::Vector3{1.0, 1.0, 0.0}, p).value();
    const auto left = ConservedVariables2D{2.0, 3.0, -1.0};
    const auto right = ConservedVariables2D{1.0, -0.5, 0.25};

    auto physical = tsunami::r2d::physical_normal_flux(left, normal, p).value();
    auto consistent = tsunami::r2d::rusanov_flux(left, left, normal, p).value();
    REQUIRE(consistent.flux.mass == Approx(physical.mass).margin(tol));
    REQUIRE(consistent.flux.momentum_x == Approx(physical.momentum_x).margin(tol));
    REQUIRE(consistent.flux.momentum_y == Approx(physical.momentum_y).margin(tol));

    auto forward = tsunami::r2d::rusanov_flux(left, right, normal, p).value();
    auto reverse = tsunami::r2d::rusanov_flux(right, left, tsunami::r2d::negated(normal), p).value();
    REQUIRE(reverse.flux.mass == Approx(-forward.flux.mass).margin(tol));
    REQUIRE(reverse.flux.momentum_x == Approx(-forward.flux.momentum_x).margin(tol));
    REQUIRE(reverse.flux.momentum_y == Approx(-forward.flux.momentum_y).margin(tol));

    for (const auto pair : {std::pair{ConservedVariables2D{}, ConservedVariables2D{}},
                            std::pair{left, ConservedVariables2D{}},
                            std::pair{ConservedVariables2D{}, right},
                            std::pair{left, right}}) {
        auto flux = tsunami::r2d::rusanov_flux(pair.first, pair.second, normal, p).value();
        REQUIRE(std::isfinite(flux.flux.mass));
        REQUIRE(std::isfinite(flux.flux.momentum_x));
        REQUIRE(std::isfinite(flux.flux.momentum_y));
        REQUIRE(std::isfinite(flux.maximum_signal_speed));
    }
    auto dry = tsunami::r2d::rusanov_flux(ConservedVariables2D{}, ConservedVariables2D{}, normal, p).value();
    REQUIRE(dry.flux.mass == 0.0);
    REQUIRE(dry.flux.momentum_x == 0.0);
    REQUIRE(dry.flux.momentum_y == 0.0);
    REQUIRE(dry.maximum_signal_speed == 0.0);
}

TEST_CASE("Regional residual preserves constant states and rejects named boundaries transactionally", "[r2d][residual]")
{
    const auto m = mesh();
    const auto p = policy();
    auto regional = filled_state(m, ConservedVariables2D{2.0, 2.0, -1.0});
    auto h_boundaries = boundary_set(m, fixed_specs(m, 2.0, "m"));
    auto qx_boundaries = boundary_set(m, fixed_specs(m, 2.0, "m2/s"));
    auto qy_boundaries = boundary_set(m, fixed_specs(m, -1.0, "m2/s"));
    auto residual = tsunami::r2d::make_regional_residual(m).value();
    auto spectral = spectral_field(m, -9.0);
    auto workspace = tsunami::r2d::make_regional_residual_workspace(m).value();
    auto max_speed = Real{-1.0};

    const auto *residual_data = residual.mass().values().data();
    const auto *workspace_data = workspace.residual().mass().values().data();
    REQUIRE(tsunami::r2d::evaluate_rusanov_residual(m, regional, h_boundaries, qx_boundaries, qy_boundaries, p, residual, spectral, max_speed, workspace).has_value());
    REQUIRE(vector_max_abs(residual) == Approx(0.0).margin(tol));
    for (const auto value : spectral.values()) {
        REQUIRE(value >= 0.0);
        REQUIRE(std::isfinite(value));
    }
    REQUIRE(max_speed > 0.0);

    auto dry = filled_state(m, ConservedVariables2D{0.0, 0.0, 0.0});
    auto dry_h = boundary_set(m, fixed_specs(m, 0.0, "m"));
    auto dry_q = boundary_set(m, fixed_specs(m, 0.0, "m2/s"));
    REQUIRE(tsunami::r2d::evaluate_rusanov_residual(m, dry, dry_h, dry_q, dry_q, p, residual, spectral, max_speed, workspace).has_value());
    REQUIRE(vector_max_abs(residual) == Approx(0.0).margin(tol));
    for (const auto value : spectral.values()) {
        REQUIRE(value == Approx(0.0).margin(tol));
    }

    residual.mass().fill(7.0);
    spectral.fill(8.0);
    max_speed = 9.0;
    auto named_specs = fixed_specs(m, 2.0, "m");
    named_specs[2].operation = NamedBoundarySpecification{"radiation"};
    auto named_depth = boundary_set(m, std::move(named_specs));
    auto failed = tsunami::r2d::evaluate_rusanov_residual(m, regional, named_depth, qx_boundaries, qy_boundaries, p, residual, spectral, max_speed, workspace);
    REQUIRE_FALSE(failed.has_value());
    REQUIRE(failed.error().code() == "r2d.residual.boundary_not_executable");
    REQUIRE(context_value(failed.error(), "state_changed") == "false");
    for (const auto value : residual.mass().values()) {
        REQUIRE(value == Approx(7.0));
    }
    for (const auto value : spectral.values()) {
        REQUIRE(value == Approx(8.0));
    }
    REQUIRE(max_speed == Approx(9.0));

    REQUIRE(residual.mass().values().data() == residual_data);
    REQUIRE(workspace.residual().mass().values().data() == workspace_data);
    REQUIRE(tsunami::r2d::evaluate_rusanov_residual(m, regional, h_boundaries, qx_boundaries, qy_boundaries, p, residual, spectral, max_speed, workspace).has_value());
}

TEST_CASE("Regional residual uses one internal face flux with opposite contributions", "[r2d][residual]")
{
    const auto m = mesh();
    const auto p = policy();
    auto normal = tsunami::r2d::make_face_normal(m.face_geometry(FaceId{2}).area_vector, p, FaceId{2}).value();
    const auto left = ConservedVariables2D{2.0, 1.0, 0.0};
    const auto right = ConservedVariables2D{1.0, -0.5, 0.25};
    auto flux = tsunami::r2d::rusanov_flux(left, right, normal, p).value();
    const auto length = normal.length;

    REQUIRE((flux.flux.mass * length + flux.flux.mass * -length) == Approx(0.0).margin(tol));
    REQUIRE((flux.flux.momentum_x * length + flux.flux.momentum_x * -length) == Approx(0.0).margin(tol));
    REQUIRE((flux.flux.momentum_y * length + flux.flux.momentum_y * -length) == Approx(0.0).margin(tol));
}

TEST_CASE("CFL timestep estimate handles restrictions and zero spectral sums", "[r2d][cfl]")
{
    const auto m = mesh();
    auto spectral = spectral_field(m);
    spectral.at(0) = 2.0;
    spectral.at(1) = 4.0;

    auto estimate = tsunami::r2d::estimate_cfl_timestep(m, spectral, 0.5).value();
    REQUIRE(estimate.stable_timestep.has_value());
    REQUIRE(*estimate.stable_timestep == Approx(0.5 * 0.5 / 4.0).margin(tol));
    REQUIRE(estimate.limiting_cell->value == 1);
    auto lower = tsunami::r2d::estimate_cfl_timestep(m, spectral, 0.25).value();
    REQUIRE(*lower.stable_timestep == Approx(0.5 * *estimate.stable_timestep).margin(tol));

    spectral.fill(0.0);
    auto none = tsunami::r2d::estimate_cfl_timestep(m, spectral, 0.5).value();
    REQUIRE_FALSE(none.stable_timestep.has_value());
    REQUIRE_FALSE(none.limiting_cell.has_value());

    spectral.at(0) = -1.0;
    REQUIRE_FALSE(tsunami::r2d::estimate_cfl_timestep(m, spectral, 0.5).has_value());
    spectral.at(0) = std::numeric_limits<Real>::quiet_NaN();
    REQUIRE_FALSE(tsunami::r2d::estimate_cfl_timestep(m, spectral, 0.5).has_value());
    REQUIRE_FALSE(tsunami::r2d::estimate_cfl_timestep(m, spectral, 0.0).has_value());
}

TEST_CASE("Forward Euler update is analytical, canonical and transactional", "[r2d][update]")
{
    const auto m = mesh();
    const auto p = policy();
    auto current = filled_state(m, ConservedVariables2D{2.0, 1.0, -1.0});
    auto destination = current.clone();
    auto residual = tsunami::r2d::make_regional_residual(m).value();
    auto workspace = tsunami::r2d::make_regional_state_update_workspace(m).value();

    const auto *destination_data = destination.depth().values().data();
    const auto *workspace_data = workspace.staging_states().data();
    REQUIRE(tsunami::r2d::forward_euler_update(m, current, residual, 0.25, p, destination, workspace).has_value());
    REQUIRE(destination.local_state(CellId{0}).depth == Approx(2.0).margin(tol));

    residual.mass().at(0) = 0.2;
    residual.momentum_x().at(0) = -0.4;
    residual.momentum_y().at(0) = 0.8;
    REQUIRE(tsunami::r2d::forward_euler_update(m, current, residual, 0.25, p, destination, workspace).has_value());
    REQUIRE(destination.local_state(CellId{0}).depth == Approx(2.0 - (0.25 / 0.5 * 0.2)).margin(tol));
    REQUIRE(destination.local_state(CellId{0}).momentum_x == Approx(1.0 - (0.25 / 0.5 * -0.4)).margin(tol));
    REQUIRE(destination.local_state(CellId{0}).momentum_y == Approx(-1.0 - (0.25 / 0.5 * 0.8)).margin(tol));

    residual.mass().at(1) = 4.0 + (4.0e-8);
    auto failed = tsunami::r2d::forward_euler_update(m, current, residual, 0.25, p, destination, workspace);
    REQUIRE_FALSE(failed.has_value());
    REQUIRE(failed.error().code() == "r2d.update.candidate_invalid");
    REQUIRE(destination.local_state(CellId{1}).depth == Approx(2.0).margin(tol));

    residual.mass().at(1) = 4.0 - (1.0e-9);
    REQUIRE(tsunami::r2d::forward_euler_update(m, current, residual, 0.25, p, destination, workspace).has_value());
    REQUIRE(destination.local_state(CellId{1}).depth == 0.0);
    REQUIRE(destination.local_state(CellId{1}).momentum_x == 0.0);
    REQUIRE(destination.depth().values().data() == destination_data);
    REQUIRE(workspace.staging_states().data() == workspace_data);
    REQUIRE_FALSE(tsunami::r2d::forward_euler_update(m, current, residual, 0.0, p, destination, workspace).has_value());
}
