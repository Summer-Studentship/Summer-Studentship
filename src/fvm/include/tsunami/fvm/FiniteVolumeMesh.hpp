#pragma once

#include <tsunami/core/Result.hpp>
#include <tsunami/fvm/MeshGeometry.hpp>
#include <tsunami/fvm/MeshTopology.hpp>

namespace tsunami::fvm
{

    class FiniteVolumeMesh final : public IMeshView
    {
    public:
        FiniteVolumeMesh() = default;

        FiniteVolumeMesh(MeshTopology topology, MeshGeometry geometry);

        [[nodiscard]] auto topology() const noexcept -> const MeshTopology &;
        [[nodiscard]] auto geometry() const noexcept -> const MeshGeometry &;

        [[nodiscard]] auto summary() const -> MeshSummary override;

        [[nodiscard]] auto vertex(VertexId id) const -> const VertexRecord &;
        [[nodiscard]] auto face(FaceId id) const -> const FaceRecord &;
        [[nodiscard]] auto cell(CellId id) const -> const CellRecord &;
        [[nodiscard]] auto boundary_patch(BoundaryPatchId id) const -> const BoundaryPatchRecord &;

        [[nodiscard]] auto face_geometry(FaceId id) const -> const FaceGeometry &;
        [[nodiscard]] auto cell_geometry(CellId id) const -> const CellGeometry &;

    private:
        MeshTopology topology_;
        MeshGeometry geometry_;
    };

    [[nodiscard]] auto make_finite_volume_mesh(MeshTopologyInput input)
        -> tsunami::core::Result<FiniteVolumeMesh>;

} // namespace tsunami::fvm
