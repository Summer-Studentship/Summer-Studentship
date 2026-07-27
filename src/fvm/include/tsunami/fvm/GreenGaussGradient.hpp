#pragma once

#include <span>
#include <utility>
#include <vector>

#include <tsunami/fvm/LinearInterpolationStencil.hpp>
#include <tsunami/fvm/MeshField.hpp>

namespace tsunami::fvm
{

    class GreenGaussGradientWorkspace
    {
    public:
        GreenGaussGradientWorkspace(const GreenGaussGradientWorkspace &) = delete;
        auto operator=(const GreenGaussGradientWorkspace &) -> GreenGaussGradientWorkspace & = delete;
        GreenGaussGradientWorkspace(GreenGaussGradientWorkspace &&) noexcept = default;
        auto operator=(GreenGaussGradientWorkspace &&) noexcept -> GreenGaussGradientWorkspace & = default;

        [[nodiscard]] auto binding() const noexcept -> const MeshBinding & { return binding_; }
        [[nodiscard]] auto staging_values() noexcept -> std::span<Vector3> { return staging_; }
        [[nodiscard]] auto staging_values() const noexcept -> std::span<const Vector3> { return staging_; }

        [[nodiscard]] auto is_bound_to(const FiniteVolumeMesh &mesh) const -> bool
        {
            return binding_ == make_mesh_binding(mesh);
        }

    private:
        friend auto make_green_gauss_gradient_workspace(const FiniteVolumeMesh &mesh)
            -> tsunami::core::Result<GreenGaussGradientWorkspace>;

        GreenGaussGradientWorkspace(MeshBinding binding, std::vector<Vector3> staging)
            : binding_{std::move(binding)}
            , staging_{std::move(staging)}
        {
        }

        MeshBinding binding_;
        std::vector<Vector3> staging_;
    };

    [[nodiscard]] auto make_green_gauss_gradient_workspace(const FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<GreenGaussGradientWorkspace>;

    auto green_gauss_gradient(
        const FiniteVolumeMesh &mesh,
        const FaceScalarField &face_values,
        CellVectorField &destination,
        GreenGaussGradientWorkspace &workspace) -> tsunami::core::Result<void>;

} // namespace tsunami::fvm
