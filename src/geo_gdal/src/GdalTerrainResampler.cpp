#include <tsunami/geo_gdal/GdalTerrainResampler.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <gdal_version.h>

namespace tsunami::geo_gdal
{
    namespace
    {
        [[nodiscard]] auto gdal_error(std::string code, std::string message, std::string rule_id)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::input_data,
                tsunami::core::Severity::error};
            error.add_context("operation", "terrain_gdal_adapter")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        auto initialise_gdal_once() -> void
        {
            GDALAllRegister();
        }

        struct DatasetCloser
        {
            void operator()(GDALDataset *dataset) const noexcept
            {
                if (dataset != nullptr) {
                    GDALClose(dataset);
                }
            }
        };

        using DatasetPtr = std::unique_ptr<GDALDataset, DatasetCloser>;

        class ScopedConfigOption
        {
          public:
            ScopedConfigOption(std::string key, const char *value)
                : key_{std::move(key)}
            {
                if (const auto *previous = CPLGetConfigOption(key_.c_str(), nullptr); previous != nullptr) {
                    previous_ = std::string{previous};
                }
                CPLSetConfigOption(key_.c_str(), value);
            }

            ScopedConfigOption(const ScopedConfigOption &) = delete;
            auto operator=(const ScopedConfigOption &) -> ScopedConfigOption & = delete;

            ~ScopedConfigOption()
            {
                CPLSetConfigOption(key_.c_str(), previous_ ? previous_->c_str() : nullptr);
            }

          private:
            std::string key_;
            std::optional<std::string> previous_;
        };

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto inverse_pixel(
            const tsunami::geo::RasterAffineTransform &transform,
            tsunami::geo::Point2D point) -> std::optional<tsunami::geo::Point2D>
        {
            const auto dx = point.x - transform.origin_x;
            const auto dy = point.y - transform.origin_y;
            const auto determinant = transform.pixel_width * transform.pixel_height -
                transform.row_rotation * transform.column_rotation;
            if (!finite(determinant) || std::abs(determinant) <= 1.0e-15) {
                return std::nullopt;
            }
            const auto column_corner = (dx * transform.pixel_height - transform.row_rotation * dy) / determinant;
            const auto row_corner = (transform.pixel_width * dy - dx * transform.column_rotation) / determinant;
            return tsunami::geo::Point2D{column_corner - 0.5, row_corner - 0.5};
        }

        [[nodiscard]] auto source_spacing(const tsunami::geo::RasterAffineTransform &transform) noexcept
            -> std::array<double, 3U>
        {
            const auto column = std::hypot(transform.pixel_width, transform.column_rotation);
            const auto row = std::hypot(transform.row_rotation, transform.pixel_height);
            return {std::min(column, row), std::max(column, row), 0.5 * (column + row)};
        }

        [[nodiscard]] auto apply_vertical_steps(
            double value,
            const tsunami::geo::VerticalTransformationSpecification &steps) -> tsunami::core::Result<double>
        {
            auto current = value;
            for (const auto &step : steps.steps) {
                switch (step.kind) {
                case tsunami::geo::VerticalTransformationStepKind::identity:
                case tsunami::geo::VerticalTransformationStepKind::geodetic_grid_operation:
                    break;
                case tsunami::geo::VerticalTransformationStepKind::unit_scale:
                    if (!step.scale_factor || !finite(*step.scale_factor) || *step.scale_factor <= 0.0) {
                        return tsunami::core::failure<double>(gdal_error("geo.terrain.vertical_chain_invalid", "invalid vertical unit-scale step", "geo.terrain.operation.accepted_reused"));
                    }
                    current *= *step.scale_factor;
                    break;
                case tsunami::geo::VerticalTransformationStepKind::sign_inversion:
                    current = -current;
                    break;
                case tsunami::geo::VerticalTransformationStepKind::constant_offset:
                    if (!step.offset_m || !finite(*step.offset_m)) {
                        return tsunami::core::failure<double>(gdal_error("geo.terrain.vertical_chain_invalid", "invalid vertical constant-offset step", "geo.terrain.operation.accepted_reused"));
                    }
                    current += *step.offset_m;
                    break;
                }
            }
            return tsunami::core::success(current);
        }

