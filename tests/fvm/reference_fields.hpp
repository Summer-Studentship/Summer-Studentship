#pragma once

#include <optional>
#include <vector>

#include <tsunami/fvm/BoundaryPatchField.hpp>
#include <tsunami/fvm/FiniteVolumeMesh.hpp>
#include <tsunami/fvm/MeshField.hpp>

#include "reference_mesh.hpp"

namespace tsunami::tests::fvm
{

    [[nodiscard]] inline auto reference_mesh() -> tsunami::fvm::FiniteVolumeMesh
    {
        return tsunami::fvm::make_finite_volume_mesh(two_triangle_unit_square_input()).value();
    }

    [[nodiscard]] inline auto multi_face_patch_input() -> tsunami::fvm::MeshTopologyInput
    {
        auto input = two_triangle_unit_square_input();
        input.id.value = "unit-square-two-triangles-multi-face-patch";
        input.faces[0].boundary_patch = tsunami::fvm::BoundaryPatchId{0};
        input.faces[1].boundary_patch = tsunami::fvm::BoundaryPatchId{0};
        input.faces[3].boundary_patch = tsunami::fvm::BoundaryPatchId{1};
        input.faces[4].boundary_patch = tsunami::fvm::BoundaryPatchId{2};
        input.boundary_patches = {
            tsunami::fvm::BoundaryPatchRecord{tsunami::fvm::BoundaryPatchId{0}, "south-east", {tsunami::fvm::FaceId{0}, tsunami::fvm::FaceId{1}}},
            tsunami::fvm::BoundaryPatchRecord{tsunami::fvm::BoundaryPatchId{1}, "north", {tsunami::fvm::FaceId{3}}},
            tsunami::fvm::BoundaryPatchRecord{tsunami::fvm::BoundaryPatchId{2}, "west", {tsunami::fvm::FaceId{4}}},
        };
        return input;
    }

    [[nodiscard]] inline auto multi_face_patch_mesh() -> tsunami::fvm::FiniteVolumeMesh
    {
        return tsunami::fvm::make_finite_volume_mesh(multi_face_patch_input()).value();
    }

    [[nodiscard]] inline auto sample_cell_scalar_field(const tsunami::fvm::FiniteVolumeMesh &mesh)
    {
        return tsunami::fvm::make_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::cell>(
            mesh,
            tsunami::fvm::FieldId{"cell-scalar"},
            "cell scalar",
            "m",
            {1.0, 2.0});
    }

    [[nodiscard]] inline auto sample_face_scalar_field(const tsunami::fvm::FiniteVolumeMesh &mesh)
    {
        return tsunami::fvm::make_mesh_field<tsunami::core::Real, tsunami::fvm::FieldLocation::face>(
            mesh,
            tsunami::fvm::FieldId{"face-scalar"},
            "face scalar",
            "m2/s",
            {0.0, 1.0, 2.0, 3.0, 4.0});
    }

    [[nodiscard]] inline auto sample_cell_vector_field(const tsunami::fvm::FiniteVolumeMesh &mesh)
    {
        std::vector<tsunami::fvm::Vector3> values;
        for (const auto &cell : mesh.geometry().cells()) {
            values.push_back(tsunami::fvm::Vector3{cell.centroid.x, cell.centroid.y, cell.centroid.z});
        }
        return tsunami::fvm::make_mesh_field<tsunami::fvm::Vector3, tsunami::fvm::FieldLocation::cell>(
            mesh,
            tsunami::fvm::FieldId{"cell-vector"},
            "cell vector",
            "m/s",
            std::move(values));
    }

    [[nodiscard]] inline auto sample_face_vector_field(const tsunami::fvm::FiniteVolumeMesh &mesh)
    {
        std::vector<tsunami::fvm::Vector3> values;
        for (const auto &face : mesh.geometry().faces()) {
            values.push_back(face.area_vector);
        }
        return tsunami::fvm::make_mesh_field<tsunami::fvm::Vector3, tsunami::fvm::FieldLocation::face>(
            mesh,
            tsunami::fvm::FieldId{"face-vector"},
            "face vector",
            "m",
            std::move(values));
    }

    [[nodiscard]] inline auto sample_patch_scalar_field(const tsunami::fvm::FiniteVolumeMesh &mesh)
    {
        return tsunami::fvm::make_boundary_patch_field<tsunami::core::Real>(
            mesh,
            tsunami::fvm::BoundaryPatchId{0},
            tsunami::fvm::FieldId{"patch-scalar"},
            "patch scalar",
            "m",
            {10.0, 20.0});
    }

    [[nodiscard]] inline auto sample_patch_vector_field(const tsunami::fvm::FiniteVolumeMesh &mesh)
    {
        return tsunami::fvm::make_boundary_patch_field<tsunami::fvm::Vector3>(
            mesh,
            tsunami::fvm::BoundaryPatchId{0},
            tsunami::fvm::FieldId{"patch-vector"},
            "patch vector",
            "m",
            {mesh.face_geometry(tsunami::fvm::FaceId{0}).area_vector, mesh.face_geometry(tsunami::fvm::FaceId{1}).area_vector});
    }

} // namespace tsunami::tests::fvm
