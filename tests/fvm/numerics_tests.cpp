#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include <tsunami/fvm/GreenGaussGradient.hpp>
#include <tsunami/fvm/LinearInterpolation.hpp>

#include "reference_boundaries.hpp"

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
    using tsunami::fvm::FieldLocation;
    using tsunami::fvm::FixedValueSpecification;
    using tsunami::fvm::NamedBoundarySpecification;
    using tsunami::fvm::Vector3;
    using tsunami::fvm::ZeroGradientSpecification;

    constexpr auto tolerance = 1.0e-12;

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        const auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }

    template <class Result>
    auto require_numerics_error(Result &&result, std::string_view code) -> void
    {
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code() == code);
        REQUIRE(result.error().category() == tsunami::core::DiagnosticCategory::numerical);
        REQUIRE(result.error().severity() == tsunami::core::Severity::error);
        REQUIRE(context_value(result.error(), "rule_id") == "SWE-FVM-NUM-WP1");
        REQUIRE(context_value(result.error(), "state_changed") == "false");
    }

    [[nodiscard]] auto skewed_mesh()
    {
        auto input = tsunami::tests::fvm::two_triangle_unit_square_input();
        input.id.value = "skewed-two-triangle-unit-square";
        input.vertices[2].position = tsunami::fvm::Point3{1.35, 1.10, 0.0};
        return tsunami::fvm::make_finite_volume_mesh(std::move(input)).value();
    }

    [[nodiscard]] auto changed_mesh()
    {
        auto input = tsunami::tests::fvm::two_triangle_unit_square_input();
        input.vertices[2].position.x = 1.25;
        return tsunami::fvm::make_finite_volume_mesh(std::move(input)).value();
    }

    [[nodiscard]] auto scalar_linear(Real a, Real b, Real c, tsunami::fvm::Point3 point) -> Real
    {
        return a + (b * point.x) + (c * point.y);
    }

    [[nodiscard]] auto vector_linear(
        Real a1,
        Real b1,
        Real c1,
        Real a2,
        Real b2,
        Real c2,
        tsunami::fvm::Point3 point) -> Vector3
    {
        return Vector3{scalar_linear(a1, b1, c1, point), scalar_linear(a2, b2, c2, point), 0.0};
    }

    [[nodiscard]] auto vector_matches(Vector3 actual, Vector3 expected) -> bool
    {
        return actual.x == Approx(expected.x).margin(tolerance) &&
               actual.y == Approx(expected.y).margin(tolerance) &&
               actual.z == Approx(expected.z).margin(tolerance);
    }

    [[nodiscard]] auto scalar_cell_field(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        FieldId id,
        std::string unit,
        const std::vector<Real> &values)
    {
        return tsunami::fvm::make_mesh_field<Real, FieldLocation::cell>(mesh, std::move(id), "scalar cell field", std::move(unit), values).value();
    }

    [[nodiscard]] auto vector_cell_field(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        FieldId id,
        std::string unit,
        const std::vector<Vector3> &values)
    {
        return tsunami::fvm::make_mesh_field<Vector3, FieldLocation::cell>(mesh, std::move(id), "vector cell field", std::move(unit), values).value();
    }

    [[nodiscard]] auto scalar_face_field(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        FieldId id,
        std::string unit,
        Real value)
    {
        return tsunami::fvm::make_filled_mesh_field<Real, FieldLocation::face>(mesh, std::move(id), "scalar face field", std::move(unit), value).value();
    }

    [[nodiscard]] auto vector_face_field(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        FieldId id,
        std::string unit,
        Vector3 value)
    {
        return tsunami::fvm::make_filled_mesh_field<Vector3, FieldLocation::face>(mesh, std::move(id), "vector face field", std::move(unit), value).value();
    }

    [[nodiscard]] auto gradient_field(const tsunami::fvm::FiniteVolumeMesh &mesh, Vector3 value = {})
    {
        return tsunami::fvm::make_filled_mesh_field<Vector3, FieldLocation::cell>(mesh, FieldId{"gradient"}, "gradient", "1", value).value();
    }

    [[nodiscard]] auto constant_scalar_specs(Real value, std::string unit = "m")
        -> std::vector<BoundarySpecification<Real>>
    {
        return {
            BoundarySpecification<Real>{BoundaryConditionId{"south"}, "south fixed", "south", unit, FixedValueSpecification<Real>{{value}}},
            BoundarySpecification<Real>{BoundaryConditionId{"east"}, "east zero", "east", unit, ZeroGradientSpecification{}},
            BoundarySpecification<Real>{BoundaryConditionId{"north"}, "north fixed", "north", unit, FixedValueSpecification<Real>{{value}}},
            BoundarySpecification<Real>{BoundaryConditionId{"west"}, "west zero", "west", unit, ZeroGradientSpecification{}},
        };
    }

    [[nodiscard]] auto constant_vector_specs(Vector3 value, std::string unit = "m/s")
        -> std::vector<BoundarySpecification<Vector3>>
    {
        return {
            BoundarySpecification<Vector3>{BoundaryConditionId{"south"}, "south fixed", "south", unit, FixedValueSpecification<Vector3>{{value}}},
            BoundarySpecification<Vector3>{BoundaryConditionId{"east"}, "east zero", "east", unit, ZeroGradientSpecification{}},
            BoundarySpecification<Vector3>{BoundaryConditionId{"north"}, "north fixed", "north", unit, FixedValueSpecification<Vector3>{{value}}},
            BoundarySpecification<Vector3>{BoundaryConditionId{"west"}, "west zero", "west", unit, ZeroGradientSpecification{}},
        };
    }

    [[nodiscard]] auto scalar_fixed_face_specs(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        Real a,
        Real b,
        Real c,
        std::string unit = "m") -> std::vector<BoundarySpecification<Real>>
    {
        std::vector<BoundarySpecification<Real>> specs;
        for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = BoundaryPatchId{index};
            const auto &patch = mesh.boundary_patch(patch_id);
            std::vector<Real> values;
            values.reserve(patch.faces.size());
            for (const auto face_id : patch.faces) {
                values.push_back(scalar_linear(a, b, c, mesh.face_geometry(face_id).centroid));
            }
            specs.push_back(BoundarySpecification<Real>{
                BoundaryConditionId{"fixed-" + patch.name},
                patch.name + " fixed",
                patch.name,
                unit,
                FixedValueSpecification<Real>{std::move(values)}});
        }
        return specs;
    }

    [[nodiscard]] auto vector_fixed_face_specs(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        Real a1,
        Real b1,
        Real c1,
        Real a2,
        Real b2,
        Real c2,
        std::string unit = "m/s") -> std::vector<BoundarySpecification<Vector3>>
    {
        std::vector<BoundarySpecification<Vector3>> specs;
        for (std::size_t index = 0; index < mesh.summary().boundary_patch_count; ++index) {
            const auto patch_id = BoundaryPatchId{index};
            const auto &patch = mesh.boundary_patch(patch_id);
            std::vector<Vector3> values;
            values.reserve(patch.faces.size());
            for (const auto face_id : patch.faces) {
                values.push_back(vector_linear(a1, b1, c1, a2, b2, c2, mesh.face_geometry(face_id).centroid));
            }
            specs.push_back(BoundarySpecification<Vector3>{
                BoundaryConditionId{"fixed-" + patch.name},
                patch.name + " fixed",
                patch.name,
                unit,
                FixedValueSpecification<Vector3>{std::move(values)}});
        }
        return specs;
    }
} // namespace

