#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <tsunami/fvm/BoundaryPatchField.hpp>
#include <tsunami/fvm/MeshBinding.hpp>
#include <tsunami/fvm/MeshField.hpp>

#include "reference_fields.hpp"

namespace
{
    using Catch::Approx;
    using tsunami::fvm::BoundaryPatchId;
    using tsunami::fvm::CellScalarField;
    using tsunami::fvm::CellVectorField;
    using tsunami::fvm::FaceScalarField;
    using tsunami::fvm::FaceVectorField;
    using tsunami::fvm::FieldId;
    using tsunami::fvm::FieldLocation;
    using tsunami::fvm::FieldValueKind;
    using tsunami::fvm::IFieldView;
    using tsunami::fvm::PatchScalarField;
    using tsunami::fvm::PatchVectorField;
    using tsunami::fvm::Vector3;

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        const auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }

    template <class Result>
    auto require_error(Result &&result, std::string_view code) -> void
    {
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code() == code);
        REQUIRE(result.error().category() == tsunami::core::DiagnosticCategory::numerical);
        REQUIRE(result.error().severity() == tsunami::core::Severity::error);
        REQUIRE(context_value(result.error(), "rule_id") == "SWE-FVM-FLD-WP1");
        REQUIRE(context_value(result.error(), "state_changed") == "false");
    }

    [[nodiscard]] auto moved_cell_scalar()
    {
        auto mesh = tsunami::tests::fvm::reference_mesh();
        return tsunami::tests::fvm::sample_cell_scalar_field(mesh).value();
    }

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

    template <class Field>
    concept HasResize = requires(Field &field) { field.resize(1); };

    template <class Field>
    concept HasPushBack = requires(Field &field) { field.push_back(1.0); };

    template <class Field>
    concept HasClear = requires(Field &field) { field.clear(); };

    template <class View>
    concept HasValues = requires(View &view) { view.values(); };
} // namespace

TEST_CASE("FVM field value traits and aliases are stable", "[fvm][field]")
{
    REQUIRE(tsunami::fvm::FieldValueTraits<tsunami::core::Real>::kind == FieldValueKind::scalar);
    REQUIRE(tsunami::fvm::FieldValueTraits<tsunami::core::Real>::component_count == 1);
    REQUIRE(tsunami::fvm::FieldValueTraits<Vector3>::kind == FieldValueKind::vector);
    REQUIRE(tsunami::fvm::FieldValueTraits<Vector3>::component_count == 3);

    static_assert(tsunami::fvm::SupportedFieldValue<tsunami::core::Real>);
    static_assert(tsunami::fvm::SupportedFieldValue<Vector3>);
    static_assert(!tsunami::fvm::SupportedFieldValue<int>);
    static_assert(!std::is_same_v<CellScalarField, FaceScalarField>);
    static_assert(!std::is_same_v<CellVectorField, FaceVectorField>);
    static_assert(!std::is_same_v<PatchScalarField, CellScalarField>);
    static_assert(std::is_base_of_v<IFieldView, CellScalarField>);
    static_assert(std::is_base_of_v<IFieldView, FaceScalarField>);
    static_assert(std::is_base_of_v<IFieldView, CellVectorField>);
    static_assert(std::is_base_of_v<IFieldView, FaceVectorField>);
    static_assert(std::is_base_of_v<IFieldView, PatchScalarField>);
    static_assert(std::is_base_of_v<IFieldView, PatchVectorField>);
}

