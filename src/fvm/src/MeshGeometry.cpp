#include <tsunami/fvm/MeshGeometry.hpp>

#include <utility>

namespace tsunami::fvm
{

    MeshGeometry::MeshGeometry(std::vector<FaceGeometry> faces, std::vector<CellGeometry> cells)
        : faces_{std::move(faces)}
        , cells_{std::move(cells)}
    {
    }

    auto MeshGeometry::faces() const noexcept -> std::span<const FaceGeometry>
    {
        return faces_;
    }

    auto MeshGeometry::cells() const noexcept -> std::span<const CellGeometry>
    {
        return cells_;
    }

    auto MeshGeometry::face(FaceId id) const -> const FaceGeometry &
    {
        return faces_.at(id.value);
    }

    auto MeshGeometry::cell(CellId id) const -> const CellGeometry &
    {
        return cells_.at(id.value);
    }

} // namespace tsunami::fvm
