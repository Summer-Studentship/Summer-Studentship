#include <tsunami/fvm/MeshBinding.hpp>

#include <bit>
#include <cstdint>
#include <string_view>

namespace tsunami::fvm
{
    namespace
    {
        constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ull;
        constexpr std::uint64_t fnv_prime = 1099511628211ull;

        class Fnva64
        {
        public:
            auto append_byte(std::uint8_t byte) -> void
            {
                value_ ^= byte;
                value_ *= fnv_prime;
            }

            auto append_u64(std::uint64_t value) -> void
            {
                for (auto shift = 0; shift < 64; shift += 8) {
                    append_byte(static_cast<std::uint8_t>((value >> shift) & 0xffu));
                }
            }

            auto append_size(std::size_t value) -> void
            {
                append_u64(static_cast<std::uint64_t>(value));
            }

            auto append_real(tsunami::core::Real value) -> void
            {
                if (value == 0.0) {
                    value = 0.0;
                }
                append_u64(std::bit_cast<std::uint64_t>(value));
            }

            auto append_string(std::string_view value) -> void
            {
                append_size(value.size());
                for (const auto byte : value) {
                    append_byte(static_cast<std::uint8_t>(byte));
                }
            }

            [[nodiscard]] auto value() const noexcept -> std::uint64_t
            {
                return value_;
            }

        private:
            std::uint64_t value_{fnv_offset_basis};
        };

        auto append_point(Fnva64 &hash, Point3 point) -> void
        {
            hash.append_real(point.x);
            hash.append_real(point.y);
            hash.append_real(point.z);
        }
    } // namespace

    auto make_mesh_binding(const FiniteVolumeMesh &mesh) -> MeshBinding
    {
        const auto summary = mesh.summary();
        auto hash = Fnva64{};
        hash.append_string(summary.id.value);
        hash.append_size(summary.spatial_dimension);
        hash.append_size(summary.vertex_count);
        hash.append_size(summary.face_count);
        hash.append_size(summary.cell_count);
        hash.append_size(summary.boundary_patch_count);

        for (const auto &vertex : mesh.topology().vertices()) {
            hash.append_size(vertex.id.value);
            append_point(hash, vertex.position);
        }

        for (const auto &face : mesh.topology().faces()) {
            hash.append_size(face.id.value);
            hash.append_size(face.vertices.size());
            for (const auto vertex_id : face.vertices) {
                hash.append_size(vertex_id.value);
            }
            hash.append_size(face.owner.value);
            hash.append_byte(face.neighbour.has_value() ? 1u : 0u);
            if (face.neighbour) {
                hash.append_size(face.neighbour->value);
            }
            hash.append_byte(face.boundary_patch.has_value() ? 1u : 0u);
            if (face.boundary_patch) {
                hash.append_size(face.boundary_patch->value);
            }
        }

        for (const auto &cell : mesh.topology().cells()) {
            hash.append_size(cell.id.value);
            hash.append_size(cell.faces.size());
            for (const auto face_id : cell.faces) {
                hash.append_size(face_id.value);
            }
        }

        for (const auto &patch : mesh.topology().boundary_patches()) {
            hash.append_size(patch.id.value);
            hash.append_string(patch.name);
            hash.append_size(patch.faces.size());
            for (const auto face_id : patch.faces) {
                hash.append_size(face_id.value);
            }
        }

        return MeshBinding{
            .mesh_id = summary.id,
            .spatial_dimension = summary.spatial_dimension,
            .vertex_count = summary.vertex_count,
            .face_count = summary.face_count,
            .cell_count = summary.cell_count,
            .boundary_patch_count = summary.boundary_patch_count,
            .compatibility_signature = hash.value(),
        };
    }

} // namespace tsunami::fvm