TEST_CASE("FVM fields construct descriptors for all supported categories", "[fvm][field]")
{
    auto mesh = tsunami::tests::fvm::multi_face_patch_mesh();
    auto cell_scalar = tsunami::tests::fvm::sample_cell_scalar_field(mesh).value();
    auto face_scalar = tsunami::tests::fvm::sample_face_scalar_field(mesh).value();
    auto cell_vector = tsunami::tests::fvm::sample_cell_vector_field(mesh).value();
    auto face_vector = tsunami::tests::fvm::sample_face_vector_field(mesh).value();
    auto patch_scalar = tsunami::tests::fvm::sample_patch_scalar_field(mesh).value();
    auto patch_vector = tsunami::tests::fvm::sample_patch_vector_field(mesh).value();

    REQUIRE(cell_scalar.descriptor().id.value == "cell-scalar");
    REQUIRE(cell_scalar.descriptor().name == "cell scalar");
    REQUIRE(cell_scalar.descriptor().mesh_id == mesh.summary().id);
    REQUIRE(cell_scalar.descriptor().location == FieldLocation::cell);
    REQUIRE(cell_scalar.descriptor().value_kind == FieldValueKind::scalar);
    REQUIRE(cell_scalar.descriptor().component_count == 1);
    REQUIRE(cell_scalar.descriptor().entity_count == 2);
    REQUIRE(cell_scalar.descriptor().unit_id == "m");
    REQUIRE_FALSE(cell_scalar.descriptor().boundary_patch.has_value());

    REQUIRE(face_scalar.descriptor().location == FieldLocation::face);
    REQUIRE(face_scalar.descriptor().entity_count == 5);
    REQUIRE(cell_vector.descriptor().value_kind == FieldValueKind::vector);
    REQUIRE(cell_vector.descriptor().component_count == 3);
    REQUIRE(face_vector.descriptor().location == FieldLocation::face);

    REQUIRE(patch_scalar.descriptor().location == FieldLocation::boundary_patch);
    REQUIRE(patch_scalar.descriptor().boundary_patch == BoundaryPatchId{0});
    REQUIRE(patch_scalar.descriptor().entity_count == 2);
    REQUIRE(patch_vector.descriptor().value_kind == FieldValueKind::vector);
    REQUIRE(patch_vector.descriptor().component_count == 3);

    REQUIRE(tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, FieldLocation::cell>(mesh, {"filled-cell"}, "filled cell", "m", 3.0).value().size() == 2);
    REQUIRE(tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, FieldLocation::face>(mesh, {"filled-face"}, "filled face", "m", 4.0).value().size() == 5);
    REQUIRE(tsunami::fvm::make_filled_mesh_field<Vector3, FieldLocation::cell>(mesh, {"filled-cell-vector"}, "filled cell vector", "m/s", Vector3{1.0, 2.0, 3.0}).value().size() == 2);
    REQUIRE(tsunami::fvm::make_filled_mesh_field<Vector3, FieldLocation::face>(mesh, {"filled-face-vector"}, "filled face vector", "m/s", Vector3{1.0, 2.0, 3.0}).value().size() == 5);
    REQUIRE(tsunami::fvm::make_filled_boundary_patch_field<tsunami::core::Real>(mesh, BoundaryPatchId{0}, {"filled-patch"}, "filled patch", "m", 5.0).value().size() == 2);
    REQUIRE(tsunami::fvm::make_filled_boundary_patch_field<Vector3>(mesh, BoundaryPatchId{0}, {"filled-patch-vector"}, "filled patch vector", "m", Vector3{1.0, 0.0, 0.0}).value().size() == 2);
}

TEST_CASE("FVM field storage is contiguous fixed-size mutable storage", "[fvm][field]")
{
    auto field = moved_cell_scalar();
    static_assert(!HasResize<CellScalarField>);
    static_assert(!HasPushBack<CellScalarField>);
    static_assert(!HasClear<CellScalarField>);

    REQUIRE(field.size() == 2);
    REQUIRE_FALSE(field.empty());
    REQUIRE(field.values().data() == &field.at(0));
    field.values()[0] = 9.0;
    REQUIRE(field.at(0) == Approx(9.0));
    const auto &const_field = field;
    REQUIRE(const_field.values()[1] == Approx(2.0));
    REQUIRE_THROWS_AS(field.at(99), std::out_of_range);

    field.fill(-1.0);
    REQUIRE(field.at(0) == Approx(-1.0));
    REQUIRE(field.at(1) == Approx(-1.0));

    auto mesh = tsunami::tests::fvm::reference_mesh();
    auto vector_field = tsunami::fvm::make_filled_mesh_field<Vector3, FieldLocation::cell>(
        mesh, {"vector"}, "vector", "m/s", Vector3{1.0, 2.0, std::numeric_limits<double>::infinity()}).value();
    REQUIRE(vector_field.at(0).x == Approx(1.0));
    REQUIRE(vector_field.at(0).y == Approx(2.0));
    REQUIRE(std::isinf(vector_field.at(0).z));
}

