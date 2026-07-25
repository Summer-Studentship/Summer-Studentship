#include <tsunami/fvm/MeshTopology.hpp>

#include <utility>

namespace tsunami::fvm
{

    MeshTopology::MeshTopology(
        MeshId id,
        std::size_t spatial_dimension,
        std::vector<VertexRecord> vertices,
        std::vector<FaceRecord> faces,
        std::vector<CellRecord> cells,
        std::vector<BoundaryPatchRecord> boundary_patches)
        : id_{std::move(id)}
        , spatial_dimension_{spatial_dimension}
        , vertices_{std::move(vertices)}
        , faces_{std::move(faces)}
        , cells_{std::move(cells)}
        , boundary_patches_{std::move(boundary_patches)}
    {
    }

    auto MeshTopology::id() const noexcept -> const MeshId &
    {
        return id_;
    }

    auto MeshTopology::spatial_dimension() const noexcept -> std::size_t
    {
        return spatial_dimension_;
    }

    auto MeshTopology::vertices() const noexcept -> std::span<const VertexRecord>
    {
        return vertices_;
    }

    auto MeshTopology::faces() const noexcept -> std::span<const FaceRecord>
    {
        return faces_;
    }

    auto MeshTopology::cells() const noexcept -> std::span<const CellRecord>
    {
        return cells_;
    }

    auto MeshTopology::boundary_patches() const noexcept -> std::span<const BoundaryPatchRecord>
    {
        return boundary_patches_;
    }

    auto MeshTopology::vertex(VertexId id) const -> const VertexRecord &
    {
        return vertices_.at(id.value);
    }

    auto MeshTopology::face(FaceId id) const -> const FaceRecord &
    {
        return faces_.at(id.value);
    }

    auto MeshTopology::cell(CellId id) const -> const CellRecord &
    {
        return cells_.at(id.value);
    }

    auto MeshTopology::boundary_patch(BoundaryPatchId id) const -> const BoundaryPatchRecord &
    {
        return boundary_patches_.at(id.value);
    }

    auto MeshTopology::summary() const -> MeshSummary
    {
        return MeshSummary{
            .id = id_,
            .spatial_dimension = spatial_dimension_,
            .cell_count = cells_.size(),
            .face_count = faces_.size(),
            .vertex_count = vertices_.size(),
            .boundary_patch_count = boundary_patches_.size(),
        };
    }

} // namespace tsunami::fvm
