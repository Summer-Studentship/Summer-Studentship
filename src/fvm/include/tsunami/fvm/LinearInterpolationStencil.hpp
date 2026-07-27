#pragma once

#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <tsunami/core/Error.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/fvm/Boundary.hpp>
#include <tsunami/fvm/Field.hpp>
#include <tsunami/fvm/FiniteVolumeMesh.hpp>
#include <tsunami/fvm/MeshBinding.hpp>

namespace tsunami::fvm
{

    struct InternalFaceInterpolationEntry
    {
        FaceId face;
        CellId owner;
        CellId neighbour;
        tsunami::core::Real owner_weight{};
        tsunami::core::Real neighbour_weight{};
    };

    class LinearInterpolationStencil
    {
    public:
        LinearInterpolationStencil(const LinearInterpolationStencil &) = delete;
        auto operator=(const LinearInterpolationStencil &) -> LinearInterpolationStencil & = delete;
        LinearInterpolationStencil(LinearInterpolationStencil &&) noexcept = default;
        auto operator=(LinearInterpolationStencil &&) noexcept -> LinearInterpolationStencil & = default;

        [[nodiscard]] auto binding() const noexcept -> const MeshBinding & { return binding_; }
        [[nodiscard]] auto entries() const noexcept -> std::span<const InternalFaceInterpolationEntry> { return entries_; }
        [[nodiscard]] auto size() const noexcept -> std::size_t { return entries_.size(); }
        [[nodiscard]] auto empty() const noexcept -> bool { return entries_.empty(); }

        [[nodiscard]] auto is_bound_to(const FiniteVolumeMesh &mesh) const -> bool
        {
            return binding_ == make_mesh_binding(mesh);
        }

    private:
        friend auto make_linear_interpolation_stencil(const FiniteVolumeMesh &mesh)
            -> tsunami::core::Result<LinearInterpolationStencil>;

        LinearInterpolationStencil(MeshBinding binding, std::vector<InternalFaceInterpolationEntry> entries)
            : binding_{std::move(binding)}
            , entries_{std::move(entries)}
        {
        }

        MeshBinding binding_;
        std::vector<InternalFaceInterpolationEntry> entries_;
    };

    namespace numerics_detail
    {
        inline constexpr auto rule_id = "SWE-FVM-NUM-WP1";
        inline constexpr auto tolerance = 1.0e-12;

        [[nodiscard]] inline auto is_finite(tsunami::core::Real value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] inline auto is_finite(Point3 value) -> bool
        {
            return is_finite(value.x) && is_finite(value.y) && is_finite(value.z);
        }

        [[nodiscard]] inline auto is_finite(Vector3 value) -> bool
        {
            return is_finite(value.x) && is_finite(value.y) && is_finite(value.z);
        }

        [[nodiscard]] inline auto subtract(Point3 left, Point3 right) -> Vector3
        {
            return Vector3{left.x - right.x, left.y - right.y, left.z - right.z};
        }

        [[nodiscard]] inline auto magnitude(Vector3 value) -> tsunami::core::Real
        {
            return std::sqrt((value.x * value.x) + (value.y * value.y) + (value.z * value.z));
        }

        [[nodiscard]] inline auto add(Vector3 left, Vector3 right) -> Vector3
        {
            return Vector3{left.x + right.x, left.y + right.y, left.z + right.z};
        }

        [[nodiscard]] inline auto scale(Vector3 value, tsunami::core::Real factor) -> Vector3
        {
            return Vector3{value.x * factor, value.y * factor, value.z * factor};
        }

        template <class Value>
        [[nodiscard]] inline auto interpolate_value(
            const Value &owner,
            const Value &neighbour,
            tsunami::core::Real owner_weight,
            tsunami::core::Real neighbour_weight) -> Value
        {
            if constexpr (std::is_same_v<Value, tsunami::core::Real>) {
                return (owner_weight * owner) + (neighbour_weight * neighbour);
            } else {
                return Vector3{
                    (owner_weight * owner.x) + (neighbour_weight * neighbour.x),
                    (owner_weight * owner.y) + (neighbour_weight * neighbour.y),
                    (owner_weight * owner.z) + (neighbour_weight * neighbour.z)};
            }
        }

        [[nodiscard]] auto numerics_error(
            std::string code,
            std::string message,
            std::string operation,
            const MeshId *mesh_id = nullptr,
            const FieldDescriptor *field = nullptr,
            std::optional<FaceId> face_id = std::nullopt,
            std::optional<CellId> cell_id = std::nullopt,
            std::optional<BoundaryPatchId> patch_id = std::nullopt,
            std::optional<BoundaryConditionKind> boundary_kind = std::nullopt,
            std::optional<std::size_t> expected_count = std::nullopt,
            std::optional<std::size_t> actual_count = std::nullopt,
            std::optional<tsunami::core::Real> owner_weight = std::nullopt,
            std::optional<tsunami::core::Real> neighbour_weight = std::nullopt) -> tsunami::core::Error;

        [[nodiscard]] auto mesh_id_from(const FiniteVolumeMesh &mesh) -> MeshId;
    } // namespace numerics_detail

    [[nodiscard]] auto make_linear_interpolation_stencil(const FiniteVolumeMesh &mesh)
        -> tsunami::core::Result<LinearInterpolationStencil>;

} // namespace tsunami::fvm