TEST_CASE("FVM fields are move-only with explicit deep clone", "[fvm][field]")
{
    static_assert(!std::is_copy_constructible_v<CellScalarField>);
    static_assert(!std::is_copy_assignable_v<CellScalarField>);
    static_assert(std::is_move_constructible_v<CellScalarField>);
    static_assert(std::is_move_assignable_v<CellScalarField>);

    auto field = moved_cell_scalar();
    auto clone = field.clone();
    clone.at(0) = 100.0;
    REQUIRE(field.at(0) == Approx(1.0));
    REQUIRE(clone.at(0) == Approx(100.0));

    auto moved = std::move(clone);
    REQUIRE(moved.descriptor().id.value == "cell-scalar");
    REQUIRE(moved.binding() == field.binding());
    REQUIRE(moved.at(1) == Approx(2.0));
}

TEST_CASE("MeshBinding rejects same-count incompatible meshes", "[fvm][field][binding]")
{
    const auto mesh = tsunami::tests::fvm::reference_mesh();
    const auto same_mesh = tsunami::tests::fvm::reference_mesh();
    const auto binding = tsunami::fvm::make_mesh_binding(mesh);
    REQUIRE(binding == tsunami::fvm::make_mesh_binding(same_mesh));
    REQUIRE(binding.compatibility_signature == tsunami::fvm::make_mesh_binding(mesh).compatibility_signature);

    auto field = tsunami::tests::fvm::sample_cell_scalar_field(mesh).value();
    REQUIRE(field.is_bound_to(mesh));
    REQUIRE(field.is_bound_to(same_mesh));

    auto changed_coordinates = tsunami::tests::fvm::two_triangle_unit_square_input();
    changed_coordinates.vertices[2].position.x = 1.25;
    auto changed_coordinates_mesh = tsunami::fvm::make_finite_volume_mesh(std::move(changed_coordinates)).value();
    REQUIRE_FALSE(field.is_bound_to(changed_coordinates_mesh));
    require_error(field.require_compatible_mesh(changed_coordinates_mesh), "fvm.field.mesh_incompatible");

    auto changed_connectivity = tsunami::tests::fvm::two_triangle_unit_square_input();
    std::ranges::reverse(changed_connectivity.faces[0].vertices);
    auto changed_connectivity_mesh = tsunami::fvm::make_finite_volume_mesh(std::move(changed_connectivity)).value();
    REQUIRE_FALSE(field.is_bound_to(changed_connectivity_mesh));

    auto changed_owner = tsunami::tests::fvm::two_triangle_unit_square_input();
    changed_owner.faces[2].owner = tsunami::fvm::CellId{1};
    changed_owner.faces[2].neighbour = tsunami::fvm::CellId{0};
    auto changed_owner_mesh = tsunami::fvm::make_finite_volume_mesh(std::move(changed_owner)).value();
    REQUIRE_FALSE(field.is_bound_to(changed_owner_mesh));

    auto changed_patch = tsunami::tests::fvm::two_triangle_unit_square_input();
    changed_patch.boundary_patches[0].name = "renamed-south";
    auto changed_patch_mesh = tsunami::fvm::make_finite_volume_mesh(std::move(changed_patch)).value();
    REQUIRE_FALSE(field.is_bound_to(changed_patch_mesh));
}

