#pragma once

#include <cstddef>
#include <string>

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
    FieldLocation location{FieldLocation::cell};
    std::size_t component_count{};
    std::size_t entity_count{};
    std::string unit_id;
};

class IFieldView {
public:
    virtual ~IFieldView() = default;

    [[nodiscard]] virtual auto descriptor() const -> FieldDescriptor = 0;
};

} // namespace tsunami::fvm
