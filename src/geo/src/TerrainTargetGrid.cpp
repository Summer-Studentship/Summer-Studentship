#include <tsunami/geo/TerrainTargetGrid.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
            error.add_context("operation", "build_corridor_aligned_terrain_grid")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto add(Point2D left, Point2D right) noexcept -> Point2D
        {
            return Point2D{left.x + right.x, left.y + right.y};
        }

        [[nodiscard]] auto subtract(Point2D left, Point2D right) noexcept -> Point2D
        {
            return Point2D{left.x - right.x, left.y - right.y};
        }

        [[nodiscard]] auto scale(Point2D point, double factor) noexcept -> Point2D
        {
            return Point2D{point.x * factor, point.y * factor};
        }

        [[nodiscard]] auto dot(Point2D left, Point2D right) noexcept -> double
        {
            return (left.x * right.x) + (left.y * right.y);
        }

        [[nodiscard]] auto finite_extent(const BoundingBox2D &extent) noexcept -> bool
        {
            return finite(extent.minimum_x) && finite(extent.minimum_y) && finite(extent.maximum_x) &&
                finite(extent.maximum_y) && extent.minimum_x <= extent.maximum_x && extent.minimum_y <= extent.maximum_y;
        }

        [[nodiscard]] auto policy_valid(const TerrainTargetGridPolicy &policy) noexcept -> bool
        {
            return finite(policy.target_spacing_m) && policy.target_spacing_m > 0.0 &&
                finite(policy.active_coverage_threshold) && policy.active_coverage_threshold > 0.0 &&
                policy.active_coverage_threshold <= 1.0 &&
                finite(policy.maximum_upsampling_factor) && policy.maximum_upsampling_factor >= 1.0 &&
                policy.maximum_output_cells > 0U &&
                finite(policy.numerical_absolute_tolerance) && policy.numerical_absolute_tolerance > 0.0 &&
                finite(policy.numerical_relative_tolerance) && policy.numerical_relative_tolerance >= 0.0 &&
                !policy.policy_basis.empty();
        }

        [[nodiscard]] auto cell_product_ok(std::uint64_t width, std::uint64_t height, std::uint64_t limit) noexcept
            -> bool
        {
            return width != 0U && height != 0U &&
                width <= std::numeric_limits<std::uint64_t>::max() / height &&
                width * height <= limit;
        }

        [[nodiscard]] auto transform_corner(const RasterAffineTransform &transform, double column, double row) noexcept
            -> Point2D
        {
            return Point2D{
                transform.origin_x + (column * transform.pixel_width) + (row * transform.row_rotation),
                transform.origin_y + (column * transform.column_rotation) + (row * transform.pixel_height)};
        }

        [[nodiscard]] auto extent_from_transform(
            std::uint64_t width,
            std::uint64_t height,
            const RasterAffineTransform &transform) -> BoundingBox2D
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

        [[nodiscard]] auto local_point(Point2D global, Point2D epicentre, const CorridorLocalBasis &basis) noexcept
            -> Point2D
        {
            const auto relative = subtract(global, epicentre);
            return Point2D{dot(relative, basis.tangent), dot(relative, basis.left_normal)};
        }

        [[nodiscard]] auto polygon_area(const std::vector<Point2D> &ring) noexcept -> double
        {
            if (ring.size() < 3U) {
                return 0.0;
            }
            auto area = 0.0;
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const auto &a = ring[i];
                const auto &b = ring[(i + 1U) % ring.size()];
                area += (a.x * b.y) - (b.x * a.y);
            }
            return std::abs(area) * 0.5;
        }

        enum class ClipEdge
        {
            left,
            right,
            bottom,
            top
        };

        [[nodiscard]] auto inside(Point2D point, ClipEdge edge, double value, double tolerance) noexcept -> bool
        {
            switch (edge) {
            case ClipEdge::left:
                return point.x >= value - tolerance;
            case ClipEdge::right:
                return point.x <= value + tolerance;
            case ClipEdge::bottom:
                return point.y >= value - tolerance;
            case ClipEdge::top:
                return point.y <= value + tolerance;
            }
            return false;
        }

        [[nodiscard]] auto intersection(Point2D a, Point2D b, ClipEdge edge, double value) noexcept -> Point2D
        {
            const auto dx = b.x - a.x;
            const auto dy = b.y - a.y;
            const auto parameter = (edge == ClipEdge::left || edge == ClipEdge::right)
                ? ((std::abs(dx) > 0.0) ? (value - a.x) / dx : 0.0)
                : ((std::abs(dy) > 0.0) ? (value - a.y) / dy : 0.0);
            return Point2D{a.x + parameter * dx, a.y + parameter * dy};
        }

        [[nodiscard]] auto clip_edge(
            const std::vector<Point2D> &input,
            ClipEdge edge,
            double value,
            double tolerance) -> std::vector<Point2D>
        {
            auto output = std::vector<Point2D>{};
            if (input.empty()) {
                return output;
            }
            auto previous = input.back();
            auto previous_inside = inside(previous, edge, value, tolerance);
            for (const auto current : input) {
                const auto current_inside = inside(current, edge, value, tolerance);
                if (current_inside) {
                    if (!previous_inside) {
                        output.push_back(intersection(previous, current, edge, value));
                    }
                    output.push_back(current);
                } else if (previous_inside) {
                    output.push_back(intersection(previous, current, edge, value));
                }
                previous = current;
                previous_inside = current_inside;
            }
            return output;
        }

        [[nodiscard]] auto clipped_area(
            std::vector<Point2D> polygon,
            double left,
            double right,
            double bottom,
            double top,
            double tolerance) -> double
        {
            polygon = clip_edge(polygon, ClipEdge::left, left, tolerance);
            polygon = clip_edge(polygon, ClipEdge::right, right, tolerance);
            polygon = clip_edge(polygon, ClipEdge::bottom, bottom, tolerance);
            polygon = clip_edge(polygon, ClipEdge::top, top, tolerance);
            return polygon_area(polygon);
        }
    }

    TerrainTargetGrid::TerrainTargetGrid(
        std::uint64_t width,
        std::uint64_t height,
        double spacing_m,
        RasterAffineTransform transform,
        BoundingBox2D extent,
        ComputationalTargetReference target_reference,
        double xi_min_m,
        double xi_max_m,
        double eta_bottom_m,
        double eta_top_m,
        double longitudinal_padding_m,
        double transverse_padding_m)
        : width_{width},
          height_{height},
          spacing_m_{spacing_m},
          transform_{transform},
          extent_{extent},
          target_reference_{std::move(target_reference)},
          xi_min_m_{xi_min_m},
          xi_max_m_{xi_max_m},
          eta_bottom_m_{eta_bottom_m},
          eta_top_m_{eta_top_m},
          longitudinal_padding_m_{longitudinal_padding_m},
          transverse_padding_m_{transverse_padding_m}
    {
    }

    auto to_string(TerrainCorridorCellClass value) noexcept -> std::string_view
    {
        switch (value) {
        case TerrainCorridorCellClass::outside_corridor:
            return "outside_corridor";
        case TerrainCorridorCellClass::excluded_boundary_fraction:
            return "excluded_boundary_fraction";
        case TerrainCorridorCellClass::active:
            return "active";
        }
        return "outside_corridor";
    }

    auto terrain_grid_cell_centre(const TerrainTargetGrid &grid, std::uint64_t column, std::uint64_t row) -> Point2D
    {
        const auto x = static_cast<double>(column) + 0.5;
        const auto y = static_cast<double>(row) + 0.5;
        const auto &a = grid.transform();
        return Point2D{
            a.origin_x + x * a.pixel_width + y * a.row_rotation,
            a.origin_y + x * a.column_rotation + y * a.pixel_height};
    }

    auto build_corridor_aligned_terrain_grid(
        const ConstructedCorridor &corridor,
        const CorridorConstructionRecord &corridor_record,
        const TerrainTargetGridPolicy &policy) -> tsunami::core::Result<TerrainTargetGrid>
    {
        if (!policy_valid(policy)) {
            return tsunami::core::failure<TerrainTargetGrid>(terrain_error("geo.terrain.grid_policy_invalid", "terrain target-grid policy is invalid", "geo.terrain.grid.spacing_positive"));
        }
        if (corridor.polygon() != corridor_record.polygon || corridor.basis() != corridor_record.local_basis ||
            corridor.stations() != corridor_record.stations) {
            return tsunami::core::failure<TerrainTargetGrid>(terrain_error("geo.terrain.corridor_record_mismatch", "corridor geometry does not match the construction record", "geo.terrain.request.corridor_matches_case"));
        }
        if (corridor_record.target_reference.horizontal_unit != "m" ||
            (corridor_record.target_reference.storage_axes != ComputationalAxisConvention::east_north &&
             corridor_record.target_reference.storage_axes != ComputationalAxisConvention::east_north_up)) {
            return tsunami::core::failure<TerrainTargetGrid>(terrain_error("geo.terrain.corridor_reference_mismatch", "corridor target reference is not metric east-north storage", "geo.terrain.grid.corridor_aligned"));
        }
        const auto length_m = corridor.stations().inland_xi_m - corridor.stations().offshore_xi_m;
        const auto width_m = corridor.offshore_width_m();
        if (!finite(length_m) || !finite(width_m) || length_m <= 0.0 || width_m <= 0.0) {
            return tsunami::core::failure<TerrainTargetGrid>(terrain_error("geo.terrain.grid_affine_invalid", "corridor station or width values are invalid", "geo.terrain.grid.corridor_aligned"));
        }
        const auto width_cells = static_cast<std::uint64_t>(std::ceil(length_m / policy.target_spacing_m));
        const auto height_cells = static_cast<std::uint64_t>(std::ceil(width_m / policy.target_spacing_m));
        if (!cell_product_ok(width_cells, height_cells, policy.maximum_output_cells)) {
            return tsunami::core::failure<TerrainTargetGrid>(terrain_error("geo.terrain.grid_cell_limit_exceeded", "terrain target-grid cell count exceeds policy", "geo.terrain.grid.cell_limit"));
        }
        const auto longitudinal_padding = static_cast<double>(width_cells) * policy.target_spacing_m - length_m;
        const auto transverse_padding = static_cast<double>(height_cells) * policy.target_spacing_m - width_m;
        const auto xi0 = corridor.stations().offshore_xi_m - 0.5 * longitudinal_padding;
        const auto xi1 = corridor.stations().inland_xi_m + 0.5 * longitudinal_padding;
        const auto eta_top = 0.5 * width_m + 0.5 * transverse_padding;
        const auto eta_bottom = -0.5 * width_m - 0.5 * transverse_padding;
        const auto epicentre = Point2D{corridor_record.epicentre.coordinate.x, corridor_record.epicentre.coordinate.y};
        const auto &basis = corridor.basis();
        const auto origin = add(add(epicentre, scale(basis.tangent, xi0)), scale(basis.left_normal, eta_top));
        const auto transform = RasterAffineTransform{
            origin.x,
            policy.target_spacing_m * basis.tangent.x,
            -policy.target_spacing_m * basis.left_normal.x,
            origin.y,
            policy.target_spacing_m * basis.tangent.y,
            -policy.target_spacing_m * basis.left_normal.y};
        const auto extent = extent_from_transform(width_cells, height_cells, transform);
        if (!finite_extent(extent)) {
            return tsunami::core::failure<TerrainTargetGrid>(terrain_error("geo.terrain.grid_affine_invalid", "terrain target-grid extent is invalid", "geo.terrain.grid_affine_invalid"));
        }
        return tsunami::core::success(TerrainTargetGrid{
            width_cells,
            height_cells,
            policy.target_spacing_m,
            transform,
            extent,
            corridor_record.target_reference,
            xi0,
            xi1,
            eta_bottom,
            eta_top,
            longitudinal_padding,
            transverse_padding});
    }

    auto calculate_corridor_coverage(
        const ConstructedCorridor &corridor,
        const CorridorConstructionRecord &corridor_record,
        const TerrainTargetGrid &grid,
        const TerrainTargetGridPolicy &policy) -> tsunami::core::Result<TerrainCorridorCoverage>
    {
        if (corridor.polygon() != corridor_record.polygon || grid.target_reference() != corridor_record.target_reference) {
            return tsunami::core::failure<TerrainCorridorCoverage>(terrain_error("geo.terrain.corridor_record_mismatch", "coverage inputs do not share corridor provenance", "geo.terrain.coverage.range"));
        }
        auto local_ring = std::vector<Point2D>{};
        const auto epicentre = Point2D{corridor_record.epicentre.coordinate.x, corridor_record.epicentre.coordinate.y};
        for (const auto point : corridor.polygon().exterior_ring) {
            local_ring.push_back(local_point(point, epicentre, corridor.basis()));
        }
        if (!local_ring.empty()) {
            local_ring.pop_back();
        }
        auto coverage = TerrainCorridorCoverage{};
        coverage.grid = grid;
        coverage.fractions.reserve(static_cast<std::size_t>(grid.cell_count()));
        coverage.cell_classes.reserve(static_cast<std::size_t>(grid.cell_count()));
        const auto h = grid.spacing_m();
        const auto cell_area = h * h;
        for (std::uint64_t row = 0U; row < grid.height(); ++row) {
            const auto top = grid.eta_top_m() - static_cast<double>(row) * h;
            const auto bottom = top - h;
            for (std::uint64_t column = 0U; column < grid.width(); ++column) {
                const auto left = grid.xi_min_m() + static_cast<double>(column) * h;
                const auto right = left + h;
                auto fraction = clipped_area(local_ring, left, right, bottom, top, policy.numerical_absolute_tolerance) / cell_area;
                if (fraction < 0.0 && fraction >= -policy.numerical_absolute_tolerance) {
                    fraction = 0.0;
                }
                if (fraction > 1.0 && fraction <= 1.0 + policy.numerical_absolute_tolerance) {
                    fraction = 1.0;
                }
                if (fraction < -policy.numerical_absolute_tolerance || fraction > 1.0 + policy.numerical_absolute_tolerance || !finite(fraction)) {
                    return tsunami::core::failure<TerrainCorridorCoverage>(terrain_error("geo.terrain.coverage_fraction_invalid", "corridor coverage fraction is outside [0,1]", "geo.terrain.coverage.range").add_context("row", std::to_string(row)).add_context("column", std::to_string(column)));
                }
                const auto cell_class = fraction <= policy.numerical_absolute_tolerance
                    ? TerrainCorridorCellClass::outside_corridor
                    : ((fraction + policy.numerical_absolute_tolerance < policy.active_coverage_threshold)
                           ? TerrainCorridorCellClass::excluded_boundary_fraction
                           : TerrainCorridorCellClass::active);
                coverage.fractions.push_back(fraction);
                coverage.cell_classes.push_back(cell_class);
                if (cell_class == TerrainCorridorCellClass::active) {
                    ++coverage.active_cell_count;
                } else if (cell_class == TerrainCorridorCellClass::outside_corridor) {
                    ++coverage.outside_cell_count;
                } else {
                    ++coverage.excluded_boundary_cell_count;
                }
            }
        }
        return tsunami::core::success(std::move(coverage));
    }

} // namespace tsunami::geo
