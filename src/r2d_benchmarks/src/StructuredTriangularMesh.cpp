#include <tsunami/r2d_benchmarks/StructuredTriangularMesh.hpp>

#include <array>
#include <cmath>
#include <map>

namespace tsunami::r2d_benchmarks
{
    namespace
    {
        enum PatchIndex : tsunami::core::Index
        {
            left = 0U,
            right = 1U,
            bottom = 2U,
            top = 3U
        };

        [[nodiscard]] auto vertex_index(std::size_t column, std::size_t row, std::size_t columns) -> tsunami::core::Index
        {
            return row * (columns + 1U) + column;
        }

        [[nodiscard]] auto boundary_patch_for(
            tsunami::fvm::VertexId first,
            tsunami::fvm::VertexId second,
            std::size_t columns,
            std::size_t rows) -> std::optional<tsunami::fvm::BoundaryPatchId>
        {
            const auto first_column = first.value % (columns + 1U);
            const auto first_row = first.value / (columns + 1U);
            const auto second_column = second.value % (columns + 1U);
            const auto second_row = second.value / (columns + 1U);
            if (first_column == 0U && second_column == 0U) {
                return tsunami::fvm::BoundaryPatchId{PatchIndex::left};
            }
            if (first_column == columns && second_column == columns) {
                return tsunami::fvm::BoundaryPatchId{PatchIndex::right};
            }
            if (first_row == 0U && second_row == 0U) {
                return tsunami::fvm::BoundaryPatchId{PatchIndex::bottom};
            }
            if (first_row == rows && second_row == rows) {
                return tsunami::fvm::BoundaryPatchId{PatchIndex::top};
            }
            return std::nullopt;
        }
    } // namespace

    auto make_structured_triangular_mesh(StructuredTriangularMeshSpec spec)
        -> tsunami::core::Result<tsunami::fvm::FiniteVolumeMesh>
    {
        if (spec.id.empty() || spec.cell_columns == 0U || spec.cell_rows == 0U ||
            !std::isfinite(spec.length_x) || !std::isfinite(spec.length_y) ||
            spec.length_x <= 0.0 || spec.length_y <= 0.0) {
            return tsunami::core::failure<tsunami::fvm::FiniteVolumeMesh>(tsunami::core::Error{
                "r2d.benchmark.mesh.invalid",
                "structured triangular benchmark mesh specification is invalid"});
        }

        std::vector<tsunami::fvm::VertexRecord> vertices;
        vertices.reserve((spec.cell_columns + 1U) * (spec.cell_rows + 1U));
        for (std::size_t row = 0; row <= spec.cell_rows; ++row) {
            for (std::size_t column = 0; column <= spec.cell_columns; ++column) {
                vertices.push_back(tsunami::fvm::VertexRecord{
                    tsunami::fvm::VertexId{vertices.size()},
                    tsunami::fvm::Point3{
                        (spec.length_x * static_cast<tsunami::core::Real>(column)) / static_cast<tsunami::core::Real>(spec.cell_columns),
                        (spec.length_y * static_cast<tsunami::core::Real>(row)) / static_cast<tsunami::core::Real>(spec.cell_rows),
                        0.0}});
            }
        }

        std::vector<tsunami::fvm::FaceRecord> faces;
        std::vector<tsunami::fvm::CellRecord> cells;
        std::array<std::vector<tsunami::fvm::FaceId>, 4> patch_faces;
        std::map<std::pair<tsunami::core::Index, tsunami::core::Index>, tsunami::fvm::FaceId> face_by_edge;

        auto add_face = [&](tsunami::fvm::VertexId first, tsunami::fvm::VertexId second, tsunami::fvm::CellId owner) {
            const auto key = std::minmax(first.value, second.value);
            const auto found = face_by_edge.find(key);
            if (found != face_by_edge.end()) {
                faces[found->second.value].neighbour = owner;
                faces[found->second.value].boundary_patch = std::nullopt;
                return found->second;
            }
            const auto patch_id = boundary_patch_for(first, second, spec.cell_columns, spec.cell_rows);
            const auto face_id = tsunami::fvm::FaceId{faces.size()};
            faces.push_back(tsunami::fvm::FaceRecord{
                face_id,
                {first, second},
                owner,
                std::nullopt,
                patch_id});
            face_by_edge.emplace(key, face_id);
            if (patch_id) {
                patch_faces[patch_id->value].push_back(face_id);
            }
            return face_id;
        };

        auto add_cell = [&](tsunami::fvm::VertexId a, tsunami::fvm::VertexId b, tsunami::fvm::VertexId c) {
            const auto cell_id = tsunami::fvm::CellId{cells.size()};
            cells.push_back(tsunami::fvm::CellRecord{cell_id, {}});
            cells.back().faces.push_back(add_face(a, b, cell_id));
            cells.back().faces.push_back(add_face(b, c, cell_id));
            cells.back().faces.push_back(add_face(c, a, cell_id));
        };

        for (std::size_t row = 0; row < spec.cell_rows; ++row) {
            for (std::size_t column = 0; column < spec.cell_columns; ++column) {
                const auto v00 = tsunami::fvm::VertexId{vertex_index(column, row, spec.cell_columns)};
                const auto v10 = tsunami::fvm::VertexId{vertex_index(column + 1U, row, spec.cell_columns)};
                const auto v01 = tsunami::fvm::VertexId{vertex_index(column, row + 1U, spec.cell_columns)};
                const auto v11 = tsunami::fvm::VertexId{vertex_index(column + 1U, row + 1U, spec.cell_columns)};
                add_cell(v00, v10, v11);
                add_cell(v00, v11, v01);
            }
        }

        std::vector<tsunami::fvm::BoundaryPatchRecord> patches{
            tsunami::fvm::BoundaryPatchRecord{tsunami::fvm::BoundaryPatchId{PatchIndex::left}, "left", patch_faces[PatchIndex::left]},
            tsunami::fvm::BoundaryPatchRecord{tsunami::fvm::BoundaryPatchId{PatchIndex::right}, "right", patch_faces[PatchIndex::right]},
            tsunami::fvm::BoundaryPatchRecord{tsunami::fvm::BoundaryPatchId{PatchIndex::bottom}, "bottom", patch_faces[PatchIndex::bottom]},
            tsunami::fvm::BoundaryPatchRecord{tsunami::fvm::BoundaryPatchId{PatchIndex::top}, "top", patch_faces[PatchIndex::top]}};

        return tsunami::fvm::make_finite_volume_mesh(tsunami::fvm::MeshTopologyInput{
            tsunami::fvm::MeshId{std::move(spec.id)},
            2U,
            std::move(vertices),
            std::move(faces),
            std::move(cells),
            std::move(patches)});
    }

} // namespace tsunami::r2d_benchmarks
