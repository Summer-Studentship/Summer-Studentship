#include <tsunami/geo_gdal/GdalGeospatialImporter.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <cpl_error.h>
#include <gdal_priv.h>

#include "GdalAdapterDetail.hpp"

namespace tsunami::geo_gdal
{
    namespace
    {
        [[nodiscard]] auto map_type(GDALDataType type) -> std::optional<tsunami::geo::NativeRasterDataType>
        {
            switch (type) {
            case GDT_Byte:
                return tsunami::geo::NativeRasterDataType::byte;
            case GDT_UInt16:
                return tsunami::geo::NativeRasterDataType::uint16;
            case GDT_Int16:
                return tsunami::geo::NativeRasterDataType::int16;
            case GDT_UInt32:
                return tsunami::geo::NativeRasterDataType::uint32;
            case GDT_Int32:
                return tsunami::geo::NativeRasterDataType::int32;
            case GDT_Float32:
                return tsunami::geo::NativeRasterDataType::float32;
            case GDT_Float64:
                return tsunami::geo::NativeRasterDataType::float64;
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] auto registration_from_metadata(GDALDataset &dataset) -> tsunami::geo::RasterCellRegistration
        {
            const auto *metadata = dataset.GetMetadataItem(GDALMD_AREA_OR_POINT);
            if (metadata == nullptr) {
                return tsunami::geo::RasterCellRegistration::unknown;
            }
            const auto value = std::string{metadata};
            if (value == GDALMD_AOP_AREA || value == "Area") {
                return tsunami::geo::RasterCellRegistration::pixel_is_area;
            }
            if (value == GDALMD_AOP_POINT || value == "Point") {
                return tsunami::geo::RasterCellRegistration::pixel_is_point;
            }
            return tsunami::geo::RasterCellRegistration::unknown;
        }
    }

    auto import_geotiff_with_gdal(const tsunami::geo::GeospatialImportRequest &request)
        -> tsunami::core::Result<tsunami::geo::RasterImportResult>
    {
        detail::initialise_gdal_once();
        auto resolved = tsunami::geo::resolve_geospatial_import_asset(request, tsunami::geo::GeospatialImportKind::raster, "GTiff");
        if (!resolved) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(resolved.error());
        }

