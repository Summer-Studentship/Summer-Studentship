#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <tsunami/fvm/BoundaryConditionSet.hpp>

#include "reference_boundaries.hpp"

namespace
{
    using Catch::Approx;
    using tsunami::core::Real;
    using tsunami::fvm::BoundaryConditionId;
    using tsunami::fvm::BoundaryConditionKind;
    using tsunami::fvm::BoundaryPatchId;
    using tsunami::fvm::FieldId;
    using tsunami::fvm::FieldLocation;
    using tsunami::fvm::FieldValueKind;
    using tsunami::fvm::IBoundaryConditionView;
    using tsunami::fvm::ScalarBoundaryCondition;
    using tsunami::fvm::ScalarBoundaryConditionSet;
    using tsunami::fvm::Vector3;

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        const auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }

    template <class Result>
    auto require_boundary_error(Result &&result, std::string_view code) -> void
    {
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code() == code);
        REQUIRE(result.error().category() == tsunami::core::DiagnosticCategory::numerical);
        REQUIRE(result.error().severity() == tsunami::core::Severity::error);
        REQUIRE(context_value(result.error(), "rule_id") == "SWE-FVM-BC-WP1");
        REQUIRE(context_value(result.error(), "state_changed") == "false");
    }

    [[nodiscard]] auto vector_matches(const Vector3 &actual, const Vector3 &expected) -> bool
    {
        return actual.x == Approx(expected.x) && actual.y == Approx(expected.y) && actual.z == Approx(expected.z);
    }

    template <class Set>
    concept HasResize = requires(Set &set) { set.resize(1); };

    template <class Set>
    concept HasPushBack = requires(Set &set, ScalarBoundaryCondition condition) {
        set.push_back(std::move(condition));
    };

    [[nodiscard]] auto fvm_header_root() -> std::filesystem::path
    {
        auto probe = std::filesystem::current_path();
        for (auto depth = 0; depth < 6; ++depth) {
            const auto candidate = probe / "src/fvm/include/tsunami/fvm";
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
            probe = probe.parent_path();
        }
        return {};
    }
} // namespace

TEST_CASE("FVM boundary identity and descriptors are stable", "[fvm][boundary]")
{
    static_assert(std::is_base_of_v<IBoundaryConditionView, ScalarBoundaryCondition>);
    static_assert(!std::is_copy_constructible_v<ScalarBoundaryCondition>);
    static_assert(!std::is_copy_assignable_v<ScalarBoundaryCondition>);
    static_assert(std::is_move_constructible_v<ScalarBoundaryCondition>);
    static_assert(!std::is_copy_constructible_v<ScalarBoundaryConditionSet>);
    static_assert(std::is_move_constructible_v<ScalarBoundaryConditionSet>);
    static_assert(!HasResize<ScalarBoundaryConditionSet>);
    static_assert(!HasPushBack<ScalarBoundaryConditionSet>);

    REQUIRE(BoundaryConditionId{"a"} < BoundaryConditionId{"b"});
    REQUIRE(tsunami::fvm::to_string(BoundaryConditionKind::fixed_value) == "fixed_value");
    REQUIRE(tsunami::fvm::to_string(BoundaryConditionKind::zero_gradient) == "zero_gradient");
    REQUIRE(tsunami::fvm::to_string(BoundaryConditionKind::named_reference) == "named_reference");
    REQUIRE(tsunami::fvm::boundary_condition_kind_from_string("zero_gradient") == BoundaryConditionKind::zero_gradient);
    REQUIRE_FALSE(tsunami::fvm::boundary_condition_kind_from_string("wall").has_value());

    const auto mesh = tsunami::tests::fvm::reference_mesh();
    auto scalar_set = tsunami::fvm::make_boundary_condition_set(mesh, tsunami::tests::fvm::scalar_boundary_specs()).value();
    auto vector_set = tsunami::fvm::make_boundary_condition_set(mesh, tsunami::tests::fvm::vector_boundary_specs()).value();

    const auto south = scalar_set.condition(BoundaryPatchId{0});
    REQUIRE(south != nullptr);
    const auto south_descriptor = south->descriptor();
    REQUIRE(south_descriptor.id.value == "bc-south-depth");
    REQUIRE(south_descriptor.name == "south fixed depth");
    REQUIRE(south_descriptor.mesh_id == mesh.summary().id);
    REQUIRE(south_descriptor.patch_id == BoundaryPatchId{0});
    REQUIRE(south_descriptor.patch_name == "south");
    REQUIRE(south_descriptor.kind == BoundaryConditionKind::fixed_value);
    REQUIRE(south_descriptor.value_kind == FieldValueKind::scalar);
    REQUIRE(south_descriptor.component_count == 1);
    REQUIRE(south_descriptor.entity_count == 1);
    REQUIRE(south_descriptor.unit_id == "m");
    REQUIRE(south_descriptor.executable);

    REQUIRE(vector_set.condition(BoundaryPatchId{0})->descriptor().value_kind == FieldValueKind::vector);
    REQUIRE(vector_set.condition(BoundaryPatchId{0})->descriptor().component_count == 3);
    REQUIRE_FALSE(scalar_set.condition(BoundaryPatchId{2})->is_executable());
}