        [[nodiscard]] auto decoded_values(
            const tsunami::geo::ImportedRaster &raster,
            const tsunami::geo::VerticalTransformationSpecification &vertical) -> tsunami::core::Result<std::vector<double>>
        {
            auto out = raster.band().values;
            const auto scale = raster.band().scale.value_or(1.0);
            const auto offset = raster.band().offset.value_or(0.0);
            if (!finite(scale) || !finite(offset)) {
                return tsunami::core::failure<std::vector<double>>(gdal_error("geo.terrain.vertical_chain_invalid", "source scale or offset is nonfinite", "geo.terrain.source.provenance_complete"));
            }
            for (std::size_t i = 0U; i < out.size(); ++i) {
                if (raster.band().valid_mask[i] == 0U) {
                    continue;
                }
                auto transformed = apply_vertical_steps(scale * out[i] + offset, vertical);
                if (!transformed) {
                    return tsunami::core::failure<std::vector<double>>(transformed.error());
                }
                out[i] = transformed.value();
            }
            return tsunami::core::success(std::move(out));
        }

        [[nodiscard]] auto source_cell(
            const tsunami::geo::ImportedRaster &raster,
            std::int64_t column,
            std::int64_t row) -> std::optional<std::size_t>
        {
            if (column < 0 || row < 0 ||
                column >= static_cast<std::int64_t>(raster.width()) ||
                row >= static_cast<std::int64_t>(raster.height())) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(static_cast<std::uint64_t>(row) * raster.width() + static_cast<std::uint64_t>(column));
        }

        [[nodiscard]] auto bilinear(
            const tsunami::geo::ImportedRaster &raster,
            const std::vector<double> &values,
            tsunami::geo::Point2D pixel) -> std::optional<double>
        {
            const auto x0 = static_cast<std::int64_t>(std::floor(pixel.x));
            const auto y0 = static_cast<std::int64_t>(std::floor(pixel.y));
            const auto tx = pixel.x - static_cast<double>(x0);
            const auto ty = pixel.y - static_cast<double>(y0);
            auto weighted_sum = 0.0;
            auto weight_total = 0.0;
            for (auto dy = 0; dy <= 1; ++dy) {
                for (auto dx = 0; dx <= 1; ++dx) {
                    const auto cell = source_cell(raster, x0 + dx, y0 + dy);
                    if (!cell || raster.band().valid_mask[*cell] == 0U) {
                        continue;
                    }
                    const auto weight = (dx == 0 ? 1.0 - tx : tx) * (dy == 0 ? 1.0 - ty : ty);
                    weighted_sum += weight * values[*cell];
                    weight_total += weight;
                }
            }
            if (weight_total <= 0.0) {
                return std::nullopt;
            }
            return weighted_sum / weight_total;
        }

        [[nodiscard]] auto average_neighbourhood(
            const tsunami::geo::ImportedRaster &raster,
            const std::vector<double> &values,
            tsunami::geo::Point2D pixel) -> std::optional<double>
        {
            const auto centre_x = static_cast<std::int64_t>(std::llround(pixel.x));
            const auto centre_y = static_cast<std::int64_t>(std::llround(pixel.y));
            auto sum = 0.0;
            auto count = 0U;
            for (auto dy = -1; dy <= 1; ++dy) {
                for (auto dx = -1; dx <= 1; ++dx) {
                    const auto cell = source_cell(raster, centre_x + dx, centre_y + dy);
                    if (cell && raster.band().valid_mask[*cell] != 0U) {
                        sum += values[*cell];
                        ++count;
                    }
                }
            }
            if (count == 0U) {
                return std::nullopt;
            }
            return sum / static_cast<double>(count);
        }

        [[nodiscard]] auto footprint_status(
            const tsunami::geo::ImportedRaster &raster,
            tsunami::geo::Point2D pixel) -> tsunami::geo::ResampledTerrainCellStatus
        {
            const auto nearest_x = static_cast<std::int64_t>(std::llround(pixel.x));
            const auto nearest_y = static_cast<std::int64_t>(std::llround(pixel.y));
            const auto cell = source_cell(raster, nearest_x, nearest_y);
            if (!cell) {
                return tsunami::geo::ResampledTerrainCellStatus::outside_source_coverage;
            }
            return raster.band().valid_mask[*cell] != 0U
                ? tsunami::geo::ResampledTerrainCellStatus::valid_resampled
                : tsunami::geo::ResampledTerrainCellStatus::source_nodata;
        }

