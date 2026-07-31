#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <tsunami/adapters/gmsh/GmshMeshImporter.hpp>

namespace
{
    using Catch::Approx;
    using tsunami::fvm::BoundaryPatchId;
    using tsunami::fvm::CellId;
    using tsunami::fvm::FaceId;
    using tsunami::fvm::VertexId;

    [[nodiscard]] auto valid_square_msh() -> std::string
    {
        return R"msh($MeshFormat
4.1 0 8
$EndMeshFormat
$PhysicalNames
5
2 1 "region.domain"
1 2 "boundary.offshore"
1 3 "boundary.inland"
1 4 "boundary.left_side"
1 5 "boundary.right_side"
$EndPhysicalNames
$Entities
4 4 1 0
1 0 0 0 0
2 1 0 0 0
3 1 1 0 0
4 0 1 0 0
11 0 0 0 1 0 0 1 2 2 1 2
12 1 0 0 1 1 0 1 5 2 2 3
13 0 1 0 1 1 0 1 3 2 3 4
14 0 0 0 0 1 0 1 4 2 4 1
21 0 0 0 1 1 0 1 1 4 11 12 -13 -14
$EndEntities
$Nodes
1 4 10 40
2 21 0 4
10
20
30
40
0 0 0
1 0 0
1 1 0
0 1 0
$EndNodes
$Elements
5 6 100 201
1 11 1 1
100 10 20
1 12 1 1
101 20 30
1 13 1 1
102 30 40
1 14 1 1
103 40 10
2 21 2 2
200 10 30 20
201 10 30 40
$EndElements
)msh";
    }

    [[nodiscard]] auto replace_once(std::string text, std::string_view from, std::string_view to) -> std::string
    {
        const auto found = text.find(from);
        REQUIRE(found != std::string::npos);
        text.replace(found, from.size(), to);
        return text;
    }

    [[nodiscard]] auto write_fixture(std::string_view name, std::string_view contents) -> std::filesystem::path
    {
        const auto directory = std::filesystem::temp_directory_path() / "tsunami-gmsh-import-tests";
        std::filesystem::create_directories(directory);
        const auto path = directory / name;
        auto file = std::ofstream{path, std::ios::binary};
        REQUIRE(file);
        file << contents;
        REQUIRE(file.good());
        return path;
    }

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        const auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }

    auto require_import_failure(std::string_view code, std::string_view name, std::string contents) -> void
    {
        const auto result = tsunami::adapters::gmsh::import_gmsh_msh41_ascii_mesh(write_fixture(name, contents));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == code);
        CHECK(result.error().category() == tsunami::core::DiagnosticCategory::input_data);
        CHECK(result.error().severity() == tsunami::core::Severity::error);
        CHECK(context_value(result.error(), "operation") == "import_gmsh_msh41_ascii_mesh");
        CHECK(context_value(result.error(), "rule_id") == "SWE-GEO-MSH-WP1");
        CHECK(context_value(result.error(), "state_changed") == "false");
        CHECK(result.error().context_value("source_path").has_value());
    }
}

