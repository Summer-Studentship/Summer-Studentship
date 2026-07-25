#pragma once

#include <compare>
#include <optional>
#include <string>
#include <vector>

#include <tsunami/core/Types.hpp>

namespace tsunami::fvm
{

    struct VertexId
    {
        tsunami::core::Index value{};

        friend auto operator<=>(const VertexId &, const VertexId &) = default;
    };

    struct FaceId
    {
        tsunami::core::Index value{};

        friend auto operator<=>(const FaceId &, const FaceId &) = default;
    };

    struct CellId
    {
        tsunami::core::Index value{};

        friend auto operator<=>(const CellId &, const CellId &) = default;
    };

    struct BoundaryPatchId
    {
        tsunami::core::Index value{};

        friend auto operator<=>(const BoundaryPatchId &, const BoundaryPatchId &) = default;
    };

    struct Point3
    {
        tsunami::core::Real x{};
        tsunami::core::Real y{};
        tsunami::core::Real z{};

        friend auto operator==(const Point3 &, const Point3 &) -> bool = default;
    };

    struct Vector3
    {
        tsunami::core::Real x{};
        tsunami::core::Real y{};
        tsunami::core::Real z{};

        friend auto operator==(const Vector3 &, const Vector3 &) -> bool = default;
    };

    /**
     * Geometric vertex in the shared finite-volume topology.
     *
     * For a two-dimensional mesh, z is zero.
     */
    struct VertexRecord
    {
        VertexId id;
        Point3 position;
    };

    /**
     * Oriented finite-volume face.
     *
     * The unit normal points:
     * - outward from the owner cell for a boundary face;
     * - from the owner cell towards the neighbour cell for an internal face.
     *
     * measure is:
     * - edge length in two dimensions;
     * - face area in three dimensions.
     */
    struct FaceRecord
    {
        FaceId id;
        std::vector<VertexId> vertices;

        CellId owner;
        std::optional<CellId> neighbour;

        Point3 centroid;
        tsunami::core::Real measure{};
        Vector3 unit_normal;

        std::optional<BoundaryPatchId> boundary_patch;

        [[nodiscard]] auto is_boundary() const noexcept -> bool
        {
            return !neighbour.has_value();
        }

        [[nodiscard]] auto is_internal() const noexcept -> bool
        {
            return neighbour.has_value();
        }
    };

    /**
     * Finite control volume.
     *
     * measure is:
     * - cell area in two dimensions;
     * - cell volume in three dimensions.
     */
    struct CellRecord
    {
        CellId id;
        std::vector<VertexId> vertices;
        std::vector<FaceId> faces;

        Point3 centroid;
        tsunami::core::Real measure{};
    };

    /**
     * Named collection of boundary faces.
     *
     * Boundary-condition type and metadata belong to the later
     * SWE-FVM-BC work package.
     */
    struct BoundaryPatchRecord
    {
        BoundaryPatchId id;
        std::string name;
        std::vector<FaceId> faces;
    };

} // namespace tsunami::fvm