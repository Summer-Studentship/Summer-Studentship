#include <tsunami/geo/TerrainConditioning.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <numeric>
#include <set>
#include <string>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto terrain_error(std::string code, std::string message, std::string rule_id)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "condition_terrain")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto has_role(const tsunami::data::DatasetRecord &dataset, tsunami::data::DatasetRole role)
            -> bool
        {
            return std::find(dataset.roles.begin(), dataset.roles.end(), role) != dataset.roles.end();
        }

        [[nodiscard]] auto validate_source(
            const TerrainSourceRequest &source,
            const tsunami::data::CaseConfiguration &configuration,
            const tsunami::data::DatasetManifest &manifest,
            const ComputationalTargetReference &target) -> tsunami::core::Result<void>
        {
            if (source.raster == nullptr || source.import_record == nullptr ||
                source.transformation_plan == nullptr || source.transformation_record == nullptr) {
                return tsunami::core::failure(terrain_error("geo.terrain.source_missing", "terrain source request is incomplete", "geo.terrain.source.provenance_complete"));
            }
            const auto expected_dataset = source.role == TerrainSourceRole::bathymetry
                ? configuration.datasets().bathymetry
                : configuration.datasets().topography;
            if (source.expected_dataset_id != expected_dataset) {
                return tsunami::core::failure(terrain_error("geo.terrain.source_binding_mismatch", "terrain source dataset does not match the case binding", "geo.terrain.request.sources_match_case").add_context("dataset_id", source.expected_dataset_id));
            }
            const auto *dataset = manifest.find_dataset(source.expected_dataset_id);
            if (dataset == nullptr || dataset->representation != tsunami::data::DatasetRepresentationKind::raster ||
                !has_role(*dataset, source.role == TerrainSourceRole::bathymetry ? tsunami::data::DatasetRole::bathymetry : tsunami::data::DatasetRole::topography)) {
                return tsunami::core::failure(terrain_error("geo.terrain.source_role_mismatch", "terrain source dataset role or representation is invalid", "geo.terrain.request.sources_match_case").add_context("dataset_id", source.expected_dataset_id));
            }
            const auto asset_matches = std::any_of(dataset->assets.begin(), dataset->assets.end(), [&](const auto &asset) {
                return asset.asset_id == source.expected_asset_id;
            });
            if (!asset_matches) {
                return tsunami::core::failure(terrain_error("geo.terrain.source_binding_mismatch", "terrain source asset is not declared in the manifest", "geo.terrain.request.sources_match_case").add_context("asset_id", source.expected_asset_id));
            }
            const auto &import = *source.import_record;
            const auto &record = *source.transformation_record;
            if (import.identity.case_revision.case_id != configuration.identity().case_id ||
                import.identity.case_revision.revision != configuration.identity().revision ||
                import.identity.manifest_id != manifest.identity().manifest_id ||
                import.identity.manifest_revision != manifest.identity().manifest_revision ||
                import.identity.dataset_id != source.expected_dataset_id ||
                import.identity.asset_id != source.expected_asset_id) {
                return tsunami::core::failure(terrain_error("geo.terrain.source_import_mismatch", "terrain source import provenance does not match case and manifest", "geo.terrain.source.provenance_complete"));
            }
            if (record.identity.case_revision != manifest.identity().case_revision ||
                record.identity.manifest_id != manifest.identity().manifest_id ||
                record.identity.manifest_revision != manifest.identity().manifest_revision ||
                record.identity.source_dataset_id != source.expected_dataset_id ||
                record.identity.source_asset_id != source.expected_asset_id ||
                record.target != target) {
                return tsunami::core::failure(terrain_error("geo.terrain.source_transformation_mismatch", "terrain source transformation provenance does not match request", "geo.terrain.source.target_matches"));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto merge_policy_valid(const TerrainMergePolicy &policy) noexcept -> bool
        {
            return !policy.first_priority_dataset_id.empty() &&
                !policy.second_priority_dataset_id.empty() &&
                policy.first_priority_dataset_id != policy.second_priority_dataset_id &&
                finite(policy.maximum_overlap_disagreement_m) &&
                policy.maximum_overlap_disagreement_m >= 0.0 &&
                !policy.priority_basis.empty();
        }

        [[nodiscard]] auto gap_policy_valid(const TerrainGapResolutionPolicy &policy) noexcept -> bool
        {
            if (policy.kind == TerrainGapResolutionKind::reject) {
                return true;
            }
            return finite(policy.maximum_fill_distance_m) && policy.maximum_fill_distance_m > 0.0 &&
                finite(policy.maximum_component_diameter_m) && policy.maximum_component_diameter_m > 0.0 &&
                policy.maximum_component_cells > 0U && policy.minimum_donor_count > 0U &&
                finite(policy.distance_exponent) && policy.distance_exponent > 0.0 &&
                finite(policy.maximum_filled_fraction) && policy.maximum_filled_fraction > 0.0 &&
                policy.maximum_filled_fraction <= 1.0 && !policy.policy_basis.empty();
        }

        [[nodiscard]] auto same_grid(const TerrainTargetGrid &left, const TerrainTargetGrid &right) noexcept -> bool
        {
            return left == right;
        }

        [[nodiscard]] auto index(std::uint64_t width, std::uint64_t column, std::uint64_t row) noexcept -> std::size_t
        {
            return static_cast<std::size_t>(row * width + column);
        }

        [[nodiscard]] auto lineage_is_bathymetry(TerrainCellLineage lineage) noexcept -> bool
        {
            return lineage == TerrainCellLineage::bathymetry_selected ||
                lineage == TerrainCellLineage::overlap_bathymetry_selected ||
                lineage == TerrainCellLineage::overlap_bathymetry_selected_with_conflict;
        }

        [[nodiscard]] auto lineage_is_topography(TerrainCellLineage lineage) noexcept -> bool
        {
            return lineage == TerrainCellLineage::topography_selected ||
                lineage == TerrainCellLineage::overlap_topography_selected ||
                lineage == TerrainCellLineage::overlap_topography_selected_with_conflict;
        }

        [[nodiscard]] auto component_touches_boundary(
            const std::vector<std::size_t> &component,
            const TerrainCorridorCoverage &coverage) -> bool
        {
            const auto width = coverage.grid.width();
            const auto height = coverage.grid.height();
            for (const auto cell : component) {
                const auto column = static_cast<std::uint64_t>(cell) % width;
                const auto row = static_cast<std::uint64_t>(cell) / width;
                if (column == 0U || row == 0U || column + 1U == width || row + 1U == height) {
                    return true;
                }
                for (auto dr = -1; dr <= 1; ++dr) {
                    for (auto dc = -1; dc <= 1; ++dc) {
                        if (dr == 0 && dc == 0) {
                            continue;
                        }
                        const auto nc = static_cast<std::uint64_t>(static_cast<int>(column) + dc);
                        const auto nr = static_cast<std::uint64_t>(static_cast<int>(row) + dr);
                        const auto neighbour = index(width, nc, nr);
                        if (coverage.cell_classes[neighbour] != TerrainCorridorCellClass::active) {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        [[nodiscard]] auto components(
            const std::vector<std::size_t> &unresolved,
            const TerrainTargetGrid &grid) -> std::vector<std::vector<std::size_t>>
        {
            auto unresolved_set = std::set<std::size_t>{unresolved.begin(), unresolved.end()};
            auto out = std::vector<std::vector<std::size_t>>{};
            while (!unresolved_set.empty()) {
                auto component = std::vector<std::size_t>{};
                auto queue = std::deque<std::size_t>{*unresolved_set.begin()};
                unresolved_set.erase(unresolved_set.begin());
                while (!queue.empty()) {
                    const auto cell = queue.front();
                    queue.pop_front();
                    component.push_back(cell);
                    const auto column = static_cast<std::uint64_t>(cell) % grid.width();
                    const auto row = static_cast<std::uint64_t>(cell) / grid.width();
                    for (auto dr = -1; dr <= 1; ++dr) {
                        for (auto dc = -1; dc <= 1; ++dc) {
                            if (dr == 0 && dc == 0) {
                                continue;
                            }
                            const auto nc_i = static_cast<int>(column) + dc;
                            const auto nr_i = static_cast<int>(row) + dr;
                            if (nc_i < 0 || nr_i < 0 ||
                                nc_i >= static_cast<int>(grid.width()) ||
                                nr_i >= static_cast<int>(grid.height())) {
                                continue;
                            }
                            const auto neighbour = index(grid.width(), static_cast<std::uint64_t>(nc_i), static_cast<std::uint64_t>(nr_i));
                            if (auto found = unresolved_set.find(neighbour); found != unresolved_set.end()) {
                                unresolved_set.erase(found);
                                queue.push_back(neighbour);
                            }
                        }
                    }
                }
                out.push_back(std::move(component));
            }
            return out;
        }

        [[nodiscard]] auto component_diameter(
            const std::vector<std::size_t> &component,
            const TerrainTargetGrid &grid) -> double
        {
            auto min_x = std::numeric_limits<double>::infinity();
            auto min_y = std::numeric_limits<double>::infinity();
            auto max_x = -std::numeric_limits<double>::infinity();
            auto max_y = -std::numeric_limits<double>::infinity();
            for (const auto cell : component) {
                const auto column = static_cast<std::uint64_t>(cell) % grid.width();
                const auto row = static_cast<std::uint64_t>(cell) / grid.width();
                const auto centre = terrain_grid_cell_centre(grid, column, row);
                min_x = std::min(min_x, centre.x);
                min_y = std::min(min_y, centre.y);
                max_x = std::max(max_x, centre.x);
                max_y = std::max(max_y, centre.y);
            }
            return std::hypot(max_x - min_x, max_y - min_y);
        }
    }

    auto prepare_terrain_conditioning(
        const TerrainConditioningRequest &request) -> tsunami::core::Result<TerrainConditioningPreparation>
    {
        if (request.configuration == nullptr || request.manifest == nullptr ||
            request.corridor == nullptr || request.corridor_record == nullptr) {
            return tsunami::core::failure<TerrainConditioningPreparation>(terrain_error("geo.terrain.request_invalid", "terrain conditioning request is missing required inputs", "geo.terrain.request.identities_match"));
        }
        const auto &configuration = *request.configuration;
        const auto &manifest = *request.manifest;
        if (manifest.identity().case_revision.case_id != configuration.identity().case_id ||
            manifest.identity().case_revision.revision != configuration.identity().revision ||
            request.identity.case_revision != manifest.identity().case_revision ||
            request.identity.manifest_id != manifest.identity().manifest_id ||
            request.identity.manifest_revision != manifest.identity().manifest_revision) {
            return tsunami::core::failure<TerrainConditioningPreparation>(terrain_error("geo.terrain.case_manifest_mismatch", "case, manifest and terrain identity revisions do not match", "geo.terrain.request.identities_match"));
        }
        if (request.corridor_record->identity.case_revision != manifest.identity().case_revision) {
            return tsunami::core::failure<TerrainConditioningPreparation>(terrain_error("geo.terrain.corridor_record_mismatch", "corridor record does not match the requested case revision", "geo.terrain.request.corridor_matches_case"));
        }
        auto grid = build_corridor_aligned_terrain_grid(*request.corridor, *request.corridor_record, request.policy.grid);
        if (!grid) {
            return tsunami::core::failure<TerrainConditioningPreparation>(grid.error());
        }
        auto coverage = calculate_corridor_coverage(*request.corridor, *request.corridor_record, grid.value(), request.policy.grid);
        if (!coverage) {
            return tsunami::core::failure<TerrainConditioningPreparation>(coverage.error());
        }
        if (auto valid = validate_source(request.bathymetry, configuration, manifest, grid.value().target_reference()); !valid) {
            return tsunami::core::failure<TerrainConditioningPreparation>(valid.error());
        }
        if (auto valid = validate_source(request.topography, configuration, manifest, grid.value().target_reference()); !valid) {
            return tsunami::core::failure<TerrainConditioningPreparation>(valid.error());
        }
        if (!merge_policy_valid(request.policy.merge) ||
            (request.policy.merge.first_priority_dataset_id != request.bathymetry.expected_dataset_id &&
             request.policy.merge.first_priority_dataset_id != request.topography.expected_dataset_id) ||
            (request.policy.merge.second_priority_dataset_id != request.bathymetry.expected_dataset_id &&
             request.policy.merge.second_priority_dataset_id != request.topography.expected_dataset_id)) {
            return tsunami::core::failure<TerrainConditioningPreparation>(terrain_error("geo.terrain.merge_policy_invalid", "terrain merge priority is invalid", "geo.terrain.merge.priority_explicit"));
        }
        if (!gap_policy_valid(request.policy.gaps)) {
            return tsunami::core::failure<TerrainConditioningPreparation>(terrain_error("geo.terrain.gap_unresolved", "terrain gap policy is invalid", "geo.terrain.gap.policy_explicit"));
        }
        auto bathymetry_request = TerrainSourceResamplingRequest{
            request.bathymetry.raster,
            request.bathymetry.import_record,
            request.bathymetry.transformation_plan,
            request.bathymetry.transformation_record,
            grid.value(),
            TerrainSourceRole::bathymetry,
            request.bathymetry.resampling_kernel,
            request.policy.grid.maximum_upsampling_factor,
            request.resource_root};
        auto topography_request = TerrainSourceResamplingRequest{
            request.topography.raster,
            request.topography.import_record,
            request.topography.transformation_plan,
            request.topography.transformation_record,
            grid.value(),
            TerrainSourceRole::topography,
            request.topography.resampling_kernel,
            request.policy.grid.maximum_upsampling_factor,
            request.resource_root};
        if (auto valid = validate_terrain_source_resampling_request(bathymetry_request); !valid) {
            return tsunami::core::failure<TerrainConditioningPreparation>(valid.error());
        }
        if (auto valid = validate_terrain_source_resampling_request(topography_request); !valid) {
            return tsunami::core::failure<TerrainConditioningPreparation>(valid.error());
        }
        return tsunami::core::success(TerrainConditioningPreparation{
            grid.value(),
            coverage.value(),
            std::move(bathymetry_request),
            std::move(topography_request),
            request.identity,
            request.policy,
            request.corridor_record->identity,
            configuration.scenario().scenario_id,
            configuration.scenario().target_site,
            tsunami::data::DatasetUncertainty{tsunami::data::UncertaintyStatus::not_reported, {}, std::string{"not_reported"}},
            default_conditioned_terrain_path(request.identity.output_dataset_id)});
    }

    auto condition_terrain_from_resampled_sources(
        const TerrainConditioningPreparation &preparation,
        const ResampledTerrainSource &bathymetry,
        const ResampledTerrainSource &topography) -> tsunami::core::Result<TerrainConditioningResult>
    {
        if (!same_grid(preparation.grid, bathymetry.grid) || !same_grid(preparation.grid, topography.grid) ||
            bathymetry.role != TerrainSourceRole::bathymetry || topography.role != TerrainSourceRole::topography) {
            return tsunami::core::failure<TerrainConditioningResult>(terrain_error("geo.terrain.target_grid_mismatch", "resampled terrain sources do not share the preparation target grid", "geo.terrain.output.lineage_complete"));
        }
        const auto cells = static_cast<std::size_t>(preparation.grid.cell_count());
        if (bathymetry.values.size() != cells || topography.values.size() != cells ||
            bathymetry.valid_mask.size() != cells || topography.valid_mask.size() != cells) {
            return tsunami::core::failure<TerrainConditioningResult>(terrain_error("geo.terrain.target_grid_mismatch", "resampled terrain vectors do not match target grid", "geo.terrain.output.lineage_complete"));
        }
        auto values = std::vector<double>(cells, 0.0);
        auto mask = std::vector<std::uint8_t>(cells, 0U);
        auto lineage = std::vector<TerrainCellLineage>(cells, TerrainCellLineage::outside_corridor);
        auto diagnostics = TerrainConditioningDiagnostics{};
        diagnostics.total_cell_count = preparation.grid.cell_count();
        diagnostics.active_cell_count = preparation.coverage.active_cell_count;
        diagnostics.outside_corridor_cell_count = preparation.coverage.outside_cell_count;
        diagnostics.excluded_boundary_cell_count = preparation.coverage.excluded_boundary_cell_count;

        auto overlap_sum = 0.0;
        auto overlap_square_sum = 0.0;
        auto unresolved = std::vector<std::size_t>{};
        for (std::size_t i = 0U; i < cells; ++i) {
            const auto cell_class = preparation.coverage.cell_classes[i];
            if (cell_class == TerrainCorridorCellClass::outside_corridor) {
                lineage[i] = TerrainCellLineage::outside_corridor;
                continue;
            }
            if (cell_class == TerrainCorridorCellClass::excluded_boundary_fraction) {
                lineage[i] = TerrainCellLineage::excluded_boundary_fraction;
                continue;
            }
            const auto bathy_valid = bathymetry.valid_mask[i] != 0U;
            const auto topo_valid = topography.valid_mask[i] != 0U;
            if (bathy_valid && topo_valid) {
                ++diagnostics.overlap_cell_count;
                ++diagnostics.overlap.overlap_cell_count;
                const auto difference = topography.values[i] - bathymetry.values[i];
                overlap_sum += difference;
                overlap_square_sum += difference * difference;
                diagnostics.overlap.maximum_absolute_difference_m = std::max(diagnostics.overlap.maximum_absolute_difference_m, std::abs(difference));
                const auto conflict = std::abs(difference) > preparation.policy.merge.maximum_overlap_disagreement_m;
                if (conflict) {
                    ++diagnostics.overlap_conflict_cell_count;
                    ++diagnostics.overlap.disagreement_exceedance_count;
                    if (preparation.policy.merge.conflict_policy == TerrainOverlapConflictPolicy::reject) {
                        return tsunami::core::failure<TerrainConditioningResult>(terrain_error("geo.terrain.overlap_conflict", "terrain overlap disagreement exceeds policy", "geo.terrain.merge.overlap_within_tolerance").add_context("cell_index", std::to_string(i)).add_context("overlap_difference_m", std::to_string(difference)));
                    }
                }
                const auto bathymetry_first = preparation.policy.merge.first_priority_dataset_id == bathymetry.dataset_id;
                values[i] = bathymetry_first ? bathymetry.values[i] : topography.values[i];
                mask[i] = 1U;
                if (bathymetry_first) {
                    lineage[i] = conflict ? TerrainCellLineage::overlap_bathymetry_selected_with_conflict : TerrainCellLineage::overlap_bathymetry_selected;
                    ++diagnostics.bathymetry_selected_cell_count;
                } else {
                    lineage[i] = conflict ? TerrainCellLineage::overlap_topography_selected_with_conflict : TerrainCellLineage::overlap_topography_selected;
                    ++diagnostics.topography_selected_cell_count;
                }
            } else if (bathy_valid) {
                values[i] = bathymetry.values[i];
                mask[i] = 1U;
                lineage[i] = TerrainCellLineage::bathymetry_selected;
                ++diagnostics.bathymetry_selected_cell_count;
            } else if (topo_valid) {
                values[i] = topography.values[i];
                mask[i] = 1U;
                lineage[i] = TerrainCellLineage::topography_selected;
                ++diagnostics.topography_selected_cell_count;
            } else {
                unresolved.push_back(i);
            }
        }
        diagnostics.initially_unresolved_cell_count = static_cast<std::uint64_t>(unresolved.size());
        if (diagnostics.overlap.overlap_cell_count > 0U) {
            const auto n = static_cast<double>(diagnostics.overlap.overlap_cell_count);
            diagnostics.overlap.mean_signed_difference_m = overlap_sum / n;
            diagnostics.overlap.root_mean_square_difference_m = std::sqrt(overlap_square_sum / n);
        }
        if (!unresolved.empty()) {
            if (preparation.policy.gaps.kind == TerrainGapResolutionKind::reject) {
                diagnostics.unresolved_cell_count = static_cast<std::uint64_t>(unresolved.size());
                return tsunami::core::failure<TerrainConditioningResult>(terrain_error("geo.terrain.gap_unresolved", "terrain conditioning leaves unresolved active nodata", "geo.terrain.gap.policy_explicit"));
            }
            for (const auto &component : components(unresolved, preparation.grid)) {
                if (component_touches_boundary(component, preparation.coverage)) {
                    return tsunami::core::failure<TerrainConditioningResult>(terrain_error("geo.terrain.gap_boundary_touching", "boundary-touching terrain gap cannot be filled", "geo.terrain.gap.component_bounded"));
                }
                if (component.size() > preparation.policy.gaps.maximum_component_cells ||
                    component_diameter(component, preparation.grid) > preparation.policy.gaps.maximum_component_diameter_m) {
                    return tsunami::core::failure<TerrainConditioningResult>(terrain_error("geo.terrain.gap_component_too_large", "terrain gap component exceeds fill bounds", "geo.terrain.gap.component_bounded"));
                }
                auto donors = std::vector<std::size_t>{};
                auto mixed_donor_lineage = false;
                for (std::size_t i = 0U; i < cells; ++i) {
                    if (mask[i] == 0U) {
                        continue;
                    }
                    auto within_component_distance = false;
                    for (const auto gap_cell : component) {
                        const auto gap_centre = terrain_grid_cell_centre(preparation.grid, static_cast<std::uint64_t>(gap_cell) % preparation.grid.width(), static_cast<std::uint64_t>(gap_cell) / preparation.grid.width());
                        const auto donor_centre = terrain_grid_cell_centre(preparation.grid, static_cast<std::uint64_t>(i) % preparation.grid.width(), static_cast<std::uint64_t>(i) / preparation.grid.width());
                        if (std::hypot(gap_centre.x - donor_centre.x, gap_centre.y - donor_centre.y) <= preparation.policy.gaps.maximum_fill_distance_m) {
                            within_component_distance = true;
                            break;
                        }
                    }
                    if (!within_component_distance) {
                        continue;
                    }
                    const auto donor_family_ok = donors.empty() ||
                        (lineage_is_bathymetry(lineage[donors.front()]) && lineage_is_bathymetry(lineage[i])) ||
                        (lineage_is_topography(lineage[donors.front()]) && lineage_is_topography(lineage[i]));
                    if (!donor_family_ok) {
                        mixed_donor_lineage = true;
                        continue;
                    }
                    donors.push_back(i);
                }
                if (mixed_donor_lineage) {
                    return tsunami::core::failure<TerrainConditioningResult>(terrain_error("geo.terrain.gap_donor_lineage_mixed", "terrain gap donors cross bathymetry/topography lineage families", "geo.terrain.gap.donor_lineage_consistent"));
                }
                if (donors.size() < preparation.policy.gaps.minimum_donor_count) {
                    return tsunami::core::failure<TerrainConditioningResult>(terrain_error("geo.terrain.gap_donors_insufficient", "terrain gap has insufficient donors", "geo.terrain.gap.donor_lineage_consistent"));
                }
                const auto bathy_family = lineage_is_bathymetry(lineage[donors.front()]);
                for (const auto cell : component) {
                    const auto centre = terrain_grid_cell_centre(preparation.grid, static_cast<std::uint64_t>(cell) % preparation.grid.width(), static_cast<std::uint64_t>(cell) / preparation.grid.width());
                    auto weighted_sum = 0.0;
                    auto weight_total = 0.0;
                    for (const auto donor : donors) {
                        const auto donor_centre = terrain_grid_cell_centre(preparation.grid, static_cast<std::uint64_t>(donor) % preparation.grid.width(), static_cast<std::uint64_t>(donor) / preparation.grid.width());
                        const auto distance = std::hypot(centre.x - donor_centre.x, centre.y - donor_centre.y);
                        if (distance <= 0.0 || distance > preparation.policy.gaps.maximum_fill_distance_m) {
                            continue;
                        }
                        const auto weight = 1.0 / std::pow(distance, preparation.policy.gaps.distance_exponent);
                        weighted_sum += weight * values[donor];
                        weight_total += weight;
                    }
                    if (weight_total <= 0.0) {
                        return tsunami::core::failure<TerrainConditioningResult>(terrain_error("geo.terrain.gap_donors_insufficient", "terrain gap has no usable donor distances", "geo.terrain.gap.donor_lineage_consistent"));
                    }
                    values[cell] = weighted_sum / weight_total;
                    mask[cell] = 1U;
                    lineage[cell] = bathy_family ? TerrainCellLineage::filled_from_bathymetry_neighbourhood : TerrainCellLineage::filled_from_topography_neighbourhood;
                    ++diagnostics.filled_cell_count;
                }
            }
            const auto filled_fraction = static_cast<double>(diagnostics.filled_cell_count) / static_cast<double>(diagnostics.active_cell_count);
            if (filled_fraction > preparation.policy.gaps.maximum_filled_fraction) {
                return tsunami::core::failure<TerrainConditioningResult>(terrain_error("geo.terrain.filled_fraction_exceeded", "terrain fill fraction exceeds policy", "geo.terrain.gap.filled_fraction_bounded"));
            }
        }
        diagnostics.unresolved_cell_count = 0U;
        auto terrain = make_conditioned_terrain_raster(
            preparation.grid,
            values,
            mask,
            preparation.coverage.fractions,
            lineage);
        if (!terrain) {
            return tsunami::core::failure<TerrainConditioningResult>(terrain.error());
        }
        diagnostics.minimum_elevation_m = terrain.value().minimum_elevation_m();
        diagnostics.maximum_elevation_m = terrain.value().maximum_elevation_m();
        auto record = TerrainConditioningRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{terrain_conditioning_record_schema_name}, supported_terrain_conditioning_record_version};
        record.policy_version = supported_terrain_conditioning_record_policy_version;
        record.formula_version = terrain_conditioning_formula_version;
        record.identity = preparation.identity;
        record.scenario_id = preparation.scenario_id;
        record.target_site = preparation.target_site;
        record.bathymetry_dataset_id = bathymetry.dataset_id;
        record.bathymetry_asset_id = bathymetry.resampling.asset_id;
        record.bathymetry_import_identity = bathymetry.resampling.import_identity;
        record.bathymetry_transformation_identity = bathymetry.resampling.transformation_identity;
        record.topography_dataset_id = topography.dataset_id;
        record.topography_asset_id = topography.resampling.asset_id;
        record.topography_import_identity = topography.resampling.import_identity;
        record.topography_transformation_identity = topography.resampling.transformation_identity;
        record.corridor_identity = preparation.corridor_identity;
        record.target_reference = preparation.grid.target_reference();
        record.grid = preparation.grid;
        record.grid_policy = preparation.policy.grid;
        record.bathymetry_resampling = bathymetry.resampling;
        record.topography_resampling = topography.resampling;
        record.merge_policy = preparation.policy.merge;
        record.gap_policy = preparation.policy.gaps;
        record.diagnostics = diagnostics;
        record.output_uncertainty = preparation.output_uncertainty;
        record.output_media_type = "image/tiff";
        record.output_path = preparation.output_path;
        record.digest_status = "not_computed_by_terrain_conditioning";
        if (auto valid = validate_terrain_conditioning_record(record); !valid) {
            return tsunami::core::failure<TerrainConditioningResult>(valid.error());
        }
        return tsunami::core::success(TerrainConditioningResult{terrain.value(), std::move(record), diagnostics});
    }

} // namespace tsunami::geo
