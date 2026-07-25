#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <type_traits>
#include <vector>

#include <tsunami/fvm/MeshGeometry.hpp>
#include <tsunami/fvm/MeshRecords.hpp>

namespace
{

    using tsunami::fvm::BoundaryPatchId;
    using tsunami::fvm::BoundaryPatchRecord;
    using tsunami::fvm::CellGeometry;
    using tsunami::fvm::CellId;
    using tsunami::fvm::CellRecord;
    using tsunami::fvm::FaceGeometry;
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

    static_assert(!std::is_convertible_v<VertexId, FaceId>);
    static_assert(!std::is_convertible_v<FaceId, CellId>);
    static_assert(!std::is_convertible_v<CellId, BoundaryPatchId>);

    REQUIRE(vertex.value == 0);
    REQUIRE(face.value == 0);
    REQUIRE(cell.value == 0);
    REQUIRE(patch.value == 0);
}

TEST_CASE("FVM face records distinguish internal and boundary topology only", "[fvm][mesh]")
{
    const FaceRecord boundary_face{
        .id = FaceId{0},
        .vertices = {VertexId{0}, VertexId{1}},
        .owner = CellId{0},
        .neighbour = std::nullopt,
        .boundary_patch = BoundaryPatchId{0},
    };

    const FaceRecord internal_face{
        .id = FaceId{1},
        .vertices = {VertexId{0}, VertexId{2}},
        .owner = CellId{0},
        .neighbour = CellId{1},
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

TEST_CASE("FVM cells use face addressing and keep geometry separate", "[fvm][mesh]")
{
    const std::vector<VertexRecord> vertices{
        {VertexId{0}, Point3{0.0, 0.0, 0.0}},
        {VertexId{1}, Point3{1.0, 0.0, 0.0}},
        {VertexId{2}, Point3{1.0, 1.0, 0.0}},
    };

    const CellRecord cell{
        .id = CellId{0},
        .faces = {FaceId{0}, FaceId{1}, FaceId{2}},
    };
    const BoundaryPatchRecord patch{BoundaryPatchId{0}, "south", {FaceId{0}}};
    const FaceGeometry face_geometry{
        .centroid = Point3{0.5, 0.0, 0.0},
        .area_vector = Vector3{0.0, -1.0, 0.0},
    };
    const CellGeometry cell_geometry{
        .centroid = Point3{2.0 / 3.0, 1.0 / 3.0, 0.0},
        .measure = 0.5,
    };

    REQUIRE(vertices.size() == 3);
    REQUIRE(cell.faces[2] == FaceId{2});
    REQUIRE(patch.faces.front() == FaceId{0});
    REQUIRE(face_geometry.area_vector.y == -1.0);
    REQUIRE(cell_geometry.measure == 0.5);
}
