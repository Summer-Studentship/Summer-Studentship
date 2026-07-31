#include <tsunami/geo/TerrainResampling.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto resampling_error(std::string code, std::string message, std::string rule_id)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_terrain_source_resampling_request")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto source_spacing(const RasterAffineTransform &transform) noexcept -> std::pair<double, double>
        {
            return {
                std::hypot(transform.pixel_width, transform.column_rotation),
                std::hypot(transform.row_rotation, transform.pixel_height)};
        }

        [[nodiscard]] auto grid_resource_valid(const CoordinateOperationGrid &grid) -> bool
        {
            return grid.available &&
                grid.verification_status != GeodeticResourceVerificationStatus::unavailable &&
                (!grid.full_path || !grid.full_path->empty());
        }
    }

    auto to_string(TerrainSourceRole value) noexcept -> std::string_view
    {
        switch (value) {
        case TerrainSourceRole::bathymetry:
            return "bathymetry";
        case TerrainSourceRole::topography:
            return "topography";
        }
        return "bathymetry";
    }

    auto to_string(RasterResamplingKernel value) noexcept -> std::string_view
    {
        switch (value) {
        case RasterResamplingKernel::bilinear:
            return "bilinear";
        case RasterResamplingKernel::area_average:
            return "area_average";
        }
        return "bilinear";
    }

    auto to_string(ResampledTerrainCellStatus value) noexcept -> std::string_view
    {
        switch (value) {
        case ResampledTerrainCellStatus::valid_resampled:
            return "valid_resampled";
        case ResampledTerrainCellStatus::source_nodata:
            return "source_nodata";
        case ResampledTerrainCellStatus::outside_source_coverage:
            return "outside_source_coverage";
        }
        return "outside_source_coverage";
    }

    auto validate_terrain_source_resampling_request(
        const TerrainSourceResamplingRequest &request) -> tsunami::core::Result<void>
    {
        if (request.source_raster == nullptr || request.import_record == nullptr ||
            request.transformation_plan == nullptr || request.transformation_record == nullptr) {
            return tsunami::core::failure(resampling_error("geo.terrain.source_missing", "terrain source resampling request is incomplete", "geo.terrain.source.provenance_complete"));
        }
        const auto &raster = *request.source_raster;
        const auto &import = *request.import_record;
        const auto &plan = *request.transformation_plan;
        const auto &record = *request.transformation_record;
        if (import.import_kind != GeospatialImportKind::raster || !import.raster) {
            return tsunami::core::failure(resampling_error("geo.terrain.source_import_mismatch", "terrain source import record is not raster", "geo.terrain.source.provenance_complete"));
        }
        if (raster.width() != import.raster->width || raster.height() != import.raster->height ||
            raster.transform() != import.raster->transform || raster.registration() != import.raster->registration ||
            raster.band().valid_mask.size() != static_cast<std::size_t>(raster.cell_count())) {
            return tsunami::core::failure(resampling_error("geo.terrain.source_import_mismatch", "imported raster does not match import summary", "geo.terrain.source.provenance_complete"));
        }
        if (raster.registration() != RasterCellRegistration::pixel_is_area) {
            return tsunami::core::failure(resampling_error("geo.terrain.source_registration_unsupported", "terrain source registration is unsupported", "geo.terrain.source.registration_supported"));
        }
        if (plan.source_reference != record.source_horizontal || plan.target_reference != record.target ||
            plan.target_reference != request.target_grid.target_reference() ||
            plan.source_width != raster.width() || plan.source_height != raster.height() ||
            plan.source_transform != raster.transform()) {
            return tsunami::core::failure(resampling_error("geo.terrain.source_plan_mismatch", "terrain transformation plan does not match source raster or target grid", "geo.terrain.source.target_matches"));
        }
        if (record.identity.source_import_id != import.identity.import_id ||
            record.identity.source_import_revision != import.identity.import_revision ||
            record.identity.source_dataset_id != import.identity.dataset_id ||
            record.identity.source_asset_id != import.identity.asset_id) {
            return tsunami::core::failure(resampling_error("geo.terrain.source_transformation_mismatch", "coordinate transformation record does not reference the import identity", "geo.terrain.operation.accepted_reused"));
        }
        if (record.horizontal_operation.ballpark) {
            return tsunami::core::failure(resampling_error("geo.terrain.operation_ballpark", "ballpark coordinate operations are forbidden for terrain conditioning", "geo.terrain.operation.ballpark_forbidden"));
        }
        const auto same_reference = record.source_horizontal == record.target.horizontal;
        if (!same_reference && !record.horizontal_operation.canonical_pipeline &&
            !record.horizontal_operation.canonical_wkt2 && !record.horizontal_operation.canonical_projjson &&
            (!record.horizontal_operation.operation_authority || !record.horizontal_operation.operation_code)) {
            return tsunami::core::failure(resampling_error("geo.terrain.operation_missing", "accepted coordinate operation has no executable representation", "geo.terrain.operation.accepted_reused"));
        }
        for (const auto &grid : record.grids) {
            if (!grid_resource_valid(grid)) {
                return tsunami::core::failure(resampling_error("geo.terrain.operation_resource_missing", "coordinate-operation grid resource is unavailable or unverified", "geo.terrain.operation.resources_available").add_context("grid_name", grid.short_name));
            }
        }
        auto grid_step_count = 0U;
        for (const auto &step : record.vertical_operation.steps) {
            if (step.kind == VerticalTransformationStepKind::geodetic_grid_operation) {
                ++grid_step_count;
                if (!step.required_resource_name) {
                    return tsunami::core::failure(resampling_error("geo.terrain.vertical_grid_missing", "vertical grid step is missing its required resource", "geo.terrain.operation.resources_available"));
                }
            } else if ((step.kind == VerticalTransformationStepKind::unit_scale &&
                        (!step.scale_factor || !finite(*step.scale_factor) || *step.scale_factor <= 0.0)) ||
                       (step.kind == VerticalTransformationStepKind::constant_offset &&
                        (!step.offset_m || !finite(*step.offset_m)))) {
                return tsunami::core::failure(resampling_error("geo.terrain.vertical_chain_invalid", "vertical transformation step has invalid numeric metadata", "geo.terrain.operation.accepted_reused"));
            }
        }
        if (grid_step_count > 1U) {
            return tsunami::core::failure(resampling_error("geo.terrain.vertical_chain_invalid", "terrain conditioning supports at most one vertical grid operation", "geo.terrain.operation.accepted_reused"));
        }
        const auto [column_spacing, row_spacing] = source_spacing(raster.transform());
        const auto maximum_spacing = std::max(column_spacing, row_spacing);
        if (!finite(maximum_spacing) || maximum_spacing <= 0.0 || request.target_grid.spacing_m() <= 0.0 ||
            !finite(request.maximum_upsampling_factor) || request.maximum_upsampling_factor < 1.0) {
            return tsunami::core::failure(resampling_error("geo.terrain.resampling_kernel_invalid", "terrain source spacing or upsampling policy is invalid", "geo.terrain.resampling.upsampling_bounded"));
        }
        const auto upsampling_factor = maximum_spacing / request.target_grid.spacing_m();
        if (upsampling_factor > request.maximum_upsampling_factor) {
            return tsunami::core::failure(resampling_error("geo.terrain.upsampling_limit_exceeded", "terrain resampling exceeds the accepted upsampling limit", "geo.terrain.resampling.upsampling_bounded"));
        }
        const auto materially_coarser = request.target_grid.spacing_m() > maximum_spacing + request.target_grid.spacing_m() * 1.0e-12;
        if (materially_coarser && request.kernel != RasterResamplingKernel::area_average) {
            return tsunami::core::failure(resampling_error("geo.terrain.resampling_kernel_incompatible", "area-average resampling is required for material downsampling", "geo.terrain.resampling.kernel_explicit"));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