TEST_CASE("Gmsh MSH 4.1 importer maps sparse tagged triangles into Regional2D mesh contracts", "[gmsh][mesh import]")
{
    const auto result = tsunami::adapters::gmsh::import_gmsh_msh41_ascii_mesh(
        write_fixture("valid-square.msh", valid_square_msh()));
    REQUIRE(result.has_value());

    const auto &metadata = result.value().metadata;
    CHECK(metadata.msh_version == "4.1");
    CHECK(metadata.imported_node_count == 4U);
    CHECK(metadata.triangle_count == 2U);
    CHECK(metadata.boundary_line_count == 4U);
    CHECK(metadata.clockwise_triangle_count == 1U);
    CHECK(metadata.physical_name_tags.at("region.domain") == 1);
    CHECK(metadata.physical_name_tags.at("boundary.offshore") == 2);
    CHECK(metadata.physical_name_tags.at("boundary.inland") == 3);
    CHECK(metadata.physical_name_tags.at("boundary.left_side") == 4);
    CHECK(metadata.physical_name_tags.at("boundary.right_side") == 5);

    const auto &mesh = result.value().mesh;
    const auto summary = mesh.summary();
    CHECK(summary.id.value == "gmsh:valid-square.msh");
    CHECK(summary.spatial_dimension == 2U);
    CHECK(summary.vertex_count == 4U);
    CHECK(summary.cell_count == 2U);
    CHECK(summary.face_count == 5U);
    CHECK(summary.boundary_patch_count == 4U);

    for (std::size_t i = 0U; i < mesh.topology().vertices().size(); ++i) {
        CHECK(mesh.topology().vertices()[i].id.value == i);
    }
    for (std::size_t i = 0U; i < mesh.topology().faces().size(); ++i) {
        CHECK(mesh.topology().faces()[i].id.value == i);
    }
    for (std::size_t i = 0U; i < mesh.topology().cells().size(); ++i) {
        CHECK(mesh.topology().cells()[i].id.value == i);
    }

    CHECK(mesh.vertex(VertexId{0}).position.x == Approx(0.0));
    CHECK(mesh.vertex(VertexId{0}).position.y == Approx(0.0));
    CHECK(mesh.vertex(VertexId{1}).position.x == Approx(1.0));
    CHECK(mesh.vertex(VertexId{2}).position.y == Approx(1.0));
    CHECK(mesh.vertex(VertexId{3}).position.x == Approx(0.0));

    auto internal_count = std::size_t{};
    for (const auto &face : mesh.topology().faces()) {
        if (!face.is_internal()) {
            continue;
        }
        ++internal_count;
        CHECK(face.owner == CellId{0});
        REQUIRE(face.neighbour.has_value());
        CHECK(*face.neighbour == CellId{1});
    }
    CHECK(internal_count == 1U);

    CHECK(mesh.cell_geometry(CellId{0}).measure == Approx(0.5));
    CHECK(mesh.cell_geometry(CellId{1}).measure == Approx(0.5));
    CHECK(mesh.boundary_patch(BoundaryPatchId{0}).name == "boundary.offshore");
    CHECK(mesh.boundary_patch(BoundaryPatchId{0}).faces == std::vector<FaceId>{FaceId{0}});
    CHECK(mesh.boundary_patch(BoundaryPatchId{1}).name == "boundary.inland");
    CHECK(mesh.boundary_patch(BoundaryPatchId{1}).faces == std::vector<FaceId>{FaceId{3}});
    CHECK(mesh.boundary_patch(BoundaryPatchId{2}).name == "boundary.left_side");
    CHECK(mesh.boundary_patch(BoundaryPatchId{2}).faces == std::vector<FaceId>{FaceId{4}});
    CHECK(mesh.boundary_patch(BoundaryPatchId{3}).name == "boundary.right_side");
    CHECK(mesh.boundary_patch(BoundaryPatchId{3}).faces == std::vector<FaceId>{FaceId{1}});
}

TEST_CASE("Gmsh importer rejects missing mandatory physical names", "[gmsh][mesh import][validation]")
{
    auto text = replace_once(valid_square_msh(), "$PhysicalNames\n5\n", "$PhysicalNames\n4\n");
    text = replace_once(text, "1 2 \"boundary.offshore\"\n", "");
    require_import_failure("mesh.gmsh.physical_name_missing", "missing-physical.msh", std::move(text));
}

TEST_CASE("Gmsh importer rejects binary MSH input", "[gmsh][mesh import][validation]")
{
    require_import_failure(
        "mesh.gmsh.binary_unsupported",
        "binary.msh",
        replace_once(valid_square_msh(), "4.1 0 8", "4.1 1 8"));
}

TEST_CASE("Gmsh importer rejects boundary lines that do not match reconstructed faces", "[gmsh][mesh import][validation]")
{
    require_import_failure(
        "mesh.gmsh.boundary_line_unmatched",
        "unmatched-boundary.msh",
        replace_once(valid_square_msh(), "100 10 20", "100 20 40"));
}

TEST_CASE("Gmsh importer rejects degenerate triangles before mesh construction", "[gmsh][mesh import][validation]")
{
    require_import_failure(
        "mesh.gmsh.degenerate_triangle",
        "degenerate-triangle.msh",
        replace_once(valid_square_msh(), "200 10 30 20", "200 10 20 20"));
}
