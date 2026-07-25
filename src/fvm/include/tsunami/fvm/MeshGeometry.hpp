#pragma once

#include <span>
#include <vector>

#include <tsunami/fvm/MeshRecords.hpp>

namespace tsunami::fvm
{

    struct FaceGeometry
    {
        Point3 centroid;
        Vector3 area_vector;
    };

    struct CellGeometry
    {
        Point3 centroid;
        tsunami::core::Real measure{};
    };

    class MeshGeometry
    {
    public:
        MeshGeometry() = default;

        MeshGeometry(std::vector<FaceGeometry> faces, std::vector<CellGeometry> cells);

        [[nodiscard]] auto faces() const noexcept -> std::span<const FaceGeometry>;
        [[nodiscard]] auto cells() const noexcept -> std::span<const CellGeometry>;

        [[nodiscard]] auto face(FaceId id) const -> const FaceGeometry &;
        [[nodiscard]] auto cell(CellId id) const -> const CellGeometry &;

    private:
        std::vector<FaceGeometry> faces_;
        std::vector<CellGeometry> cells_;
    };

} // namespace tsunami::fvm