TEST_CASE("FVM field factories reject structural metadata and count mismatches", "[fvm][field][validation]")
{
    auto mesh = tsunami::tests::fvm::multi_face_patch_mesh();
    require_error(
        tsunami::fvm::make_mesh_field<tsunami::core::Real, FieldLocation::cell>(mesh, {""}, "depth", "m", {1.0, 2.0}),
        "fvm.field.id_required");
    require_error(
        tsunami::fvm::make_mesh_field<tsunami::core::Real, FieldLocation::cell>(mesh, {"depth"}, "", "m", {1.0, 2.0}),
        "fvm.field.name_required");
    require_error(
        tsunami::fvm::make_mesh_field<tsunami::core::Real, FieldLocation::cell>(mesh, {"depth"}, "depth", "", {1.0, 2.0}),
        "fvm.field.unit_required");
    require_error(
        tsunami::fvm::make_mesh_field<tsunami::core::Real, FieldLocation::cell>(mesh, {"depth"}, "depth", "m", {1.0}),
        "fvm.field.entity_count_mismatch");
    require_error(
        tsunami::fvm::make_mesh_field<tsunami::core::Real, FieldLocation::face>(mesh, {"flux"}, "flux", "m2/s", {1.0, 2.0}),
        "fvm.field.entity_count_mismatch");
    require_error(
        tsunami::fvm::make_boundary_patch_field<tsunami::core::Real>(mesh, BoundaryPatchId{99}, {"patch"}, "patch", "m", {1.0}),
        "fvm.field.patch_id_out_of_range");
    require_error(
        tsunami::fvm::make_boundary_patch_field<tsunami::core::Real>(mesh, BoundaryPatchId{0}, {"patch"}, "patch", "m", {1.0}),
        "fvm.field.patch_entity_count_mismatch");
}

TEST_CASE("FVM field copy operations are preallocated and transactional", "[fvm][field]")
{
    auto mesh = tsunami::tests::fvm::multi_face_patch_mesh();
    auto cell_source = tsunami::tests::fvm::sample_cell_scalar_field(mesh).value();
    auto cell_dest = tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, FieldLocation::cell>(mesh, {"cell-dest"}, "cell dest", "m", 0.0).value();
    REQUIRE(cell_dest.copy_values_from(cell_source));
    REQUIRE(cell_dest.at(0) == Approx(1.0));

    auto face_source = tsunami::tests::fvm::sample_face_scalar_field(mesh).value();
    auto face_dest = tsunami::fvm::make_filled_mesh_field<tsunami::core::Real, FieldLocation::face>(mesh, {"face-dest"}, "face dest", "m2/s", 0.0).value();
    REQUIRE(face_dest.copy_values_from(face_source));
    REQUIRE(face_dest.at(4) == Approx(4.0));

    auto cell_vector_source = tsunami::tests::fvm::sample_cell_vector_field(mesh).value();
    auto cell_vector_dest = tsunami::fvm::make_filled_mesh_field<Vector3, FieldLocation::cell>(mesh, {"cv-dest"}, "cv dest", "m/s", Vector3{}).value();
    REQUIRE(cell_vector_dest.copy_values_from(cell_vector_source));
    REQUIRE(cell_vector_dest.at(0).x == Approx(2.0 / 3.0));

    auto face_vector_source = tsunami::tests::fvm::sample_face_vector_field(mesh).value();
    auto face_vector_dest = tsunami::fvm::make_filled_mesh_field<Vector3, FieldLocation::face>(mesh, {"fv-dest"}, "fv dest", "m", Vector3{}).value();
    REQUIRE(face_vector_dest.copy_values_from(face_vector_source));
    REQUIRE(face_vector_dest.at(0).y == Approx(-1.0));

    auto patch_source = tsunami::tests::fvm::sample_patch_scalar_field(mesh).value();
    auto patch_dest = tsunami::fvm::make_filled_boundary_patch_field<tsunami::core::Real>(mesh, BoundaryPatchId{0}, {"patch-dest"}, "patch dest", "m", 0.0).value();
    REQUIRE(patch_dest.copy_values_from(patch_source));
    REQUIRE(patch_dest.at(1) == Approx(20.0));

    auto patch_vector_source = tsunami::tests::fvm::sample_patch_vector_field(mesh).value();
    auto patch_vector_dest = tsunami::fvm::make_filled_boundary_patch_field<Vector3>(mesh, BoundaryPatchId{0}, {"pv-dest"}, "pv dest", "m", Vector3{}).value();
    REQUIRE(patch_vector_dest.copy_values_from(patch_vector_source));
    REQUIRE(patch_vector_dest.at(1).x == Approx(1.0));

    auto original = std::vector<double>{patch_dest.values().begin(), patch_dest.values().end()};
    auto other_mesh = tsunami::tests::fvm::reference_mesh();
    auto incompatible_mesh_source = tsunami::fvm::make_filled_boundary_patch_field<tsunami::core::Real>(other_mesh, BoundaryPatchId{0}, {"patch-other"}, "patch other", "m", 7.0).value();
    require_error(patch_dest.copy_values_from(incompatible_mesh_source), "fvm.field.mesh_incompatible");
    REQUIRE(std::ranges::equal(original, patch_dest.values()));

    auto other_patch = tsunami::fvm::make_filled_boundary_patch_field<tsunami::core::Real>(mesh, BoundaryPatchId{1}, {"patch-other-id"}, "patch other id", "m", 7.0).value();
    require_error(patch_dest.copy_values_from(other_patch), "fvm.field.patch_incompatible");
    REQUIRE(std::ranges::equal(original, patch_dest.values()));

    auto other_unit = tsunami::fvm::make_filled_boundary_patch_field<tsunami::core::Real>(mesh, BoundaryPatchId{0}, {"patch-other-unit"}, "patch other unit", "s", 7.0).value();
    require_error(patch_dest.copy_values_from(other_unit), "fvm.field.unit_incompatible");
    REQUIRE(std::ranges::equal(original, patch_dest.values()));
}

