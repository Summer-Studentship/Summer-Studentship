#include <tsunami/data/CaseConfigurationParsing.hpp>

#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include <tsunami/data/CaseConfigurationValidation.hpp>

namespace tsunami::data
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        [[nodiscard]] auto parse_error(
            std::string code,
            std::string message,
            std::string source_name,
            std::string pointer = {},
            std::string field = {},
            std::string rule = {}) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::configuration,
                tsunami::core::Severity::error};
            error.add_context("operation", "parse_case_configuration")
                .add_context("source_name", std::move(source_name))
                .add_context("state_changed", "false");
            if (!pointer.empty()) {
                error.add_context("json_pointer", std::move(pointer));
            }
            if (!field.empty()) {
                error.add_context("field", std::move(field));
            }
            if (!rule.empty()) {
                error.add_context("rule_id", std::move(rule));
            }
            return error;
        }

        [[nodiscard]] auto file_error(std::string code, std::string message, const std::filesystem::path &path) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::configuration,
                tsunami::core::Severity::error};
            error.add_context("operation", "read_case_configuration")
                .add_context("path", path.generic_string())
                .add_context("state_changed", "false");
            return error;
        }

        [[nodiscard]] auto pointer_for(std::string_view parent, std::string_view field) -> std::string
        {
            if (parent == "/") {
                return "/" + std::string{field};
            }
            return std::string{parent} + "/" + std::string{field};
        }

        auto require_object(const Json &json, std::string pointer, std::string source) -> tsunami::core::Result<void>
        {
            if (!json.is_object()) {
                return tsunami::core::failure(parse_error("data.case_config.field_type_invalid", "JSON value must be an object", std::move(source), std::move(pointer)));
            }
            return tsunami::core::success();
        }

        auto reject_unknown(const Json &json, const std::set<std::string> &allowed, std::string pointer, std::string source) -> tsunami::core::Result<void>
        {
            for (const auto &[key, value] : json.items()) {
                static_cast<void>(value);
                if (!allowed.contains(key)) {
                    return tsunami::core::failure(parse_error(
                        "data.case_config.unknown_field",
                        "unknown field is not permitted in a core section",
                        std::move(source),
                        pointer_for(pointer, key),
                        key,
                        "case.unknown_field"));
                }
            }
            return tsunami::core::success();
        }

        auto section(const Json &json, std::string_view key, std::string source) -> tsunami::core::Result<const Json *>
        {
            auto found = json.find(std::string{key});
            if (found == json.end()) {
                return tsunami::core::failure<const Json *>(parse_error(
                    "data.case_config.required_field_missing",
                    "required field is missing",
                    std::move(source),
                    pointer_for("/", key),
                    std::string{key}));
            }
            return tsunami::core::success(std::addressof(*found));
        }

        template <class T>
        auto required(const Json &json, std::string_view key, std::string pointer, std::string source, const char *type) -> tsunami::core::Result<T>
        {
            auto found = json.find(std::string{key});
            if (found == json.end()) {
                return tsunami::core::failure<T>(parse_error(
                    "data.case_config.required_field_missing",
                    "required field is missing",
                    std::move(source),
                    pointer_for(pointer, key),
                    std::string{key}));
            }
            try {
                if constexpr (std::is_same_v<T, std::string>) {
                    if (!found->is_string()) {
                        throw std::runtime_error{"type"};
                    }
                } else if constexpr (std::is_same_v<T, bool>) {
                    if (!found->is_boolean()) {
                        throw std::runtime_error{"type"};
                    }
                } else if constexpr (std::is_same_v<T, double>) {
                    if (!found->is_number()) {
                        throw std::runtime_error{"type"};
                    }
                } else if constexpr (std::is_same_v<T, std::uint64_t>) {
                    if (!found->is_number_unsigned()) {
                        throw std::runtime_error{"type"};
                    }
                }
                auto value = found->get<T>();
                if constexpr (std::is_same_v<T, double>) {
                    if (!std::isfinite(value)) {
                        throw std::runtime_error{"finite"};
                    }
                }
                return tsunami::core::success(std::move(value));
            } catch (const std::exception &) {
                auto error = parse_error(
                    "data.case_config.field_type_invalid",
                    "field has the wrong JSON type",
                    std::move(source),
                    pointer_for(pointer, key),
                    std::string{key});
                error.add_context("expected", type);
                return tsunami::core::failure<T>(std::move(error));
            }
        }

        template <class T>
        auto nullable(const Json &json, std::string_view key, std::string pointer, std::string source, const char *type)
            -> tsunami::core::Result<std::optional<T>>
        {
            auto found = json.find(std::string{key});
            if (found == json.end()) {
                return tsunami::core::failure<std::optional<T>>(parse_error(
                    "data.case_config.required_field_missing",
                    "required nullable field is missing",
                    std::move(source),
                    pointer_for(pointer, key),
                    std::string{key}));
            }
            if (found->is_null()) {
                return tsunami::core::success(std::optional<T>{});
            }
            auto value = required<T>(json, key, std::move(pointer), std::move(source), type);
            if (!value) {
                return tsunami::core::failure<std::optional<T>>(value.error());
            }
            return tsunami::core::success(std::optional<T>{std::move(value).value()});
        }

        template <class Enum>
        auto parse_enum(std::string value, std::initializer_list<std::pair<std::string_view, Enum>> values, std::string pointer, std::string source)
            -> tsunami::core::Result<Enum>
        {
            for (const auto &[name, enumeration] : values) {
                if (value == name) {
                    return tsunami::core::success(enumeration);
                }
            }
            return tsunami::core::failure<Enum>(parse_error(
                "data.case_config.field_type_invalid",
                "field value is not a supported enum string",
                std::move(source),
                std::move(pointer)));
        }

        auto case_identity(const Json &json, std::string source) -> tsunami::core::Result<CaseIdentity>
        {
            constexpr auto pointer = "/case";
            auto object = require_object(json, pointer, source);
            if (!object) {
                return tsunami::core::failure<CaseIdentity>(object.error());
            }
            auto unknown = reject_unknown(json, {"case_id", "case_slug", "revision", "created_at_utc", "created_by"}, pointer, source);
            if (!unknown) {
                return tsunami::core::failure<CaseIdentity>(unknown.error());
            }
            auto id_text = required<std::string>(json, "case_id", pointer, source, "string");
            auto slug = required<std::string>(json, "case_slug", pointer, source, "string");
            auto revision = required<std::uint64_t>(json, "revision", pointer, source, "unsigned integer");
            auto created = required<std::string>(json, "created_at_utc", pointer, source, "string");
            auto author = required<std::string>(json, "created_by", pointer, source, "string");
            if (!id_text || !slug || !revision || !created || !author) {
                return tsunami::core::failure<CaseIdentity>((!id_text ? id_text.error() : !slug ? slug.error() : !revision ? revision.error() : !created ? created.error() : author.error()));
            }
            auto case_id = tsunami::core::CaseId::from_string(std::move(id_text).value());
            if (!case_id) {
                return tsunami::core::failure<CaseIdentity>(parse_error("data.case_config.identifier_invalid", "case_id is invalid", source, "/case/case_id", "case_id"));
            }
            return tsunami::core::success(CaseIdentity{std::move(*case_id), std::move(slug).value(), std::move(revision).value(), std::move(created).value(), std::move(author).value()});
        }

        auto scenario(const Json &json, std::string source) -> tsunami::core::Result<ScenarioConfiguration>
        {
            constexpr auto pointer = "/scenario";
            auto unknown = reject_unknown(json, {"scenario_id", "event_id", "target_site", "model_family"}, pointer, source);
            if (!unknown) {
                return tsunami::core::failure<ScenarioConfiguration>(unknown.error());
            }
            auto scenario_id = required<std::string>(json, "scenario_id", pointer, source, "string");
            auto event_id = required<std::string>(json, "event_id", pointer, source, "string");
            auto target_site = required<std::string>(json, "target_site", pointer, source, "string");
            auto family_text = required<std::string>(json, "model_family", pointer, source, "string");
            if (!scenario_id || !event_id || !target_site || !family_text) {
                return tsunami::core::failure<ScenarioConfiguration>((!scenario_id ? scenario_id.error() : !event_id ? event_id.error() : !target_site ? target_site.error() : family_text.error()));
            }
            auto family = parse_enum<CaseModelFamily>(std::move(family_text).value(), {{"regional_2d", CaseModelFamily::regional_2d}}, "/scenario/model_family", source);
            if (!family) {
                return tsunami::core::failure<ScenarioConfiguration>(family.error());
            }
            return tsunami::core::success(ScenarioConfiguration{std::move(scenario_id).value(), std::move(event_id).value(), std::move(target_site).value(), family.value()});
        }

        auto coordinate_frame(const Json &json, std::string source) -> tsunami::core::Result<CoordinateFrameConfiguration>
        {
            constexpr auto pointer = "/coordinate_frame";
            auto unknown = reject_unknown(json, {"horizontal_crs", "vertical_datum", "horizontal_unit", "vertical_unit", "axis_order", "vertical_positive"}, pointer, source);
            if (!unknown) {
                return tsunami::core::failure<CoordinateFrameConfiguration>(unknown.error());
            }
            auto horizontal_crs = required<std::string>(json, "horizontal_crs", pointer, source, "string");
            auto vertical_datum = required<std::string>(json, "vertical_datum", pointer, source, "string");
            auto horizontal_unit = required<std::string>(json, "horizontal_unit", pointer, source, "string");
            auto vertical_unit = required<std::string>(json, "vertical_unit", pointer, source, "string");
            auto axis_text = required<std::string>(json, "axis_order", pointer, source, "string");
            auto positive_text = required<std::string>(json, "vertical_positive", pointer, source, "string");
            if (!horizontal_crs || !vertical_datum || !horizontal_unit || !vertical_unit || !axis_text || !positive_text) {
                return tsunami::core::failure<CoordinateFrameConfiguration>((!horizontal_crs ? horizontal_crs.error() : !vertical_datum ? vertical_datum.error() : !horizontal_unit ? horizontal_unit.error() : !vertical_unit ? vertical_unit.error() : !axis_text ? axis_text.error() : positive_text.error()));
            }
            auto axis = parse_enum<HorizontalAxisOrder>(std::move(axis_text).value(), {{"east_north", HorizontalAxisOrder::east_north}}, "/coordinate_frame/axis_order", source);
            auto positive = parse_enum<VerticalPositiveDirection>(std::move(positive_text).value(), {{"up", VerticalPositiveDirection::up}}, "/coordinate_frame/vertical_positive", source);
            if (!axis || !positive) {
                return tsunami::core::failure<CoordinateFrameConfiguration>(!axis ? axis.error() : positive.error());
            }
            return tsunami::core::success(CoordinateFrameConfiguration{std::move(horizontal_crs).value(), std::move(vertical_datum).value(), std::move(horizontal_unit).value(), std::move(vertical_unit).value(), axis.value(), positive.value()});
        }

        auto datasets(const Json &json, std::string source) -> tsunami::core::Result<DatasetBindings>
        {
            constexpr auto pointer = "/datasets";
            auto unknown = reject_unknown(json, {"manifest_path", "bindings"}, pointer, source);
            if (!unknown) {
                return tsunami::core::failure<DatasetBindings>(unknown.error());
            }
            auto manifest_path = required<std::string>(json, "manifest_path", pointer, source, "string");
            auto bindings_ptr = section(json, "bindings", source);
            if (!manifest_path || !bindings_ptr) {
                return tsunami::core::failure<DatasetBindings>(!manifest_path ? manifest_path.error() : bindings_ptr.error());
            }
            const auto &bindings = *bindings_ptr.value();
            auto object = require_object(bindings, "/datasets/bindings", source);
            if (!object) {
                return tsunami::core::failure<DatasetBindings>(object.error());
            }
            auto binding_unknown = reject_unknown(bindings, {"bathymetry", "topography", "earthquake_displacement", "prescribed_surface", "manning", "coriolis", "observations"}, "/datasets/bindings", source);
            if (!binding_unknown) {
                return tsunami::core::failure<DatasetBindings>(binding_unknown.error());
            }
            auto bathymetry = required<std::string>(bindings, "bathymetry", "/datasets/bindings", source, "string");
            auto topography = required<std::string>(bindings, "topography", "/datasets/bindings", source, "string");
            auto earthquake = nullable<std::string>(bindings, "earthquake_displacement", "/datasets/bindings", source, "string");
            auto prescribed = nullable<std::string>(bindings, "prescribed_surface", "/datasets/bindings", source, "string");
            auto manning = nullable<std::string>(bindings, "manning", "/datasets/bindings", source, "string");
            auto coriolis = nullable<std::string>(bindings, "coriolis", "/datasets/bindings", source, "string");
            if (!bathymetry || !topography || !earthquake || !prescribed || !manning || !coriolis) {
                return tsunami::core::failure<DatasetBindings>((!bathymetry ? bathymetry.error() : !topography ? topography.error() : !earthquake ? earthquake.error() : !prescribed ? prescribed.error() : !manning ? manning.error() : coriolis.error()));
            }
            auto obs_it = bindings.find("observations");
            if (obs_it == bindings.end() || !obs_it->is_array()) {
                return tsunami::core::failure<DatasetBindings>(parse_error("data.case_config.field_type_invalid", "observations must be an array", source, "/datasets/bindings/observations", "observations"));
            }
            auto observations = std::vector<std::string>{};
            for (std::size_t index = 0; index < obs_it->size(); ++index) {
                if (!(*obs_it)[index].is_string()) {
                    return tsunami::core::failure<DatasetBindings>(parse_error("data.case_config.field_type_invalid", "observation id must be a string", source, "/datasets/bindings/observations/" + std::to_string(index), "observations"));
                }
                observations.push_back((*obs_it)[index].get<std::string>());
            }
            return tsunami::core::success(DatasetBindings{std::filesystem::path{std::move(manifest_path).value()}, std::move(bathymetry).value(), std::move(topography).value(), std::move(earthquake).value(), std::move(prescribed).value(), std::move(manning).value(), std::move(coriolis).value(), std::move(observations)});
        }

        auto regional_2d(const Json &json, std::string source) -> tsunami::core::Result<Regional2DCaseConfiguration>
        {
            constexpr auto pointer = "/regional_2d";
            auto unknown = reject_unknown(json, {"corridor", "physics", "numerics", "boundaries"}, pointer, source);
            if (!unknown) {
                return tsunami::core::failure<Regional2DCaseConfiguration>(unknown.error());
            }
            const auto *corridor_json = section(json, "corridor", source).value();
            const auto *physics_json = section(json, "physics", source).value();
            const auto *numerics_json = section(json, "numerics", source).value();
            const auto *boundaries_json = section(json, "boundaries", source).value();
            auto corridor_unknown = reject_unknown(*corridor_json, {"trajectory_id", "origin", "bearing_degrees_clockwise_from_north", "width_m", "offshore_extent_m", "inland_extent_m", "narrowing", "sponge"}, "/regional_2d/corridor", source);
            auto physics_unknown = reject_unknown(*physics_json, {"gravity_m_per_s2", "manning", "coriolis", "earthquake"}, "/regional_2d/physics", source);
            auto numerics_unknown = reject_unknown(*numerics_json, {"scheme", "courant_number", "positivity_safety_factor", "relaxation_safety_factor", "source_safety_factor", "minimum_timestep_s", "maximum_timestep_s", "final_time_s", "maximum_steps"}, "/regional_2d/numerics", source);
            auto boundaries_unknown = reject_unknown(*boundaries_json, {"offshore", "inland", "left_side", "right_side", "relaxation"}, "/regional_2d/boundaries", source);
            if (!corridor_unknown || !physics_unknown || !numerics_unknown || !boundaries_unknown) {
                return tsunami::core::failure<Regional2DCaseConfiguration>((!corridor_unknown ? corridor_unknown.error() : !physics_unknown ? physics_unknown.error() : !numerics_unknown ? numerics_unknown.error() : boundaries_unknown.error()));
            }
            auto trajectory = required<std::string>(*corridor_json, "trajectory_id", "/regional_2d/corridor", source, "string");
            const auto *origin_json = section(*corridor_json, "origin", source).value();
            const auto *narrowing_json = section(*corridor_json, "narrowing", source).value();
            const auto *sponge_json = section(*corridor_json, "sponge", source).value();
            auto origin_unknown = reject_unknown(*origin_json, {"x", "y"}, "/regional_2d/corridor/origin", source);
            auto narrowing_unknown = reject_unknown(*narrowing_json, {"enabled", "inland_width_m"}, "/regional_2d/corridor/narrowing", source);
            auto sponge_unknown = reject_unknown(*sponge_json, {"offshore_width_m", "side_width_m"}, "/regional_2d/corridor/sponge", source);
            if (!trajectory || !origin_unknown || !narrowing_unknown || !sponge_unknown) {
                return tsunami::core::failure<Regional2DCaseConfiguration>((!trajectory ? trajectory.error() : !origin_unknown ? origin_unknown.error() : !narrowing_unknown ? narrowing_unknown.error() : sponge_unknown.error()));
            }
            auto corridor = CorridorRequest{
                std::move(trajectory).value(),
                CorridorOrigin{required<double>(*origin_json, "x", "/regional_2d/corridor/origin", source, "number").value(), required<double>(*origin_json, "y", "/regional_2d/corridor/origin", source, "number").value()},
                required<double>(*corridor_json, "bearing_degrees_clockwise_from_north", "/regional_2d/corridor", source, "number").value(),
                required<double>(*corridor_json, "width_m", "/regional_2d/corridor", source, "number").value(),
                required<double>(*corridor_json, "offshore_extent_m", "/regional_2d/corridor", source, "number").value(),
                required<double>(*corridor_json, "inland_extent_m", "/regional_2d/corridor", source, "number").value(),
                CorridorNarrowingConfiguration{required<bool>(*narrowing_json, "enabled", "/regional_2d/corridor/narrowing", source, "boolean").value(), nullable<double>(*narrowing_json, "inland_width_m", "/regional_2d/corridor/narrowing", source, "number").value()},
                CorridorSpongeConfiguration{required<double>(*sponge_json, "offshore_width_m", "/regional_2d/corridor/sponge", source, "number").value(), required<double>(*sponge_json, "side_width_m", "/regional_2d/corridor/sponge", source, "number").value()}};

            const auto *manning_json = section(*physics_json, "manning", source).value();
            const auto *coriolis_json = section(*physics_json, "coriolis", source).value();
            const auto *earthquake_json = section(*physics_json, "earthquake", source).value();
            auto manning_unknown = reject_unknown(*manning_json, {"kind", "value_s_per_m_one_third", "dataset_binding"}, "/regional_2d/physics/manning", source);
            auto coriolis_unknown = reject_unknown(*coriolis_json, {"kind", "value_per_s", "dataset_binding"}, "/regional_2d/physics/coriolis", source);
            auto earthquake_unknown = reject_unknown(*earthquake_json, {"enabled", "displacement_binding", "bed_mapping", "surface_transfer", "prescribed_surface_binding"}, "/regional_2d/physics/earthquake", source);
            if (!manning_unknown || !coriolis_unknown || !earthquake_unknown) {
                return tsunami::core::failure<Regional2DCaseConfiguration>((!manning_unknown ? manning_unknown.error() : !coriolis_unknown ? coriolis_unknown.error() : earthquake_unknown.error()));
            }
            auto manning_kind = parse_enum<ManningConfigurationKind>(required<std::string>(*manning_json, "kind", "/regional_2d/physics/manning", source, "string").value(), {{"disabled", ManningConfigurationKind::disabled}, {"uniform", ManningConfigurationKind::uniform}, {"dataset", ManningConfigurationKind::dataset}}, "/regional_2d/physics/manning/kind", source);
            auto coriolis_kind = parse_enum<CoriolisConfigurationKind>(required<std::string>(*coriolis_json, "kind", "/regional_2d/physics/coriolis", source, "string").value(), {{"disabled", CoriolisConfigurationKind::disabled}, {"constant", CoriolisConfigurationKind::constant}, {"dataset", CoriolisConfigurationKind::dataset}}, "/regional_2d/physics/coriolis/kind", source);
            auto bed_mapping = parse_enum<BedDeformationMapping>(required<std::string>(*earthquake_json, "bed_mapping", "/regional_2d/physics/earthquake", source, "string").value(), {{"vertical_only", BedDeformationMapping::vertical_only}, {"horizontal_slope_corrected", BedDeformationMapping::horizontal_slope_corrected}}, "/regional_2d/physics/earthquake/bed_mapping", source);
            auto surface = parse_enum<SurfaceTransfer>(required<std::string>(*earthquake_json, "surface_transfer", "/regional_2d/physics/earthquake", source, "string").value(), {{"passive_equal_to_effective_bed", SurfaceTransfer::passive_equal_to_effective_bed}, {"prescribed", SurfaceTransfer::prescribed}}, "/regional_2d/physics/earthquake/surface_transfer", source);
            auto scheme = parse_enum<RegionalTimeScheme>(required<std::string>(*numerics_json, "scheme", "/regional_2d/numerics", source, "string").value(), {{"forward_euler", RegionalTimeScheme::forward_euler}, {"ssprk2", RegionalTimeScheme::ssprk2}, {"ssprk3", RegionalTimeScheme::ssprk3}}, "/regional_2d/numerics/scheme", source);
            if (!manning_kind || !coriolis_kind || !bed_mapping || !surface || !scheme) {
                return tsunami::core::failure<Regional2DCaseConfiguration>((!manning_kind ? manning_kind.error() : !coriolis_kind ? coriolis_kind.error() : !bed_mapping ? bed_mapping.error() : !surface ? surface.error() : scheme.error()));
            }
            auto physics = RegionalPhysicsConfiguration{
                required<double>(*physics_json, "gravity_m_per_s2", "/regional_2d/physics", source, "number").value(),
                ManningConfiguration{manning_kind.value(), nullable<double>(*manning_json, "value_s_per_m_one_third", "/regional_2d/physics/manning", source, "number").value(), nullable<std::string>(*manning_json, "dataset_binding", "/regional_2d/physics/manning", source, "string").value()},
                CoriolisConfiguration{coriolis_kind.value(), nullable<double>(*coriolis_json, "value_per_s", "/regional_2d/physics/coriolis", source, "number").value(), nullable<std::string>(*coriolis_json, "dataset_binding", "/regional_2d/physics/coriolis", source, "string").value()},
                EarthquakeConfiguration{required<bool>(*earthquake_json, "enabled", "/regional_2d/physics/earthquake", source, "boolean").value(), nullable<std::string>(*earthquake_json, "displacement_binding", "/regional_2d/physics/earthquake", source, "string").value(), bed_mapping.value(), surface.value(), nullable<std::string>(*earthquake_json, "prescribed_surface_binding", "/regional_2d/physics/earthquake", source, "string").value()}};
            auto numerics = RegionalNumericsConfiguration{
                scheme.value(),
                required<double>(*numerics_json, "courant_number", "/regional_2d/numerics", source, "number").value(),
                required<double>(*numerics_json, "positivity_safety_factor", "/regional_2d/numerics", source, "number").value(),
                required<double>(*numerics_json, "relaxation_safety_factor", "/regional_2d/numerics", source, "number").value(),
                required<double>(*numerics_json, "source_safety_factor", "/regional_2d/numerics", source, "number").value(),
                required<double>(*numerics_json, "minimum_timestep_s", "/regional_2d/numerics", source, "number").value(),
                required<double>(*numerics_json, "maximum_timestep_s", "/regional_2d/numerics", source, "number").value(),
                required<double>(*numerics_json, "final_time_s", "/regional_2d/numerics", source, "number").value(),
                required<std::uint64_t>(*numerics_json, "maximum_steps", "/regional_2d/numerics", source, "unsigned integer").value()};
            const auto parse_boundary = [&](std::string_view key) -> tsunami::core::Result<RegionalBoundaryKind> {
                const auto *value = section(*boundaries_json, key, source).value();
                auto unknown_role = reject_unknown(*value, {"kind"}, pointer_for("/regional_2d/boundaries", key), source);
                if (!unknown_role) {
                    return tsunami::core::failure<RegionalBoundaryKind>(unknown_role.error());
                }
                return parse_enum<RegionalBoundaryKind>(required<std::string>(*value, "kind", pointer_for("/regional_2d/boundaries", key), source, "string").value(), {{"transmissive", RegionalBoundaryKind::transmissive}, {"radiation", RegionalBoundaryKind::radiation}}, pointer_for(pointer_for("/regional_2d/boundaries", key), "kind"), source);
            };
	auto offshore = parse_boundary("offshore");
	auto inland = parse_boundary("inland");
	auto left_side = parse_boundary("left_side");
	auto right_side = parse_boundary("right_side");
	if (!offshore || !inland || !left_side || !right_side) {
		return tsunami::core::failure<Regional2DCaseConfiguration>((!offshore ? offshore.error() : !inland ? inland.error() : !left_side ? left_side.error() : right_side.error()));
	}
	const auto *relaxation_json = section(*boundaries_json, "relaxation", source).value();
	auto relaxation_unknown = reject_unknown(*relaxation_json, {"enabled", "maximum_rate_per_s", "profile_exponent"}, "/regional_2d/boundaries/relaxation", source);
	if (!relaxation_unknown) {
		return tsunami::core::failure<Regional2DCaseConfiguration>(relaxation_unknown.error());
	}
	auto boundaries = CorridorBoundaryConfiguration{offshore.value(), inland.value(), left_side.value(), right_side.value()};
            auto relaxation = RelaxationConfiguration{required<bool>(*relaxation_json, "enabled", "/regional_2d/boundaries/relaxation", source, "boolean").value(), nullable<double>(*relaxation_json, "maximum_rate_per_s", "/regional_2d/boundaries/relaxation", source, "number").value(), nullable<double>(*relaxation_json, "profile_exponent", "/regional_2d/boundaries/relaxation", source, "number").value()};
            return tsunami::core::success(Regional2DCaseConfiguration{std::move(corridor), std::move(physics), std::move(numerics), std::move(boundaries), std::move(relaxation)});
        }

        auto outputs(const Json &json, double final_time, std::string source) -> tsunami::core::Result<CaseOutputConfiguration>
        {
            static_cast<void>(final_time);
            constexpr auto pointer = "/outputs";
            auto unknown = reject_unknown(json, {"snapshot_interval_s", "diagnostics_enabled", "initialisation_diagnostics_enabled", "checkpoint_interval_s"}, pointer, source);
            if (!unknown) {
                return tsunami::core::failure<CaseOutputConfiguration>(unknown.error());
            }
            return tsunami::core::success(CaseOutputConfiguration{
                nullable<double>(json, "snapshot_interval_s", pointer, source, "number").value(),
                required<bool>(json, "diagnostics_enabled", pointer, source, "boolean").value(),
                required<bool>(json, "initialisation_diagnostics_enabled", pointer, source, "boolean").value(),
                nullable<double>(json, "checkpoint_interval_s", pointer, source, "number").value()});
        }

        auto canonical_extension_json(Json value) -> std::string
        {
            if (value.is_object()) {
                Json sorted = Json::object();
                auto keys = std::vector<std::string>{};
                for (const auto &[key, child] : value.items()) {
                    static_cast<void>(child);
                    keys.push_back(key);
                }
                std::sort(keys.begin(), keys.end());
                for (const auto &key : keys) {
                    sorted[key] = Json::parse(canonical_extension_json(value[key]));
                }
                value = std::move(sorted);
            } else if (value.is_array()) {
                for (auto &child : value) {
                    child = Json::parse(canonical_extension_json(child));
                }
            }
            return value.dump();
        }

        auto extensions(const Json &json) -> CaseExtensions
        {
            auto values = std::vector<CaseExtension>{};
            values.reserve(json.size());
            for (const auto &[key, value] : json.items()) {
                values.push_back(CaseExtension{key, canonical_extension_json(value)});
            }
            std::sort(values.begin(), values.end(), [](const auto &left, const auto &right) { return left.name < right.name; });
            return CaseExtensions{std::move(values)};
        }
    } // namespace

    auto parse_case_configuration(std::string_view document, std::string source_name) -> tsunami::core::Result<CaseConfiguration>
    {
        if (document.empty()) {
            return tsunami::core::failure<CaseConfiguration>(parse_error(
                "data.case_config.document_empty",
                "case configuration document is empty",
                source_name));
        }
        Json root;
        try {
            root = Json::parse(document.begin(), document.end());
        } catch (const nlohmann::json::parse_error &error) {
            auto diagnostic = parse_error(
                "data.case_config.json_invalid",
                "case configuration document is not valid JSON",
                source_name);
            diagnostic.add_context("byte_offset", std::to_string(error.byte))
                .add_context("parser_detail", error.what());
            return tsunami::core::failure<CaseConfiguration>(std::move(diagnostic));
        } catch (const nlohmann::json::exception &error) {
            auto diagnostic = parse_error("data.case_config.json_invalid", "case configuration JSON parser failed", source_name);
            diagnostic.add_context("parser_detail", error.what());
            return tsunami::core::failure<CaseConfiguration>(std::move(diagnostic));
        }
        if (!root.is_object()) {
            return tsunami::core::failure<CaseConfiguration>(parse_error(
                "data.case_config.root_type_invalid",
                "case configuration root must be an object",
                source_name,
                "/"));
        }
        auto unknown = reject_unknown(root, {"schema_version", "policy_version", "case", "scenario", "coordinate_frame", "datasets", "regional_2d", "outputs", "extensions"}, "/", source_name);
        if (!unknown) {
            return tsunami::core::failure<CaseConfiguration>(unknown.error());
        }
        auto version_text = required<std::string>(root, "schema_version", "/", source_name, "string");
        auto policy = required<std::string>(root, "policy_version", "/", source_name, "string");
        if (!version_text || !policy) {
            return tsunami::core::failure<CaseConfiguration>(!version_text ? version_text.error() : policy.error());
        }
        auto version = parse_semantic_version(version_text.value());
        if (!version) {
            auto error = version.error();
            error.add_context("source_name", source_name)
                .add_context("operation", "parse_case_configuration");
            return tsunami::core::failure<CaseConfiguration>(std::move(error));
        }
        const auto compatibility = classify_case_schema_version(version.value());
        if (compatibility == CaseSchemaCompatibility::migration_required) {
            auto error = parse_error("data.case_config.migration_required", "legacy case schema requires migration", source_name, "/schema_version", "schema_version", "case.schema_version.compatible");
            error.add_context("compatibility", std::string{to_string(compatibility)})
                .add_context("schema_version", version.value().text());
            return tsunami::core::failure<CaseConfiguration>(std::move(error));
        }
        if (compatibility == CaseSchemaCompatibility::unsupported_major) {
            auto error = parse_error("data.case_config.schema_major_unsupported", "case schema major version is unsupported", source_name, "/schema_version", "schema_version", "case.schema_version.compatible");
            error.add_context("compatibility", std::string{to_string(compatibility)})
                .add_context("schema_version", version.value().text());
            return tsunami::core::failure<CaseConfiguration>(std::move(error));
        }

        auto case_ptr = section(root, "case", source_name);
        auto scenario_ptr = section(root, "scenario", source_name);
        auto frame_ptr = section(root, "coordinate_frame", source_name);
        auto datasets_ptr = section(root, "datasets", source_name);
        auto regional_ptr = section(root, "regional_2d", source_name);
        auto outputs_ptr = section(root, "outputs", source_name);
        auto extensions_ptr = section(root, "extensions", source_name);
        if (!case_ptr || !scenario_ptr || !frame_ptr || !datasets_ptr || !regional_ptr || !outputs_ptr || !extensions_ptr) {
            return tsunami::core::failure<CaseConfiguration>((!case_ptr ? case_ptr.error() : !scenario_ptr ? scenario_ptr.error() : !frame_ptr ? frame_ptr.error() : !datasets_ptr ? datasets_ptr.error() : !regional_ptr ? regional_ptr.error() : !outputs_ptr ? outputs_ptr.error() : extensions_ptr.error()));
        }
        if (!extensions_ptr.value()->is_object()) {
            return tsunami::core::failure<CaseConfiguration>(parse_error("data.case_config.field_type_invalid", "extensions must be an object", source_name, "/extensions", "extensions"));
        }

	auto id = case_identity(*case_ptr.value(), source_name);
	if (!id) {
		return tsunami::core::failure<CaseConfiguration>(id.error());
	}
	auto scen = scenario(*scenario_ptr.value(), source_name);
	if (!scen) {
		return tsunami::core::failure<CaseConfiguration>(scen.error());
	}
	auto frame = coordinate_frame(*frame_ptr.value(), source_name);
	if (!frame) {
		return tsunami::core::failure<CaseConfiguration>(frame.error());
	}
	auto data = datasets(*datasets_ptr.value(), source_name);
	if (!data) {
		return tsunami::core::failure<CaseConfiguration>(data.error());
	}
	auto r2d = regional_2d(*regional_ptr.value(), source_name);
	if (!r2d) {
		return tsunami::core::failure<CaseConfiguration>(r2d.error());
	}
        auto out = outputs(*outputs_ptr.value(), r2d.value().numerics.final_time_s, source_name);
        if (!out) {
            return tsunami::core::failure<CaseConfiguration>(out.error());
        }
        return make_case_configuration(
            SchemaIdentity{std::string{case_configuration_schema_name}, version.value()},
            compatibility,
            std::move(policy).value(),
            std::move(id).value(),
            std::move(scen).value(),
            std::move(frame).value(),
            std::move(data).value(),
            std::move(r2d).value(),
            std::move(out).value(),
            extensions(*extensions_ptr.value()));
    }

    auto read_case_configuration(const std::filesystem::path &path) -> tsunami::core::Result<CaseConfiguration>
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return tsunami::core::failure<CaseConfiguration>(file_error(
                "data.case_config.file_open_failed",
                "could not open case configuration file",
                path));
        }
        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        if (size < 0) {
            return tsunami::core::failure<CaseConfiguration>(file_error("data.case_config.file_read_failed", "could not determine case configuration file size", path));
        }
        if (static_cast<std::uint64_t>(size) > max_case_configuration_bytes) {
            auto error = file_error("data.case_config.file_too_large", "case configuration file exceeds the G1 size limit", path);
            error.add_context("expected", std::to_string(max_case_configuration_bytes))
                .add_context("actual", std::to_string(size));
            return tsunami::core::failure<CaseConfiguration>(std::move(error));
        }
        file.seekg(0, std::ios::beg);
        std::string bytes(static_cast<std::size_t>(size), '\0');
        file.read(bytes.data(), size);
        if (!file && size > 0) {
            return tsunami::core::failure<CaseConfiguration>(file_error("data.case_config.file_read_failed", "could not read complete case configuration file", path));
        }
        return parse_case_configuration(bytes, path.generic_string());
    }

} // namespace tsunami::data
