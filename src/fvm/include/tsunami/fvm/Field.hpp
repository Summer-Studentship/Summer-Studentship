#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <tsunami/fvm/FieldTraits.hpp>
#include <tsunami/fvm/Mesh.hpp>
#include <tsunami/fvm/MeshRecords.hpp>

namespace tsunami::fvm {

struct FieldId {
    std::string value;

    friend auto operator<=>(const FieldId&, const FieldId&) = default;
};

enum class FieldLocation {
    cell,
    face,
    vertex,
    boundary_patch
};

struct FieldDescriptor {
    FieldId id;
    std::string name;
    MeshId mesh_id;
    FieldLocation location{FieldLocation::cell};
    FieldValueKind value_kind{FieldValueKind::scalar};
    std::size_t component_count{};
    std::size_t entity_count{};
    std::string unit_id;
    std::optional<BoundaryPatchId> boundary_patch;
};

class IFieldView {
public:
    virtual ~IFieldView() = default;

    [[nodiscard]] virtual auto descriptor() const -> FieldDescriptor = 0;
};

[[nodiscard]] constexpr auto to_string(FieldLocation location) noexcept -> std::string_view
{
    switch (location) {
    case FieldLocation::cell:
        return "cell";
    case FieldLocation::face:
        return "face";
    case FieldLocation::vertex:
        return "vertex";
    case FieldLocation::boundary_patch:
        return "boundary_patch";
    }
    return "unknown";
}

} // namespace tsunami::fvm
