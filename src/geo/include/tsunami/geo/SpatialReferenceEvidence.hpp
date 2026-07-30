#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tsunami::geo
{
    enum class DatumReferenceComponent
    {
        horizontal,
        vertical
    };

    enum class DatumReferenceKind
    {
        geodetic_datum,
        ellipsoidal_height,
        orthometric_height,
        mean_sea_level,
        hydrographic_chart_datum,
        tide_gauge_reference,
        model_assumption,
        unknown
    };

    enum class DatumEvidenceOrigin
    {
        embedded_asset_metadata,
        manifest_declaration,
        provider_documentation,
        authority_registry,
        station_metadata
    };

    enum class DatumEvidenceStatus
    {
        authoritative_declared,
        dataset_declared,
        inferred,
        unknown,
        conflicting
    };

    [[nodiscard]] auto to_string(DatumReferenceComponent component) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(DatumReferenceKind kind) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(DatumEvidenceOrigin origin) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(DatumEvidenceStatus status) noexcept -> std::string_view;

    struct NativeSpatialReference
    {
        std::optional<std::string> authority_name;
        std::optional<std::string> authority_code;
        std::optional<std::string> crs_name;
        std::optional<std::string> datum_name;
        std::optional<std::string> canonical_wkt2;
        std::vector<std::string> axis_names;
        std::vector<std::string> axis_directions;
        std::vector<std::string> axis_units;
        std::optional<std::string> coordinate_epoch;

        [[nodiscard]] auto operator==(const NativeSpatialReference &) const -> bool = default;
    };

    struct DatumSourceEvidence
    {
        DatumReferenceComponent component{DatumReferenceComponent::horizontal};
        DatumReferenceKind reference_kind{DatumReferenceKind::unknown};
        DatumEvidenceOrigin origin{DatumEvidenceOrigin::provider_documentation};
        DatumEvidenceStatus status{DatumEvidenceStatus::unknown};
        std::string datum_name;
        std::optional<std::string> datum_realisation;
        std::optional<std::string> authority_name;
        std::optional<std::string> authority_code;
        std::optional<std::string> coordinate_epoch;
        std::optional<std::string> effective_from;
        std::optional<std::string> effective_to;
        std::optional<std::string> station_id;
        std::string unit;
        std::optional<std::string> positive_direction;
        std::optional<std::string> tide_system;
        std::string source_document_title;
        std::string source_document_uri;
        std::string accessed_at_utc;

        [[nodiscard]] auto operator==(const DatumSourceEvidence &) const -> bool = default;
    };

    struct DatumEvidenceSet
    {
        DatumSourceEvidence horizontal;
        std::optional<DatumSourceEvidence> vertical;

        [[nodiscard]] auto operator==(const DatumEvidenceSet &) const -> bool = default;
    };

} // namespace tsunami::geo