        [[nodiscard]] auto make_record(
            const tsunami::geo::TerrainSourceResamplingRequest &request,
            std::uint64_t source_valid,
            std::uint64_t output_valid,
            std::uint64_t source_nodata,
            std::uint64_t outside_coverage) -> tsunami::geo::RasterResamplingRecord
        {
            const auto spacing = source_spacing(request.source_raster->transform());
            return tsunami::geo::RasterResamplingRecord{
                request.import_record->identity.dataset_id,
                request.import_record->identity.asset_id,
                request.import_record->identity,
                request.transformation_record->identity,
                request.role,
                request.kernel,
                request.source_raster->registration(),
                request.target_grid.registration(),
                request.source_raster->band().scale,
                request.source_raster->band().offset,
                spacing[0U],
                spacing[1U],
                spacing[2U],
                request.target_grid.spacing_m(),
                request.maximum_upsampling_factor,
                source_valid,
                output_valid,
                source_nodata,
                outside_coverage,
                request.transformation_record->horizontal_operation,
                request.transformation_record->vertical_operation,
                "GDAL",
                GDALVersionInfo("RELEASE_NAME")};
        }

        [[nodiscard]] auto create_geotiff(
            const std::filesystem::path &path,
            const tsunami::geo::TerrainTargetGrid &grid,
            const std::vector<double> &values,
            const std::vector<std::uint8_t> &mask,
            GDALDataType type,
            std::string_view description,
            const tsunami::geo::TerrainConditioningRecord &record) -> tsunami::core::Result<void>
        {
            initialise_gdal_once();
            auto *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
            if (driver == nullptr) {
                return tsunami::core::failure(gdal_error("geo.terrain.gdal_output_failed", "GTiff driver is unavailable", "geo.terrain.output.positive_up"));
            }
            const auto parent = path.parent_path();
            if (!parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
                if (ec) {
                    return tsunami::core::failure(gdal_error("geo.terrain.geotiff_write_failed", "failed to create GeoTIFF parent directory", "geo.terrain.output.positive_up"));
                }
            }
            char **options = nullptr;
            options = CSLSetNameValue(options, "TILED", "YES");
            options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
            options = CSLSetNameValue(options, "PREDICTOR", type == GDT_Float64 ? "3" : "2");
            options = CSLSetNameValue(options, "BIGTIFF", "IF_SAFER");
            auto dataset = DatasetPtr{driver->Create(
                path.string().c_str(),
                static_cast<int>(grid.width()),
                static_cast<int>(grid.height()),
                1,
                type,
                options)};
            CSLDestroy(options);
            if (!dataset) {
                return tsunami::core::failure(gdal_error("geo.terrain.gdal_output_failed", "failed to create temporary GeoTIFF", "geo.terrain.output.positive_up"));
            }
            auto transform = std::array<double, 6U>{
                grid.transform().origin_x,
                grid.transform().pixel_width,
                grid.transform().row_rotation,
                grid.transform().origin_y,
                grid.transform().column_rotation,
                grid.transform().pixel_height};
            if (dataset->SetGeoTransform(transform.data()) != CE_None) {
                return tsunami::core::failure(gdal_error("geo.terrain.gdal_output_failed", "failed to set terrain GeoTIFF geotransform", "geo.terrain.output.positive_up"));
            }
            if (grid.target_reference().horizontal.canonical_wkt2) {
                if (dataset->SetProjection(grid.target_reference().horizontal.canonical_wkt2->c_str()) != CE_None) {
                    return tsunami::core::failure(gdal_error("geo.terrain.gdal_output_failed", "failed to set terrain GeoTIFF projection", "geo.terrain.output.positive_up"));
                }
            }
            dataset->SetMetadataItem("AREA_OR_POINT", "Area");
            dataset->SetMetadataItem("TSUNAMI_FORMULA_VERSION", record.formula_version.c_str());
            dataset->SetMetadataItem("TSUNAMI_TERRAIN_ID", record.identity.terrain_id.c_str());
            auto *band = dataset->GetRasterBand(1);
            if (band == nullptr) {
                return tsunami::core::failure(gdal_error("geo.terrain.gdal_output_failed", "failed to get terrain GeoTIFF band", "geo.terrain.output.positive_up"));
            }
            band->SetDescription(std::string{description}.c_str());
            band->SetUnitType("m");
            if (type == GDT_UInt16) {
                auto integers = std::vector<std::uint16_t>{};
                integers.reserve(values.size());
                for (const auto value : values) {
                    integers.push_back(static_cast<std::uint16_t>(std::llround(value)));
                }
                if (band->RasterIO(GF_Write, 0, 0, static_cast<int>(grid.width()), static_cast<int>(grid.height()),
                        integers.data(), static_cast<int>(grid.width()), static_cast<int>(grid.height()), GDT_UInt16, 0, 0) != CE_None) {
                    return tsunami::core::failure(gdal_error("geo.terrain.gdal_output_failed", "failed to write integer terrain GeoTIFF band", "geo.terrain.output.positive_up"));
                }
            } else {
                if (band->RasterIO(GF_Write, 0, 0, static_cast<int>(grid.width()), static_cast<int>(grid.height()),
                        const_cast<double *>(values.data()), static_cast<int>(grid.width()), static_cast<int>(grid.height()), GDT_Float64, 0, 0) != CE_None) {
                    return tsunami::core::failure(gdal_error("geo.terrain.gdal_output_failed", "failed to write terrain GeoTIFF band", "geo.terrain.output.positive_up"));
                }
            }
            if (band->CreateMaskBand(GMF_PER_DATASET) != CE_None) {
                return tsunami::core::failure(gdal_error("geo.terrain.gdal_output_failed", "failed to create terrain GeoTIFF mask", "geo.terrain.output.no_active_nodata"));
            }
            auto mask_bytes = std::vector<std::uint8_t>{};
            mask_bytes.reserve(mask.size());
            for (const auto valid : mask) {
                mask_bytes.push_back(valid == 0U ? 0U : 255U);
            }
            if (band->GetMaskBand()->RasterIO(GF_Write, 0, 0, static_cast<int>(grid.width()), static_cast<int>(grid.height()),
                    mask_bytes.data(), static_cast<int>(grid.width()), static_cast<int>(grid.height()), GDT_Byte, 0, 0) != CE_None) {
                return tsunami::core::failure(gdal_error("geo.terrain.gdal_output_failed", "failed to write terrain GeoTIFF mask", "geo.terrain.output.no_active_nodata"));
            }
            dataset->FlushCache();
            return tsunami::core::success();
        }

