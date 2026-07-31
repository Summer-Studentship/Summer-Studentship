#include <tsunami/geo/CorridorConstructionRecord.hpp>

#include <algorithm>
#include <cmath>
#include <regex>
#include <string>

namespace tsunami::geo
{
    namespace
    {
        [[nodiscard]] auto corridor_error(std::string code, std::string message, std::string rule_id)
            -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::validation,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_corridor_construction_record")
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

        [[nodiscard]] auto finite_box(const BoundingBox2D &box) noexcept -> bool
        {
            return finite(box.minimum_x) && finite(box.minimum_y) && finite(box.maximum_x) && finite(box.maximum_y) &&
                box.minimum_x < box.maximum_x && box.minimum_y < box.maximum_y;
        }

        [[nodiscard]] auto finite_polygon(const Polygon2D &polygon) noexcept -> bool
        {
            return polygon.interior_rings.empty() && polygon.exterior_ring.size() >= 5U &&
                polygon.exterior_ring.front() == polygon.exterior_ring.back() &&
                std::all_of(polygon.exterior_ring.begin(), polygon.exterior_ring.end(), [](Point2D point) {
                    return finite(point);
                });
        }

        [[nodiscard]] auto evidence_valid(const CorridorReferencePointEvidence &evidence) -> bool
        {
            return logical_id_valid(evidence.point_id) && text_present(evidence.definition) &&
                finite(evidence.coordinate) && text_present(evidence.source_document_title) &&
                text_present(evidence.source_document_uri) && timestamp_valid(evidence.accessed_at_utc) &&
                logical_id_valid(evidence.transformation_identity.transformation_id);
        }

        [[nodiscard]] auto policy_valid(const CorridorConstructionPolicy &policy) -> bool
        {
            return finite(policy.minimum_reference_separation_m) && policy.minimum_reference_separation_m > 0.0 &&
                finite(policy.origin_tolerance_m) && policy.origin_tolerance_m >= 0.0 &&
                finite(policy.bearing_tolerance_degrees) && policy.bearing_tolerance_degrees >= 0.0 &&
                finite(policy.basis_orthonormal_tolerance) && policy.basis_orthonormal_tolerance > 0.0 &&
                finite(policy.geometry_absolute_tolerance_m) && policy.geometry_absolute_tolerance_m > 0.0 &&
                finite(policy.geometry_relative_tolerance) && policy.geometry_relative_tolerance >= 0.0 &&
                text_present(policy.tolerance_basis);
        }
    }

    auto to_string(CorridorReferencePointRole role) noexcept -> std::string_view
    {
        switch (role) {
        case CorridorReferencePointRole::epicentre:
            return "epicentre";
        case CorridorReferencePointRole::target:
            return "target";
        }
        return "epicentre";
    }

    auto default_corridor_construction_record_path(
        std::string_view trajectory_id) -> std::filesystem::path
    {
        const auto id = std::string{trajectory_id};
        if (!logical_id_valid(id)) {
            return {};
        }
        return std::filesystem::path{"manifests"} / "corridors" / (id + ".json");
    }

    auto validate_corridor_construction_record(const CorridorConstructionRecord &record)
        -> tsunami::core::Result<void>
    {
        if (record.schema.schema_name != corridor_construction_record_schema_name ||
            record.schema.version != supported_corridor_construction_record_version ||
            record.policy_version != supported_corridor_construction_record_policy_version) {
            return tsunami::core::failure(corridor_error("geo.corridor.record_invalid", "corridor construction record schema identity is unsupported", "geo.corridor.record.schema"));
        }
        const auto &id = record.identity;
        if (!logical_id_valid(id.corridor_id) || id.corridor_revision == 0U ||
            !logical_id_valid(id.trajectory_id) || !logical_id_valid(id.output_dataset_id) ||
            !logical_id_valid(id.output_process_id) || !timestamp_valid(id.executed_at_utc)) {
            return tsunami::core::failure(corridor_error("geo.corridor.record_invalid", "corridor construction record identity is invalid", "geo.corridor.record.identity"));
        }
        if (!text_present(record.scenario_id) || !text_present(record.target_site) ||
            !evidence_valid(record.epicentre) || !evidence_valid(record.target) ||
            record.epicentre.role != CorridorReferencePointRole::epicentre ||
            record.target.role != CorridorReferencePointRole::target || !policy_valid(record.policy) ||
            !finite(record.configured_origin) || !finite(record.configured_bearing_degrees) ||
            !finite(record.derived_bearing_degrees) || !finite(record.origin_residual_m) ||
            !finite(record.bearing_residual_degrees) || !finite(record.offshore_extent_m) ||
            !finite(record.epicentre_target_distance_m) || !finite(record.inland_extent_m) ||
            !finite(record.total_length_m) || !finite(record.offshore_width_m) ||
            !finite(record.inland_width_m) || !text_present(record.narrowing_rule) ||
            !text_present(record.vertex_order_convention) || !finite_polygon(record.polygon) ||
            !finite_box(record.extent) || !finite(record.area_m2) || !finite(record.perimeter_m)) {
            return tsunami::core::failure(corridor_error("geo.corridor.record_invalid", "corridor construction record fields are invalid", "geo.corridor.record.fields"));
        }
        auto paths = record.configured_field_paths;
        if (paths.empty() || !std::all_of(paths.begin(), paths.end(), text_present)) {
            return tsunami::core::failure(corridor_error("geo.corridor.record_invalid", "configured field paths are incomplete", "geo.corridor.record.fields"));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::geo
