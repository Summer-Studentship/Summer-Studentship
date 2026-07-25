#pragma once

#include <span>
#include <vector>

#include <tsunami/fvm/Mesh.hpp>
#include <tsunami/fvm/MeshRecords.hpp>

namespace tsunami::fvm
{

    struct MeshTopologyInput
    {
        MeshId id;
        std::size_t spatial_dimension{};
        std::vector<VertexRecord> vertices;
        std::vector<FaceRecord> faces;
        std::vector<CellRecord> cells;
        std::vector<BoundaryPatchRecord> boundary_patches;
    };

    class MeshTopology
    {
    public:
        MeshTopology() = default;

        MeshTopology(
            MeshId id,
            std::size_t spatial_dimension,
            std::vector<VertexRecord> vertices,
            std::vector<FaceRecord> faces,
            std::vector<CellRecord> cells,
            std::vector<BoundaryPatchRecord> boundary_patches);

        [[nodiscard]] auto id() const noexcept -> const MeshId &;
        [[nodiscard]] auto spatial_dimension() const noexcept -> std::size_t;

        [[nodiscard]] auto vertices() const noexcept -> std::span<const VertexRecord>;
        [[nodiscard]] auto faces() const noexcept -> std::span<const FaceRecord>;
        [[nodiscard]] auto cells() const noexcept -> std::span<const CellRecord>;
        [[nodiscard]] auto boundary_patches() const noexcept -> std::span<const BoundaryPatchRecord>;

        [[nodiscard]] auto vertex(VertexId id) const -> const VertexRecord &;
        [[nodiscard]] auto face(FaceId id) const -> const FaceRecord &;
        [[nodiscard]] auto cell(CellId id) const -> const CellRecord &;
        [[nodiscard]] auto boundary_patch(BoundaryPatchId id) const -> const BoundaryPatchRecord &;

        [[nodiscard]] auto summary() const -> MeshSummary;

    private:
        MeshId id_;
        std::size_t spatial_dimension_{};
        std::vector<VertexRecord> vertices_;
        std::vector<FaceRecord> faces_;
        std::vector<CellRecord> cells_;
        std::vector<BoundaryPatchRecord> boundary_patches_;
    };

} // namespace tsunami::fvm
