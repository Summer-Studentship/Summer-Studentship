#include <tsunami/geo/TerrainConditioningRecord.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <tsunami/geo/CoordinateTransformation.hpp>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto record_error(std::string message, std::string rule_id) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                "geo.terrain.record_invalid",
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_terrain_conditioning_record")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto logical_id_valid(std::string_view text) -> bool
        {
            static const auto pattern = std::regex{"^[a-z0-9]+(?:[._-][a-z0-9]+)*$"};
            const auto copy = std::string{text};
            return !copy.empty() && copy.size() <= 128U && std::regex_match(copy, pattern);
        }

        [[nodiscard]] auto timestamp_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z$"};
            return std::regex_match(text, pattern);
        }

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto optional_text_present(const std::optional<std::string> &text) -> bool
        {
            return !text || (!text->empty() && text->find('\0') == std::string::npos);
        }

        [[nodiscard]] auto text_present(std::string_view text) -> bool
        {
            return !text.empty() && text.find('\0') == std::string_view::npos;
        }

        [[nodiscard]] auto finite_box(const BoundingBox2D &box) noexcept -> bool
        {
            return finite(box.minimum_x) && finite(box.minimum_y) && finite(box.maximum_x) && finite(box.maximum_y) &&
                box.minimum_x <= box.maximum_x && box.minimum_y <= box.maximum_y;
        }

        [[nodiscard]] auto finite_transform(const RasterAffineTransform &transform) noexcept -> bool
        {
            return finite(transform.origin_x) && finite(transform.pixel_width) && finite(transform.row_rotation) &&
                finite(transform.origin_y) && finite(transform.column_rotation) && finite(transform.pixel_height);
        }

        [[nodiscard]] auto transform_corner(const RasterAffineTransform &transform, double column, double row) noexcept -> Point2D
        {
            return Point2D{
                transform.origin_x + (column * transform.pixel_width) + (row * transform.row_rotation),
                transform.origin_y + (column * transform.column_rotation) + (row * transform.pixel_height)};
        }

        [[nodiscard]] auto extent_from_transform(std::uint64_t width, std::uint64_t height, const RasterAffineTransform &transform) -> BoundingBox2D
        {
            const auto w = static_cast<double>(width);
            const auto h = static_cast<double>(height);
            const auto corners = std::array{
                transform_corner(transform, 0.0, 0.0),
                transform_corner(transform, w, 0.0),
                transform_corner(transform, 0.0, h),
                transform_corner(transform, w, h)};
            auto box = BoundingBox2D{corners.front().x, corners.front().y, corners.front().x, corners.front().y};
            for (const auto corner : corners) {
                box.minimum_x = std::min(box.minimum_x, corner.x);
                box.minimum_y = std::min(box.minimum_y, corner.y);
                box.maximum_x = std::max(box.maximum_x, corner.x);
                box.maximum_y = std::max(box.maximum_y, corner.y);
            }
            return box;
        }

        [[nodiscard]] auto close(double left, double right, double absolute_tolerance, double relative_tolerance) noexcept -> bool
        {
            if (!finite(left) || !finite(right) || !finite(absolute_tolerance) || absolute_tolerance < 0.0 ||
                !finite(relative_tolerance) || relative_tolerance < 0.0) {
                return false;
            }
            const auto tolerance = absolute_tolerance + (relative_tolerance * std::max({1.0, std::abs(left), std::abs(right)}));
            return std::abs(left - right) <= tolerance;
        }

        [[nodiscard]] auto boxes_close(
            const BoundingBox2D &left,
            const BoundingBox2D &right,
            double absolute_tolerance,
            double relative_tolerance) noexcept -> bool
        {
            return close(left.minimum_x, right.minimum_x, absolute_tolerance, relative_tolerance) &&
                close(left.minimum_y, right.minimum_y, absolute_tolerance, relative_tolerance) &&
                close(left.maximum_x, right.maximum_x, absolute_tolerance, relative_tolerance) &&
                close(left.maximum_y, right.maximum_y, absolute_tolerance, relative_tolerance);
        }

        [[nodiscard]] auto safe_add(std::uint64_t left, std::uint64_t right, std::uint64_t &out) noexcept -> bool
        {
            if (left > std::numeric_limits<std::uint64_t>::max() - right) {
                return false;
            }
            out = left + right;
            return true;
        }

        [[nodiscard]] auto safe_sum3(
            std::uint64_t first,
            std::uint64_t second,
            std::uint64_t third,
            std::uint64_t &out) noexcept -> bool
        {
            auto partial = std::uint64_t{};
            return safe_add(first, second, partial) && safe_add(partial, third, out);
        }

        [[nodiscard]] auto safe_sum4(
            std::uint64_t first,
            std::uint64_t second,
            std::uint64_t third,
            std::uint64_t fourth,
            std::uint64_t &out) noexcept -> bool
        {
            auto partial = std::uint64_t{};
            return safe_sum3(first, second, third, partial) && safe_add(partial, fourth, out);
        }

        [[nodiscard]] auto text_array_contains_no_nul(const std::vector<std::string> &values) noexcept -> bool
        {
            return std::all_of(values.begin(), values.end(), [](const std::string &value) {
                return value.find('\0') == std::string::npos;
            });
        }

        [[nodiscard]] auto sha256_valid(std::string_view text) noexcept -> bool
        {
            return text.size() == 64U &&
                std::all_of(text.begin(), text.end(), [](char ch) {
                    const auto value = static_cast<unsigned char>(ch);
                    return std::isdigit(value) || (ch >= 'a' && ch <= 'f');
                });
        }

        [[nodiscard]] auto digest_valid(const tsunami::data::ContentDigest &digest) noexcept -> bool
        {
            return digest.algorithm == tsunami::data::DigestAlgorithm::sha256 &&
                sha256_valid(digest.value);
        }

        [[nodiscard]] auto import_identity_complete(const GeospatialImportIdentity &identity) -> bool
        {
            return logical_id_valid(identity.import_id) && identity.import_revision > 0U &&
                logical_id_valid(identity.case_revision.case_id.str()) && identity.case_revision.revision > 0U &&
                logical_id_valid(identity.manifest_id) && identity.manifest_revision > 0U &&
                logical_id_valid(identity.dataset_id) && logical_id_valid(identity.asset_id) &&
                timestamp_valid(identity.executed_at_utc);
        }

        [[nodiscard]] auto transformation_identity_complete(const CoordinateTransformationIdentity &identity) -> bool
        {
            return logical_id_valid(identity.transformation_id) && identity.transformation_revision > 0U &&
                logical_id_valid(identity.case_revision.case_id.str()) && identity.case_revision.revision > 0U &&
                logical_id_valid(identity.manifest_id) && identity.manifest_revision > 0U &&
                logical_id_valid(identity.source_import_id) && identity.source_import_revision > 0U &&
                logical_id_valid(identity.source_dataset_id) && logical_id_valid(identity.source_asset_id) &&
                logical_id_valid(identity.output_dataset_id) && logical_id_valid(identity.output_process_id) &&
                timestamp_valid(identity.executed_at_utc);
        }

        [[nodiscard]] auto corridor_identity_complete(const CorridorConstructionIdentity &identity) -> bool
        {
            return logical_id_valid(identity.corridor_id) && identity.corridor_revision > 0U &&
                logical_id_valid(identity.case_revision.case_id.str()) && identity.case_revision.revision > 0U &&
                logical_id_valid(identity.trajectory_id) && logical_id_valid(identity.output_dataset_id) &&
                logical_id_valid(identity.output_process_id) && timestamp_valid(identity.executed_at_utc);
        }

        [[nodiscard]] auto source_bound_to_terrain_identity(
            const TerrainConditioningIdentity &terrain_identity,
            const GeospatialImportIdentity &import_identity,
            const CoordinateTransformationIdentity &transformation_identity) -> bool
        {
            return import_identity.case_revision == terrain_identity.case_revision &&
                transformation_identity.case_revision == terrain_identity.case_revision &&
                import_identity.manifest_id == terrain_identity.manifest_id &&
                transformation_identity.manifest_id == terrain_identity.manifest_id &&
                import_identity.manifest_revision == terrain_identity.manifest_revision &&
                transformation_identity.manifest_revision == terrain_identity.manifest_revision;
        }

        [[nodiscard]] auto grid_policy_valid(const TerrainTargetGridPolicy &policy) -> bool
        {
            return finite(policy.target_spacing_m) && policy.target_spacing_m > 0.0 &&
                finite(policy.active_coverage_threshold) && policy.active_coverage_threshold > 0.0 &&
                policy.active_coverage_threshold <= 1.0 &&
                finite(policy.maximum_upsampling_factor) && policy.maximum_upsampling_factor >= 1.0 &&
                policy.maximum_output_cells > 0U &&
                finite(policy.numerical_absolute_tolerance) && policy.numerical_absolute_tolerance > 0.0 &&
                finite(policy.numerical_relative_tolerance) && policy.numerical_relative_tolerance >= 0.0 &&
                text_present(policy.policy_basis);
        }

        [[nodiscard]] auto uncertainty_valid(const tsunami::data::DatasetUncertainty &uncertainty) -> bool
        {
            if (!optional_text_present(uncertainty.description)) {
                return false;
            }
            const auto expects_measures = uncertainty.status == tsunami::data::UncertaintyStatus::reported ||
                uncertainty.status == tsunami::data::UncertaintyStatus::estimated;
            if (expects_measures != !uncertainty.measures.empty()) {
                return false;
            }
            for (const auto &measure : uncertainty.measures) {
                if (!text_present(measure.quantity) || !text_present(measure.unit) || !finite(measure.value) ||
                    (measure.confidence_level && (!finite(*measure.confidence_level) || *measure.confidence_level < 0.0 || *measure.confidence_level > 1.0)) ||
                    !optional_text_present(measure.method)) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] auto source_evidence_consistent(
            std::string_view dataset_id,
            std::string_view asset_id,
            const GeospatialImportIdentity &import_identity,
            const CoordinateTransformationIdentity &transformation_identity,
            const RasterResamplingRecord &resampling) -> bool
        {
            return dataset_id == import_identity.dataset_id && asset_id == import_identity.asset_id &&
                dataset_id == transformation_identity.source_dataset_id && asset_id == transformation_identity.source_asset_id &&
                import_identity == resampling.import_identity &&
                transformation_identity == resampling.transformation_identity &&
                dataset_id == resampling.dataset_id && asset_id == resampling.asset_id &&
                transformation_identity.source_import_id == import_identity.import_id &&
                transformation_identity.source_import_revision == import_identity.import_revision;
        }

        [[nodiscard]] auto resampling_evidence_valid(const RasterResamplingRecord &record) -> bool
        {
            return logical_id_valid(record.dataset_id) && logical_id_valid(record.asset_id) &&
                import_identity_complete(record.import_identity) &&
                transformation_identity_complete(record.transformation_identity) &&
                record.source_registration == RasterCellRegistration::pixel_is_area &&
                record.target_registration == RasterCellRegistration::pixel_is_area &&
                (!record.source_scale || finite(*record.source_scale)) &&
                (!record.source_offset || finite(*record.source_offset)) &&
                finite(record.minimum_source_spacing_m) && record.minimum_source_spacing_m > 0.0 &&
                finite(record.maximum_source_spacing_m) && record.maximum_source_spacing_m >= record.minimum_source_spacing_m &&
                finite(record.nominal_source_spacing_m) && record.nominal_source_spacing_m > 0.0 &&
                finite(record.target_spacing_m) && record.target_spacing_m > 0.0 &&
                finite(record.maximum_upsampling_factor) && record.maximum_upsampling_factor >= 1.0 &&
                text_present(record.adapter_name) && text_present(record.adapter_version);
        }

        [[nodiscard]] auto operation_grid_text_valid(const CoordinateOperationGrid &grid) -> bool
        {
            return text_present(grid.short_name) &&
                optional_text_present(grid.full_path) &&
                optional_text_present(grid.package_name) &&
                optional_text_present(grid.source_uri) &&
                (!grid.declared_digest || digest_valid(*grid.declared_digest));
        }

        [[nodiscard]] auto operation_text_valid(const CoordinateOperationRecord &operation) -> bool
        {
            return std::all_of(operation.grids.begin(), operation.grids.end(), operation_grid_text_valid);
        }

        [[nodiscard]] auto vertical_text_valid(const VerticalTransformationSpecification &vertical) -> bool
        {
            return std::all_of(vertical.steps.begin(), vertical.steps.end(), [](const VerticalTransformationStep &step) {
                return optional_text_present(step.operation_authority) &&
                    optional_text_present(step.operation_code) &&
                    optional_text_present(step.required_resource_name) &&
                    text_present(step.source_reference) &&
                    text_present(step.target_reference);
            });
        }

        [[nodiscard]] auto resampling_target_consistent(
            const RasterResamplingRecord &resampling,
            const TerrainTargetGrid &grid,
            const TerrainTargetGridPolicy &policy,
            const CoordinateReferenceDescriptor &target_horizontal) -> bool
        {
            auto status_cells = std::uint64_t{};
            if (!safe_sum3(
                    resampling.output_valid_cell_count,
                    resampling.source_nodata_cell_count,
                    resampling.outside_coverage_cell_count,
                    status_cells)) {
                return false;
            }
            return status_cells == grid.cell_count() &&
                resampling.target_registration == grid.registration() &&
                close(resampling.target_spacing_m, grid.spacing_m(), policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance) &&
                close(resampling.target_spacing_m, policy.target_spacing_m, policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance) &&
                close(resampling.maximum_upsampling_factor, policy.maximum_upsampling_factor, policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance) &&
                resampling.operation.target_crs == target_horizontal;
        }

        [[nodiscard]] auto target_reference_units_valid(const ComputationalTargetReference &target) -> bool
        {
            if (!text_present(target.horizontal_unit) || target.horizontal_unit != "m" ||
                !optional_text_present(target.vertical_unit) || !optional_text_present(target.vertical_positive)) {
                return false;
            }
            if (!target.vertical) {
                return !target.vertical_unit && !target.vertical_positive &&
                    target.storage_axes == ComputationalAxisConvention::east_north;
            }
            return target.vertical_unit && *target.vertical_unit == "m" &&
                target.vertical_positive && *target.vertical_positive == "up";
        }

        [[nodiscard]] auto grid_transform_consistent(
            const TerrainTargetGrid &grid,
            const TerrainTargetGridPolicy &policy) noexcept -> bool
        {
            const auto &transform = grid.transform();
            const auto column_spacing = std::hypot(transform.pixel_width, transform.column_rotation);
            const auto row_spacing = std::hypot(transform.row_rotation, transform.pixel_height);
            const auto determinant = (transform.pixel_width * transform.pixel_height) -
                (transform.row_rotation * transform.column_rotation);
            return finite(column_spacing) && finite(row_spacing) && finite(determinant) &&
                std::abs(determinant) > policy.numerical_absolute_tolerance &&
                close(column_spacing, grid.spacing_m(), policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance) &&
                close(row_spacing, grid.spacing_m(), policy.numerical_absolute_tolerance, policy.numerical_relative_tolerance);
        }

        [[nodiscard]] auto merge_policy_valid(
            const TerrainMergePolicy &policy,
            std::string_view bathymetry_dataset_id,
            std::string_view topography_dataset_id) -> bool
        {
            const auto priority_ids_match_sources =
                ((policy.first_priority_dataset_id == bathymetry_dataset_id &&
                  policy.second_priority_dataset_id == topography_dataset_id) ||
                 (policy.first_priority_dataset_id == topography_dataset_id &&
                  policy.second_priority_dataset_id == bathymetry_dataset_id));
            return priority_ids_match_sources &&
                policy.first_priority_dataset_id != policy.second_priority_dataset_id &&
                finite(policy.maximum_overlap_disagreement_m) &&
                policy.maximum_overlap_disagreement_m >= 0.0 &&
                text_present(policy.priority_basis);
        }

        [[nodiscard]] auto gap_policy_valid(const TerrainGapResolutionPolicy &policy) -> bool
        {
            if (!text_present(policy.policy_basis)) {
                return false;
            }
            if (policy.kind == TerrainGapResolutionKind::reject) {
                return policy.maximum_fill_distance_m == 0.0 &&
                    policy.maximum_component_diameter_m == 0.0 &&
                    policy.maximum_component_cells == 0U &&
                    policy.minimum_donor_count == 0U &&
                    policy.distance_exponent == 0.0 &&
                    policy.maximum_filled_fraction == 0.0;
            }
            return finite(policy.maximum_fill_distance_m) && policy.maximum_fill_distance_m > 0.0 &&
                finite(policy.maximum_component_diameter_m) && policy.maximum_component_diameter_m > 0.0 &&
                policy.maximum_component_cells > 0U && policy.minimum_donor_count > 0U &&
                finite(policy.distance_exponent) && policy.distance_exponent > 0.0 &&
                finite(policy.maximum_filled_fraction) && policy.maximum_filled_fraction > 0.0 &&
                policy.maximum_filled_fraction <= 1.0;
        }

        [[nodiscard]] auto diagnostics_valid(const TerrainConditioningDiagnostics &diagnostics) -> bool
        {
            const auto total = diagnostics.total_cell_count;
            const auto counts_bounded = diagnostics.active_cell_count <= total &&
                diagnostics.outside_corridor_cell_count <= total &&
                diagnostics.excluded_boundary_cell_count <= total &&
                diagnostics.bathymetry_selected_cell_count <= total &&
                diagnostics.topography_selected_cell_count <= total &&
                diagnostics.overlap_cell_count <= total &&
                diagnostics.overlap_conflict_cell_count <= total &&
                diagnostics.initially_unresolved_cell_count <= total &&
                diagnostics.filled_cell_count <= total &&
                diagnostics.unresolved_cell_count <= total;
            auto corridor_partition = std::uint64_t{};
            auto active_partition = std::uint64_t{};
            auto unresolved_partition = std::uint64_t{};
            if (!counts_bounded ||
                !safe_sum3(diagnostics.active_cell_count, diagnostics.outside_corridor_cell_count, diagnostics.excluded_boundary_cell_count, corridor_partition) ||
                !safe_sum4(diagnostics.bathymetry_selected_cell_count, diagnostics.topography_selected_cell_count, diagnostics.filled_cell_count, diagnostics.unresolved_cell_count, active_partition) ||
                !safe_add(diagnostics.filled_cell_count, diagnostics.unresolved_cell_count, unresolved_partition)) {
                return false;
            }
            return diagnostics.unresolved_cell_count == 0U &&
                diagnostics.active_cell_count > 0U &&
                corridor_partition == total &&
                active_partition == diagnostics.active_cell_count &&
                unresolved_partition <= diagnostics.initially_unresolved_cell_count &&
                diagnostics.overlap_conflict_cell_count <= diagnostics.overlap_cell_count &&
                diagnostics.overlap.overlap_cell_count == diagnostics.overlap_cell_count &&
                diagnostics.overlap.disagreement_exceedance_count == diagnostics.overlap_conflict_cell_count &&
                finite(diagnostics.minimum_elevation_m) &&
                finite(diagnostics.maximum_elevation_m) &&
                diagnostics.minimum_elevation_m <= diagnostics.maximum_elevation_m &&
                finite(diagnostics.overlap.mean_signed_difference_m) &&
                finite(diagnostics.overlap.root_mean_square_difference_m) &&
                diagnostics.overlap.root_mean_square_difference_m >= 0.0 &&
                finite(diagnostics.overlap.maximum_absolute_difference_m) &&
                diagnostics.overlap.maximum_absolute_difference_m >= 0.0 &&
                text_array_contains_no_nul(diagnostics.warnings);
        }

        [[nodiscard]] auto output_path_valid(const std::filesystem::path &path) -> bool
        {
            const auto text = path.generic_string();
            if (text.empty() || text.find('\0') != std::string::npos ||
                path.is_absolute() || path.has_root_name() ||
                path.lexically_normal().generic_string() != text) {
                return false;
            }
            auto parts = std::vector<std::string>{};
            for (const auto &part : path) {
                const auto value = part.generic_string();
                if (value.empty() || value == "." || value == "..") {
                    return false;
                }
                parts.push_back(value);
            }
            const auto extension = path.extension().generic_string();
            return parts.size() >= 3U && parts[0] == "outputs" && parts[1] == "terrain" &&
                (extension == ".tif" || extension == ".tiff");
        }
    }

    auto to_string(TerrainOverlapConflictPolicy value) noexcept -> std::string_view
    {
        switch (value) {
        case TerrainOverlapConflictPolicy::reject:
            return "reject";
        case TerrainOverlapConflictPolicy::accept_priority_with_warning:
            return "accept_priority_with_warning";
        }
        return "reject";
    }

    auto to_string(TerrainGapResolutionKind value) noexcept -> std::string_view
    {
        switch (value) {
        case TerrainGapResolutionKind::reject:
            return "reject";
        case TerrainGapResolutionKind::bounded_inverse_distance:
            return "bounded_inverse_distance";
        }
        return "reject";
    }

    auto to_string(TerrainUncertaintyCombination value) noexcept -> std::string_view
    {
        switch (value) {
        case TerrainUncertaintyCombination::not_computed:
            return "not_computed";
        case TerrainUncertaintyCombination::root_sum_square:
            return "root_sum_square";
        case TerrainUncertaintyCombination::conservative_sum:
            return "conservative_sum";
        }
        return "not_computed";
    }

    auto default_terrain_conditioning_record_path(
        std::string_view output_dataset_id) -> std::filesystem::path
    {
        if (!logical_id_valid(output_dataset_id)) {
            return {};
        }
        return std::filesystem::path{"manifests"} / "terrain" / (std::string{output_dataset_id} + ".json");
    }

    auto default_conditioned_terrain_path(
        std::string_view output_dataset_id) -> std::filesystem::path
    {
        if (!logical_id_valid(output_dataset_id)) {
            return {};
        }
        return std::filesystem::path{"outputs"} / "terrain" / (std::string{output_dataset_id} + ".tif");
    }

    auto validate_terrain_conditioning_record(
        const TerrainConditioningRecord &record) -> tsunami::core::Result<void>
    {
        if (record.schema.schema_name != terrain_conditioning_record_schema_name ||
            record.schema.version != supported_terrain_conditioning_record_version ||
            record.policy_version != supported_terrain_conditioning_record_policy_version ||
            record.formula_version != terrain_conditioning_formula_version) {
            return tsunami::core::failure(record_error("terrain conditioning record schema identity is unsupported", "geo.terrain.record.schema"));
        }
        if (!logical_id_valid(record.identity.terrain_id) || record.identity.terrain_revision == 0U ||
            !logical_id_valid(record.identity.case_revision.case_id.str()) || record.identity.case_revision.revision == 0U ||
            !logical_id_valid(record.identity.manifest_id) || record.identity.manifest_revision == 0U ||
            !logical_id_valid(record.identity.output_dataset_id) ||
            !logical_id_valid(record.identity.output_process_id) ||
            !timestamp_valid(record.identity.executed_at_utc)) {
            return tsunami::core::failure(record_error("terrain conditioning identity is invalid", "geo.terrain.request.identities_match"));
        }
        if (!logical_id_valid(record.scenario_id) || !logical_id_valid(record.target_site)) {
            return tsunami::core::failure(record_error("terrain scenario or target site identity is invalid", "geo.terrain.request.identities_match"));
        }
        if (!corridor_identity_complete(record.corridor_identity) ||
            record.corridor_identity.case_revision != record.identity.case_revision) {
            return tsunami::core::failure(record_error("terrain corridor identity is incomplete or inconsistent", "geo.terrain.request.corridor_matches_case"));
        }
        if (auto target = validate_coordinate_reference_descriptor(record.target_reference.horizontal); !target) {
            return target;
        }
        if (record.target_reference.vertical) {
            if (auto vertical = validate_coordinate_reference_descriptor(*record.target_reference.vertical); !vertical) {
                return vertical;
            }
        }
        if (!target_reference_units_valid(record.target_reference)) {
            return tsunami::core::failure(record_error("terrain target reference unit metadata is invalid", "geo.terrain.target_reference.units"));
        }
        if (!import_identity_complete(record.bathymetry_import_identity) ||
            !import_identity_complete(record.topography_import_identity) ||
            !transformation_identity_complete(record.bathymetry_transformation_identity) ||
            !transformation_identity_complete(record.topography_transformation_identity) ||
            !source_bound_to_terrain_identity(record.identity, record.bathymetry_import_identity, record.bathymetry_transformation_identity) ||
            !source_bound_to_terrain_identity(record.identity, record.topography_import_identity, record.topography_transformation_identity) ||
            !source_evidence_consistent(record.bathymetry_dataset_id, record.bathymetry_asset_id, record.bathymetry_import_identity, record.bathymetry_transformation_identity, record.bathymetry_resampling) ||
            !source_evidence_consistent(record.topography_dataset_id, record.topography_asset_id, record.topography_import_identity, record.topography_transformation_identity, record.topography_resampling) ||
            record.bathymetry_resampling.role != TerrainSourceRole::bathymetry ||
            record.topography_resampling.role != TerrainSourceRole::topography ||
            !resampling_evidence_valid(record.bathymetry_resampling) ||
            !resampling_evidence_valid(record.topography_resampling)) {
            return tsunami::core::failure(record_error("terrain source provenance is incomplete or contradictory", "geo.terrain.source.provenance_complete"));
        }
        if (auto operation = validate_coordinate_operation_record(record.bathymetry_resampling.operation); !operation) {
            return operation;
        }
        if (auto operation = validate_coordinate_operation_record(record.topography_resampling.operation); !operation) {
            return operation;
        }
        if (auto vertical = validate_vertical_transformation(record.bathymetry_resampling.vertical_steps); !vertical) {
            return vertical;
        }
        if (auto vertical = validate_vertical_transformation(record.topography_resampling.vertical_steps); !vertical) {
            return vertical;
        }
        if (!operation_text_valid(record.bathymetry_resampling.operation) ||
            !operation_text_valid(record.topography_resampling.operation) ||
            !vertical_text_valid(record.bathymetry_resampling.vertical_steps) ||
            !vertical_text_valid(record.topography_resampling.vertical_steps)) {
            return tsunami::core::failure(record_error("terrain operation or vertical metadata contains invalid persisted text", "geo.terrain.resampling.text"));
        }
        if (record.grid.width() == 0U || record.grid.height() == 0U ||
            record.grid.width() > std::numeric_limits<std::uint64_t>::max() / record.grid.height() ||
            !finite(record.grid.spacing_m()) || record.grid.spacing_m() <= 0.0 ||
            !finite_transform(record.grid.transform()) || !finite_box(record.grid.extent()) ||
            !finite(record.grid.xi_min_m()) || !finite(record.grid.xi_max_m()) ||
            !finite(record.grid.eta_bottom_m()) || !finite(record.grid.eta_top_m()) ||
            !finite(record.grid.longitudinal_padding_m()) || !finite(record.grid.transverse_padding_m()) ||
            record.grid.xi_min_m() >= record.grid.xi_max_m() ||
            record.grid.eta_bottom_m() >= record.grid.eta_top_m() ||
            !grid_policy_valid(record.grid_policy) ||
            record.grid.cell_count() > record.grid_policy.maximum_output_cells ||
            !grid_transform_consistent(record.grid, record.grid_policy) ||
            !boxes_close(
                record.grid.extent(),
                extent_from_transform(record.grid.width(), record.grid.height(), record.grid.transform()),
                record.grid_policy.numerical_absolute_tolerance,
                record.grid_policy.numerical_relative_tolerance)) {
            return tsunami::core::failure(record_error("terrain target grid metadata is invalid", "geo.terrain.grid.pixel_is_area"));
        }
        if (!resampling_target_consistent(record.bathymetry_resampling, record.grid, record.grid_policy, record.target_reference.horizontal) ||
            !resampling_target_consistent(record.topography_resampling, record.grid, record.grid_policy, record.target_reference.horizontal)) {
            return tsunami::core::failure(record_error("terrain resampling evidence disagrees with the target grid", "geo.terrain.resampling.target_grid"));
        }
        if (!merge_policy_valid(record.merge_policy, record.bathymetry_dataset_id, record.topography_dataset_id) ||
            !gap_policy_valid(record.gap_policy)) {
            return tsunami::core::failure(record_error("terrain merge or gap policy is invalid", "geo.terrain.policy.merge_gap"));
        }
        if (!uncertainty_valid(record.output_uncertainty)) {
            return tsunami::core::failure(record_error("terrain output uncertainty is inconsistent", "geo.terrain.record.output"));
        }
        if (!diagnostics_valid(record.diagnostics)) {
            return tsunami::core::failure(record_error("terrain diagnostics contain unresolved active cells or invalid elevation bounds", "geo.terrain.output.no_active_nodata"));
        }
        if (record.digest_status != "not_computed_by_terrain_conditioning" ||
            record.output_media_type != "image/tiff" || !output_path_valid(record.output_path) ||
            !text_array_contains_no_nul(record.warnings)) {
            return tsunami::core::failure(record_error("terrain output metadata is incomplete", "geo.terrain.record.output"));
        }
        if (record.grid.cell_count() != record.diagnostics.total_cell_count ||
            record.grid.registration() != RasterCellRegistration::pixel_is_area ||
            record.target_reference != record.grid.target_reference()) {
            return tsunami::core::failure(record_error("terrain target grid metadata is inconsistent", "geo.terrain.grid.pixel_is_area"));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
