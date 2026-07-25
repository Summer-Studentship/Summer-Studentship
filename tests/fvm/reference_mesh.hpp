#pragma once

#include <optional>

#include <tsunami/fvm/MeshTopology.hpp>

namespace tsunami::tests::fvm
{

    [[nodiscard]] inline auto two_triangle_unit_square_input() -> tsunami::fvm::MeshTopologyInput
    {
        using tsunami::fvm::BoundaryPatchId;
        using tsunami::fvm::BoundaryPatchRecord;
        using tsunami::fvm::CellId;
        using tsunami::fvm::CellRecord;
        using tsunami::fvm::FaceId;
        using tsunami::fvm::FaceRecord;
        using tsunami::fvm::MeshId;
        using tsunami::fvm::MeshTopologyInput;
        using tsunami::fvm::Point3;
        using tsunami::fvm::VertexId;
        using tsunami::fvm::VertexRecord;

        return MeshTopologyInput{
            .id = MeshId{"unit-square-two-triangles"},
            .spatial_dimension = 2,
            .vertices = {
                VertexRecord{VertexId{0}, Point3{0.0, 0.0, 0.0}},
                VertexRecord{VertexId{1}, Point3{1.0, 0.0, 0.0}},
                VertexRecord{VertexId{2}, Point3{1.0, 1.0, 0.0}},
                VertexRecord{VertexId{3}, Point3{0.0, 1.0, 0.0}},
            },
            .faces = {
                FaceRecord{FaceId{0}, {VertexId{0}, VertexId{1}}, CellId{0}, std::nullopt, BoundaryPatchId{0}},
                FaceRecord{FaceId{1}, {VertexId{1}, VertexId{2}}, CellId{0}, std::nullopt, BoundaryPatchId{1}},
                FaceRecord{FaceId{2}, {VertexId{0}, VertexId{2}}, CellId{0}, CellId{1}, std::nullopt},
                FaceRecord{FaceId{3}, {VertexId{2}, VertexId{3}}, CellId{1}, std::nullopt, BoundaryPatchId{2}},
                FaceRecord{FaceId{4}, {VertexId{3}, VertexId{0}}, CellId{1}, std::nullopt, BoundaryPatchId{3}},
            },
            .cells = {
                CellRecord{CellId{0}, {FaceId{0}, FaceId{1}, FaceId{2}}},
                CellRecord{CellId{1}, {FaceId{2}, FaceId{3}, FaceId{4}}},
            },
            .boundary_patches = {
                BoundaryPatchRecord{BoundaryPatchId{0}, "south", {FaceId{0}}},
                BoundaryPatchRecord{BoundaryPatchId{1}, "east", {FaceId{1}}},
                BoundaryPatchRecord{BoundaryPatchId{2}, "north", {FaceId{3}}},
                BoundaryPatchRecord{BoundaryPatchId{3}, "west", {FaceId{4}}},
            },
        };
    }

} // namespace tsunami::tests::fvm