TEST_CASE("Linear interpolation stencil is deterministic and geometry weighted", "[fvm][numerics][stencil]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    auto stencil = tsunami::fvm::make_linear_interpolation_stencil(mesh).value();
    auto repeated = tsunami::fvm::make_linear_interpolation_stencil(mesh).value();

    static_assert(!std::is_copy_constructible_v<tsunami::fvm::LinearInterpolationStencil>);
    REQUIRE(stencil.size() == 1);
    REQUIRE_FALSE(stencil.empty());
    REQUIRE(stencil.is_bound_to(mesh));
    REQUIRE(stencil.entries()[0].face == FaceId{2});
    REQUIRE(stencil.entries()[0].owner == CellId{0});
    REQUIRE(stencil.entries()[0].neighbour == CellId{1});
    REQUIRE(stencil.entries()[0].owner_weight == Approx(0.5));
    REQUIRE(stencil.entries()[0].neighbour_weight == Approx(0.5));
    REQUIRE((stencil.entries()[0].owner_weight + stencil.entries()[0].neighbour_weight) == Approx(1.0).margin(tolerance));
    REQUIRE(std::isfinite(stencil.entries()[0].owner_weight));
    REQUIRE(std::isfinite(stencil.entries()[0].neighbour_weight));
    REQUIRE(stencil.entries()[0].owner_weight >= 0.0);
    REQUIRE(stencil.entries()[0].owner_weight <= 1.0);
    REQUIRE(repeated.entries()[0].face == stencil.entries()[0].face);
    REQUIRE(repeated.entries()[0].owner_weight == Approx(stencil.entries()[0].owner_weight));

    const auto same_mesh = tsunami::tests::fvm::reference_mesh();
    REQUIRE(stencil.is_bound_to(same_mesh));

    const auto skewed = skewed_mesh();
    auto skewed_stencil = tsunami::fvm::make_linear_interpolation_stencil(skewed).value();
    REQUIRE(skewed_stencil.size() == 1);
    REQUIRE(skewed_stencil.entries()[0].owner_weight >= 0.0);
    REQUIRE(skewed_stencil.entries()[0].owner_weight <= 1.0);
    REQUIRE(skewed_stencil.entries()[0].neighbour_weight >= 0.0);
    REQUIRE(skewed_stencil.entries()[0].neighbour_weight <= 1.0);
    REQUIRE((skewed_stencil.entries()[0].owner_weight + skewed_stencil.entries()[0].neighbour_weight) == Approx(1.0).margin(tolerance));
    REQUIRE(std::abs(skewed_stencil.entries()[0].owner_weight - 0.5) > 1.0e-3);
}

