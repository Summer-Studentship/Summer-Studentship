#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/data/CaseConfigurationParsing.hpp>
#include <tsunami/data/CaseConfigurationSerialisation.hpp>
#include <tsunami/data/CaseConfigurationValidation.hpp>

namespace
{
    [[nodiscard]] auto source_root() -> std::filesystem::path
    {
        auto path = std::filesystem::path{__FILE__};
        for (auto current = path.parent_path(); !current.empty(); current = current.parent_path()) {
            if (std::filesystem::exists(current / "schemas/case/1.0.0/case.schema.json")) {
                return current;
            }
        }
        return path.parent_path();
    }

    [[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string
    {
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    [[nodiscard]] auto fixture(std::string_view category, std::string_view name) -> std::filesystem::path
    {
        return source_root() / "tests/fixtures/cases" / std::string{category} / std::string{name};
    }

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }
}

TEST_CASE("Case schema compatibility classification is stable", "[data][case configuration][case schema]")
{
    using tsunami::data::CaseSchemaCompatibility;
    const auto exact = tsunami::data::parse_semantic_version("1.0.0");
    REQUIRE(exact.has_value());
    REQUIRE(tsunami::data::classify_case_schema_version(exact.value()) == CaseSchemaCompatibility::exact);
    REQUIRE(tsunami::data::classify_case_schema_version(tsunami::core::SemanticVersion{1, 0, 1}) == CaseSchemaCompatibility::patch_equivalent);
    REQUIRE(tsunami::data::classify_case_schema_version(tsunami::core::SemanticVersion{1, 1, 0}) == CaseSchemaCompatibility::forward_compatible_minor);
    REQUIRE(tsunami::data::classify_case_schema_version(tsunami::core::SemanticVersion{1, 99, 4}) == CaseSchemaCompatibility::forward_compatible_minor);
    REQUIRE(tsunami::data::classify_case_schema_version(tsunami::core::SemanticVersion{0, 1, 0}) == CaseSchemaCompatibility::migration_required);
    REQUIRE(tsunami::data::classify_case_schema_version(tsunami::core::SemanticVersion{2, 0, 0}) == CaseSchemaCompatibility::unsupported_major);
    REQUIRE(tsunami::data::to_string(CaseSchemaCompatibility::forward_compatible_minor) == "forward_compatible_minor");
    REQUIRE_FALSE(tsunami::data::parse_semantic_version("1.0").has_value());
    REQUIRE_FALSE(tsunami::data::parse_semantic_version("1.0.0 ").has_value());
    REQUIRE_FALSE(tsunami::data::parse_semantic_version("1.-1.0").has_value());
}

TEST_CASE("Case schema document is committed as JSON syntax", "[data][case configuration][case schema]")
{
    const auto schema = read_text(source_root() / "schemas/case/1.0.0/case.schema.json");
    REQUIRE(schema.find("\"$schema\"") != std::string::npos);
    REQUIRE(schema.find("https://json-schema.org/draft/2020-12/schema") != std::string::npos);
    REQUIRE(schema.find("Tsunami Barrier Studio G1 Case Configuration") != std::string::npos);
    REQUIRE(schema.find("\"schema_version\"") != std::string::npos);
    REQUIRE(schema.find("\"regional_2d\"") != std::string::npos);
}

TEST_CASE("Valid case configuration fixtures parse and serialise canonically", "[data][case configuration][case fixtures]")
{
	for (const auto name : {
	         "minimal_regional_2d.json",
	         "regional_2d_with_uniform_sources.json",
	         "regional_2d_with_dataset_sources.json",
	         "regional_2d_prescribed_surface.json",
	         "forward_minor_with_extensions.json"}) {
		INFO("fixture: " << name);
		const auto document = read_text(fixture("valid", name));
		auto parsed = tsunami::data::parse_case_configuration(document, std::string{name});
		if (!parsed) {
			INFO("parse error: " << parsed.error().code());
		}
		REQUIRE(parsed.has_value());
		auto bytes = tsunami::data::serialise_case_configuration(parsed.value());
		if (!bytes) {
			INFO("serialise error: " << bytes.error().code());
		}
		REQUIRE(bytes.has_value());
        REQUIRE_FALSE(bytes.value().empty());
		REQUIRE(bytes.value().back() == '\n');
		REQUIRE(bytes.value().find("\r") == std::string::npos);
		REQUIRE(bytes.value().find("  \"schema_version\"") != std::string::npos);
		auto reparsed = tsunami::data::parse_case_configuration(bytes.value(), std::string{name} + "-canonical");
		if (!reparsed) {
			INFO("canonical parse error: " << reparsed.error().code());
		}
		REQUIRE(reparsed.has_value());
        REQUIRE(reparsed.value() == parsed.value());
        auto bytes_again = tsunami::data::serialise_case_configuration(reparsed.value());
        REQUIRE(bytes_again.has_value());
        REQUIRE(bytes_again.value() == bytes.value());
    }

    const auto forward = tsunami::data::parse_case_configuration(read_text(fixture("valid", "forward_minor_with_extensions.json")), "forward");
    REQUIRE(forward.has_value());
    REQUIRE(forward.value().compatibility() == tsunami::data::CaseSchemaCompatibility::forward_compatible_minor);
    REQUIRE(forward.value().extensions().values.front().name == "alpha_extension");
    REQUIRE(forward.value().extensions().values.back().name == "zeta_extension");
}

TEST_CASE("Invalid and migration fixtures return deterministic diagnostics", "[data][case configuration][case validation][migration]")
{
    struct Expectation
    {
        std::string_view category;
        std::string_view name;
        std::string_view code;
        std::string_view pointer;
    };
    const auto expectations = std::vector<Expectation>{
        {"invalid", "malformed_json.json", "data.case_config.json_invalid", ""},
        {"invalid", "root_array.json", "data.case_config.root_type_invalid", "/"},
        {"invalid", "missing_schema_version.json", "data.case_config.required_field_missing", "/schema_version"},
        {"invalid", "invalid_schema_version.json", "data.case_config.schema_version_invalid", "/schema_version"},
        {"invalid", "unsupported_major.json", "data.case_config.schema_major_unsupported", "/schema_version"},
        {"invalid", "unknown_top_level_field.json", "data.case_config.unknown_field", "/typo"},
        {"invalid", "unknown_core_field.json", "data.case_config.unknown_field", "/case/typo"},
	        {"invalid", "invalid_case_slug.json", "data.case_config.identifier_invalid", "/case/case_slug"},
	        {"migration", "legacy_0_1_0.json", "data.case_config.migration_required", "/schema_version"}};
	for (const auto &expectation : expectations) {
		INFO("fixture: " << expectation.category << "/" << expectation.name);
		auto parsed = tsunami::data::parse_case_configuration(read_text(fixture(expectation.category, expectation.name)), std::string{expectation.name});
        REQUIRE_FALSE(parsed.has_value());
        REQUIRE(parsed.error().code() == expectation.code);
        if (!expectation.pointer.empty()) {
            REQUIRE(context_value(parsed.error(), "json_pointer") == expectation.pointer);
        }
        REQUIRE(context_value(parsed.error(), "state_changed") == "false");
	}
}

TEST_CASE("All invalid case configuration fixtures are rejected without exceptions", "[data][case configuration][case fixtures]")
{
	const auto invalid_dir = source_root() / "tests/fixtures/cases/invalid";
	auto count = std::size_t{0U};
	for (const auto &entry : std::filesystem::directory_iterator{invalid_dir}) {
		if (!entry.is_regular_file() || entry.path().extension() != ".json") {
			continue;
		}
		INFO("fixture: " << entry.path().filename().generic_string());
		auto parsed = tsunami::data::parse_case_configuration(read_text(entry.path()), entry.path().filename().generic_string());
		REQUIRE_FALSE(parsed.has_value());
		REQUIRE(context_value(parsed.error(), "state_changed") == "false");
		++count;
	}
	REQUIRE(count >= 40U);
}

TEST_CASE("Case configuration read and write are transactional", "[data][case configuration][case serialisation]")
{
    auto parsed = tsunami::data::parse_case_configuration(read_text(fixture("valid", "minimal_regional_2d.json")), "minimal");
    REQUIRE(parsed.has_value());
    const auto dir = std::filesystem::temp_directory_path() / "tsunami-case-configuration-tests";
    std::filesystem::create_directories(dir);
    const auto output = dir / "case.json";
    REQUIRE(tsunami::data::write_case_configuration(output, parsed.value()).has_value());
    auto read_back = tsunami::data::read_case_configuration(output);
    REQUIRE(read_back.has_value());
    REQUIRE(read_back.value() == parsed.value());

    const auto original = read_text(output);
    auto bad_schema = parsed.value().schema_identity();
    bad_schema.version = tsunami::core::SemanticVersion{2, 0, 0};
    auto invalid = tsunami::data::make_case_configuration(
        bad_schema,
        tsunami::data::CaseSchemaCompatibility::unsupported_major,
        std::string{parsed.value().policy_version()},
        parsed.value().identity(),
        parsed.value().scenario(),
        parsed.value().coordinate_frame(),
        parsed.value().datasets(),
        parsed.value().regional_2d(),
        parsed.value().outputs(),
        parsed.value().extensions());
    REQUIRE_FALSE(invalid.has_value());
    REQUIRE(read_text(output) == original);
}

TEST_CASE("Case configuration programmatic construction rejects duplicate extensions", "[data][case configuration][case validation]")
{
    auto parsed = tsunami::data::parse_case_configuration(read_text(fixture("valid", "minimal_regional_2d.json")), "minimal");
    REQUIRE(parsed.has_value());
    auto extensions = tsunami::data::CaseExtensions{{{"duplicate", "{}"}, {"duplicate", "[]"}}};
    auto result = tsunami::data::make_case_configuration(
        parsed.value().schema_identity(),
        parsed.value().compatibility(),
        std::string{parsed.value().policy_version()},
        parsed.value().identity(),
        parsed.value().scenario(),
        parsed.value().coordinate_frame(),
        parsed.value().datasets(),
        parsed.value().regional_2d(),
        parsed.value().outputs(),
        std::move(extensions));
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code() == "data.case_config.extension_invalid");
}

TEST_CASE("Data public headers keep parser and solver types private", "[data][case configuration][contracts]")
{
    const auto data_include = source_root() / "src/data/include/tsunami/data";
    const auto forbidden = std::vector<std::string>{
        "nlohmann::",
        "yaml-cpp",
        "YAML::",
        "QObject",
        "QString",
        "QVariant",
        "CLI::",
        "H5::",
        "hid_t",
        "GDAL",
        "OGR",
        "PROJ",
        "Gmsh",
        "tsunami::r2d",
        "tsunami::l3d"};
    for (const auto &entry : std::filesystem::directory_iterator(data_include)) {
        if (entry.path().extension() != ".hpp") {
            continue;
        }
        const auto text = read_text(entry.path());
        for (const auto &token : forbidden) {
            REQUIRE(text.find(token) == std::string::npos);
        }
    }
}
