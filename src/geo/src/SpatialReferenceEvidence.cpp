#include <tsunami/geo/SpatialReferenceEvidence.hpp>

namespace tsunami::geo
{
    auto to_string(DatumReferenceComponent component) noexcept -> std::string_view
    {
        switch (component) {
        case DatumReferenceComponent::horizontal:
            return "horizontal";
        case DatumReferenceComponent::vertical:
            return "vertical";
        }
        return "unknown";
    }

    auto to_string(DatumReferenceKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case DatumReferenceKind::geodetic_datum:
            return "geodetic_datum";
        case DatumReferenceKind::ellipsoidal_height:
            return "ellipsoidal_height";
        case DatumReferenceKind::orthometric_height:
            return "orthometric_height";
        case DatumReferenceKind::mean_sea_level:
            return "mean_sea_level";
        case DatumReferenceKind::hydrographic_chart_datum:
            return "hydrographic_chart_datum";
        case DatumReferenceKind::tide_gauge_reference:
            return "tide_gauge_reference";
        case DatumReferenceKind::model_assumption:
            return "model_assumption";
        case DatumReferenceKind::unknown:
            return "unknown";
        }
        return "unknown";
    }

    auto to_string(DatumEvidenceOrigin origin) noexcept -> std::string_view
    {
        switch (origin) {
        case DatumEvidenceOrigin::embedded_asset_metadata:
            return "embedded_asset_metadata";
        case DatumEvidenceOrigin::manifest_declaration:
            return "manifest_declaration";
        case DatumEvidenceOrigin::provider_documentation:
            return "provider_documentation";
        case DatumEvidenceOrigin::authority_registry:
            return "authority_registry";
        case DatumEvidenceOrigin::station_metadata:
            return "station_metadata";
        }
        return "unknown";
    }

    auto to_string(DatumEvidenceStatus status) noexcept -> std::string_view
    {
        switch (status) {
        case DatumEvidenceStatus::authoritative_declared:
            return "authoritative_declared";
        case DatumEvidenceStatus::dataset_declared:
            return "dataset_declared";
        case DatumEvidenceStatus::inferred:
            return "inferred";
        case DatumEvidenceStatus::unknown:
            return "unknown";
        case DatumEvidenceStatus::conflicting:
            return "conflicting";
        }
        return "unknown";
    }

} // namespace tsunami::geo
