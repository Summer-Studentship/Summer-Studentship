#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

#include <tsunami/fvm/MeshRecords.hpp>

namespace
{

    using tsunami::fvm::BoundaryPatchId;
    using tsunami::fvm::BoundaryPatchRecord;
    using tsunami::fvm::CellId;
    using tsunami::fvm::CellRecord;
    using tsunami::fvm::FaceId;
    using tsunami::fvm::FaceRecord;
    using tsunami::fvm::Point3;
    using tsunami::fvm::Vector3;
    using tsunami::fvm::VertexId;
    using tsunami::fvm::VertexRecord;

} // namespace

TEST_CASE("FVM mesh entity identifiers are strongly separated", "[fvm][mesh]")
{
    const VertexId vertex{0};
    const FaceId face{0};
    const CellId cell{0};
    const BoundaryPatchId patch{0};

    REQUIRE(vertex.value == 0);
    REQUIRE(face.value == 0);
    REQUIRE(cell.value == 0);
    REQUIRE(patch.value == 0);
}

TEST_CASE("FVM face records distinguish internal and boundary faces", "[fvm][mesh]")
{
    const FaceRecord boundary_face{
        .id = FaceId{0},
        .vertices = {VertexId{0}, VertexId{1}},
        .owner = CellId{0},
        .neighbour = std::nullopt,
        .centroid = Point3{0.5, 0.0, 0.0},
        .measure = 1.0,
        .unit_normal = Vector3{0.0, -1.0, 0.0},
        .boundary_patch = BoundaryPatchId{0},
    };

    const FaceRecord internal_face{
        .id = FaceId{1},
        .vertices = {VertexId{0}, VertexId{2}},
        .owner = CellId{0},
        .neighbour = CellId{1},
        .centroid = Point3{0.5, 0.5, 0.0},
        .measure = 1.4142135623730951,
        .unit_normal = Vector3{-0.7071067811865475, 0.7071067811865475, 0.0},
        .boundary_patch = std::nullopt,
    };

    REQUIRE(boundary_face.is_boundary());
    REQUIRE_FALSE(boundary_face.is_internal());
    REQUIRE(boundary_face.boundary_patch == BoundaryPatchId{0});

    REQUIRE(internal_face.is_internal());
    REQUIRE_FALSE(internal_face.is_boundary());
    REQUIRE(internal_face.neighbour == CellId{1});
    REQUIRE_FALSE(internal_face.boundary_patch.has_value());
}

TEST_CASE("Two-triangle fixture records preserve basic connectivity", "[fvm][mesh]")
{
    const std::vector<VertexRecord> vertices{
        {VertexId{0}, Point3{0.0, 0.0, 0.0}},
        {VertexId{1}, Point3{1.0, 0.0, 0.0}},
        {VertexId{2}, Point3{1.0, 1.0, 0.0}},
        {VertexId{3}, Point3{0.0, 1.0, 0.0}},
    };

    const std::vector<CellRecord> cells{
        {
            .id = CellId{0},
            .vertices = {VertexId{0}, VertexId{1}, VertexId{2}},
            .faces = {FaceId{0}, FaceId{1}, FaceId{2}},
            .centroid = Point3{2.0 / 3.0, 1.0 / 3.0, 0.0},
            .measure = 0.5,
        },
        {
            .id = CellId{1},
            .vertices = {VertexId{0}, VertexId{2}, VertexId{3}},
            .faces = {FaceId{2}, FaceId{3}, FaceId{4}},
            .centroid = Point3{1.0 / 3.0, 2.0 / 3.0, 0.0},
            .measure = 0.5,
        },
    };

    const std::vector<BoundaryPatchRecord> patches{
        {BoundaryPatchId{0}, "south", {FaceId{0}}},
        {BoundaryPatchId{1}, "east", {FaceId{1}}},
        {BoundaryPatchId{2}, "north", {FaceId{3}}},
        {BoundaryPatchId{3}, "west", {FaceId{4}}},
    };

    REQUIRE(vertices.size() == 4);
    REQUIRE(cells.size() == 2);
    REQUIRE(patches.size() == 4);

    REQUIRE(cells[0].measure == 0.5);
    REQUIRE(cells[1].measure == 0.5);
    REQUIRE(cells[0].measure + cells[1].measure == 1.0);

    REQUIRE(cells[0].faces[2] == FaceId{2});
    REQUIRE(cells[1].faces[0] == FaceId{2});
}