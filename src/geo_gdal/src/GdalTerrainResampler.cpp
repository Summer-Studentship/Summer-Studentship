#include <tsunami/geo_gdal/GdalTerrainResampler.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
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

        [[nodiscard]] auto parent_or_current(const std::filesystem::path &path) -> std::filesystem::path
        {
            return path.parent_path().empty() ? std::filesystem::path{"."} : path.parent_path();
        }

        [[nodiscard]] auto unique_token() -> std::string
        {
            static auto counter = std::atomic<std::uint64_t>{0U};
            auto random = std::random_device{}();
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto thread_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
            return std::to_string(ticks) + "-" + std::to_string(thread_hash) + "-" + std::to_string(random) + "-" + std::to_string(counter.fetch_add(1U));
        }

        [[nodiscard]] auto create_single_writer_companion_directory(const std::filesystem::path &terrain_path)
            -> tsunami::core::Result<std::filesystem::path>
        {
            const auto parent = parent_or_current(terrain_path);
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return tsunami::core::failure<std::filesystem::path>(gdal_error("geo.terrain.artifact_write.replacement_failed", "failed to create conditioned terrain companion transaction parent directory", "geo.terrain.artifact.single_writer_companion"));
            }
            for (auto attempt = 0U; attempt < 64U; ++attempt) {
                const auto candidate = parent / (".tsunami-terrain-single-artifact-txn-" + unique_token() + "-" + std::to_string(attempt));
                ec.clear();
                if (std::filesystem::create_directory(candidate, ec)) {
                    return tsunami::core::success(candidate);
                }
                if (ec && !std::filesystem::exists(candidate)) {
                    return tsunami::core::failure<std::filesystem::path>(gdal_error("geo.terrain.artifact_write.replacement_failed", "failed to claim conditioned terrain companion transaction directory", "geo.terrain.artifact.single_writer_companion"));
                }
            }
            return tsunami::core::failure<std::filesystem::path>(gdal_error("geo.terrain.artifact_write.replacement_failed", "could not allocate a unique conditioned terrain companion transaction directory", "geo.terrain.artifact.single_writer_companion"));
        }

        [[nodiscard]] auto cleanup_single_writer_companion_directory(const std::filesystem::path &directory) -> bool
        {
            std::error_code ec;
            if (!std::filesystem::exists(directory, ec)) {
                return true;
            }
            std::filesystem::remove_all(directory, ec);
            return !ec;
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
        const auto companion_directory = create_single_writer_companion_directory(path);
        if (!companion_directory) {
            return tsunami::core::failure(companion_directory.error());
        }
        auto paths = ConditionedTerrainArtifactPaths{
            path.lexically_normal(),
            (companion_directory.value() / "coverage.tif").lexically_normal(),
            (companion_directory.value() / "lineage.tif").lexically_normal()};
        auto result = write_conditioned_terrain_artifacts_with_gdal(paths, terrain, record);
        const auto cleanup_ok = cleanup_single_writer_companion_directory(companion_directory.value());
        if (!result) {
            return result;
        }
        if (!cleanup_ok) {
            return tsunami::core::failure(gdal_error("geo.terrain.artifact_write.replacement_failed", "failed to remove transaction-owned conditioned terrain companion artefacts", "geo.terrain.artifact.single_writer_companion"));
        }
        return result;
    }

    auto write_terrain_inspection_geotiffs_with_gdal(
        const std::filesystem::path &terrain_path,
        const std::filesystem::path &coverage_path,
        const std::filesystem::path &lineage_path,
        const tsunami::geo::ConditionedTerrainRaster &terrain,
        const tsunami::geo::TerrainConditioningRecord &record)
        -> tsunami::core::Result<void>
    {
        return write_conditioned_terrain_artifacts_with_gdal(
            ConditionedTerrainArtifactPaths{terrain_path.lexically_normal(), coverage_path.lexically_normal(), lineage_path.lexically_normal()},
            terrain,
            record);
    }

} // namespace tsunami::geo_gdal
