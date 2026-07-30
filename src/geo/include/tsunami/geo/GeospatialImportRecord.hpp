#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/ImportedRaster.hpp>
#include <tsunami/geo/ImportedVector.hpp>
#include <tsunami/geo/SpatialReferenceEvidence.hpp>

namespace tsunami::geo
{
    inline constexpr std::string_view geospatial_import_record_schema_name{"tsunami.geospatial_import_record"};
    inline constexpr tsunami::core::SemanticVersion supported_geospatial_import_record_version{1U, 0U, 0U};
    inline constexpr std::string_view supported_geospatial_import_record_policy_version{"0.1"};
    inline constexpr std::string_view geospatial_import_record_media_type{"application/json"};

    enum class GeospatialImportKind
    {
        raster,
        vector
    };

    [[nodiscard]] auto to_string(GeospatialImportKind kind) noexcept -> std::string_view;

    struct GeospatialImportIdentity
    {
        std::string import_id;
        std::uint64_t import_revision{};
        tsunami::data::CaseRevisionRef case_revision;
        std::string manifest_id;
        std::uint64_t manifest_revision{};
        std::string dataset_id;
        std::string asset_id;
        std::string executed_at_utc;

        [[nodiscard]] auto operator==(const GeospatialImportIdentity &) const -> bool = default;
    };

    struct RasterImportSummary
    {
        std::uint64_t width{};
        std::uint64_t height{};
        std::uint64_t cell_count{};
        std::uint64_t band_count{1U};
        NativeRasterDataType native_type{NativeRasterDataType::float64};
        RasterAffineTransform transform;
        RasterCellRegistration registration{RasterCellRegistration::unknown};
        bool has_nodata{};
        std::optional<double> nodata_value;
        std::optional<double> scale;
        std::optional<double> offset;
        tsunami::data::SpatialResolution spatial_resolution;

        [[nodiscard]] auto operator==(const RasterImportSummary &) const -> bool = default;
    };

    struct VectorImportSummary
    {
        std::string layer_name;
        std::uint64_t feature_count{};
        ImportedGeometryKind geometry_kind{ImportedGeometryKind::point};
        std::uint64_t field_count{};
        std::uint64_t coordinate_count{};
        BoundingBox2D extent;

        [[nodiscard]] auto operator==(const VectorImportSummary &) const -> bool = default;
    };

    struct ImportWarning
    {
        std::string code;
        std::string message;

        [[nodiscard]] auto operator==(const ImportWarning &) const -> bool = default;
    };

    struct GeospatialImportRecord
    {
        tsunami::data::SchemaIdentity schema;
        std::string policy_version;
        GeospatialImportIdentity identity;
        GeospatialImportKind import_kind{GeospatialImportKind::raster};
        std::string adapter_name;
        std::string adapter_version;
        std::string driver_short_name;
        std::string driver_long_name;
        std::string media_type;
        std::filesystem::path managed_path;
        tsunami::data::ContentDigest declared_digest;
        std::string digest_verification_status{"not_verified"};
        NativeSpatialReference native_spatial_reference;
        DatumEvidenceSet datum_evidence;
        BoundingBox2D extent;
        std::optional<RasterImportSummary> raster;
        std::optional<VectorImportSummary> vector;
        std::vector<ImportWarning> warnings;

        [[nodiscard]] auto operator==(const GeospatialImportRecord &) const -> bool = default;
    };

    [[nodiscard]] auto default_geospatial_import_record_path(
        std::string_view dataset_id,
        std::string_view asset_id) -> std::filesystem::path;

    [[nodiscard]] auto validate_geospatial_import_record(const GeospatialImportRecord &record)
        -> tsunami::core::Result<void>;

} // namespace tsunami::geo
