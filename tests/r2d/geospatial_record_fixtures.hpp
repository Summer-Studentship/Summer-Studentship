#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <tsunami/geo/ConditionedTerrainRaster.hpp>
#include <tsunami/geo/CorridorConstructionRecord.hpp>
#include <tsunami/geo/TerrainConditioningRecord.hpp>

namespace tsunami::tests::r2d_fixtures
{
    struct TerrainRecordInput
    {
        tsunami::geo::ConditionedTerrainRaster terrain;
        tsunami::geo::TerrainConditioningRecord record;
    };

    [[nodiscard]] inline auto case_revision(std::string case_id, std::uint64_t revision = 1U)
        -> tsunami::data::CaseRevisionRef
    {
        return tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string(std::move(case_id)).value(), revision};
    }

    [[nodiscard]] inline auto horizontal_reference(std::string code = "EN-METRIC-1")
        -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"TEST"},
            std::move(code),
            "Synthetic east-north metric reference",
            std::string{"LOCAL_CS[\"Synthetic metric\"]"},
            std::nullopt,
            std::string{"Synthetic horizontal datum"},
            std::string{"Synthetic horizontal realisation"},
            2026.0,
            {"Easting", "Northing"},
            {"east", "north"},
            {"m", "m"}};
    }

    [[nodiscard]] inline auto vertical_reference(std::string name = "synthetic-positive-up")
        -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::geo::CoordinateReferenceDescriptor{
            std::nullopt,
            std::nullopt,
            std::move(name),
            std::string{"VERTCRS[\"synthetic-positive-up\"]"},
            std::nullopt,
            std::string{"Synthetic vertical datum"},
            std::string{"Synthetic vertical realisation"},
            2026.0,
            {"Gravity-related height"},
            {"up"},
            {"m"}};
    }

    [[nodiscard]] inline auto target_reference(
        std::string horizontal_code = "EN-METRIC-1",
        tsunami::geo::ComputationalAxisConvention storage_axes = tsunami::geo::ComputationalAxisConvention::east_north)
        -> tsunami::geo::ComputationalTargetReference
    {
        return tsunami::geo::ComputationalTargetReference{
            horizontal_reference(std::move(horizontal_code)),
            vertical_reference(),
            storage_axes,
            "m",
            std::string{"m"},
            std::string{"up"}};
    }

    [[nodiscard]] inline auto transformation_identity(
        std::string transformation_id,
        std::string import_id,
        std::string dataset_id,
        std::string asset_id,
        const tsunami::data::CaseRevisionRef &case_ref,
        std::string manifest_id,
        std::uint64_t manifest_revision,
        std::string output_dataset_id = "transformed-dataset",
        std::string output_process_id = "transform-process") -> tsunami::geo::CoordinateTransformationIdentity
    {
        return tsunami::geo::CoordinateTransformationIdentity{
            std::move(transformation_id),
            1U,
            case_ref,
            std::move(manifest_id),
            manifest_revision,
            std::move(import_id),
            1U,
            std::move(dataset_id),
            std::move(asset_id),
            std::move(output_dataset_id),
            std::move(output_process_id),
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard]] inline auto import_identity(
        std::string import_id,
        std::string dataset_id,
        std::string asset_id,
        const tsunami::data::CaseRevisionRef &case_ref,
        std::string manifest_id,
        std::uint64_t manifest_revision) -> tsunami::geo::GeospatialImportIdentity
    {
        return tsunami::geo::GeospatialImportIdentity{
            std::move(import_id),
            1U,
            case_ref,
            std::move(manifest_id),
            manifest_revision,
            std::move(dataset_id),
            std::move(asset_id),
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard]] inline auto operation(
        std::string dataset_id,
        const tsunami::geo::CoordinateReferenceDescriptor &target) -> tsunami::geo::CoordinateOperationRecord
    {
        return tsunami::geo::CoordinateOperationRecord{
            dataset_id + " synthetic operation",
            std::string{"TEST"},
            std::string{"1001"},
            std::string{"Synthetic fixture method"},
            0.0,
            std::string{"synthetic fixture operation scope"},
            tsunami::geo::GeographicAreaOfInterest{-1.0, -1.0, 1.0, 1.0},
            std::nullopt,
            std::string{"{\"type\":\"Conversion\"}"},
            std::string{"+proj=noop"},
            false,
            horizontal_reference("SOURCE-" + dataset_id),
            target,
            {},
            "fixture-engine",
            "1.0",
            std::string{"fixture-db"}};
    }

    [[nodiscard]] inline auto resampling_record(
        std::string dataset_id,
        std::string asset_id,
        tsunami::geo::TerrainSourceRole role,
        const tsunami::geo::TerrainTargetGrid &grid,
        const tsunami::geo::TerrainTargetGridPolicy &policy,
        const tsunami::data::CaseRevisionRef &case_ref,
        std::string manifest_id,
        std::uint64_t manifest_revision,
        std::uint64_t valid_count,
        std::uint64_t nodata_count = 0U,
        std::uint64_t outside_count = 0U) -> tsunami::geo::RasterResamplingRecord
    {
        auto record = tsunami::geo::RasterResamplingRecord{};
        record.dataset_id = dataset_id;
        record.asset_id = asset_id;
        record.import_identity = import_identity(dataset_id + "-import", dataset_id, asset_id, case_ref, manifest_id, manifest_revision);
        record.transformation_identity = transformation_identity(
            dataset_id + "-transform",
            record.import_identity.import_id,
            dataset_id,
            asset_id,
            case_ref,
            manifest_id,
            manifest_revision,
            dataset_id + "-projected",
            dataset_id + "-transform-process");
        record.role = role;
        record.kernel = tsunami::geo::RasterResamplingKernel::bilinear;
        record.source_registration = tsunami::geo::RasterCellRegistration::pixel_is_area;
        record.target_registration = grid.registration();
        record.minimum_source_spacing_m = grid.spacing_m();
        record.maximum_source_spacing_m = grid.spacing_m();
        record.nominal_source_spacing_m = grid.spacing_m();
        record.target_spacing_m = grid.spacing_m();
        record.maximum_upsampling_factor = policy.maximum_upsampling_factor;
        record.source_valid_cell_count = valid_count;
        record.output_valid_cell_count = valid_count;
        record.source_nodata_cell_count = nodata_count;
        record.outside_coverage_cell_count = outside_count;
        record.operation = operation(dataset_id, grid.target_reference().horizontal);
        record.vertical_steps = tsunami::geo::VerticalTransformationSpecification{false, {}};
        record.adapter_name = "fixture";
        record.adapter_version = "1.0";
        return record;
    }

    [[nodiscard]] inline auto valid_terrain_record(
        const tsunami::geo::TerrainTargetGrid &grid,
        std::vector<double> values,
        std::vector<std::uint8_t> mask,
        std::vector<double> coverage,
        std::vector<tsunami::geo::TerrainCellLineage> lineage,
        tsunami::geo::CorridorConstructionIdentity corridor_identity,
        std::string terrain_id,
        std::string case_id,
        std::string manifest_id,
        std::uint64_t manifest_revision = 1U) -> TerrainRecordInput
    {
        const auto count = static_cast<std::uint64_t>(values.size());
        const auto case_ref = case_revision(std::move(case_id));
        const auto policy = tsunami::geo::TerrainTargetGridPolicy{
            grid.spacing_m(),
            0.5,
            4.0,
            std::max<std::uint64_t>(count, 1U),
            1.0e-9,
            1.0e-12,
            "synthetic R2D terrain grid policy"};
        auto minimum = std::numeric_limits<double>::infinity();
        auto maximum = -std::numeric_limits<double>::infinity();
        for (const auto value : values) {
            if (std::isfinite(value)) {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }
        if (!std::isfinite(minimum)) {
            minimum = 0.0;
            maximum = 0.0;
        }

        auto terrain = tsunami::geo::ConditionedTerrainRaster{
            grid,
            std::move(values),
            std::move(mask),
            std::move(coverage),
            std::move(lineage),
            minimum,
            maximum};

        auto record = tsunami::geo::TerrainConditioningRecord{};
        record.schema = tsunami::data::SchemaIdentity{
            std::string{tsunami::geo::terrain_conditioning_record_schema_name},
            tsunami::geo::supported_terrain_conditioning_record_version};
        record.policy_version = tsunami::geo::supported_terrain_conditioning_record_policy_version;
        record.formula_version = tsunami::geo::terrain_conditioning_formula_version;
        record.identity = tsunami::geo::TerrainConditioningIdentity{
            std::move(terrain_id),
            1U,
            case_ref,
            manifest_id,
            manifest_revision,
            "conditioned-terrain-output",
            "terrain-conditioning-process",
            "2026-07-31T00:00:00Z"};
        record.scenario_id = "r2d-scenario";
        record.target_site = "r2d-target";
        record.bathymetry_dataset_id = "bathymetry-primary";
        record.bathymetry_asset_id = "bathymetry-asset";
        record.topography_dataset_id = "topography-primary";
        record.topography_asset_id = "topography-asset";
        record.corridor_identity = std::move(corridor_identity);
        record.target_reference = grid.target_reference();
        record.grid = grid;
        record.grid_policy = policy;
        record.bathymetry_resampling = resampling_record(
            record.bathymetry_dataset_id,
            record.bathymetry_asset_id,
            tsunami::geo::TerrainSourceRole::bathymetry,
            grid,
            record.grid_policy,
            case_ref,
            manifest_id,
            manifest_revision,
            count);
        record.topography_resampling = resampling_record(
            record.topography_dataset_id,
            record.topography_asset_id,
            tsunami::geo::TerrainSourceRole::topography,
            grid,
            record.grid_policy,
            case_ref,
            manifest_id,
            manifest_revision,
            0U,
            count);
        record.bathymetry_import_identity = record.bathymetry_resampling.import_identity;
        record.bathymetry_transformation_identity = record.bathymetry_resampling.transformation_identity;
        record.topography_import_identity = record.topography_resampling.import_identity;
        record.topography_transformation_identity = record.topography_resampling.transformation_identity;
        record.merge_policy = tsunami::geo::TerrainMergePolicy{
            record.bathymetry_dataset_id,
            record.topography_dataset_id,
            100.0,
            tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning,
            "bathymetry priority synthetic R2D fixture"};
        record.gap_policy = tsunami::geo::TerrainGapResolutionPolicy{
            tsunami::geo::TerrainGapResolutionKind::reject,
            0.0,
            0.0,
            0U,
            0U,
            0.0,
            0.0,
            "reject unresolved synthetic fixture gaps"};
        record.diagnostics.total_cell_count = count;
        record.diagnostics.active_cell_count = count;
        record.diagnostics.bathymetry_selected_cell_count = count;
        record.diagnostics.minimum_elevation_m = minimum;
        record.diagnostics.maximum_elevation_m = maximum;
        record.output_uncertainty = tsunami::data::DatasetUncertainty{
            tsunami::data::UncertaintyStatus::not_reported,
            {},
            std::string{"not_reported"}};
        record.output_media_type = "image/tiff";
        record.output_path = std::filesystem::path{"outputs/terrain/"} / (record.identity.output_dataset_id + ".tif");
        record.digest_status = "not_computed_by_terrain_conditioning";
        return TerrainRecordInput{std::move(terrain), std::move(record)};
    }

    [[nodiscard]] inline auto resampled_source(
        const tsunami::geo::TerrainTargetGrid &grid,
        const tsunami::geo::TerrainTargetGridPolicy &policy,
        const tsunami::data::CaseRevisionRef &case_ref,
        std::string manifest_id,
        std::uint64_t manifest_revision,
        std::string dataset_id,
        tsunami::geo::TerrainSourceRole role,
        std::vector<double> values,
        std::vector<std::uint8_t> mask,
        tsunami::geo::ResampledTerrainCellStatus invalid_status = tsunami::geo::ResampledTerrainCellStatus::source_nodata)
        -> tsunami::geo::ResampledTerrainSource
    {
        auto cell_status = std::vector<tsunami::geo::ResampledTerrainCellStatus>{};
        cell_status.reserve(mask.size());
        auto output_valid_count = std::uint64_t{};
        auto source_nodata_count = std::uint64_t{};
        auto outside_coverage_count = std::uint64_t{};
        for (const auto valid : mask) {
            if (valid != 0U) {
                cell_status.push_back(tsunami::geo::ResampledTerrainCellStatus::valid_resampled);
                ++output_valid_count;
            } else {
                cell_status.push_back(invalid_status);
                if (invalid_status == tsunami::geo::ResampledTerrainCellStatus::outside_source_coverage) {
                    ++outside_coverage_count;
                } else {
                    ++source_nodata_count;
                }
            }
        }
        const auto asset_id = role == tsunami::geo::TerrainSourceRole::bathymetry
            ? std::string{"bathymetry-asset"}
            : std::string{"topography-asset"};
        auto record = resampling_record(
            dataset_id,
            asset_id,
            role,
            grid,
            policy,
            case_ref,
            std::move(manifest_id),
            manifest_revision,
            output_valid_count,
            source_nodata_count,
            outside_coverage_count);
        return tsunami::geo::ResampledTerrainSource{
            std::move(dataset_id),
            role,
            grid,
            std::move(values),
            std::move(mask),
            std::move(cell_status),
            std::move(record)};
    }
}
