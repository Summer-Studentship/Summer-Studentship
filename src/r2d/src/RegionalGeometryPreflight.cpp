#include <tsunami/r2d/RegionalGeometryPreflight.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <tsunami/core/Error.hpp>
#include <tsunami/geo/CorridorConstruction.hpp>

namespace tsunami::r2d
{
    namespace
    {
        constexpr auto operation_name = "validate_regional2d_geometry_preflight";
        constexpr auto rule_id = "SWE-GEO-CHK-WP1";
        constexpr auto required_physical_region = "region.domain";
        constexpr auto required_boundary_names = std::array{
            "boundary.offshore",
            "boundary.inland",
            "boundary.left_side",
            "boundary.right_side"};
        constexpr auto mesh_geometry_tolerance = 1.0e-12;

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto finite(tsunami::geo::Point2D point) noexcept -> bool
        {
            return finite(point.x) && finite(point.y);
        }

        [[nodiscard]] auto finite(const tsunami::geo::BoundingBox2D &box) noexcept -> bool
        {
            return finite(box.minimum_x) && finite(box.minimum_y) && finite(box.maximum_x) && finite(box.maximum_y) &&
                box.minimum_x <= box.maximum_x && box.minimum_y <= box.maximum_y;
        }

        [[nodiscard]] auto finite(const tsunami::geo::RasterAffineTransform &transform) noexcept -> bool
        {
            return finite(transform.origin_x) && finite(transform.pixel_width) && finite(transform.row_rotation) &&
                finite(transform.origin_y) && finite(transform.column_rotation) && finite(transform.pixel_height);
        }

        [[nodiscard]] auto point_from(tsunami::fvm::Point3 point) noexcept -> tsunami::geo::Point2D
        {
            return tsunami::geo::Point2D{point.x, point.y};
        }

        [[nodiscard]] auto near_equal(double left, double right, double tolerance) noexcept -> bool
        {
            if (!finite(left) || !finite(right)) {
                return false;
            }
            const auto scale = std::max({1.0, std::abs(left), std::abs(right)});
            return std::abs(left - right) <= tolerance * scale;
        }

        [[nodiscard]] auto near_equal(
            tsunami::fvm::Point3 left,
            tsunami::fvm::Point3 right,
            double tolerance) noexcept -> bool
        {
            return near_equal(left.x, right.x, tolerance) && near_equal(left.y, right.y, tolerance) &&
                near_equal(left.z, right.z, tolerance);
        }

        [[nodiscard]] auto near_equal(
            tsunami::fvm::Vector3 left,
            tsunami::fvm::Vector3 right,
            double tolerance) noexcept -> bool
        {
            return near_equal(left.x, right.x, tolerance) && near_equal(left.y, right.y, tolerance) &&
                near_equal(left.z, right.z, tolerance);
        }

        [[nodiscard]] auto subtract(tsunami::geo::Point2D left, tsunami::geo::Point2D right) noexcept
            -> tsunami::geo::Point2D
        {
            return tsunami::geo::Point2D{left.x - right.x, left.y - right.y};
        }

        [[nodiscard]] auto cross(tsunami::geo::Point2D left, tsunami::geo::Point2D right) noexcept -> double
        {
            return left.x * right.y - left.y * right.x;
        }

        [[nodiscard]] auto dot(tsunami::geo::Point2D left, tsunami::geo::Point2D right) noexcept -> double
        {
            return left.x * right.x + left.y * right.y;
        }

        [[nodiscard]] auto length(tsunami::geo::Point2D point) noexcept -> double
        {
            return std::hypot(point.x, point.y);
        }

        [[nodiscard]] auto signed_area(const tsunami::geo::Polygon2D &polygon) noexcept -> double
        {
            auto area = 0.0;
            const auto &ring = polygon.exterior_ring;
            for (std::size_t i = 0U; i + 1U < ring.size(); ++i) {
                area += ring[i].x * ring[i + 1U].y - ring[i + 1U].x * ring[i].y;
            }
            return 0.5 * area;
        }

