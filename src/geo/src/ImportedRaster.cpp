#include <tsunami/geo/ImportedRaster.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto geo_error(std::string code, std::string message, std::string rule_id)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_imported_raster")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }
    }

    ImportedRaster::ImportedRaster(
        std::uint64_t width,
        std::uint64_t height,
        RasterAffineTransform transform,
        BoundingBox2D extent,
        RasterCellRegistration registration,
        NativeSpatialReference spatial_reference,
        ImportedRasterBand band)
        : width_{width},
          height_{height},
          transform_{transform},
          extent_{extent},
          registration_{registration},
          spatial_reference_{std::move(spatial_reference)},
          band_{std::move(band)}
    {
    }

    auto to_string(RasterCellRegistration registration) noexcept -> std::string_view
    {
        switch (registration) {
        case RasterCellRegistration::pixel_is_area:
            return "pixel_is_area";
        case RasterCellRegistration::pixel_is_point:
            return "pixel_is_point";
        case RasterCellRegistration::unknown:
            return "unknown";
        }
        return "unknown";
    }

    auto to_string(NativeRasterDataType type) noexcept -> std::string_view
    {
        switch (type) {
        case NativeRasterDataType::byte:
            return "byte";
        case NativeRasterDataType::uint16:
            return "uint16";
        case NativeRasterDataType::int16:
            return "int16";
        case NativeRasterDataType::uint32:
            return "uint32";
        case NativeRasterDataType::int32:
            return "int32";
        case NativeRasterDataType::float32:
            return "float32";
        case NativeRasterDataType::float64:
            return "float64";
        }
        return "unknown";
    }

    auto raster_extent_from_corners(std::uint64_t width, std::uint64_t height, const RasterAffineTransform &transform)
        -> tsunami::core::Result<BoundingBox2D>
    {
        const auto coefficients = std::array{
            transform.origin_x,
            transform.pixel_width,
            transform.row_rotation,
            transform.origin_y,
            transform.column_rotation,
            transform.pixel_height};
        if (!std::all_of(coefficients.begin(), coefficients.end(), finite)) {
            return tsunami::core::failure<BoundingBox2D>(geo_error("geo.import.raster_geotransform_invalid", "raster affine transform contains a nonfinite coefficient", "geo.import.raster.affine.finite"));
        }
        const auto transform_corner = [&](double column, double row) {
            return std::pair<double, double>{
                transform.origin_x + (column * transform.pixel_width) + (row * transform.row_rotation),
                transform.origin_y + (column * transform.column_rotation) + (row * transform.pixel_height)};
        };
        const auto w = static_cast<double>(width);
        const auto h = static_cast<double>(height);
        const auto corners = std::array{
            transform_corner(0.0, 0.0),
            transform_corner(w, 0.0),
            transform_corner(0.0, h),
            transform_corner(w, h)};
        auto min_x = corners.front().first;
        auto max_x = corners.front().first;
        auto min_y = corners.front().second;
        auto max_y = corners.front().second;
        for (const auto &[x, y] : corners) {
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
        }
        return tsunami::core::success(BoundingBox2D{min_x, min_y, max_x, max_y});
    }

    auto make_imported_raster(
        std::uint64_t width,
        std::uint64_t height,
        RasterAffineTransform transform,
        BoundingBox2D extent,
        RasterCellRegistration registration,
        NativeSpatialReference spatial_reference,
        ImportedRasterBand band) -> tsunami::core::Result<ImportedRaster>
    {
        if (width == 0U || height == 0U || width > std::numeric_limits<std::uint64_t>::max() / height) {
            return tsunami::core::failure<ImportedRaster>(geo_error("geo.import.raster_size_invalid", "raster dimensions are invalid", "geo.import.raster.size.valid"));
        }
        const auto cells = width * height;
        if (band.values.size() != cells || band.valid_mask.size() != cells) {
            return tsunami::core::failure<ImportedRaster>(geo_error("geo.import.raster_size_invalid", "raster band storage does not match dimensions", "geo.import.raster.band.size"));
        }
        if (!std::all_of(band.values.begin(), band.values.end(), finite)) {
            return tsunami::core::failure<ImportedRaster>(geo_error("geo.import.raster_read_failed", "raster values contain nonfinite data", "geo.import.raster.values.finite"));
        }
        if ((band.nodata_value && !finite(*band.nodata_value)) || (band.scale && !finite(*band.scale)) || (band.offset && !finite(*band.offset))) {
            return tsunami::core::failure<ImportedRaster>(geo_error("geo.import.raster_nodata_invalid", "raster nodata, scale or offset metadata is invalid", "geo.import.raster.metadata.finite"));
        }
        if (!(finite(extent.minimum_x) && finite(extent.maximum_x) && finite(extent.minimum_y) && finite(extent.maximum_y)) ||
            extent.minimum_x > extent.maximum_x || extent.minimum_y > extent.maximum_y) {
            return tsunami::core::failure<ImportedRaster>(geo_error("geo.import.raster_geotransform_invalid", "raster extent is invalid", "geo.import.raster.extent.valid"));
        }
        return tsunami::core::success(ImportedRaster{
            width,
            height,
            transform,
            extent,
            registration,
            std::move(spatial_reference),
            std::move(band)});
    }

} // namespace tsunami::geo
