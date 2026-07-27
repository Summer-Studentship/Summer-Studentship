#include <tsunami/fvm/GreenGaussGradient.hpp>

#include <algorithm>

namespace tsunami::fvm
{

    auto make_green_gauss_gradient_workspace(const FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<GreenGaussGradientWorkspace>
    {
        return tsunami::core::success(GreenGaussGradientWorkspace{
            make_mesh_binding(mesh),
            std::vector<Vector3>(mesh.summary().cell_count, Vector3{})});
    }

    auto green_gauss_gradient(
        const FiniteVolumeMesh &mesh,
        const FaceScalarField &face_values,
        CellVectorField &destination,
        GreenGaussGradientWorkspace &workspace) -> tsunami::core::Result<void>
    {
        const auto mesh_id = numerics_detail::mesh_id_from(mesh);
        const auto source_descriptor = face_values.descriptor();
        const auto destination_descriptor = destination.descriptor();
        if (face_values.binding() != make_mesh_binding(mesh) || face_values.size() != mesh.summary().face_count) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.gradient.source_incompatible",
                "face scalar field is not compatible with the supplied mesh",
                "green_gauss_gradient",
                &mesh_id,
                &source_descriptor,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                mesh.summary().face_count,
                face_values.size()));
        }
        if (destination.binding() != make_mesh_binding(mesh) || destination.size() != mesh.summary().cell_count ||
            destination_descriptor.unit_id.empty()) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.gradient.destination_incompatible",
                "cell vector destination is not compatible with the supplied mesh",
                "green_gauss_gradient",
                &mesh_id,
                &destination_descriptor,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                mesh.summary().cell_count,
                destination.size()));
        }
        if (!workspace.is_bound_to(mesh) || workspace.staging_values().size() != mesh.summary().cell_count) {
            return tsunami::core::failure(numerics_detail::numerics_error(
                "fvm.numerics.gradient.workspace_incompatible",
                "gradient workspace is not compatible with the supplied mesh",
                "green_gauss_gradient",
                &mesh_id));
        }

        for (const auto &cell_geometry : mesh.geometry().cells()) {
            if (!numerics_detail::is_finite(cell_geometry.measure) || cell_geometry.measure <= 0.0) {
                return tsunami::core::failure(numerics_detail::numerics_error(
                    "fvm.numerics.gradient.cell_measure_invalid",
                    "cell measure must be finite and positive",
                    "green_gauss_gradient",
                    &mesh_id));
            }
        }
        for (const auto &face_geometry : mesh.geometry().faces()) {
            if (!numerics_detail::is_finite(face_geometry.area_vector)) {
                return tsunami::core::failure(numerics_detail::numerics_error(
                    "fvm.numerics.gradient.face_geometry_invalid",
                    "face area vector must be finite",
                    "green_gauss_gradient",
                    &mesh_id));
            }
        }
        for (std::size_t index = 0; index < face_values.size(); ++index) {
            if (!numerics_detail::is_finite(face_values.at(index))) {
                return tsunami::core::failure(numerics_detail::numerics_error(
                    "fvm.numerics.gradient.source_nonfinite",
                    "face scalar value must be finite",
                    "green_gauss_gradient",
                    &mesh_id,
                    &source_descriptor,
                    FaceId{index}));
            }
        }

        std::ranges::fill(workspace.staging_values(), Vector3{});
        for (const auto &cell : mesh.topology().cells()) {
            auto sum = Vector3{};
            for (const auto face_id : cell.faces) {
                const auto &face = mesh.face(face_id);
                const auto sign = face.owner == cell.id ? 1.0 : -1.0;
                const auto phi = face_values.at(face_id.value);
                const auto contribution = numerics_detail::scale(mesh.face_geometry(face_id).area_vector, sign * phi);
                sum = numerics_detail::add(sum, contribution);
            }
            const auto measure = mesh.cell_geometry(cell.id).measure;
            workspace.staging_values()[cell.id.value] = Vector3{sum.x / measure, sum.y / measure, 0.0};
        }

        for (std::size_t index = 0; index < workspace.staging_values().size(); ++index) {
            destination.at(index) = workspace.staging_values()[index];
        }
        return tsunami::core::success();
    }

} // namespace tsunami::fvm
