#pragma once

#include <vector>

#include <tsunami/fvm/BoundaryConditionSet.hpp>

#include "reference_fields.hpp"

namespace tsunami::tests::fvm
{

    [[nodiscard]] inline auto scalar_boundary_specs()
        -> std::vector<tsunami::fvm::BoundarySpecification<tsunami::core::Real>>
    {
        using tsunami::fvm::BoundaryConditionId;
        using tsunami::fvm::BoundarySpecification;
        using tsunami::fvm::FixedValueSpecification;
        using tsunami::fvm::NamedBoundarySpecification;
        using tsunami::fvm::ZeroGradientSpecification;

        return {
            BoundarySpecification<tsunami::core::Real>{
                BoundaryConditionId{"bc-south-depth"},
                "south fixed depth",
                "south",
                "m",
                FixedValueSpecification<tsunami::core::Real>{{10.0}}},
            BoundarySpecification<tsunami::core::Real>{
                BoundaryConditionId{"bc-east-depth"},
                "east zero gradient depth",
                "east",
                "m",
                ZeroGradientSpecification{}},
            BoundarySpecification<tsunami::core::Real>{
                BoundaryConditionId{"bc-north-depth"},
                "north named depth",
                "north",
                "m",
                NamedBoundarySpecification{"radiation"}},
            BoundarySpecification<tsunami::core::Real>{
                BoundaryConditionId{"bc-west-depth"},
                "west named depth",
                "west",
                "m",
                NamedBoundarySpecification{"relaxation"}},
        };
    }

    [[nodiscard]] inline auto vector_boundary_specs()
        -> std::vector<tsunami::fvm::BoundarySpecification<tsunami::fvm::Vector3>>
    {
        using tsunami::fvm::BoundaryConditionId;
        using tsunami::fvm::BoundarySpecification;
        using tsunami::fvm::FixedValueSpecification;
        using tsunami::fvm::NamedBoundarySpecification;
        using tsunami::fvm::Vector3;
        using tsunami::fvm::ZeroGradientSpecification;

        return {
            BoundarySpecification<Vector3>{
                BoundaryConditionId{"bc-south-velocity"},
                "south fixed velocity",
                "south",
                "m/s",
                FixedValueSpecification<Vector3>{{Vector3{1.0, 0.0, 0.0}}}},
            BoundarySpecification<Vector3>{
                BoundaryConditionId{"bc-east-velocity"},
                "east zero gradient velocity",
                "east",
                "m/s",
                ZeroGradientSpecification{}},
            BoundarySpecification<Vector3>{
                BoundaryConditionId{"bc-north-velocity"},
                "north named velocity",
                "north",
                "m/s",
                NamedBoundarySpecification{"radiation"}},
            BoundarySpecification<Vector3>{
                BoundaryConditionId{"bc-west-velocity"},
                "west named velocity",
                "west",
                "m/s",
                NamedBoundarySpecification{"relaxation"}},
        };
    }

    [[nodiscard]] inline auto multi_face_scalar_boundary_specs()
        -> std::vector<tsunami::fvm::BoundarySpecification<tsunami::core::Real>>
    {
        using tsunami::fvm::BoundaryConditionId;
        using tsunami::fvm::BoundarySpecification;
        using tsunami::fvm::FixedValueSpecification;
        using tsunami::fvm::NamedBoundarySpecification;
        using tsunami::fvm::ZeroGradientSpecification;

        return {
            BoundarySpecification<tsunami::core::Real>{
                BoundaryConditionId{"bc-south-east-depth"},
                "south-east fixed depth",
                "south-east",
                "m",
                FixedValueSpecification<tsunami::core::Real>{{10.0, 20.0}}},
            BoundarySpecification<tsunami::core::Real>{
                BoundaryConditionId{"bc-north-depth"},
                "north zero gradient depth",
                "north",
                "m",
                ZeroGradientSpecification{}},
            BoundarySpecification<tsunami::core::Real>{
                BoundaryConditionId{"bc-west-depth"},
                "west named depth",
                "west",
                "m",
                NamedBoundarySpecification{"relaxation"}},
        };
    }

    [[nodiscard]] inline auto multi_face_vector_boundary_specs()
        -> std::vector<tsunami::fvm::BoundarySpecification<tsunami::fvm::Vector3>>
    {
        using tsunami::fvm::BoundaryConditionId;
        using tsunami::fvm::BoundarySpecification;
        using tsunami::fvm::FixedValueSpecification;
        using tsunami::fvm::NamedBoundarySpecification;
        using tsunami::fvm::Vector3;
        using tsunami::fvm::ZeroGradientSpecification;

        return {
            BoundarySpecification<Vector3>{
                BoundaryConditionId{"bc-south-east-velocity"},
                "south-east fixed velocity",
                "south-east",
                "m/s",
                FixedValueSpecification<Vector3>{{Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0}}}},
            BoundarySpecification<Vector3>{
                BoundaryConditionId{"bc-north-velocity"},
                "north zero gradient velocity",
                "north",
                "m/s",
                ZeroGradientSpecification{}},
            BoundarySpecification<Vector3>{
                BoundaryConditionId{"bc-west-velocity"},
                "west named velocity",
                "west",
                "m/s",
                NamedBoundarySpecification{"relaxation"}},
        };
    }

} // namespace tsunami::tests::fvm
