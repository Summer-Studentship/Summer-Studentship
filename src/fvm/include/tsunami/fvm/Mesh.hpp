#pragma once

#include <cstddef>
#include <string>
#include <utility>

namespace tsunami::fvm {

struct MeshId {
    std::string value;

    friend auto operator<=>(const MeshId&, const MeshId&) = default;
};

struct MeshSummary {
    MeshId id;
    std::size_t spatial_dimension{};
    std::size_t cell_count{};
    std::size_t face_count{};
    std::size_t vertex_count{};
    std::size_t boundary_patch_count{};
};

class IMeshView {
public:
    virtual ~IMeshView() = default;

    [[nodiscard]] virtual auto summary() const -> MeshSummary = 0;
};

} // namespace tsunami::fvm
