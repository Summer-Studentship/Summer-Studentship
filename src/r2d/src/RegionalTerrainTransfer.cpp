#include <tsunami/r2d/RegionalTerrainTransfer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto stencil_operation = "make_regional_raster_cell_transfer_stencil";
        constexpr auto transfer_operation = "transfer_conditioned_terrain_to_regional_bathymetry";
        constexpr auto rule_id = "r2d.terrain_transfer";
        constexpr auto affine_singularity_safety_multiplier = 64.0;

        struct Point
        {
            double column{};
            double row{};
        };

        [[nodiscard]] auto transfer_error(
            std::string code,
            std::string message,
            std::string operation,
            const tsunami::fvm::FiniteVolumeMesh *mesh = nullptr,
            std::string terrain_id = {},
            std::optional<std::size_t> cell_id = std::nullopt,
            std::optional<std::uint64_t> raster_index = std::nullopt,
            std::optional<std::uint64_t> raster_row = std::nullopt,
            std::optional<std::uint64_t> raster_column = std::nullopt) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", std::move(operation))
                .add_context("rule_id", rule_id)
                .add_context("state_changed", "false");
            if (mesh != nullptr) {
                error.add_context("mesh_id", mesh->summary().id.value);
            }
            if (!terrain_id.empty()) {
                error.add_context("terrain_id", std::move(terrain_id));
            }
            if (cell_id) {
                error.add_context("cell_id", std::to_string(*cell_id));
            }
            if (raster_row) {
                error.add_context("raster_row", std::to_string(*raster_row));
            }
            if (raster_column) {
                error.add_context("raster_column", std::to_string(*raster_column));
            }
            if (raster_index) {
                error.add_context("raster_index", std::to_string(*raster_index));
            }
            return error;
        }

        auto add_area_context(
            tsunami::core::Error &error,
            double cell_measure,
            double mapped_area,
            std::size_t contributor_count) -> void
        {
            error.add_context("cell_measure_m2", std::to_string(cell_measure))
                .add_context("mapped_area_m2", std::to_string(mapped_area))
                .add_context("area_residual_m2", std::to_string(std::abs(mapped_area - cell_measure)))
                .add_context("contributor_count", std::to_string(contributor_count));
        }

        [[nodiscard]] auto finite_transform(const tsunami::geo::RasterAffineTransform &transform) noexcept -> bool
        {
            return std::isfinite(transform.origin_x) && std::isfinite(transform.pixel_width) &&
                std::isfinite(transform.row_rotation) && std::isfinite(transform.origin_y) &&
                std::isfinite(transform.column_rotation) && std::isfinite(transform.pixel_height);
        }

        [[nodiscard]] auto affine_is_numerically_singular(
            const tsunami::geo::RasterAffineTransform &transform,
            double determinant) noexcept -> bool
        {
            if (!finite_transform(transform) || !std::isfinite(determinant)) {
                return true;
            }
            const auto scale = std::max({
                1.0,
                std::abs(transform.pixel_width * transform.pixel_height),
                std::abs(transform.row_rotation * transform.column_rotation)});
            // The multiplier keeps the affine inversion away from machine-precision cancellation without making it
            // a user-facing modelling policy.
            const auto singular_bound = affine_singularity_safety_multiplier *
                std::numeric_limits<double>::epsilon() * scale;
            return std::abs(determinant) <= singular_bound;
        }

        [[nodiscard]] auto inverse_point(
            tsunami::fvm::Point3 point,
            const tsunami::geo::RasterAffineTransform &transform,
            double determinant) noexcept -> Point
        {
            const auto x = point.x - transform.origin_x;
            const auto y = point.y - transform.origin_y;
            return Point{
                (transform.pixel_height * x - transform.row_rotation * y) / determinant,
                (-transform.column_rotation * x + transform.pixel_width * y) / determinant};
        }

        template <class Inside, class Intersection>
        [[nodiscard]] auto clip_edge(
            const std::vector<Point> &input,
            Inside inside,
            Intersection intersection) -> std::vector<Point>
        {
            auto output = std::vector<Point>{};
            if (input.empty()) {
                return output;
            }
            output.reserve(input.size() + 1U);
            auto previous = input.back();
            auto previous_inside = inside(previous);
            for (const auto current : input) {
                const auto current_inside = inside(current);
                if (current_inside != previous_inside) {
                    output.push_back(intersection(previous, current));
                }
                if (current_inside) {
                    output.push_back(current);
                }
                previous = current;
                previous_inside = current_inside;
            }
            return output;
        }

        [[nodiscard]] auto clip_cell(
            const std::array<Point, 3U> &triangle,
            std::uint64_t column,
            std::uint64_t row) -> std::vector<Point>
        {
            const auto left = static_cast<double>(column);
            const auto right = left + 1.0;
            const auto top = static_cast<double>(row);
            const auto bottom = top + 1.0;
            auto polygon = std::vector<Point>{triangle.begin(), triangle.end()};
            const auto vertical_intersection = [](double boundary, Point first, Point second) {
                const auto fraction = (boundary - first.column) / (second.column - first.column);
                return Point{boundary, first.row + fraction * (second.row - first.row)};
            };
            const auto horizontal_intersection = [](double boundary, Point first, Point second) {
                const auto fraction = (boundary - first.row) / (second.row - first.row);
                return Point{first.column + fraction * (second.column - first.column), boundary};
            };
            polygon = clip_edge(
                polygon,
                [left](Point point) { return point.column >= left; },
                [left, &vertical_intersection](Point a, Point b) { return vertical_intersection(left, a, b); });
            polygon = clip_edge(
                polygon,
                [right](Point point) { return point.column <= right; },
                [right, &vertical_intersection](Point a, Point b) { return vertical_intersection(right, a, b); });
            polygon = clip_edge(
                polygon,
                [top](Point point) { return point.row >= top; },
                [top, &horizontal_intersection](Point a, Point b) { return horizontal_intersection(top, a, b); });
            return clip_edge(
                polygon,
                [bottom](Point point) { return point.row <= bottom; },
                [bottom, &horizontal_intersection](Point a, Point b) { return horizontal_intersection(bottom, a, b); });
        }

        [[nodiscard]] auto polygon_area(const std::vector<Point> &polygon) noexcept -> double
        {
            if (polygon.size() < 3U) {
                return 0.0;
            }
            auto twice_area = 0.0;
            for (std::size_t index = 0U; index < polygon.size(); ++index) {
                const auto &first = polygon[index];
                const auto &second = polygon[(index + 1U) % polygon.size()];
                twice_area += first.column * second.row - second.column * first.row;
            }
            return 0.5 * std::abs(twice_area);
        }

        [[nodiscard]] auto cell_vertices(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            std::size_t cell_index) -> std::optional<std::array<tsunami::fvm::Point3, 3U>>
        {
            const auto &cell = mesh.cell(tsunami::fvm::CellId{cell_index});
            auto vertex_ids = std::set<tsunami::fvm::VertexId>{};
            for (const auto face_id : cell.faces) {
                const auto &face = mesh.face(face_id);
                vertex_ids.insert(face.vertices.begin(), face.vertices.end());
            }
            if (cell.faces.size() != 3U || vertex_ids.size() != 3U) {
                return std::nullopt;
            }
            auto vertices = std::array<tsunami::fvm::Point3, 3U>{};
            auto output = vertices.begin();
            for (const auto id : vertex_ids) {
                *output++ = mesh.vertex(id).position;
            }
            return vertices;
        }

        [[nodiscard]] auto active_lineage(tsunami::geo::TerrainCellLineage lineage) noexcept -> bool
        {
            return lineage != tsunami::geo::TerrainCellLineage::outside_corridor &&
                lineage != tsunami::geo::TerrainCellLineage::excluded_boundary_fraction;
        }

        [[nodiscard]] auto valid_policy(const RegionalRasterCellTransferPolicy &policy) noexcept -> bool
        {
            return std::isfinite(policy.absolute_area_tolerance_m2) && policy.absolute_area_tolerance_m2 >= 0.0 &&
                std::isfinite(policy.relative_area_tolerance) && policy.relative_area_tolerance >= 0.0 &&
                policy.maximum_contributors_per_cell > 0U;
        }
    } // namespace

    RegionalRasterCellTransferStencil::RegionalRasterCellTransferStencil(
        tsunami::fvm::MeshBinding mesh_binding,
        tsunami::geo::TerrainTargetGrid grid,
        RegionalRasterCellTransferPolicy policy,
        std::vector<RegionalRasterCellContributionRange> cell_ranges,
        std::vector<double> mapped_area_m2,
        std::vector<RegionalRasterCellContribution> contributions)
        : mesh_binding_{std::move(mesh_binding)},
          grid_{std::move(grid)},
          policy_{policy},
          cell_ranges_{std::move(cell_ranges)},
          mapped_area_m2_{std::move(mapped_area_m2)},
          contributions_{std::move(contributions)}
    {
    }

    auto make_regional_raster_cell_transfer_stencil(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::geo::TerrainTargetGrid &grid,
        const RegionalRasterCellTransferPolicy &policy)
        -> tsunami::core::Result<RegionalRasterCellTransferStencil>
    {
        const auto summary = mesh.summary();
        if (!valid_policy(policy) || summary.cell_count == 0U || grid.width() == 0U || grid.height() == 0U ||
            grid.width() > std::numeric_limits<std::uint64_t>::max() / grid.height()) {
            return tsunami::core::failure<RegionalRasterCellTransferStencil>(transfer_error(
                "r2d.terrain_transfer.request_invalid",
                "transfer policy, mesh, and terrain grid must define finite non-empty bounds",
                stencil_operation,
                &mesh));
        }
        const auto &transform = grid.transform();
        const auto determinant = transform.pixel_width * transform.pixel_height -
            transform.row_rotation * transform.column_rotation;
        if (affine_is_numerically_singular(transform, determinant)) {
            return tsunami::core::failure<RegionalRasterCellTransferStencil>(transfer_error(
                "r2d.terrain_transfer.affine_invalid",
                "terrain affine transform must be finite and numerically invertible",
                stencil_operation,
                &mesh));
        }
        const auto physical_scale = std::abs(determinant);
        auto cell_ranges = std::vector<RegionalRasterCellContributionRange>{};
        auto mapped_area_m2 = std::vector<double>{};
        auto contributions = std::vector<RegionalRasterCellContribution>{};
        cell_ranges.reserve(summary.cell_count);
        mapped_area_m2.reserve(summary.cell_count);

        for (std::size_t cell_index = 0U; cell_index < summary.cell_count; ++cell_index) {
            const auto vertices = cell_vertices(mesh, cell_index);
            if (!vertices) {
                return tsunami::core::failure<RegionalRasterCellTransferStencil>(transfer_error(
                    "r2d.terrain_transfer.cell_vertices_invalid",
                    "each Regional2D transfer cell must have exactly three faces and three unique vertices",
                    stencil_operation,
                    &mesh,
                    {},
                    cell_index));
            }
            auto triangle = std::array<Point, 3U>{};
            for (std::size_t index = 0U; index < vertices->size(); ++index) {
                triangle[index] = inverse_point((*vertices)[index], transform, determinant);
                if (!std::isfinite(triangle[index].column) || !std::isfinite(triangle[index].row)) {
                    return tsunami::core::failure<RegionalRasterCellTransferStencil>(transfer_error(
                        "r2d.terrain_transfer.affine_invalid",
                        "inverse affine transformation produced nonfinite raster coordinates",
                        stencil_operation,
                        &mesh,
                        {},
                        cell_index));
                }
            }
            const auto [minimum_column_it, maximum_column_it] = std::minmax_element(
                triangle.begin(), triangle.end(), [](Point left, Point right) { return left.column < right.column; });
            const auto [minimum_row_it, maximum_row_it] = std::minmax_element(
                triangle.begin(), triangle.end(), [](Point left, Point right) { return left.row < right.row; });
            const auto minimum_column = std::max(0.0, std::floor(minimum_column_it->column));
            const auto maximum_column = std::min(
                static_cast<double>(grid.width() - 1U), std::floor(maximum_column_it->column));
            const auto minimum_row = std::max(0.0, std::floor(minimum_row_it->row));
            const auto maximum_row = std::min(
                static_cast<double>(grid.height() - 1U), std::floor(maximum_row_it->row));
            const auto contribution_begin = contributions.size();
            auto mapped_area = 0.0;
            if (minimum_column <= maximum_column && minimum_row <= maximum_row) {
                const auto last_row = static_cast<std::uint64_t>(maximum_row);
                const auto last_column = static_cast<std::uint64_t>(maximum_column);
                for (auto row = static_cast<std::uint64_t>(minimum_row);;) {
                    for (auto column = static_cast<std::uint64_t>(minimum_column);;) {
                        const auto raster_area = polygon_area(clip_cell(triangle, column, row));
                        const auto overlap_area = raster_area * physical_scale;
                        const auto zero_scale = std::numeric_limits<double>::epsilon() *
                            std::max({1.0, physical_scale, std::abs(mesh.cell_geometry(tsunami::fvm::CellId{cell_index}).measure)}) *
                            32.0;
                        if (overlap_area <= zero_scale) {
                            if (column == last_column) {
                                break;
                            }
                            ++column;
                            continue;
                        }
                        if (contributions.size() - contribution_begin >= policy.maximum_contributors_per_cell) {
                            auto error = transfer_error(
                                "r2d.terrain_transfer.contributor_limit_exceeded",
                                "cell contributor count exceeds the configured transfer limit",
                                stencil_operation,
                                &mesh,
                                {},
                                cell_index);
                            error.add_context("contributor_count", std::to_string(contributions.size() - contribution_begin + 1U));
                            return tsunami::core::failure<RegionalRasterCellTransferStencil>(std::move(error));
                        }
                        const auto raster_index = row * grid.width() + column;
                        contributions.push_back(RegionalRasterCellContribution{raster_index, overlap_area, 0.0});
                        mapped_area += overlap_area;
                        if (column == last_column) {
                            break;
                        }
                        ++column;
                    }
                    if (row == last_row) {
                        break;
                    }
                    ++row;
                }
            }
            const auto cell_measure = mesh.cell_geometry(tsunami::fvm::CellId{cell_index}).measure;
            const auto contributor_count = contributions.size() - contribution_begin;
            const auto allowed_residual = policy.absolute_area_tolerance_m2 +
                policy.relative_area_tolerance * std::max(1.0, cell_measure);
            if (!std::isfinite(cell_measure) || cell_measure <= 0.0 || contributor_count == 0U ||
                !std::isfinite(mapped_area) || std::abs(mapped_area - cell_measure) > allowed_residual) {
                auto error = transfer_error(
                    "r2d.terrain_transfer.cell_uncovered",
                    "mapped raster area does not reproduce the authoritative FVM cell measure",
                    stencil_operation,
                    &mesh,
                    {},
                    cell_index);
                add_area_context(error, cell_measure, mapped_area, contributor_count);
                return tsunami::core::failure<RegionalRasterCellTransferStencil>(std::move(error));
            }
            auto weight_sum = 0.0;
            for (auto index = contribution_begin; index < contributions.size(); ++index) {
                contributions[index].weight = contributions[index].overlap_area_m2 / mapped_area;
                weight_sum += contributions[index].weight;
            }
            if (!std::isfinite(weight_sum) || std::abs(weight_sum - 1.0) >
                    32.0 * std::numeric_limits<double>::epsilon() * std::max(1.0, static_cast<double>(contributor_count))) {
                return tsunami::core::failure<RegionalRasterCellTransferStencil>(transfer_error(
                    "r2d.terrain_transfer.weight_invalid",
                    "normalised transfer weights do not sum to one",
                    stencil_operation,
                    &mesh,
                    {},
                    cell_index));
            }
            cell_ranges.push_back(RegionalRasterCellContributionRange{contribution_begin, contributor_count});
            mapped_area_m2.push_back(mapped_area);
        }
        return tsunami::core::success(RegionalRasterCellTransferStencil{
            tsunami::fvm::make_mesh_binding(mesh),
            grid,
            policy,
            std::move(cell_ranges),
            std::move(mapped_area_m2),
            std::move(contributions)});
    }

    auto transfer_conditioned_terrain_to_regional_bathymetry(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::geo::ConditionedTerrainRaster &terrain,
        const tsunami::geo::TerrainConditioningRecord &record,
        const RegionalGeometryPreflightReport &preflight,
        const RegionalRasterCellTransferStencil &stencil,
        tsunami::fvm::FieldId field_id,
        std::string field_name) -> tsunami::core::Result<RegionalTerrainTransferResult>
    {
        const auto summary = mesh.summary();
        const auto terrain_id = record.identity.terrain_id;
        if (const auto valid_record = tsunami::geo::validate_terrain_conditioning_record(record); !valid_record) {
            return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                "r2d.terrain_transfer.record_invalid",
                "terrain conditioning record must be valid before Regional2D transfer",
                transfer_operation,
                &mesh,
                terrain_id)
                    .with_cause_code(valid_record.error().code()));
        }
        if (preflight.validation_status != "accepted" || preflight.mesh_id != summary.id.value ||
            preflight.vertex_count != summary.vertex_count || preflight.face_count != summary.face_count ||
            preflight.cell_count != summary.cell_count || preflight.terrain_id != terrain_id) {
            return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                "r2d.terrain_transfer.preflight_mismatch",
                "accepted preflight identities and mesh counts must match transfer inputs",
                transfer_operation,
                &mesh,
                terrain_id));
        }
        if (terrain.grid() != record.grid || terrain.grid() != stencil.grid()) {
            return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                "r2d.terrain_transfer.grid_mismatch",
                "terrain, conditioning record, and stencil grids must match exactly",
                transfer_operation,
                &mesh,
                terrain_id));
        }
        const auto cell_count = terrain.cell_count();
        const auto cell_ranges = stencil.cell_ranges();
        const auto mapped_area_m2 = stencil.mapped_area_m2();
        const auto contributions = stencil.contributions();
        const auto &stencil_policy = stencil.policy();
        if (terrain.values().size() != cell_count || terrain.valid_mask().size() != cell_count ||
            terrain.corridor_coverage_fraction().size() != cell_count || terrain.cell_lineage().size() != cell_count ||
            stencil.mesh_binding() != tsunami::fvm::make_mesh_binding(mesh) ||
            cell_ranges.size() != summary.cell_count || mapped_area_m2.size() != summary.cell_count ||
            !valid_policy(stencil_policy) || !std::isfinite(record.grid_policy.active_coverage_threshold) ||
            record.grid_policy.active_coverage_threshold <= 0.0 ||
            record.grid_policy.active_coverage_threshold > 1.0) {
            return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                "r2d.terrain_transfer.request_invalid",
                "terrain arrays and stencil layout must match their bound grid and mesh",
                transfer_operation,
                &mesh,
                terrain_id));
        }

        auto values = std::vector<tsunami::core::Real>(summary.cell_count);
        auto diagnostics = RegionalTerrainTransferDiagnostics{};
        diagnostics.method_id = regional_terrain_transfer_method_id;
        diagnostics.mesh_id = summary.id.value;
        diagnostics.terrain_id = terrain_id;
        diagnostics.cell_count = summary.cell_count;
        diagnostics.total_contributor_count = contributions.size();
        diagnostics.minimum_contributors_per_cell = std::numeric_limits<std::size_t>::max();
        diagnostics.minimum_bed_elevation_m = std::numeric_limits<double>::infinity();
        diagnostics.maximum_bed_elevation_m = -std::numeric_limits<double>::infinity();

        auto expected_range_begin = std::size_t{0U};
        for (std::size_t cell_index = 0U; cell_index < summary.cell_count; ++cell_index) {
            const auto range = cell_ranges[cell_index];
            if (range.begin != expected_range_begin || range.begin > contributions.size() ||
                range.count > contributions.size() - range.begin ||
                range.count == 0U || range.count > stencil_policy.maximum_contributors_per_cell) {
                return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                    "r2d.terrain_transfer.weight_invalid",
                    "stencil contribution range is invalid",
                    transfer_operation,
                    &mesh,
                    terrain_id,
                    cell_index));
            }
            expected_range_begin += range.count;
            diagnostics.minimum_contributors_per_cell = std::min(diagnostics.minimum_contributors_per_cell, range.count);
            diagnostics.maximum_contributors_per_cell = std::max(diagnostics.maximum_contributors_per_cell, range.count);
            const auto cell_measure = mesh.cell_geometry(tsunami::fvm::CellId{cell_index}).measure;
            const auto mapped_area = mapped_area_m2[cell_index];
            if (!std::isfinite(cell_measure) || cell_measure <= 0.0 ||
                !std::isfinite(mapped_area) || mapped_area <= 0.0) {
                return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                    "r2d.terrain_transfer.weight_invalid",
                    "stencil mapped area and FVM cell measure must be finite and positive",
                    transfer_operation,
                    &mesh,
                    terrain_id,
                    cell_index));
            }
            diagnostics.total_mesh_area_m2 += cell_measure;
            diagnostics.total_mapped_terrain_area_m2 += mapped_area;
            diagnostics.maximum_cell_area_residual_m2 = std::max(
                diagnostics.maximum_cell_area_residual_m2, std::abs(mapped_area - cell_measure));
            auto elevation = 0.0;
            auto weight_sum = 0.0;
            auto overlap_area_sum = 0.0;
            auto previous_raster_index = std::optional<std::uint64_t>{};
            for (std::size_t offset = 0U; offset < range.count; ++offset) {
                const auto &contribution = contributions[range.begin + offset];
                if (contribution.raster_cell_index >= cell_count) {
                    return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                        "r2d.terrain_transfer.source_index_invalid",
                        "stencil contributor is outside the terrain raster",
                        transfer_operation,
                        &mesh,
                        terrain_id,
                        cell_index,
                        contribution.raster_cell_index));
                }
                const auto index = static_cast<std::size_t>(contribution.raster_cell_index);
                const auto row = contribution.raster_cell_index / terrain.width();
                const auto column = contribution.raster_cell_index % terrain.width();
                if (!std::isfinite(contribution.weight) || contribution.weight <= 0.0 ||
                    !std::isfinite(contribution.overlap_area_m2) || contribution.overlap_area_m2 <= 0.0) {
                    return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                        "r2d.terrain_transfer.weight_invalid",
                        "positive stencil contributions require finite positive areas and weights",
                        transfer_operation,
                        &mesh,
                        terrain_id,
                        cell_index,
                        contribution.raster_cell_index,
                        row,
                        column));
                }
                if (previous_raster_index && contribution.raster_cell_index <= *previous_raster_index) {
                    return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                        "r2d.terrain_transfer.weight_invalid",
                        "cell contributors must retain strict row-major raster order",
                        transfer_operation,
                        &mesh,
                        terrain_id,
                        cell_index,
                        contribution.raster_cell_index,
                        row,
                        column));
                }
                previous_raster_index = contribution.raster_cell_index;
                const auto expected_weight = contribution.overlap_area_m2 / mapped_area;
                const auto weight_ratio_tolerance = 64.0 * std::numeric_limits<double>::epsilon() *
                    std::max({1.0, std::abs(contribution.weight), std::abs(expected_weight)});
                if (!std::isfinite(expected_weight) ||
                    std::abs(contribution.weight - expected_weight) > weight_ratio_tolerance) {
                    return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                        "r2d.terrain_transfer.weight_invalid",
                        "stencil weight must match overlap area divided by mapped area",
                        transfer_operation,
                        &mesh,
                        terrain_id,
                        cell_index,
                        contribution.raster_cell_index,
                        row,
                        column));
                }
                if (terrain.valid_mask()[index] == 0U || !std::isfinite(terrain.corridor_coverage_fraction()[index]) ||
                    terrain.corridor_coverage_fraction()[index] < record.grid_policy.active_coverage_threshold ||
                    terrain.corridor_coverage_fraction()[index] > 1.0) {
                    return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                        "r2d.terrain_transfer.source_nodata",
                        "contributing terrain cell must be valid and have accepted corridor coverage",
                        transfer_operation,
                        &mesh,
                        terrain_id,
                        cell_index,
                        contribution.raster_cell_index,
                        row,
                        column));
                }
                const auto lineage = terrain.cell_lineage()[index];
                if (!active_lineage(lineage)) {
                    auto error = transfer_error(
                        "r2d.terrain_transfer.source_lineage_invalid",
                        "contributing terrain cell must have active terrain lineage",
                        transfer_operation,
                        &mesh,
                        terrain_id,
                        cell_index,
                        contribution.raster_cell_index,
                        row,
                        column);
                    error.add_context("lineage", std::string{tsunami::geo::to_string(lineage)});
                    return tsunami::core::failure<RegionalTerrainTransferResult>(std::move(error));
                }
                if (!std::isfinite(terrain.values()[index])) {
                    return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                        "r2d.terrain_transfer.source_value_nonfinite",
                        "contributing terrain elevation must be finite",
                        transfer_operation,
                        &mesh,
                        terrain_id,
                        cell_index,
                        contribution.raster_cell_index,
                        row,
                        column));
                }
                elevation += contribution.weight * terrain.values()[index];
                weight_sum += contribution.weight;
                overlap_area_sum += contribution.overlap_area_m2;
                ++diagnostics.contributor_lineage_counts[std::string{tsunami::geo::to_string(lineage)}];
            }
            const auto area_tolerance = stencil_policy.absolute_area_tolerance_m2 +
                stencil_policy.relative_area_tolerance * std::max(1.0, cell_measure);
            const auto weight_tolerance = 32.0 * std::numeric_limits<double>::epsilon() *
                std::max(1.0, static_cast<double>(range.count));
            if (!std::isfinite(elevation) || !std::isfinite(mapped_area) ||
                std::abs(mapped_area - cell_measure) > area_tolerance ||
                std::abs(overlap_area_sum - mapped_area) > area_tolerance ||
                std::abs(weight_sum - 1.0) > weight_tolerance) {
                return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                    "r2d.terrain_transfer.weight_invalid",
                    "cell transfer weights must sum to one and produce a finite elevation",
                    transfer_operation,
                    &mesh,
                    terrain_id,
                    cell_index));
            }
            values[cell_index] = elevation;
            diagnostics.minimum_bed_elevation_m = std::min(diagnostics.minimum_bed_elevation_m, elevation);
            diagnostics.maximum_bed_elevation_m = std::max(diagnostics.maximum_bed_elevation_m, elevation);
        }
        if (expected_range_begin != contributions.size()) {
            return tsunami::core::failure<RegionalTerrainTransferResult>(transfer_error(
                "r2d.terrain_transfer.weight_invalid",
                "stencil contains contributions outside its contiguous cell ranges",
                transfer_operation,
                &mesh,
                terrain_id));
        }
        auto bathymetry = make_regional_bathymetry(mesh, std::move(field_id), std::move(field_name), std::move(values));
        if (!bathymetry) {
            auto error = transfer_error(
                "r2d.terrain_transfer.bathymetry_creation_failed",
                "transferred values could not create RegionalBathymetry",
                transfer_operation,
                &mesh,
                terrain_id);
            error.with_cause_code(bathymetry.error().code());
            return tsunami::core::failure<RegionalTerrainTransferResult>(std::move(error));
        }
        return tsunami::core::success(RegionalTerrainTransferResult{
            std::move(bathymetry).value(), std::move(diagnostics)});
    }

} // namespace tsunami::r2d