TEST_CASE("Cell-to-face interpolation preserves constant scalar and vector fields", "[fvm][numerics][interpolation]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    auto stencil = tsunami::fvm::make_linear_interpolation_stencil(mesh).value();

    for (const auto value : {0.0, 1.0, -3.25}) {
        auto source = scalar_cell_field(mesh, FieldId{"constant-scalar"}, "m", {value, value});
        auto boundaries = tsunami::fvm::make_boundary_condition_set(mesh, constant_scalar_specs(value)).value();
        auto destination = scalar_face_field(mesh, FieldId{"scalar-destination"}, "m", 99.0);
        auto workspace = tsunami::fvm::make_linear_interpolation_workspace<Real>(mesh, "m").value();

        REQUIRE(tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, boundaries, destination, workspace).has_value());
        for (const auto actual : destination.values()) {
            REQUIRE(actual == Approx(value).margin(tolerance));
        }
    }

    const auto vector_value = Vector3{2.0, -4.0, 0.0};
    auto vector_source = vector_cell_field(mesh, FieldId{"constant-vector"}, "m/s", {vector_value, vector_value});
    auto vector_boundaries = tsunami::fvm::make_boundary_condition_set(mesh, constant_vector_specs(vector_value)).value();
    auto vector_destination = vector_face_field(mesh, FieldId{"vector-destination"}, "m/s", Vector3{99.0, 99.0, 99.0});
    auto vector_workspace = tsunami::fvm::make_linear_interpolation_workspace<Vector3>(mesh, "m/s").value();

    REQUIRE(tsunami::fvm::interpolate_cell_to_face(mesh, stencil, vector_source, vector_boundaries, vector_destination, vector_workspace).has_value());
    for (const auto actual : vector_destination.values()) {
        REQUIRE(vector_matches(actual, vector_value));
    }
}

