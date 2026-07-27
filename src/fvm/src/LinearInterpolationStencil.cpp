#include <tsunami/fvm/LinearInterpolationStencil.hpp>

#include <algorithm>
#include <cmath>
#include <string>

#include <tsunami/fvm/Boundary.hpp>

namespace tsunami::fvm
{
    namespace
    {
        [[nodiscard]] auto id_string(tsunami::core::Index value) -> std::string
        {
            return std::to_string(value);
        }
    } // namespace

    auto numerics_detail::mesh_id_from(const FiniteVolumeMesh &mesh) -> MeshId
    {
        return mesh.summary().id;
    }

    auto numerics_detail::numerics_error(
        std::string code,
        std::string message,
        std::string operation,
        const MeshId *mesh_id,
        const FieldDescriptor *field,
        std::optional<FaceId> face_id,
        std::optional<CellId> cell_id,
        std::optional<BoundaryPatchId> patch_id,
        std::optional<BoundaryConditionKind> boundary_kind,
        std::optional<std::size_t> expected_count,
        std::optional<std::size_t> actual_count,
        std::optional<tsunami::core::Real> owner_weight,
        std::optional<tsunami::core::Real> neighbour_weight) -> tsunami::core::Error
    {
        auto error = tsunami::core::Error{
            std::move(code),
            std::move(message),
            tsunami::core::DiagnosticCategory::numerical,
            tsunami::core::Severity::error};
        error.add_context("operation", std::move(operation))
            .add_context("rule_id", numerics_detail::rule_id)
            .add_context("state_changed", "false");
        if (mesh_id != nullptr) {
            error.add_context("mesh_id", mesh_id->value);
        }
        if (field != nullptr) {
            error.add_context("field_id", field->id.value).add_context("field_name", field->name);
        }
        if (face_id) {
            error.add_context("face_id", id_string(face_id->value));
        }
        if (cell_id) {
            error.add_context("cell_id", id_string(cell_id->value));
        }
        if (patch_id) {
            error.add_context("patch_id", id_string(patch_id->value));
        }
        if (boundary_kind) {
            error.add_context("boundary_kind", std::string{to_string(*boundary_kind)});
        }
        if (expected_count) {
            error.add_context("expected_count", std::to_string(*expected_count));
        }
        if (actual_count) {
            error.add_context("actual_count", std::to_string(*actual_count));
        }
        if (owner_weight) {
            error.add_context("owner_weight", std::to_string(*owner_weight));
        }
        if (neighbour_weight) {
            error.add_context("neighbour_weight", std::to_string(*neighbour_weight));
        }
        return error;
    }

    auto make_linear_interpolation_stencil(const FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<LinearInterpolationStencil>
    {
        const auto binding = make_mesh_binding(mesh);
        const auto mesh_id = mesh.summary().id;
        std::vector<InternalFaceInterpolationEntry> entries;
        entries.reserve(mesh.summary().face_count);

        for (const auto &face : mesh.topology().faces()) {
            if (face.is_boundary()) {
                continue;
            }
            if (!face.neighbour) {
                return tsunami::core::failure<LinearInterpolationStencil>(numerics_detail::numerics_error(
                    "fvm.numerics.stencil.neighbour_missing",
                    "internal face is missing a neighbour cell",
                    "make_linear_interpolation_stencil",
                    &mesh_id,
                    nullptr,
                    face.id));
            }
            if (face.owner.value >= mesh.summary().cell_count || face.neighbour->value >= mesh.summary().cell_count) {
                return tsunami::core::failure<LinearInterpolationStencil>(numerics_detail::numerics_error(
                    "fvm.numerics.stencil.face_invalid",
                    "internal face owner or neighbour is outside the mesh",
                    "make_linear_interpolation_stencil",
                    &mesh_id,
                    nullptr,
                    face.id));
            }

            const auto face_centroid = mesh.face_geometry(face.id).centroid;
            const auto owner_centroid = mesh.cell_geometry(face.owner).centroid;
            const auto neighbour_centroid = mesh.cell_geometry(*face.neighbour).centroid;
            const auto owner_distance = numerics_detail::magnitude(numerics_detail::subtract(face_centroid, owner_centroid));
            const auto neighbour_distance = numerics_detail::magnitude(numerics_detail::subtract(face_centroid, neighbour_centroid));
            if (!numerics_detail::is_finite(owner_distance) || !numerics_detail::is_finite(neighbour_distance)) {
                return tsunami::core::failure<LinearInterpolationStencil>(numerics_detail::numerics_error(
                    "fvm.numerics.stencil.distance_nonfinite",
                    "internal face interpolation distance is nonfinite",
                    "make_linear_interpolation_stencil",
                    &mesh_id,
                    nullptr,
                    face.id));
            }

            const auto denominator = owner_distance + neighbour_distance;
            if (!numerics_detail::is_finite(denominator) || denominator <= numerics_detail::tolerance) {
                return tsunami::core::failure<LinearInterpolationStencil>(numerics_detail::numerics_error(
                    "fvm.numerics.stencil.denominator_degenerate",
                    "internal face interpolation denominator is degenerate",
                    "make_linear_interpolation_stencil",
                    &mesh_id,
                    nullptr,
                    face.id));
            }

            const auto owner_weight = neighbour_distance / denominator;
            const auto neighbour_weight = owner_distance / denominator;
            if (!numerics_detail::is_finite(owner_weight) || !numerics_detail::is_finite(neighbour_weight)) {
                return tsunami::core::failure<LinearInterpolationStencil>(numerics_detail::numerics_error(
                    "fvm.numerics.stencil.weight_nonfinite",
                    "internal face interpolation weight is nonfinite",
                    "make_linear_interpolation_stencil",
                    &mesh_id,
                    nullptr,
                    face.id,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    owner_weight,
                    neighbour_weight));
            }
            if (owner_weight < -numerics_detail::tolerance || owner_weight > 1.0 + numerics_detail::tolerance ||
                neighbour_weight < -numerics_detail::tolerance || neighbour_weight > 1.0 + numerics_detail::tolerance) {
                return tsunami::core::failure<LinearInterpolationStencil>(numerics_detail::numerics_error(
                    "fvm.numerics.stencil.weight_out_of_range",
                    "internal face interpolation weight is outside [0,1]",
                    "make_linear_interpolation_stencil",
                    &mesh_id,
                    nullptr,
                    face.id,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    owner_weight,
                    neighbour_weight));
            }
            if (std::abs((owner_weight + neighbour_weight) - 1.0) > numerics_detail::tolerance) {
                return tsunami::core::failure<LinearInterpolationStencil>(numerics_detail::numerics_error(
                    "fvm.numerics.stencil.partition_failed",
                    "internal face interpolation weights do not sum to one",
                    "make_linear_interpolation_stencil",
                    &mesh_id,
                    nullptr,
                    face.id,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    owner_weight,
                    neighbour_weight));
            }

            entries.push_back(InternalFaceInterpolationEntry{
                .face = face.id,
                .owner = face.owner,
                .neighbour = *face.neighbour,
                .owner_weight = owner_weight,
                .neighbour_weight = neighbour_weight});
        }

        std::ranges::sort(entries, {}, &InternalFaceInterpolationEntry::face);
        return tsunami::core::success(LinearInterpolationStencil{binding, std::move(entries)});
    }

} // namespace tsunami::fvm
