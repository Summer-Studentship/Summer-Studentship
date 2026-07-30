#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/core/Result.hpp>
#include <tsunami/geo/SpatialReferenceEvidence.hpp>

namespace tsunami::geo
{
    struct RasterAffineTransform
    {
        double origin_x{};
        double pixel_width{};
        double row_rotation{};
        double origin_y{};
        double column_rotation{};
        double pixel_height{};

        [[nodiscard]] auto operator==(const RasterAffineTransform &) const -> bool = default;
    };

    struct BoundingBox2D
    {
        double minimum_x{};
        double minimum_y{};
        double maximum_x{};
        double maximum_y{};

        [[nodiscard]] auto operator==(const BoundingBox2D &) const -> bool = default;
    };

    enum class RasterCellRegistration
    {
        pixel_is_area,
        pixel_is_point,
        unknown
    };

    enum class NativeRasterDataType
    {
        byte,
        uint16,
        int16,
        uint32,
        int32,
        float32,
        float64
    };

    [[nodiscard]] auto to_string(RasterCellRegistration registration) noexcept -> std::string_view;
    [[nodiscard]] auto to_string(NativeRasterDataType type) noexcept -> std::string_view;

    struct ImportedRasterBand
    {
        std::string name;
        NativeRasterDataType native_type{NativeRasterDataType::float64};
        std::optional<double> nodata_value;
        std::optional<double> scale;
        std::optional<double> offset;
        std::vector<double> values;
        std::vector<std::uint8_t> valid_mask;

        [[nodiscard]] auto operator==(const ImportedRasterBand &) const -> bool = default;
    };

    class ImportedRaster
    {
    public:
        [[nodiscard]] auto width() const noexcept -> std::uint64_t { return width_; }
        [[nodiscard]] auto height() const noexcept -> std::uint64_t { return height_; }
        [[nodiscard]] auto cell_count() const noexcept -> std::uint64_t { return width_ * height_; }
        [[nodiscard]] auto transform() const noexcept -> const RasterAffineTransform & { return transform_; }
        [[nodiscard]] auto extent() const noexcept -> const BoundingBox2D & { return extent_; }
        [[nodiscard]] auto registration() const noexcept -> RasterCellRegistration { return registration_; }
        [[nodiscard]] auto spatial_reference() const noexcept -> const NativeSpatialReference & { return spatial_reference_; }
        [[nodiscard]] auto band() const noexcept -> const ImportedRasterBand & { return band_; }

        [[nodiscard]] auto operator==(const ImportedRaster &) const -> bool = default;

    private:
        friend auto make_imported_raster(
            std::uint64_t width,
            std::uint64_t height,
            RasterAffineTransform transform,
            BoundingBox2D extent,
            RasterCellRegistration registration,
            NativeSpatialReference spatial_reference,
            ImportedRasterBand band) -> tsunami::core::Result<ImportedRaster>;

        ImportedRaster(
            std::uint64_t width,
            std::uint64_t height,
            RasterAffineTransform transform,
            BoundingBox2D extent,
            RasterCellRegistration registration,
            NativeSpatialReference spatial_reference,
            ImportedRasterBand band);

        std::uint64_t width_{};
        std::uint64_t height_{};
        RasterAffineTransform transform_;
        BoundingBox2D extent_;
        RasterCellRegistration registration_{RasterCellRegistration::unknown};
        NativeSpatialReference spatial_reference_;
        ImportedRasterBand band_;
    };

    [[nodiscard]] auto make_imported_raster(
        std::uint64_t width,
        std::uint64_t height,
        RasterAffineTransform transform,
        BoundingBox2D extent,
        RasterCellRegistration registration,
        NativeSpatialReference spatial_reference,
        ImportedRasterBand band) -> tsunami::core::Result<ImportedRaster>;

    [[nodiscard]] auto raster_extent_from_corners(
        std::uint64_t width,
        std::uint64_t height,
        const RasterAffineTransform &transform) -> tsunami::core::Result<BoundingBox2D>;

} // namespace tsunami::geo