TEST_CASE("Cell-to-face interpolation handles linear internal faces and boundary scatter", "[fvm][numerics][interpolation]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    auto stencil = tsunami::fvm::make_linear_interpolation_stencil(mesh).value();
    const auto coefficients = std::vector<std::tuple<Real, Real, Real>>{{1.0, 1.0, 0.0}, {2.0, 0.0, 1.0}, {4.0, 2.0, -3.0}};

    for (const auto &[a, b, c] : coefficients) {
        auto source = scalar_cell_field(
            mesh,
            FieldId{"linear-scalar"},
            "m",
            {scalar_linear(a, b, c, mesh.cell_geometry(CellId{0}).centroid),
             scalar_linear(a, b, c, mesh.cell_geometry(CellId{1}).centroid)});
        auto boundaries = tsunami::fvm::make_boundary_condition_set(mesh, scalar_fixed_face_specs(mesh, a, b, c)).value();
        auto destination = scalar_face_field(mesh, FieldId{"linear-destination"}, "m", -100.0);
        auto workspace = tsunami::fvm::make_linear_interpolation_workspace<Real>(mesh, "m").value();

        REQUIRE(tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, boundaries, destination, workspace).has_value());
        REQUIRE(destination.at(2) == Approx(scalar_linear(a, b, c, mesh.face_geometry(FaceId{2}).centroid)).margin(tolerance));
        for (std::size_t face_index = 0; face_index < mesh.summary().face_count; ++face_index) {
            REQUIRE(destination.at(face_index) == Approx(scalar_linear(a, b, c, mesh.face_geometry(FaceId{face_index}).centroid)).margin(tolerance));
        }
    }

    auto vector_source = vector_cell_field(
        mesh,
        FieldId{"linear-vector"},
        "m/s",
        {vector_linear(1.0, 2.0, -1.0, -3.0, 0.5, 4.0, mesh.cell_geometry(CellId{0}).centroid),
         vector_linear(1.0, 2.0, -1.0, -3.0, 0.5, 4.0, mesh.cell_geometry(CellId{1}).centroid)});
    auto vector_boundaries = tsunami::fvm::make_boundary_condition_set(mesh, vector_fixed_face_specs(mesh, 1.0, 2.0, -1.0, -3.0, 0.5, 4.0)).value();
    auto vector_destination = vector_face_field(mesh, FieldId{"linear-vector-destination"}, "m/s", Vector3{-100.0, -100.0, -100.0});
    auto vector_workspace = tsunami::fvm::make_linear_interpolation_workspace<Vector3>(mesh, "m/s").value();

    REQUIRE(tsunami::fvm::interpolate_cell_to_face(mesh, stencil, vector_source, vector_boundaries, vector_destination, vector_workspace).has_value());
    REQUIRE(vector_matches(vector_destination.at(2), vector_linear(1.0, 2.0, -1.0, -3.0, 0.5, 4.0, mesh.face_geometry(FaceId{2}).centroid)));
}