TEST_CASE("Boundary condition sets resolve exact patch names in deterministic patch order", "[fvm][boundary]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    auto specs = tsunami::tests::fvm::scalar_boundary_specs();
    std::ranges::reverse(specs);

    auto result = tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs));
    REQUIRE(result.has_value());
    auto set = std::move(result).value();
    REQUIRE(set.size() == 4);
    REQUIRE_FALSE(set.empty());
    REQUIRE(set.is_bound_to(mesh));
    REQUIRE(set.is_complete_for(mesh));
    REQUIRE(set.conditions()[0].patch_id() == BoundaryPatchId{0});
    REQUIRE(set.conditions()[1].patch_id() == BoundaryPatchId{1});
    REQUIRE(set.conditions()[2].patch_id() == BoundaryPatchId{2});
    REQUIRE(set.conditions()[3].patch_id() == BoundaryPatchId{3});
    REQUIRE(set.condition(BoundaryPatchId{99}) == nullptr);

    auto clone = set.clone();
    REQUIRE(clone.size() == set.size());
    REQUIRE(clone.conditions()[0].descriptor().id == set.conditions()[0].descriptor().id);
}

TEST_CASE("Boundary condition factory rejects invalid identity and patch coverage", "[fvm][boundary][validation]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();

    auto specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs[0].id.value.clear();
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.id_required");

    specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs[1].id = specs[0].id;
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.id_duplicate");

    specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs[0].name.clear();
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.name_required");

    specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs[0].patch_tag.clear();
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.patch_tag_required");

    specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs[0].unit_id.clear();
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.unit_required");

    specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs[0].patch_tag = "sou";
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.patch_unknown");

    specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs[0].patch_tag = "South";
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.patch_unknown");

    specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs[1].patch_tag = "south";
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.patch_duplicate");

    specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs.pop_back();
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.patch_missing");

    specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs[0].operation = tsunami::fvm::FixedValueSpecification<Real>{{1.0, 2.0}};
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.patch_entity_count_mismatch");

    specs = tsunami::tests::fvm::scalar_boundary_specs();
    specs[2].operation = tsunami::fvm::NamedBoundarySpecification{""};
    require_boundary_error(tsunami::fvm::make_boundary_condition_set(mesh, std::move(specs)), "fvm.boundary.named_type_required");
}

