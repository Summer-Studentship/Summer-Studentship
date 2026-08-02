#include <tsunami/geo_gdal/GdalEarthquakeDisplacementArtifacts.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <nlohmann/json.hpp>

#include "GdalAdapterDetail.hpp"

namespace tsunami::geo_gdal
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        [[nodiscard]] auto artifact_error(
            std::string code,
            std::string message,
            tsunami::core::DiagnosticCategory category,
            std::string operation,
            bool state_changed) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{std::move(code), std::move(message), category, tsunami::core::Severity::error};
            error.add_context("operation", std::move(operation))
                .add_context("rule_id", "geo.earthquake_displacement.artifact")
                .add_context("state_changed", state_changed ? "true" : "false");
            return error;
        }

        [[nodiscard]] auto read_error(std::string code, std::string message, tsunami::core::DiagnosticCategory category)
            -> tsunami::core::Error
        {
            return artifact_error(std::move(code), std::move(message), category, "read_earthquake_displacement_artifact_with_gdal", false);
        }

        [[nodiscard]] auto write_error(std::string code, std::string message, bool state_changed = false)
            -> tsunami::core::Error
        {
            return artifact_error(
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::persistence,
                "write_earthquake_displacement_artifact_with_gdal",
                state_changed);
        }

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto close(double left, double right, double absolute_tolerance, double relative_tolerance) noexcept -> bool
        {
            const auto tolerance = absolute_tolerance + relative_tolerance * std::max({1.0, std::abs(left), std::abs(right)});
            return finite(left) && finite(right) && std::abs(left - right) <= tolerance;
        }

        [[nodiscard]] auto read_metadata(const std::filesystem::path &path)
            -> tsunami::core::Result<EarthquakeDisplacementArtifactMetadata>
        {
            auto input = std::ifstream{path, std::ios::binary};
            if (!input) {
                return tsunami::core::failure<EarthquakeDisplacementArtifactMetadata>(
                    read_error("geo.earthquake_displacement.metadata_open_failed", "could not open earthquake displacement metadata", tsunami::core::DiagnosticCategory::input_data)
                        .add_context("path", path.generic_string()));
            }
            const auto text = std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
            try {
                const auto json = Json::parse(text.begin(), text.end());
                auto metadata = EarthquakeDisplacementArtifactMetadata{};
                metadata.artifact_contract_version = json.at("artifact_contract_version").get<std::uint32_t>();
                metadata.event_id = json.at("event_id").get<std::string>();
                metadata.model_id = json.at("model_id").get<std::string>();
                metadata.source_format = json.at("source_format").get<std::string>();
                metadata.coordinate_reference = json.at("coordinate_reference").get<std::string>();
                metadata.subfault_count = json.at("subfault_count").get<std::uint64_t>();
                metadata.vertical_unit = json.at("vertical_unit").get<std::string>();
                metadata.source_uri = json.value("source_uri", std::string{});
                metadata.source_sha256 = json.value("source_sha256", std::string{});
                metadata.generated_at_utc = json.value("generated_at_utc", std::string{});
                metadata.producer = json.value("producer", std::string{});
                if (metadata.artifact_contract_version != earthquake_displacement_artifact_contract_version ||
                    metadata.event_id.empty() ||
                    metadata.model_id.empty() ||
                    metadata.source_format.empty() ||
                    metadata.coordinate_reference.empty() ||
                    metadata.subfault_count == 0U ||
                    metadata.vertical_unit != "m") {
                    return tsunami::core::failure<EarthquakeDisplacementArtifactMetadata>(
                        read_error("geo.earthquake_displacement.metadata_invalid", "earthquake displacement metadata is incomplete or unsupported", tsunami::core::DiagnosticCategory::validation)
                            .add_context("path", path.generic_string()));
                }
                return tsunami::core::success(std::move(metadata));
            } catch (const nlohmann::json::exception &error) {
                auto diagnostic = read_error("geo.earthquake_displacement.metadata_invalid", "earthquake displacement metadata JSON is invalid", tsunami::core::DiagnosticCategory::input_data);
                diagnostic.add_context("path", path.generic_string())
                    .add_context("parser_detail", error.what());
                return tsunami::core::failure<EarthquakeDisplacementArtifactMetadata>(std::move(diagnostic));
            }
        }

        [[nodiscard]] auto write_metadata(
            const std::filesystem::path &path,
            const EarthquakeDisplacementArtifactMetadata &metadata) -> tsunami::core::Result<void>
        {
            auto json = Json{
                {"artifact_contract_version", metadata.artifact_contract_version},
                {"role", std::string{earthquake_vertical_displacement_role}},
                {"event_id", metadata.event_id},
                {"model_id", metadata.model_id},
                {"source_format", metadata.source_format},
                {"coordinate_reference", metadata.coordinate_reference},
                {"subfault_count", metadata.subfault_count},
                {"vertical_unit", metadata.vertical_unit},
                {"source_uri", metadata.source_uri},
                {"source_sha256", metadata.source_sha256},
                {"generated_at_utc", metadata.generated_at_utc},
                {"producer", metadata.producer}};
            std::filesystem::create_directories(path.parent_path());
            auto output = std::ofstream{path, std::ios::binary | std::ios::trunc};
            if (!output) {
                return tsunami::core::failure(write_error("geo.earthquake_displacement.metadata_write_failed", "could not open earthquake displacement metadata for writing", true)
                                                 .add_context("path", path.generic_string()));
            }
            output << json.dump(2) << '\n';
            output.flush();
            if (!output.good()) {
                return tsunami::core::failure(write_error("geo.earthquake_displacement.metadata_write_failed", "could not write earthquake displacement metadata", true)
                                                 .add_context("path", path.generic_string()));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto grid_matches(
            GDALDataset &dataset,
            const tsunami::geo::TerrainTargetGrid &grid,
            const tsunami::geo::TerrainTargetGridPolicy &policy,
            const EarthquakeDisplacementArtifactReadPolicy &read_policy,
            const std::filesystem::path &path) -> tsunami::core::Result<std::uint64_t>
        {
            const auto width = dataset.GetRasterXSize();
            const auto height = dataset.GetRasterYSize();
            if (width <= 0 || height <= 0) {
                return tsunami::core::failure<std::uint64_t>(
                    read_error("geo.earthquake_displacement.grid_mismatch", "earthquake displacement raster dimensions are invalid", tsunami::core::DiagnosticCategory::validation)
                        .add_context("path", path.generic_string()));
            }
            const auto width_u = static_cast<std::uint64_t>(width);
            const auto height_u = static_cast<std::uint64_t>(height);
            if (width_u > std::numeric_limits<std::uint64_t>::max() / height_u) {
                return tsunami::core::failure<std::uint64_t>(
                    read_error("geo.earthquake_displacement.grid_mismatch", "earthquake displacement raster cell count overflows", tsunami::core::DiagnosticCategory::validation)
                        .add_context("path", path.generic_string()));
            }
            const auto cells = width_u * height_u;
            if (width_u != grid.width() || height_u != grid.height() ||
                cells > read_policy.maximum_cells || cells > policy.maximum_output_cells) {
                return tsunami::core::failure<std::uint64_t>(
                    read_error("geo.earthquake_displacement.grid_mismatch", "earthquake displacement raster dimensions disagree with the terrain grid", tsunami::core::DiagnosticCategory::validation)
                        .add_context("path", path.generic_string())
                        .add_context("expected", std::to_string(grid.cell_count()))
                        .add_context("actual", std::to_string(cells)));
            }
            auto raw = std::array<double, 6U>{};
            if (dataset.GetGeoTransform(raw.data()) != CE_None) {
                return tsunami::core::failure<std::uint64_t>(
                    read_error("geo.earthquake_displacement.grid_mismatch", "earthquake displacement raster geotransform is missing", tsunami::core::DiagnosticCategory::validation)
                        .add_context("path", path.generic_string()));
            }
            const auto actual = tsunami::geo::RasterAffineTransform{raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]};
            const auto &expected = grid.transform();
            if (!close(actual.origin_x, expected.origin_x, policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance) ||
                !close(actual.pixel_width, expected.pixel_width, policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance) ||
                !close(actual.row_rotation, expected.row_rotation, policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance) ||
                !close(actual.origin_y, expected.origin_y, policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance) ||
                !close(actual.column_rotation, expected.column_rotation, policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance) ||
                !close(actual.pixel_height, expected.pixel_height, policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance)) {
                return tsunami::core::failure<std::uint64_t>(
                    read_error("geo.earthquake_displacement.grid_mismatch", "earthquake displacement raster affine transform disagrees with the terrain grid", tsunami::core::DiagnosticCategory::validation)
                        .add_context("path", path.generic_string()));
            }
            return tsunami::core::success(cells);
        }

        [[nodiscard]] auto read_raster(
            const EarthquakeDisplacementArtifactPaths &paths,
            const tsunami::geo::TerrainConditioningRecord &terrain_record,
            const EarthquakeDisplacementArtifactReadPolicy &policy)
            -> tsunami::core::Result<std::pair<std::vector<double>, std::vector<std::uint8_t>>>
        {
            auto dataset = detail::DatasetHandle{static_cast<GDALDataset *>(GDALOpenEx(
                paths.displacement_path.string().c_str(),
                GDAL_OF_RASTER | GDAL_OF_READONLY,
                nullptr,
                nullptr,
                nullptr))};
            if (!dataset) {
                return tsunami::core::failure<std::pair<std::vector<double>, std::vector<std::uint8_t>>>(
                    read_error("geo.earthquake_displacement.open_failed", "GDAL could not open earthquake displacement raster", tsunami::core::DiagnosticCategory::input_data)
                        .add_context("path", paths.displacement_path.generic_string()));
            }
            const auto *driver = dataset->GetDriver();
            const auto driver_short = driver == nullptr ? std::string{} : std::string{driver->GetDescription()};
            if (driver_short != "GTiff" || dataset->GetRasterCount() != 1) {
                return tsunami::core::failure<std::pair<std::vector<double>, std::vector<std::uint8_t>>>(
                    read_error("geo.earthquake_displacement.raster_invalid", "earthquake displacement artifact must be a single-band GeoTIFF", tsunami::core::DiagnosticCategory::validation)
                        .add_context("path", paths.displacement_path.generic_string()));
            }
            auto cells = grid_matches(*dataset, terrain_record.grid, terrain_record.grid_policy, policy, paths.displacement_path);
            if (!cells) {
                return tsunami::core::failure<std::pair<std::vector<double>, std::vector<std::uint8_t>>>(cells.error());
            }
            auto *band = dataset->GetRasterBand(1);
            if (band == nullptr || (band->GetRasterDataType() != GDT_Float64 && band->GetRasterDataType() != GDT_Float32)) {
                return tsunami::core::failure<std::pair<std::vector<double>, std::vector<std::uint8_t>>>(
                    read_error("geo.earthquake_displacement.raster_invalid", "earthquake displacement band must be Float32 or Float64", tsunami::core::DiagnosticCategory::validation)
                        .add_context("path", paths.displacement_path.generic_string()));
            }
            const auto description = band->GetDescription() == nullptr ? std::string{} : std::string{band->GetDescription()};
            const auto unit = band->GetUnitType() == nullptr ? std::string{} : std::string{band->GetUnitType()};
            if (description != earthquake_vertical_displacement_role || unit != "m") {
                return tsunami::core::failure<std::pair<std::vector<double>, std::vector<std::uint8_t>>>(
                    read_error("geo.earthquake_displacement.raster_invalid", "earthquake displacement band metadata is invalid", tsunami::core::DiagnosticCategory::validation)
                        .add_context("path", paths.displacement_path.generic_string()));
            }
            auto values = std::vector<double>(static_cast<std::size_t>(cells.value()));
            if (band->RasterIO(
                    GF_Read,
                    0,
                    0,
                    dataset->GetRasterXSize(),
                    dataset->GetRasterYSize(),
                    values.data(),
                    dataset->GetRasterXSize(),
                    dataset->GetRasterYSize(),
                    GDT_Float64,
                    0,
                    0) != CE_None) {
                return tsunami::core::failure<std::pair<std::vector<double>, std::vector<std::uint8_t>>>(
                    read_error("geo.earthquake_displacement.read_failed", "GDAL could not read earthquake displacement values", tsunami::core::DiagnosticCategory::input_data)
                        .add_context("path", paths.displacement_path.generic_string()));
            }
            auto valid = std::vector<std::uint8_t>(values.size(), 1U);
            int has_nodata = 0;
            const auto nodata = band->GetNoDataValue(&has_nodata);
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (!finite(values[index]) || (has_nodata != 0 && values[index] == nodata)) {
                    valid[index] = 0U;
                }
            }
            return tsunami::core::success(std::make_pair(std::move(values), std::move(valid)));
        }
    } // namespace

    auto read_earthquake_displacement_artifact_with_gdal(
        const EarthquakeDisplacementArtifactPaths &paths,
        const tsunami::geo::TerrainConditioningRecord &terrain_record,
        const EarthquakeDisplacementArtifactReadPolicy &policy)
        -> tsunami::core::Result<EarthquakeDisplacementArtifactReadResult>
    {
        if (paths.displacement_path.empty() || paths.metadata_path.empty() || policy.maximum_cells == 0U) {
            return tsunami::core::failure<EarthquakeDisplacementArtifactReadResult>(
                read_error("geo.earthquake_displacement.request_invalid", "earthquake displacement artifact read request is incomplete", tsunami::core::DiagnosticCategory::validation));
        }
        auto metadata = read_metadata(paths.metadata_path);
        if (!metadata) {
            return tsunami::core::failure<EarthquakeDisplacementArtifactReadResult>(metadata.error());
        }
        auto raster = read_raster(paths, terrain_record, policy);
        if (!raster) {
            return tsunami::core::failure<EarthquakeDisplacementArtifactReadResult>(raster.error());
        }

        auto diagnostics = EarthquakeDisplacementArtifactReadDiagnostics{};
        diagnostics.artifact_contract_version = metadata.value().artifact_contract_version;
        diagnostics.event_id = metadata.value().event_id;
        diagnostics.model_id = metadata.value().model_id;
        diagnostics.source_format = metadata.value().source_format;
        diagnostics.coordinate_reference = metadata.value().coordinate_reference;
        diagnostics.subfault_count = metadata.value().subfault_count;
        diagnostics.width = terrain_record.grid.width();
        diagnostics.height = terrain_record.grid.height();
        diagnostics.cell_count = terrain_record.grid.cell_count();
        diagnostics.minimum_vertical_displacement_m = std::numeric_limits<double>::infinity();
        diagnostics.maximum_vertical_displacement_m = -std::numeric_limits<double>::infinity();
        diagnostics.paths = paths;
        diagnostics.validation_status = "accepted";
        const auto &values = raster.value().first;
        const auto &valid = raster.value().second;
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (valid[index] == 0U) {
                continue;
            }
            ++diagnostics.valid_cell_count;
            diagnostics.minimum_vertical_displacement_m = std::min(diagnostics.minimum_vertical_displacement_m, values[index]);
            diagnostics.maximum_vertical_displacement_m = std::max(diagnostics.maximum_vertical_displacement_m, values[index]);
        }
        if (diagnostics.valid_cell_count == 0U) {
            return tsunami::core::failure<EarthquakeDisplacementArtifactReadResult>(
                read_error("geo.earthquake_displacement.raster_invalid", "earthquake displacement raster contains no valid cells", tsunami::core::DiagnosticCategory::validation)
                    .add_context("path", paths.displacement_path.generic_string()));
        }
        return tsunami::core::success(EarthquakeDisplacementArtifactReadResult{
            terrain_record.grid,
            std::move(raster.value().first),
            std::move(raster.value().second),
            std::move(metadata).value(),
            std::move(diagnostics)});
    }

    auto write_earthquake_displacement_artifact_with_gdal(
        const EarthquakeDisplacementArtifactPaths &paths,
        const tsunami::geo::TerrainTargetGrid &grid,
        const std::vector<double> &vertical_displacement_m,
        const std::vector<std::uint8_t> &valid_mask,
        const EarthquakeDisplacementArtifactMetadata &metadata)
        -> tsunami::core::Result<void>
    {
        if (paths.displacement_path.empty() || paths.metadata_path.empty() ||
            vertical_displacement_m.size() != grid.cell_count() ||
            valid_mask.size() != grid.cell_count() ||
            metadata.artifact_contract_version != earthquake_displacement_artifact_contract_version ||
            metadata.vertical_unit != "m") {
            return tsunami::core::failure(write_error("geo.earthquake_displacement.request_invalid", "earthquake displacement write request is invalid"));
        }
        for (std::size_t index = 0; index < vertical_displacement_m.size(); ++index) {
            if (valid_mask[index] != 0U && !finite(vertical_displacement_m[index])) {
                return tsunami::core::failure(write_error("geo.earthquake_displacement.request_invalid", "valid earthquake displacement cells must be finite"));
            }
        }
        std::filesystem::create_directories(paths.displacement_path.parent_path());
        auto *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (driver == nullptr) {
            return tsunami::core::failure(write_error("geo.earthquake_displacement.driver_unavailable", "GDAL GTiff driver is unavailable"));
        }
        auto dataset = detail::DatasetHandle{driver->Create(
            paths.displacement_path.string().c_str(),
            static_cast<int>(grid.width()),
            static_cast<int>(grid.height()),
            1,
            GDT_Float64,
            nullptr)};
        if (!dataset) {
            return tsunami::core::failure(write_error("geo.earthquake_displacement.write_failed", "could not create earthquake displacement GeoTIFF", true)
                                             .add_context("path", paths.displacement_path.generic_string()));
        }
        auto raw = std::array<double, 6U>{
            grid.transform().origin_x,
            grid.transform().pixel_width,
            grid.transform().row_rotation,
            grid.transform().origin_y,
            grid.transform().column_rotation,
            grid.transform().pixel_height};
        if (dataset->SetGeoTransform(raw.data()) != CE_None) {
            return tsunami::core::failure(write_error("geo.earthquake_displacement.write_failed", "could not write earthquake displacement geotransform", true)
                                             .add_context("path", paths.displacement_path.generic_string()));
        }
        auto *band = dataset->GetRasterBand(1);
        if (band == nullptr) {
            return tsunami::core::failure(write_error("geo.earthquake_displacement.write_failed", "could not access earthquake displacement band", true)
                                             .add_context("path", paths.displacement_path.generic_string()));
        }
        band->SetDescription(std::string{earthquake_vertical_displacement_role}.c_str());
        band->SetUnitType("m");
        constexpr auto nodata = -1.0e300;
        band->SetNoDataValue(nodata);
        auto values = vertical_displacement_m;
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (valid_mask[index] == 0U) {
                values[index] = nodata;
            }
        }
        if (band->RasterIO(
                GF_Write,
                0,
                0,
                static_cast<int>(grid.width()),
                static_cast<int>(grid.height()),
                values.data(),
                static_cast<int>(grid.width()),
                static_cast<int>(grid.height()),
                GDT_Float64,
                0,
                0) != CE_None) {
            return tsunami::core::failure(write_error("geo.earthquake_displacement.write_failed", "could not write earthquake displacement values", true)
                                             .add_context("path", paths.displacement_path.generic_string()));
        }
        dataset->FlushCache();
        if (auto written = write_metadata(paths.metadata_path, metadata); !written) {
            return written;
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo_gdal
