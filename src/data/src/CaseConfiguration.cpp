#include <tsunami/data/CaseConfiguration.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <regex>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include <tsunami/data/CaseConfigurationValidation.hpp>

namespace tsunami::data
{
    namespace
    {
        constexpr auto max_identifier_length = std::size_t{128U};

        [[nodiscard]] auto data_error(
            std::string code,
            std::string message,
            std::string rule_id,
            std::string pointer = {},
            std::string field = {}) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::configuration,
                tsunami::core::Severity::error};
            error.add_context("operation", "validate_case_configuration")
                .add_context("rule_id", std::move(rule_id))
                .add_context("state_changed", "false");
            if (!pointer.empty()) {
                error.add_context("json_pointer", std::move(pointer));
            }
            if (!field.empty()) {
                error.add_context("field", std::move(field));
            }
            return error;
        }

        [[nodiscard]] auto has_embedded_null(std::string_view text) -> bool
        {
            return text.find('\0') != std::string_view::npos;
        }

        [[nodiscard]] auto matches(std::string_view text, const char *pattern) -> bool
        {
            return std::regex_match(text.begin(), text.end(), std::regex{pattern});
        }

        [[nodiscard]] auto logical_id_valid(std::string_view text) -> bool
        {
            return !text.empty() && text.size() <= max_identifier_length && !has_embedded_null(text) &&
                   matches(text, R"([a-z0-9]+(?:[._-][a-z0-9]+)*)");
        }

        [[nodiscard]] auto slug_valid(std::string_view text) -> bool
        {
            return !text.empty() && text.size() <= max_identifier_length && !has_embedded_null(text) &&
                   matches(text, R"([a-z0-9]+(?:-[a-z0-9]+)*)");
        }

        [[nodiscard]] auto finite(double value) -> bool
        {
            return std::isfinite(value);
        }

        [[nodiscard]] auto leap_year(int year) -> bool
        {
            return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        }

        [[nodiscard]] auto timestamp_valid(std::string_view text) -> bool
        {
            static const auto re = std::regex{R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})Z$)"};
            std::cmatch match;
            if (!std::regex_match(text.begin(), text.end(), match, re)) {
                return false;
            }
            const auto year = std::stoi(match[1].str());
            const auto month = std::stoi(match[2].str());
            const auto day = std::stoi(match[3].str());
            const auto hour = std::stoi(match[4].str());
            const auto minute = std::stoi(match[5].str());
            const auto second = std::stoi(match[6].str());
            if (month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) {
                return false;
            }
            const auto days = std::array<int, 12>{31, leap_year(year) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return day >= 1 && day <= days[static_cast<std::size_t>(month - 1)];
        }

        [[nodiscard]] auto path_safe(const std::filesystem::path &path) -> bool
        {
            const auto generic = path.generic_string();
            if (generic.empty() || has_embedded_null(generic) || path.is_absolute() ||
                path.has_root_name() || path.has_root_directory() || path.filename().empty()) {
                return false;
            }
            if (generic.find('\\') != std::string::npos || generic.rfind("manifests/", 0U) != 0U) {
                return false;
            }
            for (const auto &part : path) {
                const auto piece = part.generic_string();
                if (piece.empty() || piece == "." || piece == "..") {
                    return false;
                }
            }
            return path.lexically_normal().generic_string() == generic;
        }

        [[nodiscard]] auto validate_dataset_id(
            const std::optional<std::string> &value,
            std::string pointer,
            std::string rule) -> tsunami::core::Result<void>
        {
            if (value && !logical_id_valid(*value)) {
                return tsunami::core::failure(data_error(
                    "data.case_config.dataset_binding_invalid",
                    "dataset binding identifier is invalid",
                    std::move(rule),
                    std::move(pointer)));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto require_binding_equal(
            const std::optional<std::string> &actual,
            const std::optional<std::string> &expected,
            std::string pointer,
            std::string rule) -> tsunami::core::Result<void>
        {
            if (!actual) {
                return tsunami::core::failure(data_error(
                    "data.case_config.dataset_binding_missing",
                    "required dataset binding is missing",
                    rule,
                    pointer));
            }
            if (!expected || *actual != *expected) {
                return tsunami::core::failure(data_error(
                    "data.case_config.dataset_binding_invalid",
                    "dataset binding does not match the datasets section",
                    std::move(rule),
                    std::move(pointer)));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto extension_json_valid(std::string_view text) -> bool
        {
            try {
                const auto parsed = nlohmann::json::parse(text.begin(), text.end());
                static_cast<void>(parsed);
            } catch (const nlohmann::json::exception &) {
                return false;
            }
            return true;
        }

        auto validate_extensions(const CaseExtensions &extensions) -> tsunami::core::Result<void>
        {
            auto names = std::set<std::string>{};
            for (const auto &extension : extensions.values) {
                if (extension.name.empty() || has_embedded_null(extension.name)) {
                    return tsunami::core::failure(data_error(
                        "data.case_config.extension_invalid",
                        "extension name is invalid",
                        "case.extensions.unique",
                        "/extensions"));
                }
                if (!names.insert(extension.name).second) {
                    return tsunami::core::failure(data_error(
                        "data.case_config.extension_invalid",
                        "extension names must be unique",
                        "case.extensions.unique",
                        "/extensions"));
                }
                if (!extension_json_valid(extension.canonical_json)) {
                    return tsunami::core::failure(data_error(
                        "data.case_config.extension_invalid",
                        "extension JSON must be valid",
                        "case.extensions.unique",
                        "/extensions/" + extension.name));
                }
            }
            return tsunami::core::success();
        }
    } // namespace

    auto to_string(CaseSchemaCompatibility compatibility) noexcept -> std::string_view
    {
        switch (compatibility) {
        case CaseSchemaCompatibility::exact:
            return "exact";
        case CaseSchemaCompatibility::patch_equivalent:
            return "patch_equivalent";
        case CaseSchemaCompatibility::forward_compatible_minor:
            return "forward_compatible_minor";
        case CaseSchemaCompatibility::migration_required:
            return "migration_required";
        case CaseSchemaCompatibility::unsupported_major:
            return "unsupported_major";
        }
        return "unsupported_major";
    }

    auto to_string(CaseModelFamily) noexcept -> std::string_view { return "regional_2d"; }
    auto to_string(HorizontalAxisOrder) noexcept -> std::string_view { return "east_north"; }
    auto to_string(VerticalPositiveDirection) noexcept -> std::string_view { return "up"; }

    auto to_string(ManningConfigurationKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case ManningConfigurationKind::disabled:
            return "disabled";
        case ManningConfigurationKind::uniform:
            return "uniform";
        case ManningConfigurationKind::dataset:
            return "dataset";
        }
        return "disabled";
    }

    auto to_string(CoriolisConfigurationKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case CoriolisConfigurationKind::disabled:
            return "disabled";
        case CoriolisConfigurationKind::constant:
            return "constant";
        case CoriolisConfigurationKind::dataset:
            return "dataset";
        }
        return "disabled";
    }

    auto to_string(BedDeformationMapping mapping) noexcept -> std::string_view
    {
        switch (mapping) {
        case BedDeformationMapping::vertical_only:
            return "vertical_only";
        case BedDeformationMapping::horizontal_slope_corrected:
            return "horizontal_slope_corrected";
        }
        return "vertical_only";
    }

    auto to_string(SurfaceTransfer transfer) noexcept -> std::string_view
    {
        switch (transfer) {
        case SurfaceTransfer::passive_equal_to_effective_bed:
            return "passive_equal_to_effective_bed";
        case SurfaceTransfer::prescribed:
            return "prescribed";
        }
        return "passive_equal_to_effective_bed";
    }

    auto to_string(RegionalTimeScheme scheme) noexcept -> std::string_view
    {
        switch (scheme) {
        case RegionalTimeScheme::forward_euler:
            return "forward_euler";
        case RegionalTimeScheme::ssprk2:
            return "ssprk2";
        case RegionalTimeScheme::ssprk3:
            return "ssprk3";
        }
        return "ssprk3";
    }

    auto to_string(RegionalBoundaryKind kind) noexcept -> std::string_view
    {
        switch (kind) {
        case RegionalBoundaryKind::transmissive:
            return "transmissive";
        case RegionalBoundaryKind::radiation:
            return "radiation";
        }
        return "radiation";
    }

    CaseConfiguration::CaseConfiguration(
        SchemaIdentity schema,
        CaseSchemaCompatibility compatibility,
        std::string policy_version,
        CaseIdentity identity,
        ScenarioConfiguration scenario,
        CoordinateFrameConfiguration coordinate_frame,
        DatasetBindings datasets,
        Regional2DCaseConfiguration regional_2d,
        CaseOutputConfiguration outputs,
        CaseExtensions extensions)
        : schema_{std::move(schema)}
        , compatibility_{compatibility}
        , policy_version_{std::move(policy_version)}
        , identity_{std::move(identity)}
        , scenario_{std::move(scenario)}
        , coordinate_frame_{std::move(coordinate_frame)}
        , datasets_{std::move(datasets)}
        , regional_2d_{std::move(regional_2d)}
        , outputs_{std::move(outputs)}
        , extensions_{std::move(extensions)}
    {
    }

    auto validate_case_configuration(const CaseConfiguration &configuration) -> tsunami::core::Result<void>
    {
        if (configuration.schema_identity().schema_name != case_configuration_schema_name) {
            return tsunami::core::failure(data_error(
                "data.case_config.schema_version_invalid",
                "schema name is not supported",
                "case.schema_version.required",
                "/schema_version",
                "schema_version"));
        }
        const auto compatibility = classify_case_schema_version(configuration.schema_identity().version);
        if (compatibility == CaseSchemaCompatibility::migration_required) {
            return tsunami::core::failure(data_error(
                "data.case_config.migration_required",
                "legacy case schema requires migration",
                "case.schema_version.compatible",
                "/schema_version"));
        }
        if (compatibility == CaseSchemaCompatibility::unsupported_major) {
            return tsunami::core::failure(data_error(
                "data.case_config.schema_major_unsupported",
                "case schema major version is unsupported",
                "case.schema_version.compatible",
                "/schema_version"));
        }
        if (configuration.compatibility() != compatibility) {
            return tsunami::core::failure(data_error(
                "data.case_config.schema_version_invalid",
                "stored compatibility does not match schema version",
                "case.schema_version.compatible",
                "/schema_version"));
        }
        if (configuration.policy_version() != supported_case_policy_version) {
            return tsunami::core::failure(data_error(
                "data.case_config.policy_version_invalid",
                "case policy version is unsupported",
                "case.policy_version.supported",
                "/policy_version",
                "policy_version"));
        }

        const auto &identity = configuration.identity();
        if (!identity.case_id || !logical_id_valid(identity.case_id.str())) {
            return tsunami::core::failure(data_error("data.case_config.identifier_invalid", "case id is invalid", "case.identity.case_id.valid", "/case/case_id", "case_id"));
        }
        if (!slug_valid(identity.case_slug)) {
            return tsunami::core::failure(data_error("data.case_config.identifier_invalid", "case slug is invalid", "case.identity.slug.valid", "/case/case_slug", "case_slug"));
        }
        if (identity.revision == 0U) {
            return tsunami::core::failure(data_error("data.case_config.identifier_invalid", "case revision must be positive", "case.identity.revision.positive", "/case/revision", "revision"));
        }
        if (!timestamp_valid(identity.created_at_utc)) {
            return tsunami::core::failure(data_error("data.case_config.timestamp_invalid", "created_at_utc must be canonical UTC", "case.identity.created_at.utc", "/case/created_at_utc", "created_at_utc"));
        }
        if (identity.created_by.empty() || has_embedded_null(identity.created_by)) {
            return tsunami::core::failure(data_error("data.case_config.identifier_invalid", "created_by is invalid", "case.identity.created_by.valid", "/case/created_by", "created_by"));
        }

        const auto &scenario = configuration.scenario();
        if (!logical_id_valid(scenario.scenario_id) || !logical_id_valid(scenario.event_id) || !logical_id_valid(scenario.target_site)) {
            return tsunami::core::failure(data_error("data.case_config.identifier_invalid", "scenario identifiers are invalid", "case.scenario.identifiers.valid", "/scenario"));
        }

        const auto &frame = configuration.coordinate_frame();
        if (frame.horizontal_crs.empty() || has_embedded_null(frame.horizontal_crs)) {
            return tsunami::core::failure(data_error("data.case_config.coordinate_frame_invalid", "horizontal CRS is required", "case.coordinate_frame.horizontal_crs.required", "/coordinate_frame/horizontal_crs"));
        }
        if (frame.vertical_datum.empty() || has_embedded_null(frame.vertical_datum)) {
            return tsunami::core::failure(data_error("data.case_config.coordinate_frame_invalid", "vertical datum is required", "case.coordinate_frame.vertical_datum.required", "/coordinate_frame/vertical_datum"));
        }
        if (frame.horizontal_unit != "m" || frame.vertical_unit != "m") {
            return tsunami::core::failure(data_error("data.case_config.coordinate_frame_invalid", "horizontal and vertical units must be metres", "case.coordinate_frame.units.metres", "/coordinate_frame"));
        }

        const auto &datasets = configuration.datasets();
        if (!path_safe(datasets.manifest_path)) {
            return tsunami::core::failure(data_error("data.case_config.path_invalid", "manifest path is not a safe relative manifests path", "case.datasets.manifest_path.safe", "/datasets/manifest_path", "manifest_path"));
        }
        if (!logical_id_valid(datasets.bathymetry) || !logical_id_valid(datasets.topography)) {
            return tsunami::core::failure(data_error("data.case_config.dataset_binding_invalid", "bathymetry and topography bindings are required", "case.datasets.binding.required", "/datasets/bindings"));
        }
        for (const auto &[value, pointer] : {
                 std::pair{datasets.earthquake_displacement, std::string{"/datasets/bindings/earthquake_displacement"}},
                 std::pair{datasets.prescribed_surface, std::string{"/datasets/bindings/prescribed_surface"}},
                 std::pair{datasets.manning, std::string{"/datasets/bindings/manning"}},
                 std::pair{datasets.coriolis, std::string{"/datasets/bindings/coriolis"}}}) {
            auto valid = validate_dataset_id(value, pointer, "case.datasets.binding.required");
            if (!valid) {
                return valid;
            }
        }
        auto observations = std::set<std::string>{};
        for (const auto &observation : datasets.observations) {
            if (!logical_id_valid(observation) || !observations.insert(observation).second) {
                return tsunami::core::failure(data_error("data.case_config.dataset_binding_invalid", "observation bindings must be valid and unique", "case.datasets.observations.unique", "/datasets/bindings/observations"));
            }
        }

        const auto &corridor = configuration.regional_2d().corridor;
        if (!logical_id_valid(corridor.trajectory_id)) {
            return tsunami::core::failure(data_error("data.case_config.identifier_invalid", "trajectory id is invalid", "case.corridor.trajectory_id.valid", "/regional_2d/corridor/trajectory_id"));
        }
        if (!finite(corridor.origin.x) || !finite(corridor.origin.y) ||
            !finite(corridor.bearing_degrees_clockwise_from_north) || corridor.bearing_degrees_clockwise_from_north < 0.0 ||
            corridor.bearing_degrees_clockwise_from_north >= 360.0) {
            return tsunami::core::failure(data_error("data.case_config.corridor_invalid", "corridor origin or bearing is invalid", "case.corridor.bearing.range", "/regional_2d/corridor/bearing_degrees_clockwise_from_north"));
        }
        if (!finite(corridor.width_m) || corridor.width_m <= 0.0) {
            return tsunami::core::failure(data_error("data.case_config.corridor_invalid", "corridor width must be positive", "case.corridor.width.positive", "/regional_2d/corridor/width_m"));
        }
        if (!finite(corridor.offshore_extent_m) || corridor.offshore_extent_m <= 0.0) {
            return tsunami::core::failure(data_error("data.case_config.corridor_invalid", "offshore extent must be positive", "case.corridor.offshore_extent.positive", "/regional_2d/corridor/offshore_extent_m"));
        }
        if (!finite(corridor.inland_extent_m) || corridor.inland_extent_m < 0.0) {
            return tsunami::core::failure(data_error("data.case_config.corridor_invalid", "inland extent must be nonnegative", "case.corridor.inland_extent.nonnegative", "/regional_2d/corridor/inland_extent_m"));
        }
        if (!corridor.narrowing.enabled && corridor.narrowing.inland_width_m) {
            return tsunami::core::failure(data_error("data.case_config.narrowing_invalid", "disabled narrowing must not carry an inland width", "case.corridor.narrowing.consistent", "/regional_2d/corridor/narrowing/inland_width_m"));
        }
        if (corridor.narrowing.enabled &&
            (!corridor.narrowing.inland_width_m || !finite(*corridor.narrowing.inland_width_m) ||
             *corridor.narrowing.inland_width_m <= 0.0 || *corridor.narrowing.inland_width_m > corridor.width_m)) {
            return tsunami::core::failure(data_error("data.case_config.narrowing_invalid", "enabled narrowing width must be positive and no wider than the corridor", "case.corridor.narrowing.consistent", "/regional_2d/corridor/narrowing/inland_width_m"));
        }
        if (!finite(corridor.sponge.offshore_width_m) || corridor.sponge.offshore_width_m < 0.0 ||
            corridor.sponge.offshore_width_m >= corridor.offshore_extent_m) {
            return tsunami::core::failure(data_error("data.case_config.sponge_invalid", "offshore sponge width is invalid", "case.corridor.sponge.offshore_within_extent", "/regional_2d/corridor/sponge/offshore_width_m"));
        }
        if (!finite(corridor.sponge.side_width_m) || corridor.sponge.side_width_m < 0.0 ||
            corridor.sponge.side_width_m >= corridor.width_m / 2.0) {
            return tsunami::core::failure(data_error("data.case_config.sponge_invalid", "side sponge width is invalid", "case.corridor.sponge.side_within_half_width", "/regional_2d/corridor/sponge/side_width_m"));
        }

        const auto &physics = configuration.regional_2d().physics;
        if (!finite(physics.gravity_m_per_s2) || physics.gravity_m_per_s2 <= 0.0) {
            return tsunami::core::failure(data_error("data.case_config.gravity_invalid", "gravity must be positive", "case.physics.gravity.positive", "/regional_2d/physics/gravity_m_per_s2"));
        }
        if (physics.manning.kind == ManningConfigurationKind::disabled &&
            (physics.manning.value_s_per_m_one_third || physics.manning.dataset_binding)) {
            return tsunami::core::failure(data_error("data.case_config.manning_invalid", "disabled Manning must have null values", "case.physics.manning.consistent", "/regional_2d/physics/manning"));
        }
        if (physics.manning.kind == ManningConfigurationKind::uniform &&
            (!physics.manning.value_s_per_m_one_third || !finite(*physics.manning.value_s_per_m_one_third) ||
             *physics.manning.value_s_per_m_one_third < 0.0 || physics.manning.dataset_binding)) {
            return tsunami::core::failure(data_error("data.case_config.manning_invalid", "uniform Manning requires a finite nonnegative value only", "case.physics.manning.consistent", "/regional_2d/physics/manning"));
        }
        if (physics.manning.kind == ManningConfigurationKind::dataset) {
            auto match = require_binding_equal(physics.manning.dataset_binding, datasets.manning, "/regional_2d/physics/manning/dataset_binding", "case.physics.manning.consistent");
            if (!match) {
                return match;
            }
            if (physics.manning.value_s_per_m_one_third) {
                return tsunami::core::failure(data_error("data.case_config.manning_invalid", "dataset Manning must not also carry a uniform value", "case.physics.manning.consistent", "/regional_2d/physics/manning/value_s_per_m_one_third"));
            }
        }
        if (physics.coriolis.kind == CoriolisConfigurationKind::disabled &&
            (physics.coriolis.value_per_s || physics.coriolis.dataset_binding)) {
            return tsunami::core::failure(data_error("data.case_config.coriolis_invalid", "disabled Coriolis must have null values", "case.physics.coriolis.consistent", "/regional_2d/physics/coriolis"));
        }
        if (physics.coriolis.kind == CoriolisConfigurationKind::constant &&
            (!physics.coriolis.value_per_s || !finite(*physics.coriolis.value_per_s) || physics.coriolis.dataset_binding)) {
            return tsunami::core::failure(data_error("data.case_config.coriolis_invalid", "constant Coriolis requires a finite value only", "case.physics.coriolis.consistent", "/regional_2d/physics/coriolis"));
        }
        if (physics.coriolis.kind == CoriolisConfigurationKind::dataset) {
            auto match = require_binding_equal(physics.coriolis.dataset_binding, datasets.coriolis, "/regional_2d/physics/coriolis/dataset_binding", "case.physics.coriolis.consistent");
            if (!match) {
                return match;
            }
            if (physics.coriolis.value_per_s) {
                return tsunami::core::failure(data_error("data.case_config.coriolis_invalid", "dataset Coriolis must not also carry a constant value", "case.physics.coriolis.consistent", "/regional_2d/physics/coriolis/value_per_s"));
            }
        }
        if (!physics.earthquake.enabled && (physics.earthquake.displacement_binding || physics.earthquake.prescribed_surface_binding)) {
            return tsunami::core::failure(data_error("data.case_config.earthquake_invalid", "disabled earthquake must not carry bindings", "case.physics.earthquake.consistent", "/regional_2d/physics/earthquake"));
        }
        if (physics.earthquake.enabled) {
            auto displacement = require_binding_equal(physics.earthquake.displacement_binding, datasets.earthquake_displacement, "/regional_2d/physics/earthquake/displacement_binding", "case.physics.earthquake.consistent");
            if (!displacement) {
                return displacement;
            }
            if (physics.earthquake.surface_transfer == SurfaceTransfer::passive_equal_to_effective_bed && physics.earthquake.prescribed_surface_binding) {
                return tsunami::core::failure(data_error("data.case_config.earthquake_invalid", "passive transfer must not carry a prescribed surface binding", "case.physics.earthquake.consistent", "/regional_2d/physics/earthquake/prescribed_surface_binding"));
            }
            if (physics.earthquake.surface_transfer == SurfaceTransfer::prescribed) {
                auto prescribed = require_binding_equal(physics.earthquake.prescribed_surface_binding, datasets.prescribed_surface, "/regional_2d/physics/earthquake/prescribed_surface_binding", "case.physics.earthquake.consistent");
                if (!prescribed) {
                    return prescribed;
                }
            }
        }

        const auto &numerics = configuration.regional_2d().numerics;
        if (!finite(numerics.courant_number) || numerics.courant_number <= 0.0 || numerics.courant_number > 1.0) {
            return tsunami::core::failure(data_error("data.case_config.numerics_invalid", "Courant number is out of range", "case.numerics.courant.range", "/regional_2d/numerics/courant_number"));
        }
        for (const auto &[value, pointer] : {
                 std::pair{numerics.positivity_safety_factor, std::string{"/regional_2d/numerics/positivity_safety_factor"}},
                 std::pair{numerics.relaxation_safety_factor, std::string{"/regional_2d/numerics/relaxation_safety_factor"}},
                 std::pair{numerics.source_safety_factor, std::string{"/regional_2d/numerics/source_safety_factor"}}}) {
            if (!finite(value) || value <= 0.0 || value > 1.0) {
                return tsunami::core::failure(data_error("data.case_config.numerics_invalid", "safety factor is out of range", "case.numerics.safety_factor.range", pointer));
            }
        }
        if (!finite(numerics.minimum_timestep_s) || !finite(numerics.maximum_timestep_s) ||
            numerics.minimum_timestep_s <= 0.0 || numerics.maximum_timestep_s < numerics.minimum_timestep_s) {
            return tsunami::core::failure(data_error("data.case_config.numerics_invalid", "timestep bounds are invalid", "case.numerics.timestep.ordered", "/regional_2d/numerics/minimum_timestep_s"));
        }
        if (!finite(numerics.final_time_s) || numerics.final_time_s <= 0.0) {
            return tsunami::core::failure(data_error("data.case_config.numerics_invalid", "final time must be positive", "case.numerics.final_time.positive", "/regional_2d/numerics/final_time_s"));
        }
        if (numerics.maximum_steps == 0U) {
            return tsunami::core::failure(data_error("data.case_config.numerics_invalid", "maximum steps must be positive", "case.numerics.maximum_steps.positive", "/regional_2d/numerics/maximum_steps"));
        }

        const auto &relaxation = configuration.regional_2d().relaxation;
        if (!relaxation.enabled && (relaxation.maximum_rate_per_s || relaxation.profile_exponent)) {
            return tsunami::core::failure(data_error("data.case_config.relaxation_invalid", "disabled relaxation must not carry parameters", "case.relaxation.consistent", "/regional_2d/boundaries/relaxation"));
        }
        if (relaxation.enabled) {
            if (!relaxation.maximum_rate_per_s || !finite(*relaxation.maximum_rate_per_s) || *relaxation.maximum_rate_per_s <= 0.0 ||
                !relaxation.profile_exponent || !finite(*relaxation.profile_exponent) || *relaxation.profile_exponent <= 0.0) {
                return tsunami::core::failure(data_error("data.case_config.relaxation_invalid", "enabled relaxation requires positive finite parameters", "case.relaxation.consistent", "/regional_2d/boundaries/relaxation"));
            }
            if (corridor.sponge.offshore_width_m <= 0.0 && corridor.sponge.side_width_m <= 0.0) {
                return tsunami::core::failure(data_error("data.case_config.relaxation_invalid", "relaxation requires at least one positive sponge width", "case.relaxation.consistent", "/regional_2d/boundaries/relaxation"));
            }
        }

        const auto &outputs = configuration.outputs();
        for (const auto &[value, pointer] : {
                 std::pair{outputs.snapshot_interval_s, std::string{"/outputs/snapshot_interval_s"}},
                 std::pair{outputs.checkpoint_interval_s, std::string{"/outputs/checkpoint_interval_s"}}}) {
            if (value && (!finite(*value) || *value <= 0.0 || *value > numerics.final_time_s)) {
                return tsunami::core::failure(data_error("data.case_config.output_invalid", "output interval must be positive and no greater than final time", "case.outputs.interval.positive", pointer));
            }
        }

        return validate_extensions(configuration.extensions());
    }

    auto make_case_configuration(
        SchemaIdentity schema,
        CaseSchemaCompatibility compatibility,
        std::string policy_version,
        CaseIdentity identity,
        ScenarioConfiguration scenario,
        CoordinateFrameConfiguration coordinate_frame,
        DatasetBindings datasets,
        Regional2DCaseConfiguration regional_2d,
        CaseOutputConfiguration outputs,
        CaseExtensions extensions) -> tsunami::core::Result<CaseConfiguration>
    {
        auto sorted = std::move(extensions);
        std::sort(
            sorted.values.begin(),
            sorted.values.end(),
            [](const auto &left, const auto &right) { return left.name < right.name; });
        auto configuration = CaseConfiguration{
            std::move(schema),
            compatibility,
            std::move(policy_version),
            std::move(identity),
            std::move(scenario),
            std::move(coordinate_frame),
            std::move(datasets),
            std::move(regional_2d),
            std::move(outputs),
            std::move(sorted)};
        auto validation = validate_case_configuration(configuration);
        if (!validation) {
            return tsunami::core::failure<CaseConfiguration>(validation.error());
        }
        return tsunami::core::success(std::move(configuration));
    }

} // namespace tsunami::data
