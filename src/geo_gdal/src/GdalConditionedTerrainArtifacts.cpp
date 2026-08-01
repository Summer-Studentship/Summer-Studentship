#include <tsunami/geo_gdal/GdalConditionedTerrainArtifacts.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <gdal_version.h>
#include <ogr_spatialref.h>

#include "GdalAdapterDetail.hpp"

namespace tsunami::geo_gdal
{
    namespace
    {
        enum class ArtifactRole
        {
            terrain,
            coverage,
            lineage
        };

        struct RasterPayload
        {
            std::vector<double> values;
            std::vector<std::uint8_t> mask;
            std::vector<std::uint16_t> lineage_codes;
        };

        struct ReadArtifact
        {
            RasterPayload payload;
            double minimum{};
            double maximum{};
        };

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

        [[nodiscard]] auto role_name(ArtifactRole role) noexcept -> std::string_view
        {
            switch (role) {
            case ArtifactRole::terrain:
                return conditioned_terrain_role;
            case ArtifactRole::coverage:
                return corridor_coverage_fraction_role;
            case ArtifactRole::lineage:
                return terrain_cell_lineage_role;
            }
            return conditioned_terrain_role;
        }

        [[nodiscard]] auto role_description(ArtifactRole role) noexcept -> std::string_view
        {
            switch (role) {
            case ArtifactRole::terrain:
                return "bed_elevation";
            case ArtifactRole::coverage:
                return "corridor_coverage_fraction";
            case ArtifactRole::lineage:
                return "cell_lineage_code";
            }
            return "bed_elevation";
        }

        [[nodiscard]] auto role_unit(ArtifactRole role) noexcept -> std::string_view
        {
            switch (role) {
            case ArtifactRole::terrain:
                return "m";
            case ArtifactRole::coverage:
            case ArtifactRole::lineage:
                return "1";
            }
            return "1";
        }

        [[nodiscard]] auto role_type(ArtifactRole role) noexcept -> GDALDataType
        {
            return role == ArtifactRole::lineage ? GDT_UInt16 : GDT_Float64;
        }

        [[nodiscard]] auto artifact_error(
            std::string family,
            std::string suffix,
            std::string message,
            tsunami::core::DiagnosticCategory category,
            std::string rule_id,
            std::string operation) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(family) + "." + std::move(suffix),
                std::move(message),
                category,
                tsunami::core::Severity::error};
            error.add_context("operation", std::move(operation))
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto read_error(
            std::string suffix,
            std::string message,
            tsunami::core::DiagnosticCategory category,
            std::string rule_id) -> tsunami::core::Error
        {
            return artifact_error(
                "geo.terrain.artifact_read",
                std::move(suffix),
                std::move(message),
                category,
                std::move(rule_id),
                "read_conditioned_terrain_artifacts_with_gdal");
        }

        [[nodiscard]] auto write_error(
            std::string suffix,
            std::string message,
            tsunami::core::DiagnosticCategory category,
            std::string rule_id) -> tsunami::core::Error
        {
            return artifact_error(
                "geo.terrain.artifact_write",
                std::move(suffix),
                std::move(message),
                category,
                std::move(rule_id),
                "write_conditioned_terrain_artifacts_with_gdal");
        }

        enum class DiagnosticFamily
        {
            read,
            write
        };

        [[nodiscard]] auto artifact_family_error(
            DiagnosticFamily family,
            std::string suffix,
            std::string message,
            tsunami::core::DiagnosticCategory category,
            std::string rule_id) -> tsunami::core::Error
        {
            return family == DiagnosticFamily::write
                ? write_error(std::move(suffix), std::move(message), category, std::move(rule_id))
                : read_error(std::move(suffix), std::move(message), category, std::move(rule_id));
        }

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto close(double left, double right, double absolute_tolerance, double relative_tolerance) noexcept -> bool
        {
            const auto tolerance = absolute_tolerance + (relative_tolerance * std::max({1.0, std::abs(left), std::abs(right)}));
            return finite(left) && finite(right) && std::abs(left - right) <= tolerance;
        }

        [[nodiscard]] auto test_config_enabled(const char *key) noexcept -> bool
        {
            const auto *value = CPLGetConfigOption(key, nullptr);
            return value != nullptr && CPLTestBool(value);
        }

        [[nodiscard]] auto test_config_count(const char *key) -> std::optional<std::size_t>
        {
            const auto *value = CPLGetConfigOption(key, nullptr);
            if (value == nullptr || std::string{value}.empty()) {
                return std::nullopt;
            }
            try {
                return static_cast<std::size_t>(std::stoull(value));
            } catch (const std::exception &) {
                return std::nullopt;
            }
        }

        [[nodiscard]] auto path_text_valid(const std::filesystem::path &path) -> bool
        {
            return !path.empty() && path.generic_string().find('\0') == std::string::npos;
        }

        [[nodiscard]] auto extension_supported(const std::filesystem::path &path) -> bool
        {
            const auto ext = path.extension().generic_string();
            return ext == ".tif" || ext == ".tiff";
        }