TEST_CASE("Deterministic reference fields bind to mesh and preserve patch-local ordering", "[fvm][field][fixture]")
{
    auto mesh = tsunami::tests::fvm::multi_face_patch_mesh();
    REQUIRE(mesh.boundary_patch(BoundaryPatchId{0}).faces[0] == tsunami::fvm::FaceId{0});
    REQUIRE(mesh.boundary_patch(BoundaryPatchId{0}).faces[1] == tsunami::fvm::FaceId{1});

    auto cell_scalar = tsunami::tests::fvm::sample_cell_scalar_field(mesh).value();
    auto face_scalar = tsunami::tests::fvm::sample_face_scalar_field(mesh).value();
    auto cell_vector = tsunami::tests::fvm::sample_cell_vector_field(mesh).value();
    auto face_vector = tsunami::tests::fvm::sample_face_vector_field(mesh).value();
    auto patch_scalar = tsunami::tests::fvm::sample_patch_scalar_field(mesh).value();
    auto patch_vector = tsunami::tests::fvm::sample_patch_vector_field(mesh).value();

    REQUIRE(cell_scalar.is_bound_to(mesh));
    REQUIRE(face_scalar.is_bound_to(mesh));
    REQUIRE(cell_vector.is_bound_to(mesh));
    REQUIRE(face_vector.is_bound_to(mesh));
    REQUIRE(patch_scalar.is_bound_to(mesh));
    REQUIRE(patch_vector.is_bound_to(mesh));
    REQUIRE(cell_scalar.at(0) == Approx(1.0));
    REQUIRE(face_scalar.at(4) == Approx(4.0));
    REQUIRE(cell_vector.at(1).y == Approx(2.0 / 3.0));
    REQUIRE(face_vector.at(2) == mesh.face_geometry(tsunami::fvm::FaceId{2}).area_vector);
    REQUIRE(patch_scalar.at(0) == Approx(10.0));
    REQUIRE(patch_scalar.at(1) == Approx(20.0));
    REQUIRE(patch_vector.at(0) == mesh.face_geometry(tsunami::fvm::FaceId{0}).area_vector);
    REQUIRE(patch_vector.at(1) == mesh.face_geometry(tsunami::fvm::FaceId{1}).area_vector);
}

TEST_CASE("IFieldView inspection is metadata-only and FVM headers are Qt-free", "[fvm][field][architecture]")
{
    auto field = moved_cell_scalar();
    const IFieldView &view = field;
    REQUIRE(view.descriptor().id.value == "cell-scalar");
    REQUIRE(view.descriptor().unit_id == "m");
    static_assert(!HasValues<IFieldView>);

    const auto include_root = fvm_header_root();
    REQUIRE_FALSE(include_root.empty());
    for (const auto &entry : std::filesystem::directory_iterator(include_root)) {
        if (entry.path().extension() != ".hpp") {
            continue;
        }
        std::ifstream stream{entry.path()};
        REQUIRE(stream.good());
        const auto text = std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
        REQUIRE(text.find("Qt") == std::string::npos);
        REQUIRE(text.find("QObject") == std::string::npos);
        REQUIRE(text.find("QML") == std::string::npos);
    }
}