TEST_CASE("Multi-face boundary interpolation retains patch-local ordering", "[fvm][numerics][interpolation]")
{
    const auto mesh = tsunami::tests::fvm::multi_face_patch_mesh();
    auto stencil = tsunami::fvm::make_linear_interpolation_stencil(mesh).value();
    auto source = scalar_cell_field(mesh, FieldId{"depth"}, "m", {3.0, 7.0});
    auto scalar_specs = tsunami::tests::fvm::multi_face_scalar_boundary_specs();
    scalar_specs[2].operation = ZeroGradientSpecification{};
    auto boundaries = tsunami::fvm::make_boundary_condition_set(mesh, std::move(scalar_specs)).value();
    auto destination = scalar_face_field(mesh, FieldId{"face-depth"}, "m", -1.0);
    auto workspace = tsunami::fvm::make_linear_interpolation_workspace<Real>(mesh, "m").value();

    REQUIRE(tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, boundaries, destination, workspace).has_value());
    REQUIRE(destination.at(0) == Approx(10.0));
    REQUIRE(destination.at(1) == Approx(20.0));
    REQUIRE(destination.at(2) == Approx(5.0));
    REQUIRE(destination.at(3) == Approx(7.0));
    REQUIRE(destination.at(4) == Approx(7.0));

    const auto cell0 = Vector3{3.0, -3.0, 0.0};
    const auto cell1 = Vector3{7.0, -7.0, 0.0};
    auto vector_source = vector_cell_field(mesh, FieldId{"velocity"}, "m/s", {cell0, cell1});
    auto vector_specs = tsunami::tests::fvm::multi_face_vector_boundary_specs();
    vector_specs[2].operation = ZeroGradientSpecification{};
    auto vector_boundaries = tsunami::fvm::make_boundary_condition_set(mesh, std::move(vector_specs)).value();
    auto vector_destination = vector_face_field(mesh, FieldId{"face-velocity"}, "m/s", Vector3{-1.0, -1.0, -1.0});
    auto vector_workspace = tsunami::fvm::make_linear_interpolation_workspace<Vector3>(mesh, "m/s").value();

    REQUIRE(tsunami::fvm::interpolate_cell_to_face(mesh, stencil, vector_source, vector_boundaries, vector_destination, vector_workspace).has_value());
    REQUIRE(vector_matches(vector_destination.at(0), Vector3{1.0, 0.0, 0.0}));
    REQUIRE(vector_matches(vector_destination.at(1), Vector3{0.0, 1.0, 0.0}));
    REQUIRE(vector_matches(vector_destination.at(2), Vector3{5.0, -5.0, 0.0}));
    REQUIRE(vector_matches(vector_destination.at(3), cell1));
    REQUIRE(vector_matches(vector_destination.at(4), cell1));
}

