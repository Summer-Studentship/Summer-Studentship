#pragma once

#include <string>
#include <variant>
#include <vector>

#include <tsunami/fvm/Boundary.hpp>

namespace tsunami::fvm
{

    template <SupportedFieldValue Value>
    struct FixedValueSpecification
    {
        std::vector<Value> values;
    };

    struct ZeroGradientSpecification
    {
    };

    struct NamedBoundarySpecification
    {
        std::string requested_type;
    };

    template <SupportedFieldValue Value>
    using BoundaryOperationSpecification = std::variant<
        FixedValueSpecification<Value>,
        ZeroGradientSpecification,
        NamedBoundarySpecification>;

    template <SupportedFieldValue Value>
    struct BoundarySpecification
    {
        BoundaryConditionId id;
        std::string name;
        std::string patch_tag;
        std::string unit_id;
        BoundaryOperationSpecification<Value> operation;
    };

} // namespace tsunami::fvm
