#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include <tsunami/core/Diagnostic.hpp>
#include <tsunami/fvm/FiniteVolumeMesh.hpp>

#include "reference_mesh.hpp"

namespace
{
    using Catch::Approx;
    using tsunami::fvm::BoundaryPatchId;
    using tsunami::fvm::CellId;
    using tsunami::fvm::FaceId;
    using tsunami::fvm::FiniteVolumeMesh;
    using tsunami::fvm::IMeshView;
    using tsunami::fvm::Point3;
    using tsunami::fvm::Vector3;
    using tsunami::fvm::VertexId;

    [[nodiscard]] auto dot(Vector3 left, Vector3 right) -> double
    {
        return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
    }

    [[nodiscard]] auto subtract(Point3 left, Point3 right) -> Vector3
    {
        return Vector3{left.x - right.x, left.y - right.y, left.z - right.z};
    }

    [[nodiscard]] auto magnitude(Vector3 value) -> double
    {
        return std::sqrt(dot(value, value));
    }

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        const auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }

    template <class Mutator>
    auto require_failure(std::string_view code, Mutator mutator) -> void
    {
        auto input = tsunami::tests::fvm::two_triangle_unit_square_input();
        mutator(input);
        const auto result = tsunami::fvm::make_finite_volume_mesh(std::move(input));
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code() == code);
        REQUIRE(result.error().category() == tsunami::core::DiagnosticCategory::numerical);
        REQUIRE(result.error().severity() == tsunami::core::Severity::error);
        REQUIRE(context_value(result.error(), "operation") == "make_finite_volume_mesh");
        REQUIRE(context_value(result.error(), "rule_id") == "SWE-FVM-MSH-WP1");
        REQUIRE(context_value(result.error(), "state_changed") == "false");
        REQUIRE(result.error().context_value("mesh_id").has_value());
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
} // namespace

TEST_CASE("Regional2D reference mesh is accepted with immutable topology and derived geometry", "[fvm][mesh]")
{
    static_assert(std::is_base_of_v<IMeshView, FiniteVolumeMesh>);

    auto result = tsunami::fvm::make_finite_volume_mesh(tsunami::tests::fvm::two_triangle_unit_square_input());
    REQUIRE(result.has_value());
    const auto mesh = std::move(result).value();
    const auto &view = static_cast<const IMeshView &>(mesh);
    const auto summary = view.summary();

    REQUIRE(summary.id.value == "unit-square-two-triangles");
    REQUIRE(summary.spatial_dimension == 2);
    REQUIRE(summary.vertex_count == 4);
    REQUIRE(summary.face_count == 5);
    REQUIRE(summary.cell_count == 2);
    REQUIRE(summary.boundary_patch_count == 4);

    REQUIRE(mesh.topology().vertices().size() == 4);
    REQUIRE(mesh.topology().faces().size() == 5);
    REQUIRE(mesh.topology().cells().size() == 2);
    REQUIRE(mesh.topology().boundary_patches().size() == 4);
    REQUIRE(mesh.geometry().faces().size() == 5);
    REQUIRE(mesh.geometry().cells().size() == 2);

    REQUIRE(mesh.vertex(VertexId{2}).position.x == Approx(1.0));
    REQUIRE(mesh.face(FaceId{2}).is_internal());
    REQUIRE(mesh.face(FaceId{0}).is_boundary());
    REQUIRE(mesh.cell(CellId{1}).faces[0] == FaceId{2});
    REQUIRE(mesh.boundary_patch(BoundaryPatchId{0}).name == "south");
    REQUIRE(mesh.boundary_patch(BoundaryPatchId{1}).name == "east");
    REQUIRE(mesh.boundary_patch(BoundaryPatchId{2}).name == "north");
    REQUIRE(mesh.boundary_patch(BoundaryPatchId{3}).name == "west");

    const auto &cell0 = mesh.cell_geometry(CellId{0});
    const auto &cell1 = mesh.cell_geometry(CellId{1});
    REQUIRE(cell0.measure == Approx(0.5));
    REQUIRE(cell1.measure == Approx(0.5));
    REQUIRE((cell0.measure + cell1.measure) == Approx(1.0));
    REQUIRE(cell0.centroid.x == Approx(2.0 / 3.0));
    REQUIRE(cell0.centroid.y == Approx(1.0 / 3.0));
    REQUIRE(cell0.centroid.z == Approx(0.0));
    REQUIRE(cell1.centroid.x == Approx(1.0 / 3.0));
    REQUIRE(cell1.centroid.y == Approx(2.0 / 3.0));
    REQUIRE(cell1.centroid.z == Approx(0.0));

    const auto &internal = mesh.face_geometry(FaceId{2});
    REQUIRE(dot(internal.area_vector, subtract(cell1.centroid, cell0.centroid)) > 0.0);
    REQUIRE(magnitude(internal.area_vector) == Approx(std::sqrt(2.0)));

    for (const auto face_id : {FaceId{0}, FaceId{1}, FaceId{3}, FaceId{4}}) {
        const auto &face = mesh.face(face_id);
        const auto &geometry = mesh.face_geometry(face_id);
        REQUIRE(dot(geometry.area_vector, subtract(geometry.centroid, mesh.cell_geometry(face.owner).centroid)) > 0.0);
        REQUIRE(magnitude(geometry.area_vector) == Approx(1.0));
    }

    for (const auto cell_id : {CellId{0}, CellId{1}}) {
        auto closure = Vector3{};
        for (const auto face_id : mesh.cell(cell_id).faces) {
            const auto &face = mesh.face(face_id);
            const auto &geometry = mesh.face_geometry(face_id);
            const auto sign = face.owner == cell_id ? 1.0 : -1.0;
            closure.x += sign * geometry.area_vector.x;
            closure.y += sign * geometry.area_vector.y;
            closure.z += sign * geometry.area_vector.z;
        }
        REQUIRE(magnitude(closure) == Approx(0.0).margin(1.0e-12));
    }

    static_assert(std::is_const_v<std::remove_reference_t<decltype(mesh.topology().faces().front())>>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(mesh.geometry().cells().front())>>);
}