TEST_CASE("Fixed-value boundaries copy prescribed scalar and vector patch values transactionally", "[fvm][boundary]")
{
    const auto mesh = tsunami::tests::fvm::multi_face_patch_mesh();
    auto scalar_set = tsunami::fvm::make_boundary_condition_set(mesh, tsunami::tests::fvm::multi_face_scalar_boundary_specs()).value();
    auto internal = tsunami::tests::fvm::sample_cell_scalar_field(mesh).value();
    auto target = tsunami::fvm::make_boundary_patch_field<Real>(
        mesh, BoundaryPatchId{0}, FieldId{"dest-depth"}, "destination depth", "m", {-1.0, -2.0}).value();

    REQUIRE(scalar_set.condition(BoundaryPatchId{0})->apply(mesh, internal, target).has_value());
    REQUIRE(target.at(0) == Approx(10.0));
    REQUIRE(target.at(1) == Approx(20.0));
    REQUIRE(internal.at(0) == Approx(1.0));

    auto wrong_unit = tsunami::fvm::make_boundary_patch_field<Real>(
        mesh, BoundaryPatchId{0}, FieldId{"wrong-unit"}, "wrong unit", "kg", {-1.0, -2.0}).value();
    require_boundary_error(
        scalar_set.condition(BoundaryPatchId{0})->apply(mesh, internal, wrong_unit),
        "fvm.boundary.unit_incompatible");
    REQUIRE(wrong_unit.at(0) == Approx(-1.0));
    REQUIRE(wrong_unit.at(1) == Approx(-2.0));

    auto wrong_patch = tsunami::fvm::make_boundary_patch_field<Real>(
        mesh, BoundaryPatchId{1}, FieldId{"wrong-patch"}, "wrong patch", "m", {-9.0}).value();
    require_boundary_error(
        scalar_set.condition(BoundaryPatchId{0})->apply(mesh, internal, wrong_patch),
        "fvm.boundary.patch_incompatible");
    REQUIRE(wrong_patch.at(0) == Approx(-9.0));

    auto vector_set = tsunami::fvm::make_boundary_condition_set(mesh, tsunami::tests::fvm::multi_face_vector_boundary_specs()).value();
    auto vector_internal = tsunami::tests::fvm::sample_cell_vector_field(mesh).value();
    auto vector_target = tsunami::fvm::make_boundary_patch_field<Vector3>(
        mesh,
        BoundaryPatchId{0},
        FieldId{"dest-velocity"},
        "destination velocity",
        "m/s",
        {Vector3{-1.0, -1.0, -1.0}, Vector3{-2.0, -2.0, -2.0}}).value();

    REQUIRE(vector_set.condition(BoundaryPatchId{0})->apply(mesh, vector_internal, vector_target).has_value());
    REQUIRE(vector_matches(vector_target.at(0), Vector3{1.0, 0.0, 0.0}));
    REQUIRE(vector_matches(vector_target.at(1), Vector3{0.0, 1.0, 0.0}));
}

TEST_CASE("Zero-gradient boundaries copy owner-cell values in patch-local face order", "[fvm][boundary]")
{
    const auto mesh = tsunami::tests::fvm::multi_face_patch_mesh();
    auto scalar_set = tsunami::fvm::make_boundary_condition_set(mesh, tsunami::tests::fvm::multi_face_scalar_boundary_specs()).value();
    auto internal = tsunami::fvm::make_mesh_field<Real, FieldLocation::cell>(
        mesh, FieldId{"depth"}, "depth", "m", {3.5, 9.25}).value();
    auto target = tsunami::fvm::make_boundary_patch_field<Real>(
        mesh, BoundaryPatchId{1}, FieldId{"north-depth"}, "north depth", "m", {-1.0}).value();

    REQUIRE(scalar_set.condition(BoundaryPatchId{1})->apply(mesh, internal, target).has_value());
    REQUIRE(target.at(0) == Approx(9.25));

    auto vector_set = tsunami::fvm::make_boundary_condition_set(mesh, tsunami::tests::fvm::multi_face_vector_boundary_specs()).value();
    auto vector_internal = tsunami::fvm::make_mesh_field<Vector3, FieldLocation::cell>(
        mesh,
        FieldId{"velocity"},
        "velocity",
        "m/s",
        {Vector3{2.0, 3.0, 4.0}, Vector3{5.0, 6.0, 7.0}}).value();
    auto vector_target = tsunami::fvm::make_boundary_patch_field<Vector3>(
        mesh, BoundaryPatchId{1}, FieldId{"north-velocity"}, "north velocity", "m/s", {Vector3{-1.0, -1.0, -1.0}}).value();

    REQUIRE(vector_set.condition(BoundaryPatchId{1})->apply(mesh, vector_internal, vector_target).has_value());
    REQUIRE(vector_matches(vector_target.at(0), Vector3{5.0, 6.0, 7.0}));

    auto wrong_unit = tsunami::fvm::make_mesh_field<Real, FieldLocation::cell>(
        mesh, FieldId{"wrong-unit"}, "wrong unit", "kg", {1.0, 2.0}).value();
    target.at(0) = -4.0;
    require_boundary_error(
        scalar_set.condition(BoundaryPatchId{1})->apply(mesh, wrong_unit, target),
        "fvm.boundary.unit_incompatible");
    REQUIRE(target.at(0) == Approx(-4.0));
}

