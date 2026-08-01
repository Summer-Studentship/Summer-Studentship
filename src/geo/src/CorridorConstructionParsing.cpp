#include <tsunami/geo/CorridorConstructionParsing.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

#include <tsunami/geo/CorridorConstructionSerialisation.hpp>

#include "GeospatialRecordParsingDetail.hpp"

namespace tsunami::geo
{
    namespace
    {
        constexpr auto kind = detail::RecordKind::corridor;

        [[nodiscard]] auto parse_identity(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> CorridorConstructionIdentity
        {
            detail::reject_unknown(
                json,
                {"corridor_id", "corridor_revision", "case_id", "case_revision", "trajectory_id", "output_dataset_id", "output_process_id", "executed_at_utc"},
                pointer,
                source,
                kind);
            return CorridorConstructionIdentity{
                detail::string_value(json, "corridor_id", pointer, source, kind),
                detail::uint_value(json, "corridor_revision", pointer, source, kind),
                detail::parse_case_revision(json, pointer, source, kind),
                detail::string_value(json, "trajectory_id", pointer, source, kind),
                detail::string_value(json, "output_dataset_id", pointer, source, kind),
                detail::string_value(json, "output_process_id", pointer, source, kind),
                detail::string_value(json, "executed_at_utc", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_evidence(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> CorridorReferencePointEvidence
        {
            detail::reject_unknown(
                json,
                {"role", "point_id", "definition", "coordinate", "coordinate_index", "source_feature_id", "transformation_identity", "source_reference", "target_reference", "source_document_title", "source_document_uri", "accessed_at_utc"},
                pointer,
                source,
                kind);
            return CorridorReferencePointEvidence{
                detail::enum_value<CorridorReferencePointRole>(
                    json,
                    "role",
                    pointer,
                    source,
                    kind,
                    {{"epicentre", CorridorReferencePointRole::epicentre}, {"target", CorridorReferencePointRole::target}}),
                detail::string_value(json, "point_id", pointer, source, kind),
                detail::string_value(json, "definition", pointer, source, kind),
                detail::parse_coordinate3(detail::child(json, "coordinate", pointer, source, kind), detail::pointer_for(pointer, "coordinate"), source, kind),
                detail::size_value(json, "coordinate_index", pointer, source, kind),
                detail::nullable_string(json, "source_feature_id", pointer, source, kind),
                detail::parse_transformation_identity(detail::child(json, "transformation_identity", pointer, source, kind), detail::pointer_for(pointer, "transformation_identity"), source, kind),
                detail::parse_reference(detail::child(json, "source_reference", pointer, source, kind), detail::pointer_for(pointer, "source_reference"), source, kind),
                detail::parse_target(detail::child(json, "target_reference", pointer, source, kind), detail::pointer_for(pointer, "target_reference"), source, kind),
                detail::string_value(json, "source_document_title", pointer, source, kind),
                detail::string_value(json, "source_document_uri", pointer, source, kind),
                detail::string_value(json, "accessed_at_utc", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_policy(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> CorridorConstructionPolicy
        {
            detail::reject_unknown(
                json,
                {"minimum_reference_separation_m", "origin_tolerance_m", "bearing_tolerance_degrees", "basis_orthonormal_tolerance", "geometry_absolute_tolerance_m", "geometry_relative_tolerance", "tolerance_basis"},
                pointer,
                source,
                kind);
            return CorridorConstructionPolicy{
                detail::number_value(json, "minimum_reference_separation_m", pointer, source, kind),
                detail::number_value(json, "origin_tolerance_m", pointer, source, kind),
                detail::number_value(json, "bearing_tolerance_degrees", pointer, source, kind),
                detail::number_value(json, "basis_orthonormal_tolerance", pointer, source, kind),
                detail::number_value(json, "geometry_absolute_tolerance_m", pointer, source, kind),
                detail::number_value(json, "geometry_relative_tolerance", pointer, source, kind),
                detail::string_value(json, "tolerance_basis", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_basis(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> CorridorLocalBasis
        {
            detail::reject_unknown(json, {"tangent", "left_normal", "epicentre_target_distance_m", "derived_bearing_degrees_clockwise_from_north"}, pointer, source, kind);
            return CorridorLocalBasis{
                detail::parse_point2(detail::child(json, "tangent", pointer, source, kind), detail::pointer_for(pointer, "tangent"), source, kind),
                detail::parse_point2(detail::child(json, "left_normal", pointer, source, kind), detail::pointer_for(pointer, "left_normal"), source, kind),
                detail::number_value(json, "epicentre_target_distance_m", pointer, source, kind),
                detail::number_value(json, "derived_bearing_degrees_clockwise_from_north", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_stations(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> CorridorLongitudinalStations
        {
            detail::reject_unknown(json, {"offshore_xi_m", "epicentre_xi_m", "target_xi_m", "inland_xi_m"}, pointer, source, kind);
            return CorridorLongitudinalStations{
                detail::number_value(json, "offshore_xi_m", pointer, source, kind),
                detail::number_value(json, "epicentre_xi_m", pointer, source, kind),
                detail::number_value(json, "target_xi_m", pointer, source, kind),
                detail::number_value(json, "inland_xi_m", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_sponge(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> CorridorSpongeLimits
        {
            detail::reject_unknown(json, {"offshore_start_xi_m", "offshore_end_xi_m", "side_width_m", "minimum_unsponge_width_m"}, pointer, source, kind);
            return CorridorSpongeLimits{
                detail::number_value(json, "offshore_start_xi_m", pointer, source, kind),
                detail::number_value(json, "offshore_end_xi_m", pointer, source, kind),
                detail::number_value(json, "side_width_m", pointer, source, kind),
                detail::number_value(json, "minimum_unsponge_width_m", pointer, source, kind)};
        }

        [[nodiscard]] auto parse_polygon(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> Polygon2D
        {
            detail::reject_unknown(json, {"exterior_ring", "interior_rings"}, pointer, source, kind);
            const auto &exterior = detail::child(json, "exterior_ring", pointer, source, kind);
            if (!exterior.is_array()) {
                detail::fail(kind, "type_invalid", "polygon exterior ring must be an array", source, detail::pointer_for(pointer, "exterior_ring"), "array", detail::actual_type(exterior), "exterior_ring");
            }
            auto ring = std::vector<Point2D>{};
            ring.reserve(exterior.size());
            const auto exterior_pointer = detail::pointer_for(pointer, "exterior_ring");
            for (std::size_t i = 0U; i < exterior.size(); ++i) {
                ring.push_back(detail::parse_point2(exterior[i], exterior_pointer + "/" + std::to_string(i), source, kind));
            }
            const auto &interiors = detail::child(json, "interior_rings", pointer, source, kind);
            if (!interiors.is_array() || !interiors.empty()) {
                detail::fail(kind, "type_invalid", "polygon interior rings must be the canonical empty array", source, detail::pointer_for(pointer, "interior_rings"), "empty array", detail::actual_type(interiors), "interior_rings");
            }
            return Polygon2D{std::move(ring), {}};
        }

        [[nodiscard]] auto parse_diagnostics(
            const detail::Json &json,
            const std::string &pointer,
            const std::string &source) -> CorridorConstructionDiagnostics
        {
            detail::reject_unknown(
                json,
                {"origin_residual_m", "bearing_residual_degrees", "basis_tangent_norm_residual", "basis_normal_norm_residual", "basis_orthogonality_residual", "basis_determinant_residual", "analytic_area_m2", "polygon_area_m2", "area_residual_m2", "analytic_perimeter_m", "polygon_perimeter_m", "perimeter_residual_m", "warnings"},
                pointer,
                source,
                kind);
            return CorridorConstructionDiagnostics{
                detail::number_value(json, "origin_residual_m", pointer, source, kind),
                detail::number_value(json, "bearing_residual_degrees", pointer, source, kind),
                detail::number_value(json, "basis_tangent_norm_residual", pointer, source, kind),
                detail::number_value(json, "basis_normal_norm_residual", pointer, source, kind),
                detail::number_value(json, "basis_orthogonality_residual", pointer, source, kind),
                detail::number_value(json, "basis_determinant_residual", pointer, source, kind),
                detail::number_value(json, "analytic_area_m2", pointer, source, kind),
                detail::number_value(json, "polygon_area_m2", pointer, source, kind),
                detail::number_value(json, "area_residual_m2", pointer, source, kind),
                detail::number_value(json, "analytic_perimeter_m", pointer, source, kind),
                detail::number_value(json, "polygon_perimeter_m", pointer, source, kind),
                detail::number_value(json, "perimeter_residual_m", pointer, source, kind),
                detail::string_array(json, "warnings", pointer, source, kind)};
        }

        [[nodiscard]] auto validation_error(
            const tsunami::core::Error &cause,
            const std::string &source) -> tsunami::core::Error
        {
            auto error = detail::parse_error(kind, "validation_failed", "corridor construction record failed semantic validation", source, "/", "valid corridor construction record", "object");
            error.with_cause_code(cause.code());
            return error;
        }
    }

    auto parse_corridor_construction_record(
        std::string_view document,
        std::string source_name) -> tsunami::core::Result<CorridorConstructionRecord>
    {
        try {
            auto root = detail::parse_json_document(document, source_name, kind);
            detail::require_object(root, "/", source_name, kind);
            detail::reject_unknown(
                root,
                {"schema", "policy_version", "formula_version", "identity", "scenario_id", "target_site", "epicentre", "target", "target_reference", "policy", "configured_origin", "configured_bearing_degrees", "derived_bearing_degrees", "origin_residual_m", "bearing_residual_degrees", "offshore_extent_m", "epicentre_target_distance_m", "inland_extent_m", "total_length_m", "offshore_width_m", "inland_width_m", "narrowing_enabled", "narrowing_rule", "local_basis", "stations", "sponge_limits", "polygon", "vertex_order_convention", "extent", "area_m2", "perimeter_m", "diagnostics", "configured_field_paths", "warnings"},
                "/",
                source_name,
                kind);
            const auto formula = detail::string_value(root, "formula_version", "/", source_name, kind);
            if (formula != corridor_construction_formula_version) {
                return tsunami::core::failure<CorridorConstructionRecord>(
                    detail::parse_error(kind, "validation_failed", "corridor construction formula version is unsupported", source_name, "/formula_version", std::string{corridor_construction_formula_version}, "string", "formula_version")
                        .with_cause_code("geo.corridor.record.schema"));
            }
            auto record = CorridorConstructionRecord{};
            record.schema = detail::parse_schema(detail::child(root, "schema", "/", source_name, kind), "/schema", source_name, kind);
            record.policy_version = detail::string_value(root, "policy_version", "/", source_name, kind);
            record.identity = parse_identity(detail::child(root, "identity", "/", source_name, kind), "/identity", source_name);
            record.scenario_id = detail::string_value(root, "scenario_id", "/", source_name, kind);
            record.target_site = detail::string_value(root, "target_site", "/", source_name, kind);
            record.epicentre = parse_evidence(detail::child(root, "epicentre", "/", source_name, kind), "/epicentre", source_name);
            record.target = parse_evidence(detail::child(root, "target", "/", source_name, kind), "/target", source_name);
            record.target_reference = detail::parse_target(detail::child(root, "target_reference", "/", source_name, kind), "/target_reference", source_name, kind);
            record.policy = parse_policy(detail::child(root, "policy", "/", source_name, kind), "/policy", source_name);
            record.configured_origin = detail::parse_point2(detail::child(root, "configured_origin", "/", source_name, kind), "/configured_origin", source_name, kind);
            record.configured_bearing_degrees = detail::number_value(root, "configured_bearing_degrees", "/", source_name, kind);
            record.derived_bearing_degrees = detail::number_value(root, "derived_bearing_degrees", "/", source_name, kind);
            record.origin_residual_m = detail::number_value(root, "origin_residual_m", "/", source_name, kind);
            record.bearing_residual_degrees = detail::number_value(root, "bearing_residual_degrees", "/", source_name, kind);
            record.offshore_extent_m = detail::number_value(root, "offshore_extent_m", "/", source_name, kind);
            record.epicentre_target_distance_m = detail::number_value(root, "epicentre_target_distance_m", "/", source_name, kind);
            record.inland_extent_m = detail::number_value(root, "inland_extent_m", "/", source_name, kind);
            record.total_length_m = detail::number_value(root, "total_length_m", "/", source_name, kind);
            record.offshore_width_m = detail::number_value(root, "offshore_width_m", "/", source_name, kind);
            record.inland_width_m = detail::number_value(root, "inland_width_m", "/", source_name, kind);
            record.narrowing_enabled = detail::bool_value(root, "narrowing_enabled", "/", source_name, kind);
            record.narrowing_rule = detail::string_value(root, "narrowing_rule", "/", source_name, kind);
            record.local_basis = parse_basis(detail::child(root, "local_basis", "/", source_name, kind), "/local_basis", source_name);
            record.stations = parse_stations(detail::child(root, "stations", "/", source_name, kind), "/stations", source_name);
            record.sponge_limits = parse_sponge(detail::child(root, "sponge_limits", "/", source_name, kind), "/sponge_limits", source_name);
            record.polygon = parse_polygon(detail::child(root, "polygon", "/", source_name, kind), "/polygon", source_name);
            record.vertex_order_convention = detail::string_value(root, "vertex_order_convention", "/", source_name, kind);
            record.extent = detail::parse_box(detail::child(root, "extent", "/", source_name, kind), "/extent", source_name, kind);
            record.area_m2 = detail::number_value(root, "area_m2", "/", source_name, kind);
            record.perimeter_m = detail::number_value(root, "perimeter_m", "/", source_name, kind);
            record.diagnostics = parse_diagnostics(detail::child(root, "diagnostics", "/", source_name, kind), "/diagnostics", source_name);
            record.configured_field_paths = detail::string_array(root, "configured_field_paths", "/", source_name, kind);
            record.warnings = detail::string_array(root, "warnings", "/", source_name, kind);

            if (auto valid = validate_corridor_construction_record(record); !valid) {
                return tsunami::core::failure<CorridorConstructionRecord>(validation_error(valid.error(), source_name));
            }
            return tsunami::core::success(std::move(record));
        } catch (const detail::ParseFailure &failure) {
            return tsunami::core::failure<CorridorConstructionRecord>(failure.error);
        } catch (const std::exception &) {
            return tsunami::core::failure<CorridorConstructionRecord>(
                detail::parse_error(kind, "malformed", "parser exception was translated", source_name, "/", "canonical corridor construction JSON", "exception"));
        }
    }

    auto read_corridor_construction_record(
        const std::filesystem::path &path) -> tsunami::core::Result<CorridorConstructionRecord>
    {
        auto bytes = detail::read_bounded_file(path, maximum_corridor_construction_record_document_bytes, kind);
        if (!bytes) {
            return tsunami::core::failure<CorridorConstructionRecord>(bytes.error());
        }
        return parse_corridor_construction_record(bytes.value(), path.generic_string());
    }

} // namespace tsunami::geo
