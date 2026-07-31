#include <tsunami/geo/CorridorConstruction.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <regex>
#include <string>
#include <utility>

namespace tsunami::geo
{
    namespace
    {
        constexpr auto pi = 3.141592653589793238462643383279502884;

        [[nodiscard]] auto corridor_error(std::string code, std::string message, std::string rule_id)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "construct_corridor")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto text_present(const std::string &text) -> bool
        {
            return !text.empty() && text.find('\0') == std::string::npos;
        }

        [[nodiscard]] auto logical_id_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^[a-z0-9]+(?:[._-][a-z0-9]+)*$"};
            return !text.empty() && text.size() <= 128U && std::regex_match(text, pattern);
        }

        [[nodiscard]] auto timestamp_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z$"};
            return std::regex_match(text, pattern);
        }

        [[nodiscard]] auto uri_valid(const std::string &text) -> bool
        {
            static const auto pattern = std::regex{"^https?://[^/@\\s]+(?:/[^\\s@]*)?$"};
            return std::regex_match(text, pattern);
        }

        [[nodiscard]] auto finite(double value) noexcept -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto finite(Point2D point) noexcept -> bool
        {
            return finite(point.x) && finite(point.y);
        }

        [[nodiscard]] auto finite(Coordinate3D point) noexcept -> bool
        {
            return finite(point.x) && finite(point.y) && finite(point.z);
        }

        [[nodiscard]] auto length(Point2D point) noexcept -> double
        {
            return std::hypot(point.x, point.y);
        }

        [[nodiscard]] auto subtract(Point2D left, Point2D right) noexcept -> Point2D
        {
            return Point2D{left.x - right.x, left.y - right.y};
        }

        [[nodiscard]] auto add(Point2D left, Point2D right) noexcept -> Point2D
        {
            return Point2D{left.x + right.x, left.y + right.y};
        }

        [[nodiscard]] auto scale(Point2D point, double factor) noexcept -> Point2D
        {
            return Point2D{point.x * factor, point.y * factor};
        }

        [[nodiscard]] auto dot(Point2D left, Point2D right) noexcept -> double
        {
            return (left.x * right.x) + (left.y * right.y);
        }

        [[nodiscard]] auto cross(Point2D left, Point2D right) noexcept -> double
        {
            return (left.x * right.y) - (left.y * right.x);
        }

        [[nodiscard]] auto near_equal(double left, double right, double absolute, double relative) noexcept -> bool
        {
            const auto scale_value = std::max({1.0, std::abs(left), std::abs(right)});
            return std::abs(left - right) <= absolute + (relative * scale_value);
        }

        [[nodiscard]] auto points_near(Point2D left, Point2D right, double tolerance) noexcept -> bool
        {
            return length(subtract(left, right)) <= tolerance;
        }

        [[nodiscard]] auto positive_policy(const CorridorConstructionPolicy &policy) noexcept -> bool
        {
            return finite(policy.minimum_reference_separation_m) && policy.minimum_reference_separation_m > 0.0 &&
                finite(policy.origin_tolerance_m) && policy.origin_tolerance_m >= 0.0 &&
                finite(policy.bearing_tolerance_degrees) && policy.bearing_tolerance_degrees >= 0.0 &&
                finite(policy.basis_orthonormal_tolerance) && policy.basis_orthonormal_tolerance > 0.0 &&
                finite(policy.geometry_absolute_tolerance_m) && policy.geometry_absolute_tolerance_m > 0.0 &&
                finite(policy.geometry_relative_tolerance) && policy.geometry_relative_tolerance >= 0.0 &&
                text_present(policy.tolerance_basis);
        }

        [[nodiscard]] auto evidence_from_request(
            const CorridorReferencePointRequest &request,
            const tsunami::data::CaseConfiguration &configuration,
            const tsunami::data::DatasetManifest &manifest,
            CorridorReferencePointRole expected_role) -> tsunami::core::Result<CorridorReferencePointEvidence>
        {
            if (request.role != expected_role) {
                return tsunami::core::failure<CorridorReferencePointEvidence>(corridor_error("geo.corridor.request_invalid", "reference point role is not in the requested position", "geo.corridor.request.references_present").add_context("point_role", std::string{to_string(request.role)}));
            }
            if (request.point_set == nullptr) {
                return tsunami::core::failure<CorridorReferencePointEvidence>(corridor_error("geo.corridor.point_set_missing", "corridor reference point set is missing", "geo.corridor.request.references_present").add_context("point_role", std::string{to_string(request.role)}));
            }
            if (request.transformation_record == nullptr) {
                return tsunami::core::failure<CorridorReferencePointEvidence>(corridor_error("geo.corridor.transformation_record_missing", "corridor transformation record is missing", "geo.corridor.request.references_present").add_context("point_role", std::string{to_string(request.role)}));
            }
            if (request.coordinate_index >= request.point_set->coordinates().size()) {
                return tsunami::core::failure<CorridorReferencePointEvidence>(corridor_error("geo.corridor.coordinate_index_invalid", "corridor reference coordinate index is out of range", "geo.corridor.reference.coordinate_exists").add_context("point_role", std::string{to_string(request.role)}).add_context("coordinate_index", std::to_string(request.coordinate_index)));
            }
            const auto coordinate = request.point_set->coordinates()[request.coordinate_index];
            if (!finite(coordinate)) {
                return tsunami::core::failure<CorridorReferencePointEvidence>(corridor_error("geo.corridor.coordinate_nonfinite", "corridor reference coordinate is nonfinite", "geo.corridor.reference.coordinate_exists").add_context("point_role", std::string{to_string(request.role)}));
            }
            const auto &record = *request.transformation_record;
            if (request.point_set->target_reference() != record.target ||
                request.point_set->source_reference() != record.source_horizontal) {
                return tsunami::core::failure<CorridorReferencePointEvidence>(corridor_error("geo.corridor.reference_mismatch", "transformed point set and transformation record references differ", "geo.corridor.reference.target_matches").add_context("point_role", std::string{to_string(request.role)}).add_context("transformation_id", record.identity.transformation_id));
            }
            if (record.identity.case_revision.case_id != configuration.identity().case_id ||
                record.identity.case_revision.revision != configuration.identity().revision ||
                record.identity.case_revision != manifest.identity().case_revision ||
                record.identity.manifest_id != manifest.identity().manifest_id ||
                record.identity.manifest_revision != manifest.identity().manifest_revision) {
                return tsunami::core::failure<CorridorReferencePointEvidence>(corridor_error("geo.corridor.provenance_invalid", "transformation provenance does not match case and manifest revisions", "geo.corridor.reference.provenance_complete").add_context("point_role", std::string{to_string(request.role)}).add_context("transformation_id", record.identity.transformation_id));
            }
            if (!logical_id_valid(request.point_id) || !text_present(request.definition) ||
                (request.source_feature_id && !logical_id_valid(*request.source_feature_id)) ||
                !text_present(request.source_document_title) || !uri_valid(request.source_document_uri) ||
                !timestamp_valid(request.accessed_at_utc)) {
                return tsunami::core::failure<CorridorReferencePointEvidence>(corridor_error("geo.corridor.provenance_invalid", "reference point provenance is incomplete", "geo.corridor.reference.provenance_complete").add_context("point_role", std::string{to_string(request.role)}).add_context("point_id", request.point_id));
            }
            return tsunami::core::success(CorridorReferencePointEvidence{
                request.role,
                request.point_id,
                request.definition,
                coordinate,
                request.coordinate_index,
                request.source_feature_id,
                record.identity,
                request.point_set->source_reference(),
                request.point_set->target_reference(),
                request.source_document_title,
                request.source_document_uri,
                request.accessed_at_utc});
        }

        [[nodiscard]] auto signed_area(const Polygon2D &polygon) noexcept -> double
        {
            auto area = 0.0;
            const auto &ring = polygon.exterior_ring;
            for (std::size_t i = 0; i + 1U < ring.size(); ++i) {
                area += (ring[i].x * ring[i + 1U].y) - (ring[i + 1U].x * ring[i].y);
            }
            return 0.5 * area;
        }

        [[nodiscard]] auto perimeter(const Polygon2D &polygon) noexcept -> double
        {
            auto value = 0.0;
            const auto &ring = polygon.exterior_ring;
            for (std::size_t i = 0; i + 1U < ring.size(); ++i) {
                value += length(subtract(ring[i + 1U], ring[i]));
            }
            return value;
        }

        [[nodiscard]] auto extent_for(const Polygon2D &polygon) -> BoundingBox2D
        {
            const auto &ring = polygon.exterior_ring;
            auto box = BoundingBox2D{ring.front().x, ring.front().y, ring.front().x, ring.front().y};
            for (std::size_t i = 1U; i + 1U < ring.size(); ++i) {
                box.minimum_x = std::min(box.minimum_x, ring[i].x);
                box.minimum_y = std::min(box.minimum_y, ring[i].y);
                box.maximum_x = std::max(box.maximum_x, ring[i].x);
                box.maximum_y = std::max(box.maximum_y, ring[i].y);
            }
            return box;
        }

        [[nodiscard]] auto point_at(Point2D epicentre, const CorridorLocalBasis &basis, double xi, double eta) noexcept -> Point2D
        {
            return add(add(epicentre, scale(basis.tangent, xi)), scale(basis.left_normal, eta));
        }

        auto remove_consecutive_duplicates(std::vector<Point2D> &ring, double tolerance) -> void
        {
            auto cleaned = std::vector<Point2D>{};
            for (const auto point : ring) {
                if (cleaned.empty() || !points_near(cleaned.back(), point, tolerance)) {
                    cleaned.push_back(point);
                }
            }
            ring = std::move(cleaned);
        }

        [[nodiscard]] auto segments_intersect(
            Point2D a,
            Point2D b,
            Point2D c,
            Point2D d,
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
            const auto on_segment = [&](Point2D p, Point2D q, Point2D r) {
                return std::abs(cross(subtract(q, p), subtract(r, p))) <= tolerance &&
                    q.x >= std::min(p.x, r.x) - tolerance && q.x <= std::max(p.x, r.x) + tolerance &&
                    q.y >= std::min(p.y, r.y) - tolerance && q.y <= std::max(p.y, r.y) + tolerance;
            };
            return on_segment(a, c, b) || on_segment(a, d, b) || on_segment(c, a, d) || on_segment(c, b, d);
        }

        [[nodiscard]] auto validate_polygon(
            const Polygon2D &polygon,
            const CorridorLocalBasis &basis,
            double offshore_width,
            double inland_width,
            double analytic_area,
            double analytic_perimeter,
            const CorridorConstructionPolicy &policy) -> tsunami::core::Result<CorridorConstructionDiagnostics>
        {
            if (polygon.interior_rings.size() != 0U || polygon.exterior_ring.size() < 5U) {
                return tsunami::core::failure<CorridorConstructionDiagnostics>(corridor_error("geo.corridor.polygon_invalid", "corridor polygon has an invalid ring shape", "geo.corridor.polygon.closed"));
            }
            const auto &ring = polygon.exterior_ring;
            if (!points_near(ring.front(), ring.back(), policy.geometry_absolute_tolerance_m)) {
                return tsunami::core::failure<CorridorConstructionDiagnostics>(corridor_error("geo.corridor.polygon_not_closed", "corridor polygon is not closed", "geo.corridor.polygon.closed"));
            }
            for (std::size_t i = 0; i + 1U < ring.size(); ++i) {
                if (!finite(ring[i]) || length(subtract(ring[i + 1U], ring[i])) <= policy.geometry_absolute_tolerance_m) {
                    return tsunami::core::failure<CorridorConstructionDiagnostics>(corridor_error("geo.corridor.polygon_zero_edge", "corridor polygon has a zero-length edge", "geo.corridor.polygon.edges_positive").add_context("edge_index", std::to_string(i)));
                }
            }
            for (std::size_t i = 0; i + 1U < ring.size(); ++i) {
                for (std::size_t j = i + 1U; j + 1U < ring.size(); ++j) {
                    const auto adjacent = (j == i + 1U) || (i == 0U && j + 2U == ring.size());
                    if (!adjacent && segments_intersect(ring[i], ring[i + 1U], ring[j], ring[j + 1U], policy.geometry_absolute_tolerance_m)) {
                        return tsunami::core::failure<CorridorConstructionDiagnostics>(corridor_error("geo.corridor.polygon_self_intersection", "corridor polygon has a nonadjacent edge intersection", "geo.corridor.polygon.simple").add_context("edge_index", std::to_string(i)).add_context("actual", std::to_string(j)));
                    }
                }
            }
            const auto area = signed_area(polygon);
            if (area <= 0.0) {
                return tsunami::core::failure<CorridorConstructionDiagnostics>(corridor_error("geo.corridor.polygon_clockwise", "corridor polygon is not counter-clockwise", "geo.corridor.polygon.counter_clockwise"));
            }
            const auto perim = perimeter(polygon);
            if (!near_equal(area, analytic_area, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance)) {
                return tsunami::core::failure<CorridorConstructionDiagnostics>(corridor_error("geo.corridor.area_mismatch", "corridor polygon area does not match the analytic formula", "geo.corridor.polygon.area_matches").add_context("expected", std::to_string(analytic_area)).add_context("actual", std::to_string(area)));
            }
            if (!near_equal(perim, analytic_perimeter, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance)) {
                return tsunami::core::failure<CorridorConstructionDiagnostics>(corridor_error("geo.corridor.perimeter_mismatch", "corridor polygon perimeter does not match the analytic formula", "geo.corridor.polygon.perimeter_matches").add_context("expected", std::to_string(analytic_perimeter)).add_context("actual", std::to_string(perim)));
            }
            const auto offshore_cap = subtract(ring[1U], ring[0U]);
            auto maximum_xi = dot(ring.front(), basis.tangent);
            for (std::size_t i = 1U; i + 1U < ring.size(); ++i) {
                maximum_xi = std::max(maximum_xi, dot(ring[i], basis.tangent));
            }
            auto inland_cap_points = std::vector<Point2D>{};
            for (std::size_t i = 0U; i + 1U < ring.size(); ++i) {
                if (std::abs(dot(ring[i], basis.tangent) - maximum_xi) <= policy.geometry_absolute_tolerance_m) {
                    inland_cap_points.push_back(ring[i]);
                }
            }
            if (inland_cap_points.size() != 2U) {
                return tsunami::core::failure<CorridorConstructionDiagnostics>(corridor_error("geo.corridor.flat_end_invalid", "corridor inland cap does not have two terminal vertices", "geo.corridor.polygon.ends_flat"));
            }
            auto inland_left = inland_cap_points[0U];
            auto inland_right = inland_cap_points[1U];
            if (dot(inland_left, basis.left_normal) < dot(inland_right, basis.left_normal)) {
                std::swap(inland_left, inland_right);
            }
            const auto inland_cap = subtract(inland_left, inland_right);
            if (!near_equal(dot(offshore_cap, basis.tangent), 0.0, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance) ||
                !near_equal(dot(inland_cap, basis.tangent), 0.0, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance) ||
                !near_equal(length(offshore_cap), offshore_width, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance) ||
                !near_equal(length(inland_cap), inland_width, policy.geometry_absolute_tolerance_m, policy.geometry_relative_tolerance)) {
                return tsunami::core::failure<CorridorConstructionDiagnostics>(corridor_error("geo.corridor.flat_end_invalid", "corridor polygon end caps are not flat or width-consistent", "geo.corridor.polygon.ends_flat"));
            }
            auto diagnostics = CorridorConstructionDiagnostics{};
            diagnostics.analytic_area_m2 = analytic_area;
            diagnostics.polygon_area_m2 = area;
            diagnostics.area_residual_m2 = std::abs(area - analytic_area);
            diagnostics.analytic_perimeter_m = analytic_perimeter;
            diagnostics.polygon_perimeter_m = perim;
            diagnostics.perimeter_residual_m = std::abs(perim - analytic_perimeter);
            return tsunami::core::success(diagnostics);
        }
    }

    auto make_corridor_local_basis(Point2D epicentre, Point2D target, const CorridorConstructionPolicy &policy)
        -> tsunami::core::Result<CorridorLocalBasis>
    {
        const auto delta = subtract(target, epicentre);
        const auto distance = length(delta);
        if (!finite(distance) || distance < policy.minimum_reference_separation_m) {
            return tsunami::core::failure<CorridorLocalBasis>(corridor_error("geo.corridor.reference_points_coincident", "corridor reference points are coincident or too close", "geo.corridor.reference.points_distinct"));
        }
        const auto tangent = Point2D{delta.x / distance, delta.y / distance};
        const auto normal = Point2D{-tangent.y, tangent.x};
        const auto bearing = std::fmod((std::atan2(tangent.x, tangent.y) * 180.0 / pi) + 360.0, 360.0);
        const auto basis = CorridorLocalBasis{tangent, normal, distance, bearing};
        const auto tangent_residual = std::abs(length(basis.tangent) - 1.0);
        const auto normal_residual = std::abs(length(basis.left_normal) - 1.0);
        const auto orthogonality = std::abs(dot(basis.tangent, basis.left_normal));
        const auto determinant = std::abs(cross(basis.tangent, basis.left_normal) - 1.0);
        if (tangent_residual > policy.basis_orthonormal_tolerance ||
            normal_residual > policy.basis_orthonormal_tolerance ||
            orthogonality > policy.basis_orthonormal_tolerance ||
            determinant > policy.basis_orthonormal_tolerance) {
            return tsunami::core::failure<CorridorLocalBasis>(corridor_error("geo.corridor.basis_invalid", "corridor local basis is not orthonormal and right-handed", "geo.corridor.basis.right_handed"));
        }
        return tsunami::core::success(basis);
    }

    auto to_corridor_local_coordinates(Point2D global, Point2D epicentre, const CorridorLocalBasis &basis) -> Point2D
    {
        const auto relative = subtract(global, epicentre);
        return Point2D{dot(relative, basis.tangent), dot(relative, basis.left_normal)};
    }

    auto from_corridor_local_coordinates(Point2D local, Point2D epicentre, const CorridorLocalBasis &basis) -> Point2D
    {
        return point_at(epicentre, basis, local.x, local.y);
    }

    auto circular_bearing_residual_degrees(double configured, double derived) noexcept -> double
    {
        const auto delta = std::abs(std::fmod(configured - derived, 360.0));
        return std::min(delta, 360.0 - delta);
    }

    auto construct_corridor(const CorridorConstructionRequest &request)
        -> tsunami::core::Result<CorridorConstructionResult>
    {
        if (request.configuration == nullptr || request.manifest == nullptr || !positive_policy(request.policy)) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.request_invalid", "corridor construction request is incomplete", "geo.corridor.request.references_present"));
        }
        const auto &configuration = *request.configuration;
        const auto &manifest = *request.manifest;
        if (configuration.identity().case_id != manifest.identity().case_revision.case_id ||
            configuration.identity().revision != manifest.identity().case_revision.revision) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.case_manifest_mismatch", "case configuration and dataset manifest identities differ", "geo.corridor.request.identities_match").add_context("case_id", configuration.identity().case_id.str()).add_context("manifest_id", manifest.identity().manifest_id));
        }
        if (request.identity.case_revision.case_id != configuration.identity().case_id ||
            request.identity.case_revision.revision != configuration.identity().revision) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.case_revision_mismatch", "corridor identity case revision differs from configuration", "geo.corridor.request.identities_match"));
        }
        const auto &corridor = configuration.regional_2d().corridor;
        if (!logical_id_valid(request.identity.corridor_id) || request.identity.corridor_revision == 0U ||
            !logical_id_valid(request.identity.output_dataset_id) || !logical_id_valid(request.identity.output_process_id) ||
            !timestamp_valid(request.identity.executed_at_utc)) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.request_invalid", "corridor construction identity is invalid", "geo.corridor.request.identities_match"));
        }
        if (request.identity.trajectory_id != corridor.trajectory_id) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.trajectory_mismatch", "corridor identity trajectory does not match case configuration", "geo.corridor.request.trajectory_matches").add_context("expected", corridor.trajectory_id).add_context("actual", request.identity.trajectory_id));
        }
        auto epicentre_evidence = evidence_from_request(request.epicentre, configuration, manifest, CorridorReferencePointRole::epicentre);
        if (!epicentre_evidence) {
            return tsunami::core::failure<CorridorConstructionResult>(epicentre_evidence.error());
        }
        auto target_evidence = evidence_from_request(request.target, configuration, manifest, CorridorReferencePointRole::target);
        if (!target_evidence) {
            return tsunami::core::failure<CorridorConstructionResult>(target_evidence.error());
        }
        const auto &epicentre = epicentre_evidence.value();
        const auto &target = target_evidence.value();
        if (epicentre.point_id == target.point_id) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.reference_points_coincident", "epicentre and target point identifiers must be distinct", "geo.corridor.reference.points_distinct"));
        }
        if (epicentre.target_reference != target.target_reference ||
            epicentre.target_reference.horizontal_unit != "m" ||
            (epicentre.target_reference.storage_axes != ComputationalAxisConvention::east_north &&
             epicentre.target_reference.storage_axes != ComputationalAxisConvention::east_north_up)) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.reference_mismatch", "corridor reference points must share a metric target reference", "geo.corridor.reference.target_matches"));
        }
        if (request.identity.output_dataset_id == epicentre.transformation_identity.source_dataset_id ||
            request.identity.output_dataset_id == target.transformation_identity.source_dataset_id ||
            request.identity.output_dataset_id == epicentre.transformation_identity.output_dataset_id ||
            request.identity.output_dataset_id == target.transformation_identity.output_dataset_id) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.request_invalid", "corridor output dataset must be distinct from source and transformed point datasets", "geo.corridor.request.identities_match"));
        }
        if (!finite(corridor.width_m) || corridor.width_m <= 0.0 ||
            !finite(corridor.offshore_extent_m) || corridor.offshore_extent_m <= 0.0 ||
            !finite(corridor.inland_extent_m) || corridor.inland_extent_m < 0.0) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.extent_invalid", "corridor width and extents must be finite and valid", "geo.corridor.width.offshore_positive"));
        }
        auto inland_width = corridor.width_m;
        auto narrowing_rule = std::string{"constant_width"};
        if (corridor.narrowing.enabled) {
            if (!corridor.narrowing.inland_width_m || !finite(*corridor.narrowing.inland_width_m) ||
                *corridor.narrowing.inland_width_m <= 0.0 || *corridor.narrowing.inland_width_m >= corridor.width_m) {
                return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.narrowing_invalid", "enabled narrowing requires a smaller positive inland width", "geo.corridor.narrowing.rule_supported"));
            }
            inland_width = *corridor.narrowing.inland_width_m;
            narrowing_rule = "epicentre_to_target_linear_taper";
        } else if (corridor.narrowing.inland_width_m) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.narrowing_invalid", "disabled narrowing must not define an inland width", "geo.corridor.width.inland_consistent"));
        }
        auto basis = make_corridor_local_basis(
            Point2D{epicentre.coordinate.x, epicentre.coordinate.y},
            Point2D{target.coordinate.x, target.coordinate.y},
            request.policy);
        if (!basis) {
            return tsunami::core::failure<CorridorConstructionResult>(basis.error());
        }
        const auto epicentre_xy = Point2D{epicentre.coordinate.x, epicentre.coordinate.y};
        const auto configured_origin = Point2D{corridor.origin.x, corridor.origin.y};
        const auto origin_residual = length(subtract(epicentre_xy, configured_origin));
        if (origin_residual > request.policy.origin_tolerance_m) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.origin_mismatch", "configured corridor origin does not match transformed epicentre", "geo.corridor.origin.case_matches_epicentre").add_context("origin_residual_m", std::to_string(origin_residual)));
        }
        const auto bearing_residual = circular_bearing_residual_degrees(corridor.bearing_degrees_clockwise_from_north, basis.value().derived_bearing_degrees_clockwise_from_north);
        if (bearing_residual > request.policy.bearing_tolerance_degrees) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.bearing_mismatch", "configured corridor bearing does not match transformed evidence", "geo.corridor.bearing.case_matches_evidence").add_context("bearing_residual_degrees", std::to_string(bearing_residual)));
        }
        const auto total_length = corridor.offshore_extent_m + basis.value().epicentre_target_distance_m + corridor.inland_extent_m;
        if (!finite(corridor.sponge.offshore_width_m) || corridor.sponge.offshore_width_m < 0.0 ||
            !finite(corridor.sponge.side_width_m) || corridor.sponge.side_width_m < 0.0 ||
            corridor.sponge.offshore_width_m >= total_length ||
            (2.0 * corridor.sponge.side_width_m) >= inland_width) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.sponge_invalid", "corridor sponge widths must remain inside the corridor", "geo.corridor.sponge.side_inside"));
        }
        const auto stations = CorridorLongitudinalStations{
            -corridor.offshore_extent_m,
            0.0,
            basis.value().epicentre_target_distance_m,
            basis.value().epicentre_target_distance_m + corridor.inland_extent_m};
        auto polygon = Polygon2D{};
        if (!corridor.narrowing.enabled) {
            const auto half = corridor.width_m / 2.0;
            polygon.exterior_ring = {
                point_at(epicentre_xy, basis.value(), stations.offshore_xi_m, half),
                point_at(epicentre_xy, basis.value(), stations.offshore_xi_m, -half),
                point_at(epicentre_xy, basis.value(), stations.inland_xi_m, -half),
                point_at(epicentre_xy, basis.value(), stations.inland_xi_m, half)};
        } else {
            const auto offshore_half = corridor.width_m / 2.0;
            const auto inland_half = inland_width / 2.0;
            polygon.exterior_ring = {
                point_at(epicentre_xy, basis.value(), stations.offshore_xi_m, offshore_half),
                point_at(epicentre_xy, basis.value(), stations.offshore_xi_m, -offshore_half),
                point_at(epicentre_xy, basis.value(), stations.epicentre_xi_m, -offshore_half),
                point_at(epicentre_xy, basis.value(), stations.target_xi_m, -inland_half),
                point_at(epicentre_xy, basis.value(), stations.inland_xi_m, -inland_half),
                point_at(epicentre_xy, basis.value(), stations.inland_xi_m, inland_half),
                point_at(epicentre_xy, basis.value(), stations.target_xi_m, inland_half),
                point_at(epicentre_xy, basis.value(), stations.epicentre_xi_m, offshore_half)};
            remove_consecutive_duplicates(polygon.exterior_ring, request.policy.geometry_absolute_tolerance_m);
        }
        polygon.exterior_ring.push_back(polygon.exterior_ring.front());
        const auto delta_half_width = (corridor.width_m - inland_width) / 2.0;
        const auto analytic_area = corridor.narrowing.enabled
            ? (corridor.width_m * corridor.offshore_extent_m) +
                (((corridor.width_m + inland_width) / 2.0) * basis.value().epicentre_target_distance_m) +
                (inland_width * corridor.inland_extent_m)
            : corridor.width_m * total_length;
        const auto analytic_perimeter = corridor.narrowing.enabled
            ? corridor.width_m + inland_width + (2.0 * corridor.offshore_extent_m) +
                (2.0 * corridor.inland_extent_m) +
                (2.0 * std::hypot(basis.value().epicentre_target_distance_m, delta_half_width))
            : (2.0 * total_length) + (2.0 * corridor.width_m);
        auto geometry = validate_polygon(polygon, basis.value(), corridor.width_m, inland_width, analytic_area, analytic_perimeter, request.policy);
        if (!geometry) {
            return tsunami::core::failure<CorridorConstructionResult>(geometry.error());
        }
        auto diagnostics = geometry.value();
        diagnostics.origin_residual_m = origin_residual;
        diagnostics.bearing_residual_degrees = bearing_residual;
        diagnostics.basis_tangent_norm_residual = std::abs(length(basis.value().tangent) - 1.0);
        diagnostics.basis_normal_norm_residual = std::abs(length(basis.value().left_normal) - 1.0);
        diagnostics.basis_orthogonality_residual = std::abs(dot(basis.value().tangent, basis.value().left_normal));
        diagnostics.basis_determinant_residual = std::abs(cross(basis.value().tangent, basis.value().left_normal) - 1.0);
        const auto extent = extent_for(polygon);
        if (!(extent.minimum_x < extent.maximum_x && extent.minimum_y < extent.maximum_y)) {
            return tsunami::core::failure<CorridorConstructionResult>(corridor_error("geo.corridor.polygon_invalid", "corridor polygon extent is degenerate", "geo.corridor.polygon.simple"));
        }
        const auto sponge = CorridorSpongeLimits{
            stations.offshore_xi_m,
            stations.offshore_xi_m + corridor.sponge.offshore_width_m,
            corridor.sponge.side_width_m,
            inland_width - (2.0 * corridor.sponge.side_width_m)};
        const auto constructed = ConstructedCorridor{
            polygon,
            extent,
            basis.value(),
            stations,
            sponge,
            corridor.width_m,
            inland_width,
            total_length,
            diagnostics.polygon_area_m2,
            diagnostics.polygon_perimeter_m};
        auto record = CorridorConstructionRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{corridor_construction_record_schema_name}, supported_corridor_construction_record_version};
        record.policy_version = supported_corridor_construction_record_policy_version;
        record.identity = request.identity;
        record.scenario_id = configuration.scenario().scenario_id;
        record.target_site = configuration.scenario().target_site;
        record.epicentre = epicentre;
        record.target = target;
        record.target_reference = epicentre.target_reference;
        record.policy = request.policy;
        record.configured_origin = configured_origin;
        record.configured_bearing_degrees = corridor.bearing_degrees_clockwise_from_north;
        record.derived_bearing_degrees = basis.value().derived_bearing_degrees_clockwise_from_north;
        record.origin_residual_m = origin_residual;
        record.bearing_residual_degrees = bearing_residual;
        record.offshore_extent_m = corridor.offshore_extent_m;
        record.epicentre_target_distance_m = basis.value().epicentre_target_distance_m;
        record.inland_extent_m = corridor.inland_extent_m;
        record.total_length_m = total_length;
        record.offshore_width_m = corridor.width_m;
        record.inland_width_m = inland_width;
        record.narrowing_enabled = corridor.narrowing.enabled;
        record.narrowing_rule = narrowing_rule;
        record.local_basis = basis.value();
        record.stations = stations;
        record.sponge_limits = sponge;
        record.polygon = polygon;
        record.vertex_order_convention = "counter_clockwise_closed_offshore_left_offshore_right_inland_right_inland_left";
        record.extent = extent;
        record.area_m2 = diagnostics.polygon_area_m2;
        record.perimeter_m = diagnostics.polygon_perimeter_m;
        record.diagnostics = diagnostics;
        record.configured_field_paths = {
            "/regional_2d/corridor/bearing_degrees_clockwise_from_north",
            "/regional_2d/corridor/inland_extent_m",
            "/regional_2d/corridor/narrowing/enabled",
            "/regional_2d/corridor/narrowing/inland_width_m",
            "/regional_2d/corridor/offshore_extent_m",
            "/regional_2d/corridor/origin/x",
            "/regional_2d/corridor/origin/y",
            "/regional_2d/corridor/sponge/offshore_width_m",
            "/regional_2d/corridor/sponge/side_width_m",
            "/regional_2d/corridor/trajectory_id",
            "/regional_2d/corridor/width_m"};
        if (auto valid = validate_corridor_construction_record(record); !valid) {
            return tsunami::core::failure<CorridorConstructionResult>(valid.error());
        }
        return tsunami::core::success(CorridorConstructionResult{constructed, record, diagnostics});
    }

} // namespace tsunami::geo