TEST_CASE("Interpolation rejects named and incompatible inputs transactionally", "[fvm][numerics][interpolation][validation]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    const auto other_mesh = changed_mesh();
    auto stencil = tsunami::fvm::make_linear_interpolation_stencil(mesh).value();
    auto other_stencil = tsunami::fvm::make_linear_interpolation_stencil(other_mesh).value();
    auto source = scalar_cell_field(mesh, FieldId{"source"}, "m", {1.0, 1.0});
    auto other_source = scalar_cell_field(other_mesh, FieldId{"other-source"}, "m", {1.0, 1.0});
    auto destination = scalar_face_field(mesh, FieldId{"destination"}, "m", -2.0);
    auto other_destination = scalar_face_field(other_mesh, FieldId{"other-destination"}, "m", -3.0);
    auto workspace = tsunami::fvm::make_linear_interpolation_workspace<Real>(mesh, "m").value();
    auto other_workspace = tsunami::fvm::make_linear_interpolation_workspace<Real>(other_mesh, "m").value();
    auto valid_boundaries = tsunami::fvm::make_boundary_condition_set(mesh, constant_scalar_specs(1.0)).value();
    auto named_specs = constant_scalar_specs(1.0);
    named_specs[2].operation = NamedBoundarySpecification{"radiation"};
    auto named_boundaries = tsunami::fvm::make_boundary_condition_set(mesh, std::move(named_specs)).value();
    auto other_boundaries = tsunami::fvm::make_boundary_condition_set(other_mesh, constant_scalar_specs(1.0)).value();

    auto result = tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, named_boundaries, destination, workspace);
    require_numerics_error(result, "fvm.numerics.interpolation.boundary_not_executable");
    REQUIRE(context_value(result.error(), "patch_id") == "2");
    REQUIRE(context_value(result.error(), "boundary_kind") == "named_reference");
    for (const auto actual : destination.values()) {
        REQUIRE(actual == Approx(-2.0));
    }

    REQUIRE(tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, valid_boundaries, destination, workspace).has_value());
    for (const auto actual : destination.values()) {
        REQUIRE(actual == Approx(1.0).margin(tolerance));
    }
    destination.fill(-2.0);

    require_numerics_error(
        tsunami::fvm::interpolate_cell_to_face(mesh, other_stencil, source, valid_boundaries, destination, workspace),
        "fvm.numerics.interpolation.mesh_incompatible");
    require_numerics_error(
        tsunami::fvm::interpolate_cell_to_face(mesh, stencil, other_source, valid_boundaries, destination, workspace),
        "fvm.numerics.interpolation.source_incompatible");
    require_numerics_error(
        tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, valid_boundaries, other_destination, workspace),
        "fvm.numerics.interpolation.destination_incompatible");
    require_numerics_error(
        tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, valid_boundaries, destination, other_workspace),
        "fvm.numerics.interpolation.workspace_incompatible");
    require_numerics_error(
        tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, other_boundaries, destination, workspace),
        "fvm.numerics.interpolation.boundary_set_incompatible");

    auto wrong_unit_destination = scalar_face_field(mesh, FieldId{"wrong-unit-destination"}, "kg", -2.0);
    require_numerics_error(
        tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, valid_boundaries, wrong_unit_destination, workspace),
        "fvm.numerics.interpolation.unit_incompatible");
    auto wrong_unit_workspace = tsunami::fvm::make_linear_interpolation_workspace<Real>(mesh, "kg").value();
    require_numerics_error(
        tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, valid_boundaries, destination, wrong_unit_workspace),
        "fvm.numerics.interpolation.unit_incompatible");
    auto wrong_unit_boundaries = tsunami::fvm::make_boundary_condition_set(mesh, constant_scalar_specs(1.0, "kg")).value();
    require_numerics_error(
        tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, wrong_unit_boundaries, destination, workspace),
        "fvm.numerics.interpolation.unit_incompatible");

    for (const auto actual : destination.values()) {
        REQUIRE(actual == Approx(-2.0));
    }
}

TEST_CASE("Interpolation and gradient workspaces are reusable with stable storage", "[fvm][numerics][workspace]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    auto stencil = tsunami::fvm::make_linear_interpolation_stencil(mesh).value();
    auto source = scalar_cell_field(mesh, FieldId{"source"}, "m", {2.0, 2.0});
    auto boundaries = tsunami::fvm::make_boundary_condition_set(mesh, constant_scalar_specs(2.0)).value();
    auto destination = scalar_face_field(mesh, FieldId{"destination"}, "m", 0.0);
    auto workspace = tsunami::fvm::make_linear_interpolation_workspace<Real>(mesh, "m").value();
    auto gradient_destination = gradient_field(mesh);
    auto gradient_workspace = tsunami::fvm::make_green_gauss_gradient_workspace(mesh).value();

    const auto *destination_data = destination.values().data();
    const auto *staging_data = workspace.staging_field().values().data();
    std::vector<const Real *> patch_data;
    for (const auto &patch : workspace.patch_workspaces()) {
        patch_data.push_back(patch.values().data());
    }
    const auto *gradient_destination_data = gradient_destination.values().data();
    const auto *gradient_staging_data = gradient_workspace.staging_values().data();

    for (auto repeat = 0; repeat < 3; ++repeat) {
        REQUIRE(tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, boundaries, destination, workspace).has_value());
        REQUIRE(tsunami::fvm::green_gauss_gradient(mesh, destination, gradient_destination, gradient_workspace).has_value());
        REQUIRE(destination.values().data() == destination_data);
        REQUIRE(workspace.staging_field().values().data() == staging_data);
        for (std::size_t index = 0; index < workspace.patch_workspaces().size(); ++index) {
            REQUIRE(workspace.patch_workspaces()[index].values().data() == patch_data[index]);
        }
        REQUIRE(gradient_destination.values().data() == gradient_destination_data);
        REQUIRE(gradient_workspace.staging_values().data() == gradient_staging_data);
    }
}