TEST_CASE("Regional2D mesh validation reports deterministic diagnostic codes", "[fvm][mesh][validation]")
{
    require_failure("fvm.mesh.id_required", [](auto &input) { input.id.value.clear(); });
    require_failure("fvm.mesh.invalid_dimension", [](auto &input) { input.spatial_dimension = 3; });
    require_failure("fvm.mesh.non_planar_point", [](auto &input) { input.vertices[0].position.z = 1.0e-6; });
    require_failure("fvm.mesh.vertex_id_mismatch", [](auto &input) { input.vertices[1].id = VertexId{7}; });
    require_failure("fvm.mesh.face_id_mismatch", [](auto &input) { input.faces[1].id = FaceId{7}; });
    require_failure("fvm.mesh.cell_id_mismatch", [](auto &input) { input.cells[1].id = CellId{7}; });
    require_failure("fvm.mesh.patch_id_mismatch", [](auto &input) { input.boundary_patches[1].id = BoundaryPatchId{7}; });
    require_failure("fvm.mesh.face_vertex_count_unsupported", [](auto &input) { input.faces[0].vertices.push_back(VertexId{2}); });
    require_failure("fvm.mesh.face_vertex_out_of_range", [](auto &input) { input.faces[0].vertices[1] = VertexId{99}; });
    require_failure("fvm.mesh.face_owner_out_of_range", [](auto &input) { input.faces[0].owner = CellId{99}; });
    require_failure("fvm.mesh.face_neighbour_out_of_range", [](auto &input) { input.faces[2].neighbour = CellId{99}; });
    require_failure("fvm.mesh.face_owner_neighbour_equal", [](auto &input) { input.faces[2].neighbour = CellId{0}; });
    require_failure("fvm.mesh.internal_face_has_patch", [](auto &input) { input.faces[2].boundary_patch = BoundaryPatchId{0}; });
    require_failure("fvm.mesh.boundary_face_missing_patch", [](auto &input) { input.faces[0].boundary_patch = std::nullopt; });
    require_failure("fvm.mesh.patch_face_duplicate", [](auto &input) { input.boundary_patches[1].faces = {FaceId{0}}; });
    require_failure("fvm.mesh.boundary_face_unassigned", [](auto &input) { input.boundary_patches[0].faces.clear(); });
    require_failure("fvm.mesh.internal_face_in_patch", [](auto &input) {
        input.boundary_patches[0].faces = {FaceId{2}};
    });
    require_failure("fvm.mesh.cell_face_count_unsupported", [](auto &input) { input.cells[0].faces.pop_back(); });
    require_failure("fvm.mesh.cell_face_duplicate", [](auto &input) { input.cells[0].faces[2] = FaceId{1}; });
    require_failure("fvm.mesh.cell_face_membership_invalid", [](auto &input) { input.cells[1].faces[1] = FaceId{1}; });
    require_failure("fvm.mesh.cell_vertex_count_unsupported", [](auto &input) { input.faces[1].vertices = {VertexId{2}, VertexId{3}}; });
    require_failure("fvm.mesh.edge_degenerate", [](auto &input) { input.faces[0].vertices = {VertexId{0}, VertexId{0}}; });
    require_failure("fvm.mesh.cell_degenerate", [](auto &input) { input.vertices[2].position = Point3{2.0, 0.0, 0.0}; });
    require_failure("fvm.mesh.face_orientation_ambiguous", [](auto &input) {
        input.vertices[0].position = Point3{0.0, 0.0, 0.0};
        input.vertices[1].position = Point3{1.0, 0.0, 0.0};
        input.vertices[2].position = Point3{0.5, 1.0, 0.0};
        input.vertices[3].position = Point3{0.5, -1.0, 0.0};
    });
}

TEST_CASE("Public FVM headers remain isolated from Qt", "[fvm][architecture]")
{
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
