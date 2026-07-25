#pragma once

#include <compare>
#include <cstdint>
#include <cstddef>

#include <tsunami/fvm/FiniteVolumeMesh.hpp>

namespace tsunami::fvm
{

    struct MeshBinding
    {
        MeshId mesh_id;
        std::size_t spatial_dimension{};
        std::size_t vertex_count{};
        std::size_t face_count{};
        std::size_t cell_count{};
        std::size_t boundary_patch_count{};
        std::uint64_t compatibility_signature{};

        friend auto operator<=>(const MeshBinding &, const MeshBinding &) = default;
    };

    [[nodiscard]] auto make_mesh_binding(const FiniteVolumeMesh &mesh) -> MeshBinding;

} // namespace tsunami::fvm