TEST_CASE("Named boundary references are retained but never executable", "[fvm][boundary]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    auto set = tsunami::fvm::make_boundary_condition_set(mesh, tsunami::tests::fvm::scalar_boundary_specs()).value();
    auto internal = tsunami::tests::fvm::sample_cell_scalar_field(mesh).value();
    auto target = tsunami::fvm::make_boundary_patch_field<Real>(
        mesh, BoundaryPatchId{2}, FieldId{"north-depth"}, "north depth", "m", {-8.0}).value();

    const auto named = set.condition(BoundaryPatchId{2});
    REQUIRE(named != nullptr);
    REQUIRE(named->kind() == BoundaryConditionKind::named_reference);
    REQUIRE_FALSE(named->is_executable());

    const auto result = named->apply(mesh, internal, target);
    require_boundary_error(result, "fvm.boundary.named_condition_not_executable");
    REQUIRE(context_value(result.error(), "requested_type") == "radiation");
    REQUIRE(target.at(0) == Approx(-8.0));
}

TEST_CASE("Boundary conditions reject incompatible meshes without mutating targets", "[fvm][boundary][validation]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    auto changed_input = tsunami::tests::fvm::two_triangle_unit_square_input();
    changed_input.vertices[2].position.x = 1.25;
    const auto changed_mesh = tsunami::fvm::make_finite_volume_mesh(std::move(changed_input)).value();
    auto set = tsunami::fvm::make_boundary_condition_set(mesh, tsunami::tests::fvm::scalar_boundary_specs()).value();
    auto internal = tsunami::tests::fvm::sample_cell_scalar_field(mesh).value();
    auto target = tsunami::fvm::make_boundary_patch_field<Real>(
        mesh, BoundaryPatchId{0}, FieldId{"south-depth"}, "south depth", "m", {-3.0}).value();

    require_boundary_error(
        set.condition(BoundaryPatchId{0})->apply(changed_mesh, internal, target),
        "fvm.boundary.mesh_incompatible");
    REQUIRE(target.at(0) == Approx(-3.0));

    auto incompatible_internal = tsunami::tests::fvm::sample_cell_scalar_field(changed_mesh).value();
    require_boundary_error(
        set.condition(BoundaryPatchId{0})->apply(mesh, incompatible_internal, target),
        "fvm.boundary.internal_field_incompatible");
    REQUIRE(target.at(0) == Approx(-3.0));

    auto changed_target = tsunami::fvm::make_boundary_patch_field<Real>(
        changed_mesh, BoundaryPatchId{0}, FieldId{"changed-target"}, "changed target", "m", {-5.0}).value();
    require_boundary_error(
        set.condition(BoundaryPatchId{0})->apply(mesh, internal, changed_target),
        "fvm.boundary.mesh_incompatible");
    REQUIRE(changed_target.at(0) == Approx(-5.0));
}

TEST_CASE("Boundary public headers stay inside the FVM/Core architecture", "[fvm][boundary][architecture]")
{
    const auto header_root = fvm_header_root();
    REQUIRE_FALSE(header_root.empty());
    const std::vector<std::string> prohibited = {
        "Qt",
        "QObject",
        "QML",
        "tsunami/r2d",
        "tsunami/l3d",
        "tsunami/coupling",
        "solver",
        "equation",
        "register_boundary",
    };

    for (const auto &entry : std::filesystem::directory_iterator(header_root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".hpp") {
            continue;
        }
        auto stream = std::ifstream(entry.path());
        const auto text = std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
        for (const auto &token : prohibited) {
            REQUIRE(text.find(token) == std::string::npos);
        }
    }
}
