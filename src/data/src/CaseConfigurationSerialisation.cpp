#include <tsunami/data/CaseConfigurationSerialisation.hpp>

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include <tsunami/data/CaseConfigurationValidation.hpp>

namespace tsunami::data
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        [[nodiscard]] auto io_error(std::string code, std::string message, const std::filesystem::path &path) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::configuration,
                tsunami::core::Severity::error};
            error.add_context("operation", "write_case_configuration")
                .add_context("path", path.generic_string())
                .add_context("state_changed", "false");
            return error;
        }

	auto optional_string(const std::optional<std::string> &value) -> Json
	{
		return value ? Json(*value) : Json(nullptr);
	}

	auto optional_double(const std::optional<double> &value) -> Json
	{
		return value ? Json(*value) : Json(nullptr);
	}

        auto append_lf(std::string text) -> std::string
        {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                text.pop_back();
            }
            text.push_back('\n');
            return text;
        }

        auto configuration_json(const CaseConfiguration &configuration) -> Json
        {
            const auto &id = configuration.identity();
            const auto &scenario = configuration.scenario();
            const auto &frame = configuration.coordinate_frame();
            const auto &datasets = configuration.datasets();
            const auto &r2d = configuration.regional_2d();
            const auto &corridor = r2d.corridor;
            const auto &physics = r2d.physics;
            const auto &numerics = r2d.numerics;
            const auto &boundaries = r2d.boundaries;
            const auto &relaxation = r2d.relaxation;
            const auto &outputs = configuration.outputs();

            Json root = Json::object();
            root["schema_version"] = configuration.schema_identity().version.text();
            root["policy_version"] = std::string{configuration.policy_version()};
            root["case"] = Json::object({
                {"case_id", id.case_id.str()},
                {"case_slug", id.case_slug},
                {"revision", id.revision},
                {"created_at_utc", id.created_at_utc},
                {"created_by", id.created_by}});
            root["scenario"] = Json::object({
                {"scenario_id", scenario.scenario_id},
                {"event_id", scenario.event_id},
                {"target_site", scenario.target_site},
                {"model_family", std::string{to_string(scenario.model_family)}}});
            root["coordinate_frame"] = Json::object({
                {"horizontal_crs", frame.horizontal_crs},
                {"vertical_datum", frame.vertical_datum},
                {"horizontal_unit", frame.horizontal_unit},
                {"vertical_unit", frame.vertical_unit},
                {"axis_order", std::string{to_string(frame.axis_order)}},
                {"vertical_positive", std::string{to_string(frame.vertical_positive)}}});
            root["datasets"] = Json::object({
                {"manifest_path", datasets.manifest_path.generic_string()},
                {"bindings", Json::object({
                                 {"bathymetry", datasets.bathymetry},
                                 {"topography", datasets.topography},
                                 {"earthquake_displacement", optional_string(datasets.earthquake_displacement)},
                                 {"prescribed_surface", optional_string(datasets.prescribed_surface)},
                                 {"manning", optional_string(datasets.manning)},
                                 {"coriolis", optional_string(datasets.coriolis)},
                                 {"observations", datasets.observations}})}});
            root["regional_2d"] = Json::object({
                {"corridor", Json::object({
                                 {"trajectory_id", corridor.trajectory_id},
                                 {"origin", Json::object({{"x", corridor.origin.x}, {"y", corridor.origin.y}})},
                                 {"bearing_degrees_clockwise_from_north", corridor.bearing_degrees_clockwise_from_north},
                                 {"width_m", corridor.width_m},
                                 {"offshore_extent_m", corridor.offshore_extent_m},
                                 {"inland_extent_m", corridor.inland_extent_m},
                                 {"narrowing", Json::object({
                                                   {"enabled", corridor.narrowing.enabled},
                                                   {"inland_width_m", optional_double(corridor.narrowing.inland_width_m)}})},
                                 {"sponge", Json::object({
                                                {"offshore_width_m", corridor.sponge.offshore_width_m},
                                                {"side_width_m", corridor.sponge.side_width_m}})}})},
                {"physics", Json::object({
                                {"gravity_m_per_s2", physics.gravity_m_per_s2},
                                {"manning", Json::object({
                                                {"kind", std::string{to_string(physics.manning.kind)}},
                                                {"value_s_per_m_one_third", optional_double(physics.manning.value_s_per_m_one_third)},
                                                {"dataset_binding", optional_string(physics.manning.dataset_binding)}})},
                                {"coriolis", Json::object({
                                                 {"kind", std::string{to_string(physics.coriolis.kind)}},
                                                 {"value_per_s", optional_double(physics.coriolis.value_per_s)},
                                                 {"dataset_binding", optional_string(physics.coriolis.dataset_binding)}})},
                                {"earthquake", Json::object({
                                                   {"enabled", physics.earthquake.enabled},
                                                   {"displacement_binding", optional_string(physics.earthquake.displacement_binding)},
                                                   {"bed_mapping", std::string{to_string(physics.earthquake.bed_mapping)}},
                                                   {"surface_transfer", std::string{to_string(physics.earthquake.surface_transfer)}},
                                                   {"prescribed_surface_binding", optional_string(physics.earthquake.prescribed_surface_binding)}})}})},
                {"numerics", Json::object({
                                 {"scheme", std::string{to_string(numerics.scheme)}},
                                 {"courant_number", numerics.courant_number},
                                 {"positivity_safety_factor", numerics.positivity_safety_factor},
                                 {"relaxation_safety_factor", numerics.relaxation_safety_factor},
                                 {"source_safety_factor", numerics.source_safety_factor},
                                 {"minimum_timestep_s", numerics.minimum_timestep_s},
                                 {"maximum_timestep_s", numerics.maximum_timestep_s},
                                 {"final_time_s", numerics.final_time_s},
                                 {"maximum_steps", numerics.maximum_steps}})},
                {"boundaries", Json::object({
                                   {"offshore", Json::object({{"kind", std::string{to_string(boundaries.offshore)}}})},
                                   {"inland", Json::object({{"kind", std::string{to_string(boundaries.inland)}}})},
                                   {"left_side", Json::object({{"kind", std::string{to_string(boundaries.left_side)}}})},
                                   {"right_side", Json::object({{"kind", std::string{to_string(boundaries.right_side)}}})},
                                   {"relaxation", Json::object({
                                                      {"enabled", relaxation.enabled},
                                                      {"maximum_rate_per_s", optional_double(relaxation.maximum_rate_per_s)},
                                                      {"profile_exponent", optional_double(relaxation.profile_exponent)}})}})}});
            root["outputs"] = Json::object({
                {"snapshot_interval_s", optional_double(outputs.snapshot_interval_s)},
                {"diagnostics_enabled", outputs.diagnostics_enabled},
                {"initialisation_diagnostics_enabled", outputs.initialisation_diagnostics_enabled},
                {"checkpoint_interval_s", optional_double(outputs.checkpoint_interval_s)}});
            Json extension_object = Json::object();
            for (const auto &extension : configuration.extensions().values) {
                extension_object[extension.name] = Json::parse(extension.canonical_json);
            }
            root["extensions"] = std::move(extension_object);
            return root;
        }
    } // namespace

    auto serialise_case_configuration(const CaseConfiguration &configuration) -> tsunami::core::Result<std::string>
    {
        auto validation = validate_case_configuration(configuration);
        if (!validation) {
            return tsunami::core::failure<std::string>(validation.error());
        }
        try {
            return tsunami::core::success(append_lf(configuration_json(configuration).dump(2)));
        } catch (const nlohmann::json::exception &error) {
            auto diagnostic = tsunami::core::Error{
                "data.case_config.serialisation_failed",
                "case configuration serialisation failed",
                tsunami::core::DiagnosticCategory::configuration,
                tsunami::core::Severity::error};
            diagnostic.add_context("operation", "serialise_case_configuration")
                .add_context("parser_detail", error.what())
                .add_context("state_changed", "false");
            return tsunami::core::failure<std::string>(std::move(diagnostic));
        }
    }

    auto write_case_configuration(const std::filesystem::path &path, const CaseConfiguration &configuration)
        -> tsunami::core::Result<void>
    {
        auto bytes = serialise_case_configuration(configuration);
        if (!bytes) {
            return tsunami::core::failure(bytes.error());
        }
        const auto temporary = path.parent_path() / (path.filename().generic_string() + ".tmp");
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) {
                return tsunami::core::failure(io_error("data.case_config.write_open_failed", "could not open temporary case configuration file", temporary));
            }
            file.write(bytes.value().data(), static_cast<std::streamsize>(bytes.value().size()));
            file.flush();
            if (!file) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return tsunami::core::failure(io_error("data.case_config.write_failed", "could not write complete case configuration file", temporary));
            }
        }
        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);
        if (ec) {
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(temporary, path, ec);
        }
        if (ec) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return tsunami::core::failure(io_error("data.case_config.commit_failed", "could not commit case configuration file", path));
        }
        return tsunami::core::success();
    }

} // namespace tsunami::data
