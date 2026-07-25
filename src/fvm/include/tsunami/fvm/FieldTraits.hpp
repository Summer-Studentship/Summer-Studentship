#pragma once

#include <cstddef>
#include <concepts>
#include <string_view>
#include <type_traits>

#include <tsunami/core/Types.hpp>
#include <tsunami/fvm/MeshRecords.hpp>

namespace tsunami::fvm
{

    enum class FieldValueKind
    {
        scalar,
        vector
    };

    [[nodiscard]] constexpr auto to_string(FieldValueKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case FieldValueKind::scalar:
            return "scalar";
        case FieldValueKind::vector:
            return "vector";
        }
        return "unknown";
    }

    template <class Value>
    struct FieldValueTraits;

    template <>
    struct FieldValueTraits<tsunami::core::Real>
    {
        static constexpr auto kind = FieldValueKind::scalar;
        static constexpr std::size_t component_count = 1;
    };

    template <>
    struct FieldValueTraits<Vector3>
    {
        static constexpr auto kind = FieldValueKind::vector;
        static constexpr std::size_t component_count = 3;
    };

    template <class Value>
    concept SupportedFieldValue = requires {
        FieldValueTraits<std::remove_cvref_t<Value>>::kind;
        FieldValueTraits<std::remove_cvref_t<Value>>::component_count;
    } && (std::same_as<std::remove_cvref_t<Value>, tsunami::core::Real> ||
          std::same_as<std::remove_cvref_t<Value>, Vector3>);

} // namespace tsunami::fvm