TEST_CASE("Green-Gauss gradient preserves constants and rejects invalid inputs", "[fvm][numerics][gradient]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    auto face_values = scalar_face_field(mesh, FieldId{"face-values"}, "m", 4.25);
    auto destination = gradient_field(mesh, Vector3{99.0, 99.0, 99.0});
    auto workspace = tsunami::fvm::make_green_gauss_gradient_workspace(mesh).value();

    REQUIRE(tsunami::fvm::green_gauss_gradient(mesh, face_values, destination, workspace).has_value());
    for (const auto gradient : destination.values()) {
        REQUIRE(vector_matches(gradient, Vector3{0.0, 0.0, 0.0}));
    }

    const auto other_mesh = changed_mesh();
    auto other_face_values = scalar_face_field(other_mesh, FieldId{"other-face-values"}, "m", 1.0);
    auto other_destination = gradient_field(other_mesh);
    auto other_workspace = tsunami::fvm::make_green_gauss_gradient_workspace(other_mesh).value();
    destination.fill(Vector3{99.0, 99.0, 99.0});

    require_numerics_error(
        tsunami::fvm::green_gauss_gradient(mesh, other_face_values, destination, workspace),
        "fvm.numerics.gradient.source_incompatible");
    require_numerics_error(
        tsunami::fvm::green_gauss_gradient(mesh, face_values, other_destination, workspace),
        "fvm.numerics.gradient.destination_incompatible");
    require_numerics_error(
        tsunami::fvm::green_gauss_gradient(mesh, face_values, destination, other_workspace),
        "fvm.numerics.gradient.workspace_incompatible");
    face_values.at(0) = std::numeric_limits<Real>::quiet_NaN();
    require_numerics_error(
        tsunami::fvm::green_gauss_gradient(mesh, face_values, destination, workspace),
        "fvm.numerics.gradient.source_nonfinite");
    for (const auto gradient : destination.values()) {
        REQUIRE(vector_matches(gradient, Vector3{99.0, 99.0, 99.0}));
    }
}

TEST_CASE("Green-Gauss gradient recovers manufactured linear fields on the reference mesh", "[fvm][numerics][gradient]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    auto stencil = tsunami::fvm::make_linear_interpolation_stencil(mesh).value();
    const auto coefficients = std::vector<std::tuple<Real, Real, Real>>{{1.0, 1.0, 0.0}, {2.0, 0.0, 1.0}, {4.0, 2.0, -3.0}};

    for (const auto &[a, b, c] : coefficients) {
        auto source = scalar_cell_field(
            mesh,
            FieldId{"linear-source"},
            "m",
            {scalar_linear(a, b, c, mesh.cell_geometry(CellId{0}).centroid),
             scalar_linear(a, b, c, mesh.cell_geometry(CellId{1}).centroid)});
        auto boundaries = tsunami::fvm::make_boundary_condition_set(mesh, scalar_fixed_face_specs(mesh, a, b, c)).value();
        auto face_values = scalar_face_field(mesh, FieldId{"linear-face-values"}, "m", -100.0);
        auto interpolation_workspace = tsunami::fvm::make_linear_interpolation_workspace<Real>(mesh, "m").value();
        auto gradient_destination = gradient_field(mesh, Vector3{-100.0, -100.0, -100.0});
        auto gradient_workspace = tsunami::fvm::make_green_gauss_gradient_workspace(mesh).value();

        REQUIRE(tsunami::fvm::interpolate_cell_to_face(mesh, stencil, source, boundaries, face_values, interpolation_workspace).has_value());
        REQUIRE(tsunami::fvm::green_gauss_gradient(mesh, face_values, gradient_destination, gradient_workspace).has_value());
        for (const auto gradient : gradient_destination.values()) {
            REQUIRE(vector_matches(gradient, Vector3{b, c, 0.0}));
        }
    }
}