        [[nodiscard]] auto segments_intersect(
            tsunami::geo::Point2D a,
            tsunami::geo::Point2D b,
            tsunami::geo::Point2D c,
            tsunami::geo::Point2D d,
            double tolerance) noexcept -> bool
        {
            const auto ab = subtract(b, a);
            const auto ac = subtract(c, a);
            const auto ad = subtract(d, a);
            const auto cd = subtract(d, c);
            const auto ca = subtract(a, c);
            const auto cb = subtract(b, c);
            const auto c1 = cross(ab, ac);
            const auto c2 = cross(ab, ad);
            const auto c3 = cross(cd, ca);
            const auto c4 = cross(cd, cb);
            if (((c1 > tolerance && c2 < -tolerance) || (c1 < -tolerance && c2 > tolerance)) &&
                ((c3 > tolerance && c4 < -tolerance) || (c3 < -tolerance && c4 > tolerance))) {
                return true;
            }
            const auto on_segment = [&](tsunami::geo::Point2D p, tsunami::geo::Point2D q, tsunami::geo::Point2D r) {
                return std::abs(cross(subtract(q, p), subtract(r, p))) <= tolerance &&
                    q.x >= std::min(p.x, r.x) - tolerance && q.x <= std::max(p.x, r.x) + tolerance &&
                    q.y >= std::min(p.y, r.y) - tolerance && q.y <= std::max(p.y, r.y) + tolerance;
            };
            return on_segment(a, c, b) || on_segment(a, d, b) || on_segment(c, a, d) || on_segment(c, b, d);
        }

        [[nodiscard]] auto contains_point(
            const tsunami::geo::Polygon2D &polygon,
            tsunami::geo::Point2D point,
            double tolerance) noexcept -> bool
        {
            const auto &ring = polygon.exterior_ring;
            if (ring.size() < 4U) {
                return false;
            }
            auto inside = false;
            for (std::size_t i = 0U, j = ring.size() - 2U; i + 1U < ring.size(); j = i++) {
                const auto a = ring[i];
                const auto b = ring[i + 1U];
                const auto edge = subtract(b, a);
                if (std::abs(cross(edge, subtract(point, a))) <= tolerance &&
                    point.x >= std::min(a.x, b.x) - tolerance && point.x <= std::max(a.x, b.x) + tolerance &&
                    point.y >= std::min(a.y, b.y) - tolerance && point.y <= std::max(a.y, b.y) + tolerance) {
                    return true;
                }
                const auto c = ring[j];
                const auto intersects = ((a.y > point.y) != (c.y > point.y)) &&
                    (point.x < (c.x - a.x) * (point.y - a.y) / (c.y - a.y) + a.x);
                if (intersects) {
                    inside = !inside;
                }
            }
            return inside;
        }

        [[nodiscard]] auto make_error(
            std::string code,
            std::string message,
            const RegionalGeometryPreflightRequest &request,
            std::optional<tsunami::fvm::VertexId> vertex_id = std::nullopt,
            std::optional<tsunami::fvm::CellId> cell_id = std::nullopt,
            std::optional<tsunami::fvm::FaceId> face_id = std::nullopt,
            std::optional<tsunami::fvm::BoundaryPatchId> patch_id = std::nullopt,
            std::string patch_name = {},
            std::string support_kind = {},
            std::optional<std::size_t> terrain_cell_index = std::nullopt,
            std::optional<tsunami::geo::Point2D> point = std::nullopt) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", operation_name)
                .add_context("rule_id", rule_id)
                .add_context("state_changed", "false");
            if (request.mesh != nullptr) {
                error.add_context("mesh_id", request.mesh->summary().id.value);
            }
            if (request.corridor_record != nullptr) {
                error.add_context("corridor_id", request.corridor_record->identity.corridor_id);
            }
            if (request.terrain_record != nullptr) {
                error.add_context("terrain_id", request.terrain_record->identity.terrain_id);
            }
            if (vertex_id) {
                error.add_context("vertex_id", std::to_string(vertex_id->value));
            }
            if (cell_id) {
                error.add_context("cell_id", std::to_string(cell_id->value));
            }
            if (face_id) {
                error.add_context("face_id", std::to_string(face_id->value));
            }
            if (patch_id) {
                error.add_context("patch_id", std::to_string(patch_id->value));
            }
            if (!patch_name.empty()) {
                error.add_context("patch_name", std::move(patch_name));
            }
            if (!support_kind.empty()) {
                error.add_context("support_kind", std::move(support_kind));
            }
            if (terrain_cell_index) {
                error.add_context("terrain_cell_index", std::to_string(*terrain_cell_index));
            }
            if (point) {
                error.add_context("x", std::to_string(point->x)).add_context("y", std::to_string(point->y));
            }
            return error;
        }

