#pragma once

#include <string>

#include <tsunami/fvm/FiniteVolumeMesh.hpp>

namespace tsunami::r2d_benchmarks
{
    struct StructuredTriangularMeshSpec
    {
        std::string id{"regional-structured-triangles"};
        std::size_t cell_columns{4};
        std::size_t cell_rows{2};
        tsunami::core::Real length_x{1.0};
        tsunami::core::Real length_y{1.0};
    };

    [[nodiscard]] auto make_structured_triangular_mesh(StructuredTriangularMeshSpec spec)
        -> tsunami::core::Result<tsunami::fvm::FiniteVolumeMesh>;

} // namespace tsunami::r2d_benchmarks
