#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include <tsunami/core/Result.hpp>
#include <tsunami/data/DatasetManifest.hpp>
#include <tsunami/geo/GeospatialImportRecord.hpp>
#include <tsunami/geo/ImportedRaster.hpp>
#include <tsunami/geo/ImportedVector.hpp>

namespace tsunami::geo
{
    struct GeospatialImportRequest
    {
        const tsunami::data::DatasetManifest *manifest{};
        std::string dataset_id;
        std::optional<std::string> asset_id;
        std::optional<std::string> layer_name;
        std::filesystem::path case_root;
        DatumEvidenceSet datum_evidence;
        std::string executed_at_utc;
        std::string import_id;
        std::uint64_t import_revision{1U};
        std::size_t maximum_raster_cells{10'000'000U};
        std::size_t maximum_vector_features{100'000U};
        std::size_t maximum_geometry_coordinates{5'000'000U};
    };

    struct ResolvedGeospatialAsset
    {
        const tsunami::data::DatasetRecord *dataset{};
        const tsunami::data::DatasetAsset *asset{};
        std::filesystem::path absolute_path;
    };

    struct RasterImportResult
    {
        ImportedRaster raster;
        GeospatialImportRecord record;
    };

    struct VectorImportResult
    {
        ImportedVectorLayer vector_layer;
        GeospatialImportRecord record;
    };

    [[nodiscard]] auto validate_datum_evidence_set(
        const tsunami::data::DatasetRecord &dataset,
        const NativeSpatialReference &native_reference,
        const DatumEvidenceSet &evidence) -> tsunami::core::Result<void>;

    [[nodiscard]] auto resolve_geospatial_import_asset(
        const GeospatialImportRequest &request,
        GeospatialImportKind import_kind,
        std::string_view expected_driver_short_name) -> tsunami::core::Result<ResolvedGeospatialAsset>;

    [[nodiscard]] auto make_raster_import_record(
        const GeospatialImportRequest &request,
        const tsunami::data::DatasetRecord &dataset,
        const tsunami::data::DatasetAsset &asset,
        std::string driver_short_name,
        std::string driver_long_name,
        const ImportedRaster &raster,
        std::vector<ImportWarning> warnings) -> tsunami::core::Result<GeospatialImportRecord>;

    [[nodiscard]] auto make_vector_import_record(
        const GeospatialImportRequest &request,
        const tsunami::data::DatasetRecord &dataset,
        const tsunami::data::DatasetAsset &asset,
        std::string driver_short_name,
        std::string driver_long_name,
        const ImportedVectorLayer &layer,
        std::size_t coordinate_count,
        std::vector<ImportWarning> warnings) -> tsunami::core::Result<GeospatialImportRecord>;

} // namespace tsunami::geo