        [[nodiscard]] auto replace_with_temporary(
            const std::filesystem::path &temporary,
            const std::filesystem::path &target) -> tsunami::core::Result<void>
        {
            {
                auto reopened = DatasetPtr{static_cast<GDALDataset *>(GDALOpen(temporary.string().c_str(), GA_ReadOnly))};
                if (!reopened) {
                    std::error_code ignored;
                    std::filesystem::remove(temporary, ignored);
                    return tsunami::core::failure(gdal_error("geo.terrain.geotiff_write_failed", "temporary terrain GeoTIFF failed reopen verification", "geo.terrain.output.positive_up"));
                }
            }
            std::error_code ec;
            std::filesystem::rename(temporary, target, ec);
            if (ec) {
                std::filesystem::remove(temporary, ec);
                return tsunami::core::failure(gdal_error("geo.terrain.geotiff_write_failed", "failed to replace terrain GeoTIFF target", "geo.terrain.output.positive_up"));
            }
            return tsunami::core::success();
        }
    }

    auto resample_terrain_source_with_gdal(
        const tsunami::geo::TerrainSourceResamplingRequest &request)
        -> tsunami::core::Result<tsunami::geo::ResampledTerrainSource>
    {
        initialise_gdal_once();
        const auto disable_proj_network = ScopedConfigOption{"PROJ_NETWORK", "OFF"};
        if (auto valid = tsunami::geo::validate_terrain_source_resampling_request(request); !valid) {
            return tsunami::core::failure<tsunami::geo::ResampledTerrainSource>(valid.error());
        }
        auto decoded = decoded_values(*request.source_raster, request.transformation_record->vertical_operation);
        if (!decoded) {
            return tsunami::core::failure<tsunami::geo::ResampledTerrainSource>(decoded.error());
        }
        const auto cells = static_cast<std::size_t>(request.target_grid.cell_count());
        auto values = std::vector<double>(cells, 0.0);
        auto mask = std::vector<std::uint8_t>(cells, 0U);
        auto status = std::vector<tsunami::geo::ResampledTerrainCellStatus>(cells, tsunami::geo::ResampledTerrainCellStatus::outside_source_coverage);
        auto output_valid = std::uint64_t{};
        auto source_nodata = std::uint64_t{};
        auto outside = std::uint64_t{};
        const auto source_valid = static_cast<std::uint64_t>(std::count_if(
            request.source_raster->band().valid_mask.begin(),
            request.source_raster->band().valid_mask.end(),
            [](std::uint8_t value) { return value != 0U; }));
        for (std::uint64_t row = 0U; row < request.target_grid.height(); ++row) {
            for (std::uint64_t column = 0U; column < request.target_grid.width(); ++column) {
                const auto cell = static_cast<std::size_t>(row * request.target_grid.width() + column);
                const auto centre = tsunami::geo::terrain_grid_cell_centre(request.target_grid, column, row);
                const auto pixel = inverse_pixel(request.source_raster->transform(), centre);
                if (!pixel) {
                    ++outside;
                    continue;
                }
                auto sampled = request.kernel == tsunami::geo::RasterResamplingKernel::bilinear
                    ? bilinear(*request.source_raster, decoded.value(), *pixel)
                    : average_neighbourhood(*request.source_raster, decoded.value(), *pixel);
                if (sampled) {
                    values[cell] = *sampled;
                    mask[cell] = 1U;
                    status[cell] = tsunami::geo::ResampledTerrainCellStatus::valid_resampled;
                    ++output_valid;
                } else {
                    status[cell] = footprint_status(*request.source_raster, *pixel);
                    if (status[cell] == tsunami::geo::ResampledTerrainCellStatus::source_nodata) {
                        ++source_nodata;
                    } else {
                        ++outside;
                    }
                }
            }
        }
        return tsunami::core::success(tsunami::geo::ResampledTerrainSource{
            request.import_record->identity.dataset_id,
            request.role,
            request.target_grid,
            std::move(values),
            std::move(mask),
            std::move(status),
            make_record(request, source_valid, output_valid, source_nodata, outside)});
    }

    auto condition_terrain_with_gdal(
        const tsunami::geo::TerrainConditioningRequest &request)
        -> tsunami::core::Result<tsunami::geo::TerrainConditioningResult>
    {
        auto preparation = tsunami::geo::prepare_terrain_conditioning(request);
        if (!preparation) {
            return tsunami::core::failure<tsunami::geo::TerrainConditioningResult>(preparation.error());
        }
        auto bathymetry = resample_terrain_source_with_gdal(preparation.value().bathymetry_resampling);
        if (!bathymetry) {
            return tsunami::core::failure<tsunami::geo::TerrainConditioningResult>(bathymetry.error());
        }
        auto topography = resample_terrain_source_with_gdal(preparation.value().topography_resampling);
        if (!topography) {
            return tsunami::core::failure<tsunami::geo::TerrainConditioningResult>(topography.error());
        }
        return tsunami::geo::condition_terrain_from_resampled_sources(preparation.value(), bathymetry.value(), topography.value());
    }

    auto write_conditioned_terrain_geotiff_with_gdal(
        const std::filesystem::path &path,
        const tsunami::geo::ConditionedTerrainRaster &terrain,
        const tsunami::geo::TerrainConditioningRecord &record)
        -> tsunami::core::Result<void>
    {
        if (auto valid = tsunami::geo::validate_terrain_conditioning_record(record); !valid) {
            return valid;
        }
        const auto temporary = std::filesystem::path{path.string() + ".tmp.tif"};
        if (auto written = create_geotiff(temporary, terrain.grid(), terrain.values(), terrain.valid_mask(), GDT_Float64, "bed_elevation", record); !written) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return written;
        }
        return replace_with_temporary(temporary, path);
    }

    auto write_terrain_inspection_geotiffs_with_gdal(
        const std::filesystem::path &terrain_path,
        const std::filesystem::path &coverage_path,
        const std::filesystem::path &lineage_path,
        const tsunami::geo::ConditionedTerrainRaster &terrain,
        const tsunami::geo::TerrainConditioningRecord &record)
        -> tsunami::core::Result<void>
    {
        if (auto terrain_result = write_conditioned_terrain_geotiff_with_gdal(terrain_path, terrain, record); !terrain_result) {
            return terrain_result;
        }
        auto coverage_mask = std::vector<std::uint8_t>(terrain.cell_count(), 1U);
        const auto coverage_tmp = std::filesystem::path{coverage_path.string() + ".tmp.tif"};
        if (auto written = create_geotiff(coverage_tmp, terrain.grid(), terrain.corridor_coverage_fraction(), coverage_mask, GDT_Float64, "corridor_coverage_fraction", record); !written) {
            std::error_code ignored;
            std::filesystem::remove(coverage_tmp, ignored);
            return written;
        }
        if (auto replaced = replace_with_temporary(coverage_tmp, coverage_path); !replaced) {
            return replaced;
        }
        auto lineage_values = std::vector<double>{};
        lineage_values.reserve(terrain.cell_count());
        for (const auto lineage : terrain.cell_lineage()) {
            lineage_values.push_back(static_cast<double>(tsunami::geo::terrain_lineage_code(lineage)));
        }
        const auto lineage_tmp = std::filesystem::path{lineage_path.string() + ".tmp.tif"};
        if (auto written = create_geotiff(lineage_tmp, terrain.grid(), lineage_values, coverage_mask, GDT_UInt16, "cell_lineage_code", record); !written) {
            std::error_code ignored;
            std::filesystem::remove(lineage_tmp, ignored);
            return written;
        }
        return replace_with_temporary(lineage_tmp, lineage_path);
    }

} // namespace tsunami::geo_gdal
