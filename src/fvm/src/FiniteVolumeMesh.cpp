#include <tsunami/fvm/FiniteVolumeMesh.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>

#include <tsunami/core/Error.hpp>

namespace tsunami::fvm
{
    namespace
    {
        constexpr auto geometry_tolerance = 1.0e-12;
        constexpr auto operation_name = "make_finite_volume_mesh";
        constexpr auto mesh_rule_id = "SWE-FVM-MSH-WP1";

        [[nodiscard]] auto id_string(tsunami::core::Index value) -> std::string
        {
            return std::to_string(value);
        }

        [[nodiscard]] auto make_error(
            const std::string &mesh_id,
            std::string code,
            std::string message,
            std::string entity_type = {},
            std::string entity_id = {},
            std::string referenced_id = {}) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::numerical,
                tsunami::core::Severity::error};
            error.add_context("operation", operation_name)
                .add_context("rule_id", mesh_rule_id)
                .add_context("mesh_id", mesh_id)
                .add_context("state_changed", "false");
            if (!entity_type.empty()) {
                error.add_context("entity_type", std::move(entity_type));
            }
            if (!entity_id.empty()) {
                error.add_context("entity_id", std::move(entity_id));
            }
            if (!referenced_id.empty()) {
                error.add_context("referenced_id", std::move(referenced_id));
            }
            return error;
        }

        [[nodiscard]] auto subtract(Point3 left, Point3 right) -> Vector3
        {
            return Vector3{left.x - right.x, left.y - right.y, left.z - right.z};
        }

        [[nodiscard]] auto add(Vector3 left, Vector3 right) -> Vector3
        {
            return Vector3{left.x + right.x, left.y + right.y, left.z + right.z};
        }

        [[nodiscard]] auto negate(Vector3 vector) -> Vector3
        {
            return Vector3{-vector.x, -vector.y, -vector.z};
        }

        [[nodiscard]] auto dot(Vector3 left, Vector3 right) -> tsunami::core::Real
        {
            return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
        }

        [[nodiscard]] auto magnitude(Vector3 vector) -> tsunami::core::Real
        {
            return std::sqrt(dot(vector, vector));
        }

        [[nodiscard]] auto edge_vector(const VertexRecord &first, const VertexRecord &second) -> Vector3
        {
            return subtract(second.position, first.position);
        }

        [[nodiscard]] auto midpoint(Point3 first, Point3 second) -> Point3
        {
            return Point3{(first.x + second.x) * 0.5, (first.y + second.y) * 0.5, 0.0};
        }

        [[nodiscard]] auto triangle_area(Point3 first, Point3 second, Point3 third) -> tsunami::core::Real
        {
            return std::abs(
                       ((second.x - first.x) * (third.y - first.y)) -
                       ((third.x - first.x) * (second.y - first.y))) *
                   0.5;
        }

        [[nodiscard]] auto triangle_centroid(Point3 first, Point3 second, Point3 third) -> Point3
        {
            return Point3{(first.x + second.x + third.x) / 3.0, (first.y + second.y + third.y) / 3.0, 0.0};
        }

        [[nodiscard]] auto recovered_cell_vertices(const MeshTopologyInput &input, const CellRecord &cell)
            -> std::vector<VertexId>
        {
            std::vector<VertexId> vertices;
            for (const auto face_id : cell.faces) {
                if (face_id.value >= input.faces.size()) {
                    continue;
                }
                for (const auto vertex_id : input.faces[face_id.value].vertices) {
                    if (std::ranges::find(vertices, vertex_id) == vertices.end()) {
                        vertices.push_back(vertex_id);
                    }
                }
            }
            std::ranges::sort(vertices, {}, &VertexId::value);
            return vertices;
        }

        [[nodiscard]] auto validate_topology(const MeshTopologyInput &input) -> tsunami::core::Result<void>
        {
            if (input.id.value.empty()) {
                return tsunami::core::failure(make_error(input.id.value, "fvm.mesh.id_required", "mesh id is required"));
            }
            if (input.spatial_dimension != 2) {
                return tsunami::core::failure(make_error(
                    input.id.value,
                    "fvm.mesh.invalid_dimension",
                    "Regional2D finite-volume meshes require spatial dimension 2"));
            }
            for (tsunami::core::Index index = 0; index < input.vertices.size(); ++index) {
                const auto &vertex = input.vertices[index];
                if (vertex.id.value != index) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.vertex_id_mismatch",
                        "vertex ids must be contiguous and index-addressable",
                        "vertex",
                        id_string(vertex.id.value),
                        id_string(index)));
                }
                if (std::abs(vertex.position.z) > geometry_tolerance) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.non_planar_point",
                        "Regional2D mesh vertices must lie in the z=0 plane",
                        "vertex",
                        id_string(vertex.id.value)));
                }
            }

            for (tsunami::core::Index index = 0; index < input.faces.size(); ++index) {
                const auto &face = input.faces[index];
                if (face.id.value != index) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.face_id_mismatch",
                        "face ids must be contiguous and index-addressable",
                        "face",
                        id_string(face.id.value),
                        id_string(index)));
                }
                if (face.vertices.size() != 2) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.face_vertex_count_unsupported",
                        "Regional2D faces must be straight two-point edges",
                        "face",
                        id_string(face.id.value)));
                }
                for (const auto vertex_id : face.vertices) {
                    if (vertex_id.value >= input.vertices.size()) {
                        return tsunami::core::failure(make_error(
                            input.id.value,
                            "fvm.mesh.face_vertex_out_of_range",
                            "face references a vertex outside the mesh",
                            "face",
                            id_string(face.id.value),
                            id_string(vertex_id.value)));
                    }
                }
                if (face.owner.value >= input.cells.size()) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.face_owner_out_of_range",
                        "face owner is outside the mesh cell range",
                        "face",
                        id_string(face.id.value),
                        id_string(face.owner.value)));
                }
                if (face.neighbour && face.neighbour->value >= input.cells.size()) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.face_neighbour_out_of_range",
                        "face neighbour is outside the mesh cell range",
                        "face",
                        id_string(face.id.value),
                        id_string(face.neighbour->value)));
                }
                if (face.neighbour && *face.neighbour == face.owner) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.face_owner_neighbour_equal",
                        "face owner and neighbour must differ",
                        "face",
                        id_string(face.id.value),
                        id_string(face.owner.value)));
                }
                if (face.is_internal() && face.boundary_patch.has_value()) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.internal_face_has_patch",
                        "internal faces must not carry boundary patch ids",
                        "face",
                        id_string(face.id.value),
                        id_string(face.boundary_patch->value)));
                }
                if (face.is_boundary() && !face.boundary_patch.has_value()) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.boundary_face_missing_patch",
                        "boundary faces require a boundary patch id",
                        "face",
                        id_string(face.id.value)));
                }
                const auto edge = edge_vector(input.vertices[face.vertices[0].value], input.vertices[face.vertices[1].value]);
                if (magnitude(edge) <= geometry_tolerance) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.edge_degenerate",
                        "face edge length must be non-zero",
                        "face",
                        id_string(face.id.value)));
                }
            }

            for (tsunami::core::Index index = 0; index < input.cells.size(); ++index) {
                const auto &cell = input.cells[index];
                if (cell.id.value != index) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.cell_id_mismatch",
                        "cell ids must be contiguous and index-addressable",
                        "cell",
                        id_string(cell.id.value),
                        id_string(index)));
                }
            }

            for (tsunami::core::Index index = 0; index < input.boundary_patches.size(); ++index) {
                const auto &patch = input.boundary_patches[index];
                if (patch.id.value != index) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.patch_id_mismatch",
                        "boundary patch ids must be contiguous and index-addressable",
                        "boundary_patch",
                        id_string(patch.id.value),
                        id_string(index)));
                }
            }

            std::vector<bool> patch_face_seen(input.faces.size(), false);
            for (const auto &patch : input.boundary_patches) {
                for (const auto face_id : patch.faces) {
                    if (face_id.value >= input.faces.size()) {
                        return tsunami::core::failure(make_error(
                            input.id.value,
                            "fvm.mesh.boundary_face_unassigned",
                            "boundary patch references a face outside the mesh",
                            "boundary_patch",
                            id_string(patch.id.value),
                            id_string(face_id.value)));
                    }
                    if (patch_face_seen[face_id.value]) {
                        return tsunami::core::failure(make_error(
                            input.id.value,
                            "fvm.mesh.patch_face_duplicate",
                            "boundary face appears in more than one patch assignment",
                            "face",
                            id_string(face_id.value),
                            id_string(patch.id.value)));
                    }
                    patch_face_seen[face_id.value] = true;

                    const auto &face = input.faces[face_id.value];
                    if (face.is_internal()) {
                        return tsunami::core::failure(make_error(
                            input.id.value,
                            "fvm.mesh.internal_face_in_patch",
                            "internal faces must not appear in boundary patches",
                            "face",
                            id_string(face.id.value),
                            id_string(patch.id.value)));
                    }
                    if (!face.boundary_patch || *face.boundary_patch != patch.id) {
                        return tsunami::core::failure(make_error(
                            input.id.value,
                            "fvm.mesh.boundary_face_unassigned",
                            "boundary patch membership must match the face boundary_patch id",
                            "face",
                            id_string(face.id.value),
                            id_string(patch.id.value)));
                    }
                }
            }
            for (const auto &face : input.faces) {
                if (face.is_boundary() && !patch_face_seen[face.id.value]) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.boundary_face_unassigned",
                        "boundary face must appear in its named boundary patch",
                        "face",
                        id_string(face.id.value),
                        id_string(face.boundary_patch->value)));
                }
            }

            for (const auto &cell : input.cells) {
                if (cell.faces.size() != 3) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.cell_face_count_unsupported",
                        "Regional2D cells must reference exactly three faces",
                        "cell",
                        id_string(cell.id.value)));
                }

                std::set<tsunami::core::Index> seen_faces;
                for (const auto face_id : cell.faces) {
                    if (!seen_faces.insert(face_id.value).second) {
                        return tsunami::core::failure(make_error(
                            input.id.value,
                            "fvm.mesh.cell_face_duplicate",
                            "cell contains a duplicate face reference",
                            "cell",
                            id_string(cell.id.value),
                            id_string(face_id.value)));
                    }
                    if (face_id.value >= input.faces.size()) {
                        return tsunami::core::failure(make_error(
                            input.id.value,
                            "fvm.mesh.cell_face_membership_invalid",
                            "cell references a face outside the mesh",
                            "cell",
                            id_string(cell.id.value),
                            id_string(face_id.value)));
                    }
                    const auto &face = input.faces[face_id.value];
                    if (face.owner != cell.id && (!face.neighbour || *face.neighbour != cell.id)) {
                        return tsunami::core::failure(make_error(
                            input.id.value,
                            "fvm.mesh.cell_face_membership_invalid",
                            "cell-face membership must match owner/neighbour addressing",
                            "cell",
                            id_string(cell.id.value),
                            id_string(face_id.value)));
                    }
                }

                const auto vertices = recovered_cell_vertices(input, cell);
                if (vertices.size() != 3) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.cell_vertex_count_unsupported",
                        "Regional2D cell faces must recover exactly three unique vertices",
                        "cell",
                        id_string(cell.id.value)));
                }
                const auto area = triangle_area(
                    input.vertices[vertices[0].value].position,
                    input.vertices[vertices[1].value].position,
                    input.vertices[vertices[2].value].position);
                if (area <= geometry_tolerance) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.cell_degenerate",
                        "Regional2D cell triangle area must be non-zero",
                        "cell",
                        id_string(cell.id.value)));
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto derive_cell_geometry(const MeshTopologyInput &input) -> std::vector<CellGeometry>
        {
            std::vector<CellGeometry> cells;
            cells.reserve(input.cells.size());
            for (const auto &cell : input.cells) {
                const auto vertices = recovered_cell_vertices(input, cell);
                const auto first = input.vertices[vertices[0].value].position;
                const auto second = input.vertices[vertices[1].value].position;
                const auto third = input.vertices[vertices[2].value].position;
                cells.push_back(CellGeometry{.centroid = triangle_centroid(first, second, third), .measure = triangle_area(first, second, third)});
            }
            return cells;
        }

        [[nodiscard]] auto oriented_area_vector(
            const MeshTopologyInput &input,
            const std::vector<CellGeometry> &cell_geometry,
            const FaceRecord &face) -> tsunami::core::Result<Vector3>
        {
            const auto &first = input.vertices[face.vertices[0].value];
            const auto &second = input.vertices[face.vertices[1].value];
            const auto edge = edge_vector(first, second);
            auto area = Vector3{edge.y, -edge.x, 0.0};
            const auto face_centroid = midpoint(first.position, second.position);
            const auto owner_centroid = cell_geometry[face.owner.value].centroid;
            const auto target = face.neighbour
                                    ? subtract(cell_geometry[face.neighbour->value].centroid, owner_centroid)
                                    : subtract(face_centroid, owner_centroid);
            const auto alignment = dot(area, target);
            const auto scale = std::max({magnitude(area), magnitude(target), tsunami::core::Real{1.0}});
            if (std::abs(alignment) <= geometry_tolerance * scale) {
                return tsunami::core::failure<Vector3>(make_error(
                    input.id.value,
                    "fvm.mesh.face_orientation_ambiguous",
                    "face area vector cannot be oriented unambiguously",
                    "face",
                    id_string(face.id.value)));
            }
            if (alignment < 0.0) {
                area = negate(area);
            }
            return tsunami::core::success(area);
        }

        [[nodiscard]] auto derive_face_geometry(
            const MeshTopologyInput &input,
            const std::vector<CellGeometry> &cell_geometry) -> tsunami::core::Result<std::vector<FaceGeometry>>
        {
            std::vector<FaceGeometry> faces;
            faces.reserve(input.faces.size());
            for (const auto &face : input.faces) {
                const auto &first = input.vertices[face.vertices[0].value];
                const auto &second = input.vertices[face.vertices[1].value];
                auto area = oriented_area_vector(input, cell_geometry, face);
                if (!area) {
                    return tsunami::core::failure<std::vector<FaceGeometry>>(area.error());
                }
                faces.push_back(FaceGeometry{.centroid = midpoint(first.position, second.position), .area_vector = std::move(area).value()});
            }
            return tsunami::core::success(std::move(faces));
        }

        [[nodiscard]] auto validate_closure(
            const MeshTopologyInput &input,
            const std::vector<FaceGeometry> &face_geometry) -> tsunami::core::Result<void>
        {
            for (const auto &cell : input.cells) {
                auto sum = Vector3{};
                auto perimeter = tsunami::core::Real{};
                for (const auto face_id : cell.faces) {
                    const auto &face = input.faces[face_id.value];
                    const auto &geometry = face_geometry[face_id.value];
                    const auto contribution = face.owner == cell.id ? geometry.area_vector : negate(geometry.area_vector);
                    sum = add(sum, contribution);
                    perimeter += magnitude(geometry.area_vector);
                }
                const auto tolerance = geometry_tolerance * std::max(perimeter, tsunami::core::Real{1.0}) * 16.0;
                if (magnitude(sum) > tolerance) {
                    return tsunami::core::failure(make_error(
                        input.id.value,
                        "fvm.mesh.cell_closure_failed",
                        "cell oriented face-area vectors must close",
                        "cell",
                        id_string(cell.id.value)));
                }
            }
            return tsunami::core::success();
        }
    } // namespace

    FiniteVolumeMesh::FiniteVolumeMesh(MeshTopology topology, MeshGeometry geometry)
        : topology_{std::move(topology)}
        , geometry_{std::move(geometry)}
    {
    }

    auto FiniteVolumeMesh::topology() const noexcept -> const MeshTopology &
    {
        return topology_;
    }

    auto FiniteVolumeMesh::geometry() const noexcept -> const MeshGeometry &
    {
        return geometry_;
    }

    auto FiniteVolumeMesh::summary() const -> MeshSummary
    {
        return topology_.summary();
    }

    auto FiniteVolumeMesh::vertex(VertexId id) const -> const VertexRecord &
    {
        return topology_.vertex(id);
    }

    auto FiniteVolumeMesh::face(FaceId id) const -> const FaceRecord &
    {
        return topology_.face(id);
    }

    auto FiniteVolumeMesh::cell(CellId id) const -> const CellRecord &
    {
        return topology_.cell(id);
    }

    auto FiniteVolumeMesh::boundary_patch(BoundaryPatchId id) const -> const BoundaryPatchRecord &
    {
        return topology_.boundary_patch(id);
    }

    auto FiniteVolumeMesh::face_geometry(FaceId id) const -> const FaceGeometry &
    {
        return geometry_.face(id);
    }

    auto FiniteVolumeMesh::cell_geometry(CellId id) const -> const CellGeometry &
    {
        return geometry_.cell(id);
    }

    auto make_finite_volume_mesh(MeshTopologyInput input) -> tsunami::core::Result<FiniteVolumeMesh>
    {
        auto validation = validate_topology(input);
        if (!validation) {
            return tsunami::core::failure<FiniteVolumeMesh>(validation.error());
        }

        auto cell_geometry = derive_cell_geometry(input);
        auto face_geometry = derive_face_geometry(input, cell_geometry);
        if (!face_geometry) {
            return tsunami::core::failure<FiniteVolumeMesh>(face_geometry.error());
        }

        auto closure = validate_closure(input, face_geometry.value());
        if (!closure) {
            return tsunami::core::failure<FiniteVolumeMesh>(closure.error());
        }

        auto topology = MeshTopology{
            std::move(input.id),
            input.spatial_dimension,
            std::move(input.vertices),
            std::move(input.faces),
            std::move(input.cells),
            std::move(input.boundary_patches)};
        auto geometry = MeshGeometry{std::move(face_geometry).value(), std::move(cell_geometry)};
        return tsunami::core::success(FiniteVolumeMesh{std::move(topology), std::move(geometry)});
    }

} // namespace tsunami::fvm