        auto add_forwarded_mesh_context(
            tsunami::core::Error &error,
            const tsunami::core::Error &cause) -> void
        {
            for (const auto &entry : cause.context()) {
                if (entry.key == "entity_type" || entry.key == "entity_id" || entry.key == "referenced_id") {
                    error.add_context(entry.key, entry.value);
                }
            }
        }

        [[nodiscard]] auto validate_polygon(
            const RegionalGeometryPreflightRequest &request,
            double tolerance) -> tsunami::core::Result<void>
        {
            const auto &polygon = request.corridor->polygon();
            const auto &record_polygon = request.corridor_record->polygon;
            if (polygon != record_polygon || request.corridor->extent() != request.corridor_record->extent ||
                request.corridor->basis() != request.corridor_record->local_basis ||
                request.corridor->stations() != request.corridor_record->stations) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.corridor_record_mismatch",
                    "corridor geometry does not match its accepted construction record",
                    request));
            }
            if (!polygon.interior_rings.empty() || polygon.exterior_ring.size() < 5U ||
                !(polygon.exterior_ring.front() == polygon.exterior_ring.back())) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.corridor_polygon_invalid",
                    "corridor polygon must be a closed exterior ring without interior rings",
                    request));
            }
            if (signed_area(polygon) <= tolerance) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.corridor_polygon_orientation_invalid",
                    "corridor polygon must use the accepted counter-clockwise orientation",
                    request));
            }
            const auto &ring = polygon.exterior_ring;
            for (std::size_t i = 0U; i + 1U < ring.size(); ++i) {
                if (!finite(ring[i]) || length(subtract(ring[i + 1U], ring[i])) <= tolerance) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.corridor_polygon_invalid",
                        "corridor polygon contains a nonfinite vertex or degenerate edge",
                        request));
                }
                for (std::size_t j = i + 1U; j + 1U < ring.size(); ++j) {
                    const auto adjacent = (j == i + 1U) || (i == 0U && j + 2U == ring.size());
                    if (!adjacent && segments_intersect(ring[i], ring[i + 1U], ring[j], ring[j + 1U], tolerance)) {
                        return tsunami::core::failure(make_error(
                            "r2d.preflight.corridor_polygon_self_intersection",
                            "corridor polygon has a non-adjacent edge intersection",
                            request));
                    }
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto validate_corridor_frame(
            const RegionalGeometryPreflightRequest &request) -> tsunami::core::Result<void>
        {
            const auto &basis = request.corridor->basis();
            const auto &policy = request.corridor_record->policy;
            const auto tangent_norm = length(basis.tangent);
            const auto normal_norm = length(basis.left_normal);
            const auto orthogonality = std::abs(dot(basis.tangent, basis.left_normal));
            const auto determinant = cross(basis.tangent, basis.left_normal);
            if (!finite(basis.tangent) || !finite(basis.left_normal) ||
                std::abs(tangent_norm - 1.0) > policy.basis_orthonormal_tolerance ||
                std::abs(normal_norm - 1.0) > policy.basis_orthonormal_tolerance ||
                orthogonality > policy.basis_orthonormal_tolerance ||
                std::abs(determinant - 1.0) > policy.basis_orthonormal_tolerance) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.corridor_basis_invalid",
                    "corridor local basis must be finite, orthonormal and right-handed",
                    request));
            }
            const auto &stations = request.corridor->stations();
            const auto tolerance = policy.geometry_absolute_tolerance_m;
            const auto zero_inland_extent = std::abs(request.corridor_record->inland_extent_m) <= tolerance;
            const auto target_before_inland = stations.target_xi_m < stations.inland_xi_m;
            const auto target_at_inland_for_zero_extent =
                zero_inland_extent && std::abs(stations.target_xi_m - stations.inland_xi_m) <= tolerance;
            if (!finite(stations.offshore_xi_m) || !finite(stations.epicentre_xi_m) ||
                !finite(stations.target_xi_m) || !finite(stations.inland_xi_m) ||
                !(stations.offshore_xi_m < stations.epicentre_xi_m) ||
                !(stations.epicentre_xi_m < stations.target_xi_m) ||
                !(target_before_inland || target_at_inland_for_zero_extent) ||
                !finite(request.corridor->offshore_width_m()) || request.corridor->offshore_width_m() <= 0.0 ||
                !finite(request.corridor->inland_width_m()) || request.corridor->inland_width_m() <= 0.0 ||
                !finite(request.corridor->total_length_m()) || request.corridor->total_length_m() <= 0.0) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.corridor_dimensions_invalid",
                    "corridor dimensions and station limits must be finite and ordered",
                    request));
            }
            const auto residual = tsunami::geo::circular_bearing_residual_degrees(
                request.corridor_record->configured_bearing_degrees,
                request.corridor_record->derived_bearing_degrees);
            if (!finite(residual) || residual > request.corridor_record->policy.bearing_tolerance_degrees) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.corridor_bearing_mismatch",
                    "configured and derived corridor bearings are incompatible",
                    request));
            }
            if (request.corridor_record->epicentre.transformation_identity.transformation_id.empty() ||
                request.corridor_record->target.transformation_identity.transformation_id.empty()) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.transformation_evidence_missing",
                    "corridor transformation evidence is incomplete",
                    request));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto validate_records(
            const RegionalGeometryPreflightRequest &request) -> tsunami::core::Result<void>
        {
            if (request.corridor == nullptr || request.corridor_record == nullptr ||
                request.terrain == nullptr || request.terrain_record == nullptr || request.mesh == nullptr) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.request_missing",
                    "regional geometry preflight request is missing required inputs",
                    request));
            }
            if (auto valid = tsunami::geo::validate_corridor_construction_record(*request.corridor_record); !valid) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.corridor_record_invalid",
                    "accepted corridor construction record is invalid",
                    request).with_cause_code(valid.error().code()));
            }
            if (auto valid = tsunami::geo::validate_terrain_conditioning_record(*request.terrain_record); !valid) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.terrain_record_invalid",
                    "accepted terrain conditioning record is invalid",
                    request).with_cause_code(valid.error().code()));
            }
            if (request.terrain_record->corridor_identity != request.corridor_record->identity ||
                request.terrain_record->identity.case_revision != request.corridor_record->identity.case_revision) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.corridor_identity_mismatch",
                    "terrain record does not identify the accepted corridor and case revision",
                    request));
            }
            if (request.terrain->grid() != request.terrain_record->grid ||
                request.terrain->grid().target_reference() != request.terrain_record->target_reference) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.terrain_record_mismatch",
                    "conditioned terrain raster does not match its accepted record",
                    request));
            }
            if (request.corridor_record->target_reference != request.terrain_record->target_reference ||
                request.corridor_record->target_reference != request.terrain->grid().target_reference()) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.crs_mismatch",
                    "corridor and terrain do not share the same accepted local computational target reference",
                    request));
            }
            if (request.terrain_record->bathymetry_transformation_identity.transformation_id.empty() ||
                request.terrain_record->topography_transformation_identity.transformation_id.empty()) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.transformation_evidence_missing",
                    "terrain transformation evidence is incomplete",
                    request));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto validate_terrain_arrays(
            const RegionalGeometryPreflightRequest &request) -> tsunami::core::Result<void>
        {
            const auto cells = static_cast<std::size_t>(request.terrain->grid().cell_count());
            if (request.terrain->values().size() != cells || request.terrain->valid_mask().size() != cells ||
                request.terrain->corridor_coverage_fraction().size() != cells ||
                request.terrain->cell_lineage().size() != cells ||
                !finite(request.terrain->grid().transform()) || !finite(request.terrain->grid().extent()) ||
                request.terrain->grid().width() == 0U || request.terrain->grid().height() == 0U ||
                request.terrain->grid().registration() != tsunami::geo::RasterCellRegistration::pixel_is_area) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.terrain_raster_invalid",
                    "conditioned terrain raster dimensions, affine transform and stored vectors must agree",
                    request));
            }
            for (std::size_t i = 0U; i < cells; ++i) {
                const auto coverage = request.terrain->corridor_coverage_fraction()[i];
                if (!finite(coverage) || coverage < 0.0 || coverage > 1.0) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.terrain_raster_invalid",
                        "conditioned terrain coverage fraction is outside the accepted range",
                        request,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        {},
                        {},
                        i));
                }
                if (request.terrain->valid_mask()[i] != 0U && !finite(request.terrain->values()[i])) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.terrain_nodata",
                        "conditioned terrain marks a nonfinite value as valid",
                        request,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        {},
                        {},
                        i));
                }
            }
            return tsunami::core::success();
        }

        struct TerrainCellLookup
        {
            std::size_t index{};
            std::uint64_t column{};
            std::uint64_t row{};
        };

        [[nodiscard]] auto terrain_cell_for(
            const tsunami::geo::TerrainTargetGrid &grid,
            tsunami::geo::Point2D point,
            double tolerance) -> std::optional<TerrainCellLookup>
        {
            const auto &a = grid.transform();
            const auto determinant = a.pixel_width * a.pixel_height - a.row_rotation * a.column_rotation;
            if (!finite(determinant) || std::abs(determinant) <= tolerance) {
                return std::nullopt;
            }
            const auto dx = point.x - a.origin_x;
            const auto dy = point.y - a.origin_y;
            auto column = (dx * a.pixel_height - a.row_rotation * dy) / determinant;
            auto row = (a.pixel_width * dy - dx * a.column_rotation) / determinant;
            const auto width = static_cast<double>(grid.width());
            const auto height = static_cast<double>(grid.height());
            if (column < 0.0 && column >= -tolerance) {
                column = 0.0;
            }
            if (row < 0.0 && row >= -tolerance) {
                row = 0.0;
            }
            if (column >= width && column <= width + tolerance) {
                column = std::nextafter(width, 0.0);
            }
            if (row >= height && row <= height + tolerance) {
                row = std::nextafter(height, 0.0);
            }
            if (column < 0.0 || row < 0.0 || column >= width || row >= height) {
                return std::nullopt;
            }
            const auto c = static_cast<std::uint64_t>(std::floor(column));
            const auto r = static_cast<std::uint64_t>(std::floor(row));
            return TerrainCellLookup{static_cast<std::size_t>(r * grid.width() + c), c, r};
        }

        [[nodiscard]] auto lineage_is_active(tsunami::geo::TerrainCellLineage lineage) noexcept -> bool
        {
            return lineage != tsunami::geo::TerrainCellLineage::outside_corridor &&
                lineage != tsunami::geo::TerrainCellLineage::excluded_boundary_fraction;
        }

        [[nodiscard]] auto validate_terrain_support_point(
            const RegionalGeometryPreflightRequest &request,
            tsunami::geo::Point2D point,
            std::string support_kind,
            std::optional<tsunami::fvm::VertexId> vertex_id,
            std::optional<tsunami::fvm::CellId> cell_id) -> tsunami::core::Result<void>
        {
            const auto tolerance = std::max(
                request.terrain_record->grid_policy.numerical_absolute_tolerance,
                request.corridor_record->policy.geometry_absolute_tolerance_m);
            const auto lookup = terrain_cell_for(request.terrain->grid(), point, tolerance);
            if (!lookup) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.terrain_support_missing",
                    "required Regional2D support location lies outside conditioned terrain support",
                    request,
                    vertex_id,
                    cell_id,
                    std::nullopt,
                    std::nullopt,
                    {},
                    std::move(support_kind),
                    std::nullopt,
                    point));
            }
            const auto index = lookup->index;
            if (request.terrain->corridor_coverage_fraction()[index] + tolerance <
                    request.terrain_record->grid_policy.active_coverage_threshold ||
                !lineage_is_active(request.terrain->cell_lineage()[index])) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.terrain_support_missing",
                    "required Regional2D support location maps to terrain outside accepted active coverage",
                    request,
                    vertex_id,
                    cell_id,
                    std::nullopt,
                    std::nullopt,
                    {},
                    std::move(support_kind),
                    index,
                    point));
            }
            if (request.terrain->valid_mask()[index] == 0U || !finite(request.terrain->values()[index])) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.terrain_nodata",
                    "required Regional2D support location resolves to unexplained nodata",
                    request,
                    vertex_id,
                    cell_id,
                    std::nullopt,
                    std::nullopt,
                    {},
                    std::move(support_kind),
                    index,
                    point));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto reconstruct_validated_mesh(
            const RegionalGeometryPreflightRequest &request)
            -> tsunami::core::Result<tsunami::fvm::FiniteVolumeMesh>
        {
            auto input = tsunami::fvm::MeshTopologyInput{};
            input.id = request.mesh->topology().id();
            input.spatial_dimension = request.mesh->topology().spatial_dimension();
            input.vertices = {
                request.mesh->topology().vertices().begin(),
                request.mesh->topology().vertices().end()};
            input.faces = {
                request.mesh->topology().faces().begin(),
                request.mesh->topology().faces().end()};
            input.cells = {
                request.mesh->topology().cells().begin(),
                request.mesh->topology().cells().end()};
            input.boundary_patches = {
                request.mesh->topology().boundary_patches().begin(),
                request.mesh->topology().boundary_patches().end()};

            auto reconstructed = tsunami::fvm::make_finite_volume_mesh(std::move(input));
            if (!reconstructed) {
                auto error = make_error(
                    "r2d.preflight.mesh_invalid",
                    "Regional2D preflight mesh failed authoritative finite-volume validation",
                    request)
                                 .with_cause_code(reconstructed.error().code());
                add_forwarded_mesh_context(error, reconstructed.error());
                return tsunami::core::failure<tsunami::fvm::FiniteVolumeMesh>(std::move(error));
            }
            return reconstructed;
        }

        [[nodiscard]] auto validate_geometry_consistency(
            const RegionalGeometryPreflightRequest &request,
            const tsunami::fvm::FiniteVolumeMesh &reconstructed) -> tsunami::core::Result<void>
        {
            const auto supplied_faces = request.mesh->geometry().faces();
            const auto reconstructed_faces = reconstructed.geometry().faces();
            if (supplied_faces.size() != reconstructed_faces.size()) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.mesh_geometry_mismatch",
                    "supplied mesh face-geometry count differs from reconstructed finite-volume geometry",
                    request));
            }
            for (std::size_t index = 0U; index < supplied_faces.size(); ++index) {
                const auto face_id = tsunami::fvm::FaceId{index};
                if (!near_equal(supplied_faces[index].centroid, reconstructed_faces[index].centroid, mesh_geometry_tolerance) ||
                    !near_equal(supplied_faces[index].area_vector, reconstructed_faces[index].area_vector, mesh_geometry_tolerance)) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.mesh_geometry_mismatch",
                        "supplied mesh face geometry differs from reconstructed finite-volume geometry",
                        request,
                        std::nullopt,
                        std::nullopt,
                        face_id));
                }
            }

            const auto supplied_cells = request.mesh->geometry().cells();
            const auto reconstructed_cells = reconstructed.geometry().cells();
            if (supplied_cells.size() != reconstructed_cells.size()) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.mesh_geometry_mismatch",
                    "supplied mesh cell-geometry count differs from reconstructed finite-volume geometry",
                    request));
            }
            for (std::size_t index = 0U; index < supplied_cells.size(); ++index) {
                const auto cell_id = tsunami::fvm::CellId{index};
                if (!near_equal(supplied_cells[index].centroid, reconstructed_cells[index].centroid, mesh_geometry_tolerance) ||
                    !near_equal(supplied_cells[index].measure, reconstructed_cells[index].measure, mesh_geometry_tolerance)) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.mesh_geometry_mismatch",
                        "supplied mesh cell geometry differs from reconstructed finite-volume geometry",
                        request,
                        std::nullopt,
                        cell_id));
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto validate_mesh(
            const RegionalGeometryPreflightRequest &request,
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            RegionalGeometryPreflightReport &report) -> tsunami::core::Result<void>
        {
            const auto summary = mesh.summary();
            if (summary.spatial_dimension != 2U || summary.cell_count == 0U) {
                return tsunami::core::failure(make_error(
                    "r2d.preflight.mesh_empty_or_invalid",
                    "Regional2D preflight requires a non-empty two-dimensional finite-volume mesh",
                    request));
            }
            report.mesh_id = summary.id.value;
            report.vertex_count = summary.vertex_count;
            report.cell_count = summary.cell_count;
            report.face_count = summary.face_count;

            auto first_vertex = true;
            for (const auto &vertex : mesh.topology().vertices()) {
                const auto point = point_from(vertex.position);
                if (!finite(point) || !contains_point(request.corridor->polygon(), point, request.corridor_record->policy.geometry_absolute_tolerance_m)) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.mesh_outside_corridor",
                        "mesh vertex lies outside the accepted corridor",
                        request,
                        vertex.id,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        {},
                        {},
                        std::nullopt,
                        point));
                }
                if (auto terrain = validate_terrain_support_point(request, point, "vertex", vertex.id, std::nullopt); !terrain) {
                    return terrain;
                }
                if (first_vertex) {
                    report.mesh_bounds = tsunami::geo::BoundingBox2D{point.x, point.y, point.x, point.y};
                    first_vertex = false;
                } else {
                    report.mesh_bounds.minimum_x = std::min(report.mesh_bounds.minimum_x, point.x);
                    report.mesh_bounds.minimum_y = std::min(report.mesh_bounds.minimum_y, point.y);
                    report.mesh_bounds.maximum_x = std::max(report.mesh_bounds.maximum_x, point.x);
                    report.mesh_bounds.maximum_y = std::max(report.mesh_bounds.maximum_y, point.y);
                }
            }

            report.minimum_cell_measure = std::numeric_limits<double>::infinity();
            for (const auto &cell : mesh.topology().cells()) {
                const auto &geometry = mesh.cell_geometry(cell.id);
                const auto point = point_from(geometry.centroid);
                if (!contains_point(request.corridor->polygon(), point, request.corridor_record->policy.geometry_absolute_tolerance_m)) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.mesh_outside_corridor",
                        "mesh cell centroid lies outside the accepted corridor",
                        request,
                        std::nullopt,
                        cell.id,
                        std::nullopt,
                        std::nullopt,
                        {},
                        {},
                        std::nullopt,
                        point));
                }
                if (auto terrain = validate_terrain_support_point(request, point, "cell_centroid", std::nullopt, cell.id); !terrain) {
                    return terrain;
                }
                report.minimum_cell_measure = std::min(report.minimum_cell_measure, geometry.measure);
            }

            report.minimum_face_length = std::numeric_limits<double>::infinity();
            for (const auto &face : mesh.topology().faces()) {
                const auto &geometry = mesh.face_geometry(face.id);
                const auto length_value = std::hypot(geometry.area_vector.x, geometry.area_vector.y);
                report.minimum_face_length = std::min(report.minimum_face_length, length_value);
                if (face.is_internal()) {
                    ++report.internal_face_count;
                    if (face.neighbour->value <= face.owner.value) {
                        return tsunami::core::failure(make_error(
                            "r2d.preflight.internal_owner_not_canonical",
                            "internal face owner must be the lower canonical CellId",
                            request,
                            std::nullopt,
                            std::nullopt,
                            face.id));
                    }
                } else {
                    ++report.boundary_face_count;
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto validate_patches(
            const RegionalGeometryPreflightRequest &request,
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            RegionalGeometryPreflightReport &report) -> tsunami::core::Result<void>
        {
            auto seen = std::set<std::string>{};
            for (const auto &patch : mesh.topology().boundary_patches()) {
                if (!seen.insert(patch.name).second) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.patch_duplicate",
                        "Regional2D boundary patch names must be unique",
                        request,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        patch.id,
                        patch.name));
                }
                const auto required = std::ranges::find(required_boundary_names, patch.name) != required_boundary_names.end();
                if (!required) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.patch_unsupported",
                        "unsupported Regional2D boundary patch is not accepted",
                        request,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        patch.id,
                        patch.name));
                }
                if (patch.faces.empty()) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.patch_empty",
                        "required Regional2D boundary patch must be non-empty",
                        request,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        patch.id,
                        patch.name));
                }
                report.patches.push_back(RegionalGeometryPreflightPatchReport{patch.name, patch.faces.size()});
            }
            for (const auto &required : required_boundary_names) {
                if (!seen.contains(required)) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.patch_missing",
                        "required Regional2D boundary patch is missing",
                        request,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        required));
                }
            }
            if (!request.import_physical_groups.physical_name_tags.empty()) {
                if (!request.import_physical_groups.physical_name_tags.contains(required_physical_region)) {
                    return tsunami::core::failure(make_error(
                        "r2d.preflight.import_physical_group_missing",
                        "import metadata must preserve the Regional2D domain physical group",
                        request,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        required_physical_region));
                }
                for (const auto &required : required_boundary_names) {
                    if (!request.import_physical_groups.physical_name_tags.contains(required)) {
                        return tsunami::core::failure(make_error(
                            "r2d.preflight.import_physical_group_missing",
                            "import metadata must preserve the Regional2D boundary physical groups",
                            request,
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            std::nullopt,
                            required));
                    }
                }
            }
            return tsunami::core::success();
        }
    } // namespace

    auto validate_regional2d_geometry_preflight(
        const RegionalGeometryPreflightRequest &request)
        -> tsunami::core::Result<RegionalGeometryPreflightReport>
    {
        if (auto valid = validate_records(request); !valid) {
            return tsunami::core::failure<RegionalGeometryPreflightReport>(valid.error());
        }
        const auto tolerance = request.corridor_record->policy.geometry_absolute_tolerance_m;
        if (auto valid = validate_polygon(request, tolerance); !valid) {
            return tsunami::core::failure<RegionalGeometryPreflightReport>(valid.error());
        }
        if (auto valid = validate_corridor_frame(request); !valid) {
            return tsunami::core::failure<RegionalGeometryPreflightReport>(valid.error());
        }
        if (auto valid = validate_terrain_arrays(request); !valid) {
            return tsunami::core::failure<RegionalGeometryPreflightReport>(valid.error());
        }

        auto report = RegionalGeometryPreflightReport{};
        report.validation_status = "accepted";
        report.corridor_id = request.corridor_record->identity.corridor_id;
        report.terrain_id = request.terrain_record->identity.terrain_id;
        report.terrain_support_bounds = request.terrain->grid().extent();

        auto reconstructed = reconstruct_validated_mesh(request);
        if (!reconstructed) {
            return tsunami::core::failure<RegionalGeometryPreflightReport>(reconstructed.error());
        }
        if (auto valid = validate_geometry_consistency(request, reconstructed.value()); !valid) {
            return tsunami::core::failure<RegionalGeometryPreflightReport>(valid.error());
        }
        if (auto valid = validate_mesh(request, reconstructed.value(), report); !valid) {
            return tsunami::core::failure<RegionalGeometryPreflightReport>(valid.error());
        }
        if (auto valid = validate_patches(request, reconstructed.value(), report); !valid) {
            return tsunami::core::failure<RegionalGeometryPreflightReport>(valid.error());
        }
        return tsunami::core::success(std::move(report));
    }

} // namespace tsunami::r2d