        [[nodiscard]] auto lexically_under_root(
            const std::filesystem::path &root,
            const std::filesystem::path &path) -> bool
        {
            const auto normal_root = root.lexically_normal();
            const auto normal_path = path.lexically_normal();
            auto root_it = normal_root.begin();
            auto path_it = normal_path.begin();
            for (; root_it != normal_root.end(); ++root_it, ++path_it) {
                if (path_it == normal_path.end() || *root_it != *path_it) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] auto contains_parent_component(const std::filesystem::path &path) -> bool
        {
            for (const auto &part : path) {
                if (part == "..") {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto existing_symlink_escapes_root(
            const std::filesystem::path &case_root,
            const std::filesystem::path &relative_output) -> bool
        {
            std::error_code ec;
            if (!std::filesystem::exists(case_root, ec)) {
                return false;
            }
            const auto canonical_root = std::filesystem::weakly_canonical(case_root, ec);
            if (ec) {
                return false;
            }
            auto current = case_root;
            auto remaining = relative_output;
            remaining.remove_filename();
            for (const auto &part : remaining) {
                current /= part;
                if (std::filesystem::exists(current, ec) && std::filesystem::is_symlink(current, ec)) {
                    const auto canonical_current = std::filesystem::weakly_canonical(current, ec);
                    if (!ec && !lexically_under_root(canonical_root, canonical_current)) {
                        return true;
                    }
                }
                ec.clear();
            }
            return false;
        }

        [[nodiscard]] auto paths_distinct(const ConditionedTerrainArtifactPaths &paths) -> bool
        {
            return paths.terrain_path != paths.coverage_path &&
                paths.terrain_path != paths.lineage_path &&
                paths.coverage_path != paths.lineage_path;
        }

        [[nodiscard]] auto validate_bundle_paths(
            const ConditionedTerrainArtifactPaths &paths,
            bool require_existing_files) -> tsunami::core::Result<void>
        {
            if (!path_text_valid(paths.terrain_path) || !path_text_valid(paths.coverage_path) ||
                !path_text_valid(paths.lineage_path) || !paths_distinct(paths) ||
                !extension_supported(paths.terrain_path) || !extension_supported(paths.coverage_path) ||
                !extension_supported(paths.lineage_path) ||
                paths.terrain_path.lexically_normal() != paths.terrain_path ||
                paths.coverage_path.lexically_normal() != paths.coverage_path ||
                paths.lineage_path.lexically_normal() != paths.lineage_path) {
                return tsunami::core::failure(read_error("path_invalid", "conditioned terrain artefact paths are invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.path_bundle"));
            }
            if (require_existing_files) {
                for (const auto *path : {&paths.terrain_path, &paths.coverage_path, &paths.lineage_path}) {
                    std::error_code ec;
                    if (!std::filesystem::exists(*path, ec)) {
                        return tsunami::core::failure(read_error("file_missing", "conditioned terrain artefact file is missing", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.file_present")
                            .add_context("path", path->generic_string()));
                    }
                    if (!std::filesystem::is_regular_file(*path, ec)) {
                        return tsunami::core::failure(read_error("file_missing", "conditioned terrain artefact path is not a regular file", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.file_regular")
                            .add_context("path", path->generic_string()));
                    }
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto version_text(const tsunami::core::SemanticVersion &version) -> std::string
        {
            return version.text();
        }

        [[nodiscard]] auto metadata_value(
            const tsunami::geo::TerrainConditioningRecord &record,
            std::string_view key,
            ArtifactRole role) -> std::string
        {
            if (key == "TSUNAMI_ARTIFACT_CONTRACT_VERSION") {
                return std::to_string(conditioned_terrain_artifact_contract_version);
            }
            if (key == "TSUNAMI_ARTIFACT_ROLE") {
                return std::string{role_name(role)};
            }
            if (key == "TSUNAMI_TERRAIN_ID") {
                return record.identity.terrain_id;
            }
            if (key == "TSUNAMI_TERRAIN_REVISION") {
                return std::to_string(record.identity.terrain_revision);
            }
            if (key == "TSUNAMI_CASE_ID") {
                return record.identity.case_revision.case_id.str();
            }
            if (key == "TSUNAMI_CASE_REVISION") {
                return std::to_string(record.identity.case_revision.revision);
            }
            if (key == "TSUNAMI_MANIFEST_ID") {
                return record.identity.manifest_id;
            }
            if (key == "TSUNAMI_MANIFEST_REVISION") {
                return std::to_string(record.identity.manifest_revision);
            }
            if (key == "TSUNAMI_OUTPUT_DATASET_ID") {
                return record.identity.output_dataset_id;
            }
            if (key == "TSUNAMI_OUTPUT_PROCESS_ID") {
                return record.identity.output_process_id;
            }
            if (key == "TSUNAMI_SCHEMA_NAME") {
                return record.schema.schema_name;
            }
            if (key == "TSUNAMI_SCHEMA_VERSION") {
                return version_text(record.schema.version);
            }
            if (key == "TSUNAMI_FORMULA_VERSION") {
                return record.formula_version;
            }
            if (key == "TSUNAMI_VERTICAL_DATUM_NAME") {
                return record.target_reference.vertical && record.target_reference.vertical->datum_name
                    ? *record.target_reference.vertical->datum_name
                    : "";
            }
            if (key == "TSUNAMI_VERTICAL_UNIT") {
                return record.target_reference.vertical_unit.value_or("");
            }
            if (key == "TSUNAMI_VERTICAL_POSITIVE") {
                return record.target_reference.vertical_positive.value_or("");
            }
            if (key == "TSUNAMI_LINEAGE_ENCODING_VERSION") {
                return std::string{tsunami::geo::terrain_cell_lineage_encoding_version};
            }
            return {};
        }

        [[nodiscard]] auto required_metadata_keys(ArtifactRole role) -> std::vector<std::string_view>
        {
            auto keys = std::vector<std::string_view>{
                "TSUNAMI_ARTIFACT_CONTRACT_VERSION",
                "TSUNAMI_ARTIFACT_ROLE",
                "TSUNAMI_TERRAIN_ID",
                "TSUNAMI_TERRAIN_REVISION",
                "TSUNAMI_CASE_ID",
                "TSUNAMI_CASE_REVISION",
                "TSUNAMI_MANIFEST_ID",
                "TSUNAMI_MANIFEST_REVISION",
                "TSUNAMI_OUTPUT_DATASET_ID",
                "TSUNAMI_OUTPUT_PROCESS_ID",
                "TSUNAMI_SCHEMA_NAME",
                "TSUNAMI_SCHEMA_VERSION",
                "TSUNAMI_FORMULA_VERSION",
                "TSUNAMI_VERTICAL_DATUM_NAME",
                "TSUNAMI_VERTICAL_UNIT",
                "TSUNAMI_VERTICAL_POSITIVE"};
            if (role == ArtifactRole::lineage) {
                keys.push_back("TSUNAMI_LINEAGE_ENCODING_VERSION");
            }
            return keys;
        }

        [[nodiscard]] auto metadata_suffix(std::string_view key) -> std::string
        {
            if (key == "TSUNAMI_ARTIFACT_ROLE") {
                return "role_mismatch";
            }
            if (key == "TSUNAMI_TERRAIN_ID" || key == "TSUNAMI_TERRAIN_REVISION" ||
                key == "TSUNAMI_CASE_ID" || key == "TSUNAMI_CASE_REVISION" ||
                key == "TSUNAMI_MANIFEST_ID" || key == "TSUNAMI_MANIFEST_REVISION") {
                return "identity_mismatch";
            }
            if (key == "TSUNAMI_LINEAGE_ENCODING_VERSION" || key == "TSUNAMI_FORMULA_VERSION") {
                return "band_metadata_mismatch";
            }
            return "band_metadata_mismatch";
        }

        auto set_metadata(GDALDataset &dataset, const tsunami::geo::TerrainConditioningRecord &record, ArtifactRole role)
            -> tsunami::core::Result<void>
        {
            if (dataset.SetMetadataItem("AREA_OR_POINT", "Area") != CE_None) {
                return tsunami::core::failure(write_error("temporary_validation_failed", "failed to set conditioned terrain artefact pixel registration metadata", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.metadata"));
            }
            for (const auto key : required_metadata_keys(role)) {
                if (dataset.SetMetadataItem(std::string{key}.c_str(), metadata_value(record, key, role).c_str()) != CE_None) {
                    return tsunami::core::failure(write_error("temporary_validation_failed", "failed to set conditioned terrain artefact metadata", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.metadata")
                        .add_context("expected", std::string{key}));
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto metadata_matches(
            GDALDataset &dataset,
            const tsunami::geo::TerrainConditioningRecord &record,
            ArtifactRole role,
            const std::filesystem::path &path) -> tsunami::core::Result<void>
        {
            const auto *area = dataset.GetMetadataItem(GDALMD_AREA_OR_POINT);
            if (area == nullptr || (std::string{area} != "Area" && std::string{area} != GDALMD_AOP_AREA)) {
                return tsunami::core::failure(read_error("grid_mismatch", "conditioned terrain artefact is not pixel-is-area", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.pixel_is_area")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)}));
            }
            for (const auto key : required_metadata_keys(role)) {
                const auto expected = metadata_value(record, key, role);
                const auto *actual_ptr = dataset.GetMetadataItem(std::string{key}.c_str());
                const auto actual = actual_ptr == nullptr ? std::string{} : std::string{actual_ptr};
                if (actual != expected) {
                    return tsunami::core::failure(read_error(metadata_suffix(key), "conditioned terrain artefact metadata does not match the accepted record", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.metadata")
                        .add_context("path", path.generic_string())
                        .add_context("artifact_role", std::string{role_name(role)})
                        .add_context("terrain_id", record.identity.terrain_id)
                        .add_context("terrain_revision", std::to_string(record.identity.terrain_revision))
                        .add_context("case_id", record.identity.case_revision.case_id.str())
                        .add_context("case_revision", std::to_string(record.identity.case_revision.revision))
                        .add_context("expected", expected)
                        .add_context("actual", actual));
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto make_expected_srs(
            const tsunami::geo::CoordinateReferenceDescriptor &descriptor) -> std::optional<OGRSpatialReference>
        {
            auto reference = OGRSpatialReference{};
            reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if (descriptor.canonical_wkt2) {
                const char *wkt = descriptor.canonical_wkt2->c_str();
                if (reference.importFromWkt(&wkt) == OGRERR_NONE) {
                    reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                    return reference;
                }
            }
            if (descriptor.canonical_projjson) {
                if (reference.SetFromUserInput(descriptor.canonical_projjson->c_str()) == OGRERR_NONE) {
                    reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                    return reference;
                }
            }
            if (descriptor.authority_name && descriptor.authority_code) {
                const auto code = *descriptor.authority_name + ":" + *descriptor.authority_code;
                if (reference.SetFromUserInput(code.c_str()) == OGRERR_NONE) {
                    reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
                    return reference;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] auto crs_matches(
            GDALDataset &dataset,
            const tsunami::geo::TerrainConditioningRecord &record,
            ArtifactRole role,
            const std::filesystem::path &path) -> tsunami::core::Result<void>
        {
            const auto expected = make_expected_srs(record.target_reference.horizontal);
            if (!expected) {
                return tsunami::core::failure(read_error("crs_mismatch", "accepted terrain record does not provide a GDAL-readable horizontal CRS", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.crs")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)}));
            }
            const auto *actual_ptr = dataset.GetSpatialRef();
            if (actual_ptr == nullptr) {
                return tsunami::core::failure(read_error("crs_mismatch", "conditioned terrain artefact has no horizontal CRS", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.crs")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)}));
            }
            auto actual = actual_ptr->Clone();
            if (actual == nullptr) {
                return tsunami::core::failure(read_error("crs_mismatch", "conditioned terrain artefact CRS could not be cloned", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.crs")
                    .add_context("path", path.generic_string()));
            }
            std::unique_ptr<OGRSpatialReference> actual_handle{actual};
            actual_handle->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            auto expected_copy = expected.value();
            expected_copy.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if (!expected_copy.IsSame(actual_handle.get())) {
                return tsunami::core::failure(read_error("crs_mismatch", "conditioned terrain artefact CRS differs from the accepted terrain record", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.crs")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)}));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto raster_extent(
            std::uint64_t width,
            std::uint64_t height,
            const tsunami::geo::RasterAffineTransform &transform) -> tsunami::geo::BoundingBox2D
        {
            const auto corner = [&](double column, double row) {
                return tsunami::geo::Point2D{
                    transform.origin_x + column * transform.pixel_width + row * transform.row_rotation,
                    transform.origin_y + column * transform.column_rotation + row * transform.pixel_height};
            };
            const auto corners = std::array{
                corner(0.0, 0.0),
                corner(static_cast<double>(width), 0.0),
                corner(0.0, static_cast<double>(height)),
                corner(static_cast<double>(width), static_cast<double>(height))};
            auto out = tsunami::geo::BoundingBox2D{corners.front().x, corners.front().y, corners.front().x, corners.front().y};
            for (const auto &candidate : corners) {
                out.minimum_x = std::min(out.minimum_x, candidate.x);
                out.minimum_y = std::min(out.minimum_y, candidate.y);
                out.maximum_x = std::max(out.maximum_x, candidate.x);
                out.maximum_y = std::max(out.maximum_y, candidate.y);
            }
            return out;
        }

        [[nodiscard]] auto geometry_matches(
            GDALDataset &dataset,
            const tsunami::geo::TerrainConditioningRecord &record,
            const ConditionedTerrainArtifactReadPolicy &policy,
            ArtifactRole role,
            const std::filesystem::path &path) -> tsunami::core::Result<std::uint64_t>
        {
            const auto width = dataset.GetRasterXSize();
            const auto height = dataset.GetRasterYSize();
            if (width <= 0 || height <= 0) {
                return tsunami::core::failure<std::uint64_t>(read_error("grid_mismatch", "conditioned terrain artefact dimensions are invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.dimensions")
                    .add_context("path", path.generic_string()));
            }
            const auto width_u = static_cast<std::uint64_t>(width);
            const auto height_u = static_cast<std::uint64_t>(height);
            if (width_u > std::numeric_limits<std::uint64_t>::max() / height_u) {
                return tsunami::core::failure<std::uint64_t>(read_error("grid_mismatch", "conditioned terrain artefact cell count overflows", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.cell_count")
                    .add_context("path", path.generic_string()));
            }
            const auto cells = width_u * height_u;
            if (width_u != record.grid.width() || height_u != record.grid.height() ||
                cells > policy.maximum_cells || cells > record.grid_policy.maximum_output_cells) {
                return tsunami::core::failure<std::uint64_t>(read_error("grid_mismatch", "conditioned terrain artefact grid dimensions disagree with the accepted record", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.dimensions")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)})
                    .add_context("expected", std::to_string(record.grid.cell_count()))
                    .add_context("actual", std::to_string(cells)));
            }
            auto raw = std::array<double, 6U>{};
            if (dataset.GetGeoTransform(raw.data()) != CE_None) {
                return tsunami::core::failure<std::uint64_t>(read_error("grid_mismatch", "conditioned terrain artefact geotransform is missing", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.affine")
                    .add_context("path", path.generic_string()));
            }
            const auto actual_transform = tsunami::geo::RasterAffineTransform{raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]};
            const auto &expected_transform = record.grid.transform();
            const auto abs_tol = record.grid_policy.numerical_absolute_tolerance;
            const auto rel_tol = record.grid_policy.numerical_relative_tolerance;
            if (!close(actual_transform.origin_x, expected_transform.origin_x, abs_tol, rel_tol) ||
                !close(actual_transform.pixel_width, expected_transform.pixel_width, abs_tol, rel_tol) ||
                !close(actual_transform.row_rotation, expected_transform.row_rotation, abs_tol, rel_tol) ||
                !close(actual_transform.origin_y, expected_transform.origin_y, abs_tol, rel_tol) ||
                !close(actual_transform.column_rotation, expected_transform.column_rotation, abs_tol, rel_tol) ||
                !close(actual_transform.pixel_height, expected_transform.pixel_height, abs_tol, rel_tol)) {
                return tsunami::core::failure<std::uint64_t>(read_error("grid_mismatch", "conditioned terrain artefact affine transform disagrees with the accepted record", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.affine")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)}));
            }
            const auto extent = raster_extent(width_u, height_u, actual_transform);
            const auto &expected_extent = record.grid.extent();
            if (!close(extent.minimum_x, expected_extent.minimum_x, abs_tol, rel_tol) ||
                !close(extent.minimum_y, expected_extent.minimum_y, abs_tol, rel_tol) ||
                !close(extent.maximum_x, expected_extent.maximum_x, abs_tol, rel_tol) ||
                !close(extent.maximum_y, expected_extent.maximum_y, abs_tol, rel_tol)) {
                return tsunami::core::failure<std::uint64_t>(read_error("grid_mismatch", "conditioned terrain artefact affine-derived extent disagrees with the accepted record", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.extent")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)}));
            }
            return tsunami::core::success(cells);
        }

        [[nodiscard]] auto band_metadata_matches(
            GDALRasterBand &band,
            ArtifactRole role,
            const std::filesystem::path &path) -> tsunami::core::Result<void>
        {
            if (band.GetRasterDataType() != role_type(role)) {
                return tsunami::core::failure(read_error("datatype_mismatch", "conditioned terrain artefact band native type is invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.band_type")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)})
                    .add_context("expected", GDALGetDataTypeName(role_type(role)))
                    .add_context("actual", GDALGetDataTypeName(band.GetRasterDataType())));
            }
            const auto description = band.GetDescription() == nullptr ? std::string{} : std::string{band.GetDescription()};
            if (description != role_description(role)) {
                return tsunami::core::failure(read_error("band_metadata_mismatch", "conditioned terrain artefact band description is invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.band_description")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)})
                    .add_context("expected", std::string{role_description(role)})
                    .add_context("actual", description));
            }
            const auto unit = band.GetUnitType() == nullptr ? std::string{} : std::string{band.GetUnitType()};
            if (unit != role_unit(role)) {
                return tsunami::core::failure(read_error("band_metadata_mismatch", "conditioned terrain artefact band unit is invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.band_unit")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)})
                    .add_context("expected", std::string{role_unit(role)})
                    .add_context("actual", unit));
            }
            int scale_set = 0;
            static_cast<void>(band.GetScale(&scale_set));
            int offset_set = 0;
            static_cast<void>(band.GetOffset(&offset_set));
            int nodata_set = 0;
            static_cast<void>(band.GetNoDataValue(&nodata_set));
            if (scale_set != 0 || offset_set != 0 || nodata_set != 0) {
                return tsunami::core::failure(read_error("band_metadata_mismatch", "conditioned terrain artefact band has unexpected scale, offset or nodata metadata", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.no_scale_offset_nodata")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)}));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto read_artifact(
            const std::filesystem::path &path,
            ArtifactRole role,
            const tsunami::geo::TerrainConditioningRecord &record,
            const ConditionedTerrainArtifactReadPolicy &policy) -> tsunami::core::Result<ReadArtifact>
        {
            auto dataset = detail::DatasetHandle{static_cast<GDALDataset *>(GDALOpenEx(
                path.string().c_str(),
                GDAL_OF_RASTER | GDAL_OF_READONLY,
                nullptr,
                nullptr,
                nullptr))};
            if (!dataset) {
                return tsunami::core::failure<ReadArtifact>(read_error("open_failed", "GDAL could not open conditioned terrain artefact", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.open")
                    .add_context("path", path.generic_string())
                    .add_context("artifact_role", std::string{role_name(role)}));
            }
            const auto *driver = dataset->GetDriver();
            const auto driver_short = driver == nullptr ? std::string{} : std::string{driver->GetDescription()};
            if (driver_short != "GTiff") {
                return tsunami::core::failure<ReadArtifact>(read_error("driver_mismatch", "conditioned terrain artefact is not a GeoTIFF", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.driver")
                    .add_context("path", path.generic_string())
                    .add_context("expected", "GTiff")
                    .add_context("actual", driver_short));
            }
            if (dataset->GetRasterCount() != 1) {
                return tsunami::core::failure<ReadArtifact>(read_error("band_count_invalid", "conditioned terrain artefact must contain exactly one band", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.band_count")
                    .add_context("path", path.generic_string())
                    .add_context("actual", std::to_string(dataset->GetRasterCount())));
            }
            const auto cells = geometry_matches(*dataset, record, policy, role, path);
            if (!cells) {
                return tsunami::core::failure<ReadArtifact>(cells.error());
            }
            if (auto metadata = metadata_matches(*dataset, record, role, path); !metadata) {
                return tsunami::core::failure<ReadArtifact>(metadata.error());
            }
            if (auto crs = crs_matches(*dataset, record, role, path); !crs) {
                return tsunami::core::failure<ReadArtifact>(crs.error());
            }
            auto *band = dataset->GetRasterBand(1);
            if (band == nullptr) {
                return tsunami::core::failure<ReadArtifact>(read_error("band_count_invalid", "conditioned terrain artefact band is missing", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.band_count")
                    .add_context("path", path.generic_string()));
            }
            if (auto band_valid = band_metadata_matches(*band, role, path); !band_valid) {
                return tsunami::core::failure<ReadArtifact>(band_valid.error());
            }
            auto output = ReadArtifact{};
            const auto width = static_cast<int>(record.grid.width());
            const auto height = static_cast<int>(record.grid.height());
            if (role == ArtifactRole::lineage) {
                output.payload.lineage_codes.resize(static_cast<std::size_t>(cells.value()));
                if (band->RasterIO(GF_Read, 0, 0, width, height, output.payload.lineage_codes.data(), width, height, GDT_UInt16, 0, 0) != CE_None) {
                    return tsunami::core::failure<ReadArtifact>(read_error("open_failed", "GDAL could not read lineage artefact values", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.read")
                        .add_context("path", path.generic_string()));
                }
                return tsunami::core::success(std::move(output));
            }
            output.payload.values.resize(static_cast<std::size_t>(cells.value()));
            if (band->RasterIO(GF_Read, 0, 0, width, height, output.payload.values.data(), width, height, GDT_Float64, 0, 0) != CE_None) {
                return tsunami::core::failure<ReadArtifact>(read_error("open_failed", "GDAL could not read conditioned terrain artefact values", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.read")
                    .add_context("path", path.generic_string()));
            }
            output.minimum = output.payload.values.empty() ? 0.0 : output.payload.values.front();
            output.maximum = output.minimum;
            for (const auto value : output.payload.values) {
                output.minimum = std::min(output.minimum, value);
                output.maximum = std::max(output.maximum, value);
            }
            if (role == ArtifactRole::terrain) {
                auto *mask_band = band->GetMaskBand();
                if (mask_band == nullptr) {
                    return tsunami::core::failure<ReadArtifact>(read_error("mask_invalid", "conditioned terrain artefact mask band is missing", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.mask")
                        .add_context("path", path.generic_string()));
                }
                output.payload.mask.resize(static_cast<std::size_t>(cells.value()));
                if (mask_band->RasterIO(GF_Read, 0, 0, width, height, output.payload.mask.data(), width, height, GDT_Byte, 0, 0) != CE_None) {
                    return tsunami::core::failure<ReadArtifact>(read_error("mask_invalid", "GDAL could not read terrain artefact mask band", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.mask")
                        .add_context("path", path.generic_string()));
                }
                for (std::size_t i = 0U; i < output.payload.mask.size(); ++i) {
                    if (output.payload.mask[i] != 0U && output.payload.mask[i] != 255U) {
                        return tsunami::core::failure<ReadArtifact>(read_error("mask_invalid", "conditioned terrain artefact mask is not binary", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.mask_binary")
                            .add_context("path", path.generic_string())
                            .add_context("raster_index", std::to_string(i))
                            .add_context("actual", std::to_string(output.payload.mask[i])));
                    }
                    output.payload.mask[i] = output.payload.mask[i] == 0U ? 0U : 1U;
                }
            }
            return tsunami::core::success(std::move(output));
        }

        [[nodiscard]] auto lineage_is_active(tsunami::geo::TerrainCellLineage lineage) noexcept -> bool
        {
            return lineage != tsunami::geo::TerrainCellLineage::outside_corridor &&
                lineage != tsunami::geo::TerrainCellLineage::excluded_boundary_fraction;
        }

        [[nodiscard]] auto lineage_is_bathymetry(tsunami::geo::TerrainCellLineage lineage) noexcept -> bool
        {
            return lineage == tsunami::geo::TerrainCellLineage::bathymetry_selected ||
                lineage == tsunami::geo::TerrainCellLineage::overlap_bathymetry_selected ||
                lineage == tsunami::geo::TerrainCellLineage::overlap_bathymetry_selected_with_conflict;
        }

        [[nodiscard]] auto lineage_is_topography(tsunami::geo::TerrainCellLineage lineage) noexcept -> bool
        {
            return lineage == tsunami::geo::TerrainCellLineage::topography_selected ||
                lineage == tsunami::geo::TerrainCellLineage::overlap_topography_selected ||
                lineage == tsunami::geo::TerrainCellLineage::overlap_topography_selected_with_conflict;
        }

        [[nodiscard]] auto lineage_is_overlap(tsunami::geo::TerrainCellLineage lineage) noexcept -> bool
        {
            return lineage == tsunami::geo::TerrainCellLineage::overlap_bathymetry_selected ||
                lineage == tsunami::geo::TerrainCellLineage::overlap_topography_selected ||
                lineage == tsunami::geo::TerrainCellLineage::overlap_bathymetry_selected_with_conflict ||
                lineage == tsunami::geo::TerrainCellLineage::overlap_topography_selected_with_conflict;
        }

        [[nodiscard]] auto lineage_is_conflict(tsunami::geo::TerrainCellLineage lineage) noexcept -> bool
        {
            return lineage == tsunami::geo::TerrainCellLineage::overlap_bathymetry_selected_with_conflict ||
                lineage == tsunami::geo::TerrainCellLineage::overlap_topography_selected_with_conflict;
        }

        [[nodiscard]] auto lineage_is_filled(tsunami::geo::TerrainCellLineage lineage) noexcept -> bool
        {
            return lineage == tsunami::geo::TerrainCellLineage::filled_from_bathymetry_neighbourhood ||
                lineage == tsunami::geo::TerrainCellLineage::filled_from_topography_neighbourhood;
        }

        struct SemanticCounts
        {
            std::uint64_t active{};
            std::uint64_t outside{};
            std::uint64_t excluded{};
            std::uint64_t bathymetry{};
            std::uint64_t topography{};
            std::uint64_t overlap{};
            std::uint64_t conflict{};
            std::uint64_t filled{};
            std::uint64_t unresolved{};
            std::uint64_t valid{};
            std::uint64_t invalid{};
            double minimum_bed{};
            double maximum_bed{};
            double minimum_coverage{};
            double maximum_coverage{};
            std::map<std::string, std::uint64_t> lineage_counts;
        };

        [[nodiscard]] auto validate_semantics(
            const std::vector<double> &bed,
            const std::vector<std::uint8_t> &mask,
            const std::vector<double> &coverage,
            const std::vector<tsunami::geo::TerrainCellLineage> &lineage,
            const tsunami::geo::TerrainConditioningRecord &record,
            DiagnosticFamily diagnostic_family) -> tsunami::core::Result<SemanticCounts>
        {
            auto counts = SemanticCounts{};
            counts.minimum_bed = std::numeric_limits<double>::infinity();
            counts.maximum_bed = -std::numeric_limits<double>::infinity();
            counts.minimum_coverage = std::numeric_limits<double>::infinity();
            counts.maximum_coverage = -std::numeric_limits<double>::infinity();
            for (std::size_t i = 0U; i < bed.size(); ++i) {
                const auto row = i / static_cast<std::size_t>(record.grid.width());
                const auto column = i % static_cast<std::size_t>(record.grid.width());
                const auto cov = coverage[i];
                if (!finite(cov) || cov < 0.0 || cov > 1.0) {
                    return tsunami::core::failure<SemanticCounts>(artifact_family_error(diagnostic_family, "coverage_invalid", "corridor coverage value is outside [0, 1]", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.coverage_range")
                        .add_context("row", std::to_string(row))
                        .add_context("column", std::to_string(column))
                        .add_context("raster_index", std::to_string(i)));
                }
                counts.minimum_coverage = std::min(counts.minimum_coverage, cov);
                counts.maximum_coverage = std::max(counts.maximum_coverage, cov);
                if (tsunami::geo::terrain_lineage_code(lineage[i]) == 0U) {
                    return tsunami::core::failure<SemanticCounts>(artifact_family_error(diagnostic_family, "lineage_code_invalid", "conditioned terrain contains an unrecognised lineage value", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.lineage_code")
                        .add_context("row", std::to_string(row))
                        .add_context("column", std::to_string(column))
                        .add_context("raster_index", std::to_string(i)));
                }
                counts.lineage_counts[std::string{tsunami::geo::to_string(lineage[i])}] += 1U;
                const auto is_outside = cov <= record.grid_policy.numerical_absolute_tolerance;
                const auto is_excluded = !is_outside &&
                    cov + record.grid_policy.numerical_absolute_tolerance < record.grid_policy.active_coverage_threshold;
                if (is_outside) {
                    ++counts.outside;
                    if (lineage[i] != tsunami::geo::TerrainCellLineage::outside_corridor || mask[i] != 0U) {
                        return tsunami::core::failure<SemanticCounts>(artifact_family_error(diagnostic_family, "bundle_inconsistent", "outside-corridor cell has inconsistent mask or lineage", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.semantic_partition")
                            .add_context("row", std::to_string(row))
                            .add_context("column", std::to_string(column)));
                    }
                    ++counts.invalid;
                    continue;
                }
                if (is_excluded) {
                    ++counts.excluded;
                    if (lineage[i] != tsunami::geo::TerrainCellLineage::excluded_boundary_fraction || mask[i] != 0U) {
                        return tsunami::core::failure<SemanticCounts>(artifact_family_error(diagnostic_family, "bundle_inconsistent", "excluded boundary-fraction cell has inconsistent mask or lineage", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.semantic_partition")
                            .add_context("row", std::to_string(row))
                            .add_context("column", std::to_string(column)));
                    }
                    ++counts.invalid;
                    continue;
                }
                ++counts.active;
                if (!lineage_is_active(lineage[i]) || mask[i] == 0U || !finite(bed[i])) {
                    return tsunami::core::failure<SemanticCounts>(artifact_family_error(diagnostic_family, "bundle_inconsistent", "active terrain cell has inconsistent value, mask or lineage", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.semantic_partition")
                        .add_context("row", std::to_string(row))
                        .add_context("column", std::to_string(column)));
                }
                ++counts.valid;
                counts.minimum_bed = std::min(counts.minimum_bed, bed[i]);
                counts.maximum_bed = std::max(counts.maximum_bed, bed[i]);
                if (lineage_is_bathymetry(lineage[i])) {
                    ++counts.bathymetry;
                }
                if (lineage_is_topography(lineage[i])) {
                    ++counts.topography;
                }
                if (lineage_is_overlap(lineage[i])) {
                    ++counts.overlap;
                }
                if (lineage_is_conflict(lineage[i])) {
                    ++counts.conflict;
                }
                if (lineage_is_filled(lineage[i])) {
                    ++counts.filled;
                }
            }
            if (counts.active != record.diagnostics.active_cell_count ||
                counts.outside != record.diagnostics.outside_corridor_cell_count ||
                counts.excluded != record.diagnostics.excluded_boundary_cell_count ||
                counts.bathymetry != record.diagnostics.bathymetry_selected_cell_count ||
                counts.topography != record.diagnostics.topography_selected_cell_count ||
                counts.overlap != record.diagnostics.overlap_cell_count ||
                counts.conflict != record.diagnostics.overlap_conflict_cell_count ||
                counts.filled != record.diagnostics.filled_cell_count ||
                counts.unresolved != record.diagnostics.unresolved_cell_count ||
                !close(counts.minimum_bed, record.diagnostics.minimum_elevation_m, record.grid_policy.numerical_absolute_tolerance, record.grid_policy.numerical_relative_tolerance) ||
                !close(counts.maximum_bed, record.diagnostics.maximum_elevation_m, record.grid_policy.numerical_absolute_tolerance, record.grid_policy.numerical_relative_tolerance)) {
                return tsunami::core::failure<SemanticCounts>(artifact_family_error(diagnostic_family, "bundle_inconsistent", "conditioned terrain artefact semantics disagree with record diagnostics", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.diagnostics")
                    .add_context("expected", "record diagnostics")
                    .add_context("actual", "reconstructed artefact diagnostics"));
            }
            return tsunami::core::success(std::move(counts));
        }

        [[nodiscard]] auto read_impl(
            const ConditionedTerrainArtifactPaths &paths,
            const tsunami::geo::TerrainConditioningRecord &record,
            const ConditionedTerrainArtifactReadPolicy &policy,
            bool require_existing_files) -> tsunami::core::Result<ConditionedTerrainArtifactReadResult>
        {
            detail::initialise_gdal_once();
            const auto disable_proj_network = ScopedConfigOption{"PROJ_NETWORK", "OFF"};
            if (auto valid = tsunami::geo::validate_terrain_conditioning_record(record); !valid) {
                return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(read_error("request_invalid", "accepted terrain record is invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.record")
                    .with_cause_code(valid.error().code()));
            }
            auto effective_policy = policy;
            if (effective_policy.maximum_cells == 0U) {
                effective_policy.maximum_cells = record.grid_policy.maximum_output_cells;
            }
            if (effective_policy.maximum_cells == 0U) {
                return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(read_error("request_invalid", "conditioned terrain artefact read policy cell limit is zero", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.policy"));
            }
            if (auto valid = validate_bundle_paths(paths, require_existing_files); !valid) {
                return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(valid.error());
            }
            auto terrain = read_artifact(paths.terrain_path, ArtifactRole::terrain, record, effective_policy);
            if (!terrain) {
                return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(terrain.error());
            }
            auto coverage = read_artifact(paths.coverage_path, ArtifactRole::coverage, record, effective_policy);
            if (!coverage) {
                return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(coverage.error());
            }
            auto lineage_artifact = read_artifact(paths.lineage_path, ArtifactRole::lineage, record, effective_policy);
            if (!lineage_artifact) {
                return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(lineage_artifact.error());
            }
            auto lineage = std::vector<tsunami::geo::TerrainCellLineage>{};
            lineage.reserve(lineage_artifact.value().payload.lineage_codes.size());
            for (std::size_t i = 0U; i < lineage_artifact.value().payload.lineage_codes.size(); ++i) {
                auto decoded = tsunami::geo::terrain_cell_lineage_from_code(lineage_artifact.value().payload.lineage_codes[i]);
                if (!decoded) {
                    return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(read_error("lineage_code_invalid", "conditioned terrain artefact contains an unknown lineage code", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.lineage_code")
                        .add_context("raster_index", std::to_string(i))
                        .add_context("actual", std::to_string(lineage_artifact.value().payload.lineage_codes[i]))
                        .with_cause_code(decoded.error().code()));
                }
                lineage.push_back(decoded.value());
            }
            auto counts = validate_semantics(
                terrain.value().payload.values,
                terrain.value().payload.mask,
                coverage.value().payload.values,
                lineage,
                record,
                DiagnosticFamily::read);
            if (!counts) {
                return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(counts.error());
            }
            auto reconstructed = tsunami::geo::make_conditioned_terrain_raster(
                record.grid,
                std::move(terrain.value().payload.values),
                std::move(terrain.value().payload.mask),
                std::move(coverage.value().payload.values),
                std::move(lineage));
            if (!reconstructed) {
                return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(read_error("construction_failed", "conditioned terrain raster reconstruction failed", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.construct")
                    .with_cause_code(reconstructed.error().code()));
            }
            if (!(reconstructed.value().grid() == record.grid)) {
                return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(read_error("grid_mismatch", "reconstructed conditioned terrain grid disagrees with record grid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.grid"));
            }
            auto diagnostics = ConditionedTerrainArtifactReadDiagnostics{};
            diagnostics.artefact_contract_version = conditioned_terrain_artifact_contract_version;
            diagnostics.gdal_runtime_version = GDALVersionInfo("RELEASE_NAME");
            diagnostics.terrain_id = record.identity.terrain_id;
            diagnostics.terrain_revision = record.identity.terrain_revision;
            diagnostics.width = record.grid.width();
            diagnostics.height = record.grid.height();
            diagnostics.cell_count = record.grid.cell_count();
            diagnostics.valid_terrain_cell_count = counts.value().valid;
            diagnostics.invalid_terrain_cell_count = counts.value().invalid;
            diagnostics.minimum_bed_elevation_m = counts.value().minimum_bed;
            diagnostics.maximum_bed_elevation_m = counts.value().maximum_bed;
            diagnostics.minimum_coverage_fraction = counts.value().minimum_coverage;
            diagnostics.maximum_coverage_fraction = counts.value().maximum_coverage;
            diagnostics.lineage_counts = std::move(counts.value().lineage_counts);
            diagnostics.paths = paths;
            diagnostics.validation_status = "accepted";
            return tsunami::core::success(ConditionedTerrainArtifactReadResult{std::move(reconstructed.value()), std::move(diagnostics)});
        }

        auto create_geotiff(
            const std::filesystem::path &path,
            const tsunami::geo::ConditionedTerrainRaster &terrain,
            const tsunami::geo::TerrainConditioningRecord &record,
            ArtifactRole role) -> tsunami::core::Result<void>
        {
            detail::initialise_gdal_once();
            auto *driver = GetGDALDriverManager()->GetDriverByName("GTiff");
            if (driver == nullptr) {
                return tsunami::core::failure(write_error("temporary_validation_failed", "GTiff driver is unavailable", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.gdal_driver"));
            }
            const auto parent = path.parent_path();
            if (!parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
                if (ec) {
                    return tsunami::core::failure(write_error("temporary_validation_failed", "failed to create conditioned terrain artefact parent directory", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.create_parent")
                        .add_context("path", path.generic_string()));
                }
            }
            char **options = nullptr;
            options = CSLSetNameValue(options, "TILED", "YES");
            options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
            options = CSLSetNameValue(options, "PREDICTOR", role_type(role) == GDT_Float64 ? "3" : "2");
            options = CSLSetNameValue(options, "BIGTIFF", "IF_SAFER");
            if (record.grid.width() > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
                record.grid.height() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                CSLDestroy(options);
                return tsunami::core::failure(write_error("request_invalid", "conditioned terrain artefact dimensions exceed GDAL integer limits", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.dimension_narrowing")
                    .add_context("path", path.generic_string()));
            }
            const auto width = static_cast<int>(record.grid.width());
            const auto height = static_cast<int>(record.grid.height());
            auto dataset = detail::DatasetHandle{driver->Create(
                path.string().c_str(),
                width,
                height,
                1,
                role_type(role),
                options)};
            CSLDestroy(options);
            if (!dataset) {
                return tsunami::core::failure(write_error("temporary_validation_failed", "failed to create conditioned terrain artefact GeoTIFF", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.create")
                    .add_context("path", path.generic_string()));
            }
            const auto &grid = terrain.grid();
            auto transform = std::array<double, 6U>{
                grid.transform().origin_x,
                grid.transform().pixel_width,
                grid.transform().row_rotation,
                grid.transform().origin_y,
                grid.transform().column_rotation,
                grid.transform().pixel_height};
            if (dataset->SetGeoTransform(transform.data()) != CE_None) {
                return tsunami::core::failure(write_error("temporary_validation_failed", "failed to set conditioned terrain artefact affine transform", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.affine")
                    .add_context("path", path.generic_string()));
            }
            const auto expected_srs = make_expected_srs(grid.target_reference().horizontal);
            if (!expected_srs) {
                return tsunami::core::failure(write_error("request_invalid", "terrain grid lacks a GDAL-readable horizontal CRS", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.crs"));
            }
            if (dataset->SetSpatialRef(&expected_srs.value()) != CE_None) {
                return tsunami::core::failure(write_error("temporary_validation_failed", "failed to set conditioned terrain artefact spatial reference", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.crs")
                    .add_context("path", path.generic_string()));
            }
            if (auto metadata = set_metadata(*dataset, record, role); !metadata) {
                return metadata;
            }
            auto *band = dataset->GetRasterBand(1);
            if (band == nullptr) {
                return tsunami::core::failure(write_error("temporary_validation_failed", "failed to access conditioned terrain artefact band", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.band"));
            }
            band->SetDescription(std::string{role_description(role)}.c_str());
            if (band->SetUnitType(std::string{role_unit(role)}.c_str()) != CE_None) {
                return tsunami::core::failure(write_error("temporary_validation_failed", "failed to set conditioned terrain artefact band unit", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.band_unit")
                    .add_context("path", path.generic_string()));
            }
            if (role == ArtifactRole::lineage) {
                auto codes = std::vector<std::uint16_t>{};
                codes.reserve(terrain.cell_lineage().size());
                for (std::size_t i = 0U; i < terrain.cell_lineage().size(); ++i) {
                    const auto code = tsunami::geo::terrain_lineage_code(terrain.cell_lineage()[i]);
                    if (code == 0U) {
                        return tsunami::core::failure(write_error("lineage_code_invalid", "conditioned terrain contains an unrecognised lineage value that cannot be persisted", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.lineage_code")
                            .add_context("path", path.generic_string())
                            .add_context("raster_index", std::to_string(i)));
                    }
                    codes.push_back(code);
                }
                if (band->RasterIO(GF_Write, 0, 0, width, height, codes.data(), width, height, GDT_UInt16, 0, 0) != CE_None) {
                    return tsunami::core::failure(write_error("temporary_validation_failed", "failed to write conditioned terrain lineage artefact", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.write")
                        .add_context("path", path.generic_string()));
                }
            } else {
                const auto &values = role == ArtifactRole::terrain ? terrain.values() : terrain.corridor_coverage_fraction();
                if (band->RasterIO(GF_Write, 0, 0, width, height, const_cast<double *>(values.data()), width, height, GDT_Float64, 0, 0) != CE_None) {
                    return tsunami::core::failure(write_error("temporary_validation_failed", "failed to write conditioned terrain artefact", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.write")
                        .add_context("path", path.generic_string()));
                }
            }
            if (role == ArtifactRole::terrain) {
                if (band->CreateMaskBand(GMF_PER_DATASET) != CE_None) {
                    return tsunami::core::failure(write_error("temporary_validation_failed", "failed to create conditioned terrain mask band", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.mask")
                        .add_context("path", path.generic_string()));
                }
                auto mask = std::vector<std::uint8_t>{};
                mask.reserve(terrain.valid_mask().size());
                for (const auto valid : terrain.valid_mask()) {
                    mask.push_back(valid == 0U ? 0U : 255U);
                }
                if (band->GetMaskBand()->RasterIO(GF_Write, 0, 0, width, height, mask.data(), width, height, GDT_Byte, 0, 0) != CE_None) {
                    return tsunami::core::failure(write_error("temporary_validation_failed", "failed to write conditioned terrain mask band", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.mask")
                        .add_context("path", path.generic_string()));
                }
            }
            if (dataset->FlushCache() != CE_None) {
                return tsunami::core::failure(write_error("temporary_validation_failed", "failed to flush conditioned terrain artefact GeoTIFF", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.flush")
                    .add_context("path", path.generic_string()));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto validate_terrain_against_record(
            const tsunami::geo::ConditionedTerrainRaster &terrain,
            const tsunami::geo::TerrainConditioningRecord &record) -> tsunami::core::Result<void>
        {
            if (!(terrain.grid() == record.grid) ||
                terrain.cell_count() != record.diagnostics.total_cell_count ||
                !close(terrain.minimum_elevation_m(), record.diagnostics.minimum_elevation_m, record.grid_policy.numerical_absolute_tolerance, record.grid_policy.numerical_relative_tolerance) ||
                !close(terrain.maximum_elevation_m(), record.diagnostics.maximum_elevation_m, record.grid_policy.numerical_absolute_tolerance, record.grid_policy.numerical_relative_tolerance)) {
                return tsunami::core::failure(write_error("request_invalid", "conditioned terrain raster disagrees with the accepted terrain record", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.request_terrain"));
            }
            auto semantic = validate_semantics(terrain.values(), terrain.valid_mask(), terrain.corridor_coverage_fraction(), terrain.cell_lineage(), record, DiagnosticFamily::write);
            if (!semantic) {
                return tsunami::core::failure(semantic.error());
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto parent_or_current(const std::filesystem::path &path) -> std::filesystem::path
        {
            return path.parent_path().empty() ? std::filesystem::path{"."} : path.parent_path();
        }

        [[nodiscard]] auto path_with_suffix(const std::filesystem::path &path, std::string_view suffix) -> std::filesystem::path
        {
            return std::filesystem::path{path.string() + std::string{suffix}};
        }

        [[nodiscard]] auto filesystem_items_for_artifact(const std::filesystem::path &path) -> std::array<std::filesystem::path, 3U>
        {
            return {path, path_with_suffix(path, ".msk"), path_with_suffix(path, ".aux.xml")};
        }

        [[nodiscard]] auto unique_token() -> std::string
        {
            static auto counter = std::atomic<std::uint64_t>{0U};
            auto random = std::random_device{}();
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto thread_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
            return std::to_string(ticks) + "-" + std::to_string(thread_hash) + "-" + std::to_string(random) + "-" + std::to_string(counter.fetch_add(1U));
        }

        [[nodiscard]] auto create_transaction_directory(const std::filesystem::path &parent) -> tsunami::core::Result<std::filesystem::path>
        {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return tsunami::core::failure<std::filesystem::path>(write_error("replacement_failed", "failed to create conditioned terrain artefact transaction parent directory", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.transaction_parent")
                    .add_context("path", parent.generic_string()));
            }
            for (auto attempt = 0U; attempt < 64U; ++attempt) {
                const auto candidate = parent / (".tsunami-terrain-artifact-txn-" + unique_token() + "-" + std::to_string(attempt));
                ec.clear();
                if (std::filesystem::create_directory(candidate, ec)) {
                    return tsunami::core::success(candidate);
                }
                if (ec && !std::filesystem::exists(candidate)) {
                    return tsunami::core::failure<std::filesystem::path>(write_error("replacement_failed", "failed to claim conditioned terrain artefact transaction directory", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.transaction_claim")
                        .add_context("path", candidate.generic_string()));
                }
            }
            return tsunami::core::failure<std::filesystem::path>(write_error("replacement_failed", "could not allocate a unique conditioned terrain artefact transaction directory", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.transaction_unique")
                .add_context("path", parent.generic_string()));
        }

        struct TransactionItem
        {
            std::filesystem::path original;
            std::filesystem::path backup;
            bool moved{};
        };

        struct InstalledItem
        {
            std::filesystem::path target;
        };

        struct BundleTransaction
        {
            ConditionedTerrainArtifactPaths staging_paths;
            std::vector<std::filesystem::path> directories;
            std::vector<TransactionItem> backup_items;
        };

        [[nodiscard]] auto directory_for_parent(
            BundleTransaction &transaction,
            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> &mapped_directories,
            const std::filesystem::path &parent) -> tsunami::core::Result<std::filesystem::path>
        {
            for (const auto &[mapped_parent, directory] : mapped_directories) {
                if (mapped_parent == parent) {
                    return tsunami::core::success(directory);
                }
            }
            auto directory = create_transaction_directory(parent);
            if (!directory) {
                return directory;
            }
            mapped_directories.push_back({parent, directory.value()});
            transaction.directories.push_back(directory.value());
            return directory;
        }

        [[nodiscard]] auto make_transaction_paths(const ConditionedTerrainArtifactPaths &targets) -> tsunami::core::Result<BundleTransaction>
        {
            auto transaction = BundleTransaction{};
            auto mapped_directories = std::vector<std::pair<std::filesystem::path, std::filesystem::path>>{};
            const auto terrain_dir = directory_for_parent(transaction, mapped_directories, parent_or_current(targets.terrain_path));
            if (!terrain_dir) {
                return tsunami::core::failure<BundleTransaction>(terrain_dir.error());
            }
            const auto coverage_dir = directory_for_parent(transaction, mapped_directories, parent_or_current(targets.coverage_path));
            if (!coverage_dir) {
                return tsunami::core::failure<BundleTransaction>(coverage_dir.error());
            }
            const auto lineage_dir = directory_for_parent(transaction, mapped_directories, parent_or_current(targets.lineage_path));
            if (!lineage_dir) {
                return tsunami::core::failure<BundleTransaction>(lineage_dir.error());
            }
            transaction.staging_paths = ConditionedTerrainArtifactPaths{
                terrain_dir.value() / ("staging-terrain" + targets.terrain_path.extension().generic_string()),
                coverage_dir.value() / ("staging-coverage" + targets.coverage_path.extension().generic_string()),
                lineage_dir.value() / ("staging-lineage" + targets.lineage_path.extension().generic_string())};
            const auto add_backup_items = [&](const std::filesystem::path &target, std::string_view role) -> void {
                const auto parent = parent_or_current(target);
                auto directory = std::filesystem::path{};
                for (const auto &[mapped_parent, mapped_directory] : mapped_directories) {
                    if (mapped_parent == parent) {
                        directory = mapped_directory;
                        break;
                    }
                }
                const auto items = filesystem_items_for_artifact(target);
                transaction.backup_items.push_back(TransactionItem{items[0], directory / ("backup-" + std::string{role} + target.extension().generic_string()), false});
                transaction.backup_items.push_back(TransactionItem{items[1], directory / ("backup-" + std::string{role} + ".msk"), false});
                transaction.backup_items.push_back(TransactionItem{items[2], directory / ("backup-" + std::string{role} + ".aux.xml"), false});
            };
            add_backup_items(targets.terrain_path, "terrain");
            add_backup_items(targets.coverage_path, "coverage");
            add_backup_items(targets.lineage_path, "lineage");
            auto all_paths = std::vector<std::filesystem::path>{
                targets.terrain_path,
                targets.coverage_path,
                targets.lineage_path,
                transaction.staging_paths.terrain_path,
                transaction.staging_paths.coverage_path,
                transaction.staging_paths.lineage_path};
            for (const auto &item : transaction.backup_items) {
                all_paths.push_back(item.backup);
            }
            for (std::size_t i = 0U; i < all_paths.size(); ++i) {
                for (std::size_t j = i + 1U; j < all_paths.size(); ++j) {
                    if (all_paths[i] == all_paths[j]) {
                        return tsunami::core::failure<BundleTransaction>(write_error("path_invalid", "conditioned terrain transaction paths collide", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.transaction_paths")
                            .add_context("path", all_paths[i].generic_string()));
                    }
                }
            }
            return tsunami::core::success(std::move(transaction));
        }

        [[nodiscard]] auto remove_owned_tree(const std::filesystem::path &path) -> bool
        {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                return true;
            }
            std::filesystem::remove_all(path, ec);
            return !ec;
        }

        [[nodiscard]] auto cleanup_staging(BundleTransaction &transaction) -> bool
        {
            auto ok = true;
            for (const auto &path : {
                     transaction.staging_paths.terrain_path,
                     transaction.staging_paths.coverage_path,
                     transaction.staging_paths.lineage_path}) {
                for (const auto &item : filesystem_items_for_artifact(path)) {
                    ok = remove_owned_tree(item) && ok;
                }
            }
            return ok;
        }

        [[nodiscard]] auto cleanup_transaction_directories(const BundleTransaction &transaction) -> bool
        {
            auto ok = true;
            for (auto it = transaction.directories.rbegin(); it != transaction.directories.rend(); ++it) {
                ok = remove_owned_tree(*it) && ok;
            }
            return ok;
        }

        auto ignore_cleanup(bool value) noexcept -> void
        {
            (void)value;
        }

        [[nodiscard]] auto restore_backups(std::vector<TransactionItem> &items) -> bool
        {
            auto ok = true;
            for (auto it = items.rbegin(); it != items.rend(); ++it) {
                if (!it->moved) {
                    continue;
                }
                std::error_code ec;
                std::filesystem::rename(it->backup, it->original, ec);
                ok = !ec && ok;
                if (!ec) {
                    it->moved = false;
                }
            }
            return ok;
        }

        [[nodiscard]] auto backup_existing_targets(BundleTransaction &transaction) -> tsunami::core::Result<void>
        {
            const auto fail_after_moved = test_config_count("TSUNAMI_TEST_TERRAIN_ARTIFACT_FAIL_BACKUP_AFTER_MOVED");
            auto moved_count = std::size_t{};
            for (auto &item : transaction.backup_items) {
                std::error_code ec;
                if (!std::filesystem::exists(item.original, ec)) {
                    continue;
                }
                if (fail_after_moved && moved_count >= *fail_after_moved) {
                    const auto rollback_ok = restore_backups(transaction.backup_items);
                    ignore_cleanup(cleanup_staging(transaction));
                    if (rollback_ok) {
                        ignore_cleanup(cleanup_transaction_directories(transaction));
                    }
                    return tsunami::core::failure(write_error(rollback_ok ? "replacement_failed" : "rollback_failed", rollback_ok ? "failed to prepare conditioned terrain artefact backups" : "failed to restore conditioned terrain artefact bundle after backup preparation failure", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.backup_prepare")
                        .add_context("path", item.original.generic_string())
                        .add_context("recovery_path", item.backup.generic_string()));
                }
                ec.clear();
                std::filesystem::rename(item.original, item.backup, ec);
                if (ec) {
                    const auto rollback_ok = restore_backups(transaction.backup_items);
                    ignore_cleanup(cleanup_staging(transaction));
                    if (rollback_ok) {
                        ignore_cleanup(cleanup_transaction_directories(transaction));
                    }
                    return tsunami::core::failure(write_error(rollback_ok ? "replacement_failed" : "rollback_failed", rollback_ok ? "failed to prepare conditioned terrain artefact backups" : "failed to restore conditioned terrain artefact bundle after backup preparation failure", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.backup_prepare")
                        .add_context("path", item.original.generic_string())
                        .add_context("recovery_path", item.backup.generic_string()));
                }
                item.moved = true;
                ++moved_count;
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto install_staging_item(
            const std::filesystem::path &source,
            const std::filesystem::path &target,
            std::vector<InstalledItem> &installed) -> tsunami::core::Result<void>
        {
            std::error_code ec;
            if (!std::filesystem::exists(source, ec)) {
                return tsunami::core::success();
            }
            std::filesystem::rename(source, target, ec);
            if (ec) {
                return tsunami::core::failure(write_error("replacement_failed", "failed to install conditioned terrain artefact transaction item", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.install")
                    .add_context("path", target.generic_string())
                    .add_context("recovery_path", source.generic_string()));
            }
            installed.push_back(InstalledItem{target});
            return tsunami::core::success();
        }

        auto remove_installed_items(std::vector<InstalledItem> &installed) -> bool
        {
            auto ok = true;
            for (auto it = installed.rbegin(); it != installed.rend(); ++it) {
                ok = remove_owned_tree(it->target) && ok;
            }
            installed.clear();
            return ok;
        }

        [[nodiscard]] auto install_staging_bundle(
            const ConditionedTerrainArtifactPaths &staging,
            const ConditionedTerrainArtifactPaths &targets,
            std::vector<InstalledItem> &installed) -> tsunami::core::Result<void>
        {
            const auto install_artifact = [&](const std::filesystem::path &source, const std::filesystem::path &target) -> tsunami::core::Result<void> {
                for (std::size_t i = 0U; i < filesystem_items_for_artifact(source).size(); ++i) {
                    const auto source_items = filesystem_items_for_artifact(source);
                    const auto target_items = filesystem_items_for_artifact(target);
                    if (auto installed_item = install_staging_item(source_items[i], target_items[i], installed); !installed_item) {
                        return installed_item;
                    }
                }
                return tsunami::core::success();
            };
            if (auto installed_artifact = install_artifact(staging.terrain_path, targets.terrain_path); !installed_artifact) {
                return installed_artifact;
            }
            if (auto installed_artifact = install_artifact(staging.coverage_path, targets.coverage_path); !installed_artifact) {
                return installed_artifact;
            }
            if (auto installed_artifact = install_artifact(staging.lineage_path, targets.lineage_path); !installed_artifact) {
                return installed_artifact;
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto diagnostics_equivalent_except_paths(
            const ConditionedTerrainArtifactReadDiagnostics &left,
            const ConditionedTerrainArtifactReadDiagnostics &right) -> bool
        {
            return left.artefact_contract_version == right.artefact_contract_version &&
                left.gdal_runtime_version == right.gdal_runtime_version &&
                left.terrain_id == right.terrain_id &&
                left.terrain_revision == right.terrain_revision &&
                left.width == right.width &&
                left.height == right.height &&
                left.cell_count == right.cell_count &&
                left.valid_terrain_cell_count == right.valid_terrain_cell_count &&
                left.invalid_terrain_cell_count == right.invalid_terrain_cell_count &&
                left.minimum_bed_elevation_m == right.minimum_bed_elevation_m &&
                left.maximum_bed_elevation_m == right.maximum_bed_elevation_m &&
                left.minimum_coverage_fraction == right.minimum_coverage_fraction &&
                left.maximum_coverage_fraction == right.maximum_coverage_fraction &&
                left.lineage_counts == right.lineage_counts &&
                left.validation_status == right.validation_status;
        }

        auto replace_bundle(
            BundleTransaction &transaction,
            const ConditionedTerrainArtifactPaths &targets,
            const tsunami::geo::ConditionedTerrainRaster &terrain,
            const tsunami::geo::TerrainConditioningRecord &record,
            const ConditionedTerrainArtifactReadResult &temporary_validation) -> tsunami::core::Result<void>
        {
            if (auto backed_up = backup_existing_targets(transaction); !backed_up) {
                return backed_up;
            }
            auto installed = std::vector<InstalledItem>{};
            auto installed_ok = install_staging_bundle(transaction.staging_paths, targets, installed);
            if (!installed_ok) {
                const auto cleanup_ok = remove_installed_items(installed);
                const auto rollback_ok = restore_backups(transaction.backup_items);
                ignore_cleanup(cleanup_staging(transaction));
                if (rollback_ok && cleanup_ok) {
                    ignore_cleanup(cleanup_transaction_directories(transaction));
                }
                return tsunami::core::failure(write_error((rollback_ok && cleanup_ok) ? "replacement_failed" : "rollback_failed", (rollback_ok && cleanup_ok) ? "failed to replace conditioned terrain artefact bundle" : "failed to restore conditioned terrain artefact bundle after replacement failure", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.replace")
                    .with_cause_code(installed_ok.error().code()));
            }
            if (test_config_enabled("TSUNAMI_TEST_TERRAIN_ARTIFACT_FORCE_FINAL_READBACK_FAILURE")) {
                const auto cleanup_ok = remove_installed_items(installed);
                const auto rollback_ok = restore_backups(transaction.backup_items);
                ignore_cleanup(cleanup_staging(transaction));
                if (rollback_ok && cleanup_ok) {
                    ignore_cleanup(cleanup_transaction_directories(transaction));
                }
                return tsunami::core::failure(write_error((rollback_ok && cleanup_ok) ? "replacement_failed" : "rollback_failed", (rollback_ok && cleanup_ok) ? "final conditioned terrain artefact bundle failed strict validation" : "failed to restore conditioned terrain artefact bundle after final validation failure", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.final_readback"));
            }
            auto final_validation = read_impl(targets, record, ConditionedTerrainArtifactReadPolicy{record.grid_policy.maximum_output_cells}, true);
            if (!final_validation ||
                !(final_validation.value().terrain == terrain) ||
                !diagnostics_equivalent_except_paths(final_validation.value().diagnostics, temporary_validation.diagnostics)) {
                const auto cleanup_ok = remove_installed_items(installed);
                const auto rollback_ok = restore_backups(transaction.backup_items);
                ignore_cleanup(cleanup_staging(transaction));
                if (rollback_ok && cleanup_ok) {
                    ignore_cleanup(cleanup_transaction_directories(transaction));
                }
                auto error = write_error((rollback_ok && cleanup_ok) ? "replacement_failed" : "rollback_failed", (rollback_ok && cleanup_ok) ? "final conditioned terrain artefact bundle failed strict validation" : "failed to restore conditioned terrain artefact bundle after final validation failure", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.final_readback");
                if (!final_validation) {
                    error.with_cause_code(final_validation.error().code());
                }
                return tsunami::core::failure(error);
            }
            ignore_cleanup(cleanup_staging(transaction));
            if (!cleanup_transaction_directories(transaction)) {
                return tsunami::core::failure(write_error("replacement_failed", "failed to remove conditioned terrain artefact transaction-owned backup files after successful replacement", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.transaction_cleanup"));
            }
            return tsunami::core::success();
        }
    }

    auto make_conditioned_terrain_artifact_paths(
        const std::filesystem::path &case_root,
        const tsunami::geo::TerrainConditioningRecord &record)
        -> tsunami::core::Result<ConditionedTerrainArtifactPaths>
    {
        if (auto valid = tsunami::geo::validate_terrain_conditioning_record(record); !valid) {
            return tsunami::core::failure<ConditionedTerrainArtifactPaths>(read_error("request_invalid", "accepted terrain record is invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.record")
                .with_cause_code(valid.error().code()));
        }
        if (!path_text_valid(case_root) || record.output_path.is_absolute() || record.output_path.has_root_name()) {
            return tsunami::core::failure<ConditionedTerrainArtifactPaths>(read_error("path_invalid", "case root or terrain output path is invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.path_bundle"));
        }
        const auto root = case_root.lexically_normal();
        const auto primary_relative = record.output_path.lexically_normal();
        if (contains_parent_component(primary_relative) ||
            primary_relative.empty() ||
            !extension_supported(primary_relative) ||
            primary_relative.begin()->generic_string() != "outputs" ||
            existing_symlink_escapes_root(root, primary_relative)) {
            const auto primary = (root == "." ? primary_relative : (root / primary_relative)).lexically_normal();
            return tsunami::core::failure<ConditionedTerrainArtifactPaths>(read_error("path_invalid", "conditioned terrain primary artefact path is outside the case root or unsupported", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.path_bundle")
                .add_context("path", primary.generic_string()));
        }
        auto rel_it = primary_relative.begin();
        if (rel_it == primary_relative.end() || rel_it->generic_string() != "outputs") {
            return tsunami::core::failure<ConditionedTerrainArtifactPaths>(read_error("path_invalid", "conditioned terrain primary artefact must be under outputs/terrain", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.path_bundle"));
        }
        ++rel_it;
        if (rel_it == primary_relative.end() || rel_it->generic_string() != "terrain") {
            return tsunami::core::failure<ConditionedTerrainArtifactPaths>(read_error("path_invalid", "conditioned terrain primary artefact must be under outputs/terrain", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.path_bundle"));
        }
        const auto primary = (root == "." ? primary_relative : (root / primary_relative)).lexically_normal();
        const auto stem = primary.stem().generic_string();
        const auto ext = primary.extension().generic_string();
        auto paths = ConditionedTerrainArtifactPaths{
            primary,
            primary.parent_path() / (stem + ".coverage" + ext),
            primary.parent_path() / (stem + ".lineage" + ext)};
        paths.coverage_path = paths.coverage_path.lexically_normal();
        paths.lineage_path = paths.lineage_path.lexically_normal();
        const auto under_root = root == "." ||
            (lexically_under_root(root, paths.terrain_path) &&
                lexically_under_root(root, paths.coverage_path) &&
                lexically_under_root(root, paths.lineage_path));
        if (!paths_distinct(paths) || !under_root) {
            return tsunami::core::failure<ConditionedTerrainArtifactPaths>(read_error("path_invalid", "conditioned terrain artefact sibling paths are invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.path_bundle"));
        }
        return tsunami::core::success(std::move(paths));
    }

    auto read_conditioned_terrain_artifacts_with_gdal(
        const ConditionedTerrainArtifactPaths &paths,
        const tsunami::geo::TerrainConditioningRecord &record,
        const ConditionedTerrainArtifactReadPolicy &policy)
        -> tsunami::core::Result<ConditionedTerrainArtifactReadResult>
    {
        try {
            return read_impl(paths, record, policy, true);
        } catch (const std::exception &ex) {
            return tsunami::core::failure<ConditionedTerrainArtifactReadResult>(read_error("open_failed", ex.what(), tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.no_throw"));
        }
    }

    auto write_conditioned_terrain_artifacts_with_gdal(
        const ConditionedTerrainArtifactPaths &paths,
        const tsunami::geo::ConditionedTerrainRaster &terrain,
        const tsunami::geo::TerrainConditioningRecord &record)
        -> tsunami::core::Result<void>
    {
        try {
            detail::initialise_gdal_once();
            const auto disable_proj_network = ScopedConfigOption{"PROJ_NETWORK", "OFF"};
            const auto internal_gtiff_masks = ScopedConfigOption{"GDAL_TIFF_INTERNAL_MASK", "YES"};
            if (auto valid = tsunami::geo::validate_terrain_conditioning_record(record); !valid) {
                return tsunami::core::failure(write_error("request_invalid", "accepted terrain record is invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.record")
                    .with_cause_code(valid.error().code()));
            }
            if (auto valid = validate_bundle_paths(paths, false); !valid) {
                return tsunami::core::failure(write_error("path_invalid", "conditioned terrain artefact output paths are invalid", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.path_bundle")
                    .with_cause_code(valid.error().code()));
            }
            if (auto valid = validate_terrain_against_record(terrain, record); !valid) {
                return valid;
            }
            auto transaction = make_transaction_paths(paths);
            if (!transaction) {
                return tsunami::core::failure(transaction.error());
            }
            if (auto written = create_geotiff(transaction.value().staging_paths.terrain_path, terrain, record, ArtifactRole::terrain); !written) {
                ignore_cleanup(cleanup_staging(transaction.value()));
                ignore_cleanup(cleanup_transaction_directories(transaction.value()));
                return written;
            }
            if (test_config_enabled("TSUNAMI_TEST_TERRAIN_ARTIFACT_FAIL_AFTER_TERRAIN_STAGING")) {
                ignore_cleanup(cleanup_staging(transaction.value()));
                ignore_cleanup(cleanup_transaction_directories(transaction.value()));
                return tsunami::core::failure(write_error("temporary_validation_failed", "test seam forced failure after terrain staging", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.test_seam"));
            }
            if (auto written = create_geotiff(transaction.value().staging_paths.coverage_path, terrain, record, ArtifactRole::coverage); !written) {
                ignore_cleanup(cleanup_staging(transaction.value()));
                ignore_cleanup(cleanup_transaction_directories(transaction.value()));
                return written;
            }
            if (test_config_enabled("TSUNAMI_TEST_TERRAIN_ARTIFACT_FAIL_AFTER_COVERAGE_STAGING")) {
                ignore_cleanup(cleanup_staging(transaction.value()));
                ignore_cleanup(cleanup_transaction_directories(transaction.value()));
                return tsunami::core::failure(write_error("temporary_validation_failed", "test seam forced failure after coverage staging", tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.test_seam"));
            }
            if (auto written = create_geotiff(transaction.value().staging_paths.lineage_path, terrain, record, ArtifactRole::lineage); !written) {
                ignore_cleanup(cleanup_staging(transaction.value()));
                ignore_cleanup(cleanup_transaction_directories(transaction.value()));
                return written;
            }
            auto validation = read_impl(transaction.value().staging_paths, record, ConditionedTerrainArtifactReadPolicy{record.grid_policy.maximum_output_cells}, true);
            if (!validation || !(validation.value().terrain == terrain) ||
                validation.value().diagnostics.valid_terrain_cell_count != record.diagnostics.active_cell_count) {
                ignore_cleanup(cleanup_staging(transaction.value()));
                ignore_cleanup(cleanup_transaction_directories(transaction.value()));
                auto error = write_error("temporary_validation_failed", "temporary conditioned terrain artefact bundle failed strict read-back validation", tsunami::core::DiagnosticCategory::validation, "geo.terrain.artifact.temporary_readback");
                if (!validation) {
                    error.with_cause_code(validation.error().code());
                }
                return tsunami::core::failure(error);
            }
            auto replaced = replace_bundle(transaction.value(), paths, terrain, record, validation.value());
            if (!replaced) {
                ignore_cleanup(cleanup_staging(transaction.value()));
                return replaced;
            }
            return tsunami::core::success();
        } catch (const std::exception &ex) {
            return tsunami::core::failure(write_error("temporary_validation_failed", ex.what(), tsunami::core::DiagnosticCategory::input_data, "geo.terrain.artifact.no_throw"));
        }
    }

} // namespace tsunami::geo_gdal