        auto dataset = detail::DatasetHandle{static_cast<GDALDataset *>(GDALOpenEx(
            resolved.value().absolute_path.c_str(),
            GDAL_OF_RASTER | GDAL_OF_READONLY,
            nullptr,
            nullptr,
            nullptr))};
        if (!dataset) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(
                detail::gdal_error("geo.import.raster_open_failed", "GDAL could not open raster asset", "geo.import.raster.open", "import_geotiff_with_gdal")
                    .add_context("path", resolved.value().absolute_path.generic_string()));
        }
        const auto *driver = dataset->GetDriver();
        const auto driver_short = driver == nullptr ? std::string{} : std::string{driver->GetDescription()};
        if (driver_short != "GTiff") {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(
                detail::gdal_error("geo.import.driver_unsupported", "raster driver is not the selected G1 GeoTIFF driver", "geo.import.driver.supported", "import_geotiff_with_gdal")
                    .add_context("expected", "GTiff")
                    .add_context("actual", driver_short));
        }
        if (dataset->GetRasterCount() != 1) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(
                detail::gdal_error("geo.import.raster_band_count_unsupported", "G1 GeoTIFF import supports exactly one scalar band", "geo.import.raster.band_count", "import_geotiff_with_gdal")
                    .add_context("actual", std::to_string(dataset->GetRasterCount())));
        }
        const auto width = dataset->GetRasterXSize();
        const auto height = dataset->GetRasterYSize();
        if (width <= 0 || height <= 0) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(
                detail::gdal_error("geo.import.raster_size_invalid", "raster dimensions are invalid", "geo.import.raster.size.valid", "import_geotiff_with_gdal"));
        }
        const auto width_u = static_cast<std::uint64_t>(width);
        const auto height_u = static_cast<std::uint64_t>(height);
        if (width_u > std::numeric_limits<std::uint64_t>::max() / height_u) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(
                detail::gdal_error("geo.import.raster_size_invalid", "raster cell count overflows", "geo.import.raster.size.overflow", "import_geotiff_with_gdal"));
        }
        const auto cells = width_u * height_u;
        if (cells > request.maximum_raster_cells) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(
                detail::gdal_error("geo.import.raster_resource_limit", "raster exceeds the requested cell limit", "geo.import.raster.resource_limit", "import_geotiff_with_gdal")
                    .add_context("actual", std::to_string(cells))
                    .add_context("expected", std::to_string(request.maximum_raster_cells)));
        }

        auto *band = dataset->GetRasterBand(1);
        if (band == nullptr) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(
                detail::gdal_error("geo.import.raster_read_failed", "raster band is missing", "geo.import.raster.band.present", "import_geotiff_with_gdal"));
        }
        auto native_type = map_type(band->GetRasterDataType());
        if (!native_type) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(
                detail::gdal_error("geo.import.raster_type_unsupported", "raster band type is not supported by G1", "geo.import.raster.type.supported", "import_geotiff_with_gdal")
                    .add_context("actual", GDALGetDataTypeName(band->GetRasterDataType())));
        }

        auto coefficients = std::array<double, 6>{};
        if (dataset->GetGeoTransform(coefficients.data()) != CE_None) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(
                detail::gdal_error("geo.import.raster_geotransform_invalid", "raster geotransform is missing or invalid", "geo.import.raster.affine.present", "import_geotiff_with_gdal"));
        }
        auto transform = tsunami::geo::RasterAffineTransform{
            coefficients[0],
            coefficients[1],
            coefficients[2],
            coefficients[3],
            coefficients[4],
            coefficients[5]};
        auto extent = tsunami::geo::raster_extent_from_corners(width_u, height_u, transform);
        if (!extent) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(extent.error());
        }

        auto values = std::vector<double>(static_cast<std::size_t>(cells));
        if (band->RasterIO(GF_Read, 0, 0, width, height, values.data(), width, height, GDT_Float64, 0, 0) != CE_None) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(
                detail::gdal_error("geo.import.raster_read_failed", "GDAL could not read raster values", "geo.import.raster.read", "import_geotiff_with_gdal"));
        }
        int has_nodata = 0;
        const auto nodata = band->GetNoDataValue(&has_nodata);
        auto mask = std::vector<std::uint8_t>(values.size(), std::uint8_t{1U});
        if (has_nodata != 0) {
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (values[i] == nodata) {
                    mask[i] = std::uint8_t{0U};
                }
            }
        }
        auto raster_band = tsunami::geo::ImportedRasterBand{};
        raster_band.name = band->GetDescription() == nullptr || std::string{band->GetDescription()}.empty() ? "band-1" : band->GetDescription();
        raster_band.native_type = *native_type;
        if (has_nodata != 0) {
            raster_band.nodata_value = nodata;
        }
        const auto scale = band->GetScale();
        const auto offset = band->GetOffset();
        if (scale != 1.0) {
            raster_band.scale = scale;
        }
        if (offset != 0.0) {
            raster_band.offset = offset;
        }
        raster_band.values = std::move(values);
        raster_band.valid_mask = std::move(mask);

        auto warnings = std::vector<tsunami::geo::ImportWarning>{};
        const auto registration = registration_from_metadata(*dataset);
        if (registration == tsunami::geo::RasterCellRegistration::unknown) {
            warnings.push_back({"geo.import.warning.cell_registration_unknown", "raster cell registration metadata is absent"});
        }
        if (!resolved.value().asset->byte_size) {
            warnings.push_back({"geo.import.warning.byte_size_missing", "manifest asset byte size is not declared"});
        }

        auto raster = tsunami::geo::make_imported_raster(
            width_u,
            height_u,
            transform,
            extent.value(),
            registration,
            detail::extract_native_spatial_reference(dataset->GetSpatialRef()),
            std::move(raster_band));
        if (!raster) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(raster.error());
        }
        auto record = tsunami::geo::make_raster_import_record(
            request,
            *resolved.value().dataset,
            *resolved.value().asset,
            driver_short,
            detail::driver_long_name(driver),
            raster.value(),
            std::move(warnings));
        if (!record) {
            return tsunami::core::failure<tsunami::geo::RasterImportResult>(record.error());
        }
        return tsunami::core::success(tsunami::geo::RasterImportResult{std::move(raster.value()), std::move(record.value())});
    }

} // namespace tsunami::geo_gdal
