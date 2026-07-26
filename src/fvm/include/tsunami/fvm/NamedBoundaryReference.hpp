#pragma once

#include <string>
#include <utility>

#include <tsunami/fvm/BoundaryPatchField.hpp>
#include <tsunami/fvm/BoundarySpecification.hpp>
#include <tsunami/fvm/MeshField.hpp>

namespace tsunami::fvm
{

    template <SupportedFieldValue Value>
    class NamedBoundaryReference
    {
    public:
        NamedBoundaryReference(BoundaryDescriptor descriptor, MeshBinding binding, std::string requested_type)
            : descriptor_{std::move(descriptor)}
            , binding_{std::move(binding)}
            , requested_type_{std::move(requested_type)}
        {
        }

        NamedBoundaryReference(const NamedBoundaryReference &) = delete;
        auto operator=(const NamedBoundaryReference &) -> NamedBoundaryReference & = delete;
        NamedBoundaryReference(NamedBoundaryReference &&) noexcept = default;
        auto operator=(NamedBoundaryReference &&) noexcept -> NamedBoundaryReference & = default;

        [[nodiscard]] auto descriptor() const -> BoundaryDescriptor { return descriptor_; }
        [[nodiscard]] auto binding() const noexcept -> const MeshBinding & { return binding_; }
        [[nodiscard]] auto patch_id() const noexcept -> BoundaryPatchId { return descriptor_.patch_id; }
        [[nodiscard]] auto requested_type() const noexcept -> std::string_view { return requested_type_; }

        [[nodiscard]] auto clone() const -> NamedBoundaryReference
        {
            return NamedBoundaryReference{descriptor_, binding_, requested_type_};
        }

        [[nodiscard]] auto is_bound_to(const FiniteVolumeMesh &mesh) const -> bool
        {
            return binding_ == make_mesh_binding(mesh);
        }

        auto apply(
            const MeshField<Value, FieldLocation::cell> &,
            BoundaryPatchField<Value> &) const -> tsunami::core::Result<void>
        {
            return tsunami::core::failure(boundary_detail::boundary_error(
                "fvm.boundary.named_condition_not_executable",
                "named boundary reference is a metadata placeholder and cannot be executed",
                &descriptor_,
                "apply",
                {},
                std::nullopt,
                std::nullopt,
                requested_type_));
        }

    private:
        BoundaryDescriptor descriptor_;
        MeshBinding binding_;
        std::string requested_type_;
    };

} // namespace tsunami::fvm
