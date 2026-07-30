#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/data/CaseConfigurationParsing.hpp>
#include <tsunami/data/DatasetManifestParsing.hpp>
#include <tsunami/data/DatasetManifestSerialisation.hpp>
#include <tsunami/data/DatasetManifestValidation.hpp>

namespace
{
    [[nodiscard]] auto source_root() -> std::filesystem::path
    {
        auto path = std::filesystem::path{__FILE__};
        for (auto current = path.parent_path(); !current.empty(); current = current.parent_path()) {
            if (std::filesystem::exists(current / "schemas/dataset_manifest/1.0.0/dataset_manifest.schema.json")) {
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

    [[nodiscard]] auto manifest_fixture(std::string_view category, std::string_view name) -> std::filesystem::path
    {
        return source_root() / "tests/fixtures/manifests" / std::string{category} / std::string{name};
    }

    [[nodiscard]] auto case_fixture(std::string_view category, std::string_view name) -> std::filesystem::path
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

TEST_CASE("Dataset manifest schema compatibility classification is stable", "[data][dataset manifest][schema]")
{
    using tsunami::data::DatasetManifestCompatibility;
    const auto exact = tsunami::data::parse_dataset_manifest_version("1.0.0");
    REQUIRE(exact.has_value());
    REQUIRE(tsunami::data::classify_dataset_manifest_version(exact.value()) == DatasetManifestCompatibility::exact);
    REQUIRE(tsunami::data::classify_dataset_manifest_version(tsunami::core::SemanticVersion{1, 0, 1}) == DatasetManifestCompatibility::patch_equivalent);
    REQUIRE(tsunami::data::classify_dataset_manifest_version(tsunami::core::SemanticVersion{1, 1, 0}) == DatasetManifestCompatibility::forward_compatible_minor);
    REQUIRE(tsunami::data::classify_dataset_manifest_version(tsunami::core::SemanticVersion{1, 99, 4}) == DatasetManifestCompatibility::forward_compatible_minor);
    REQUIRE(tsunami::data::classify_dataset_manifest_version(tsunami::core::SemanticVersion{0, 1, 0}) == DatasetManifestCompatibility::migration_required);
    REQUIRE(tsunami::data::classify_dataset_manifest_version(tsunami::core::SemanticVersion{2, 0, 0}) == DatasetManifestCompatibility::unsupported_major);
    REQUIRE(tsunami::data::to_string(DatasetManifestCompatibility::forward_compatible_minor) == "forward_compatible_minor");
    REQUIRE_FALSE(tsunami::data::parse_dataset_manifest_version("1.0").has_value());
    REQUIRE_FALSE(tsunami::data::parse_dataset_manifest_version("1.0.0 ").has_value());
    REQUIRE_FALSE(tsunami::data::parse_dataset_manifest_version("1.-1.0").has_value());
}

TEST_CASE("Dataset manifest schema document is committed as JSON syntax", "[data][dataset manifest][schema]")
{
    const auto schema = read_text(source_root() / "schemas/dataset_manifest/1.0.0/dataset_manifest.schema.json");
    REQUIRE(schema.find("\"$schema\"") != std::string::npos);
    REQUIRE(schema.find("https://json-schema.org/draft/2020-12/schema") != std::string::npos);
    REQUIRE(schema.find("tsunami.dataset_manifest") != std::string::npos);
    REQUIRE(schema.find("\"schema_version\"") != std::string::npos);
    REQUIRE(schema.find("\"processes\"") != std::string::npos);
    REQUIRE(schema.find("\"sha256\"") != std::string::npos);
}

TEST_CASE("Valid dataset manifest fixtures parse and serialise canonically", "[data][dataset manifest][fixtures]")
{
    for (const auto name : {
             "minimal_source_manifest.json",
             "generated_lineage_manifest.json",
             "multi_provider_manifest.json",
             "illustrative_tohoku_kamaishi_corridor_manifest.json",
             "forward_minor_with_extensions.json"}) {
        INFO("fixture: " << name);
        const auto document = read_text(manifest_fixture("valid", name));
        auto parsed = tsunami::data::parse_dataset_manifest(document, std::string{name});
        if (!parsed) {
            INFO("parse error: " << parsed.error().code());
        }
        REQUIRE(parsed.has_value());

        auto bytes = tsunami::data::serialise_dataset_manifest(parsed.value());
        if (!bytes) {
            INFO("serialise error: " << bytes.error().code());
        }
        REQUIRE(bytes.has_value());
        REQUIRE_FALSE(bytes.value().empty());
        REQUIRE(bytes.value().back() == '\n');
        REQUIRE(bytes.value().find('\r') == std::string::npos);
        REQUIRE(bytes.value().find("  \"schema_version\"") != std::string::npos);
        REQUIRE(bytes.value().find("\"media_type\"") != std::string::npos);

        auto reparsed = tsunami::data::parse_dataset_manifest(bytes.value(), std::string{name} + "-canonical");
        if (!reparsed) {
            INFO("canonical parse error: " << reparsed.error().code());
        }
        REQUIRE(reparsed.has_value());
        REQUIRE(reparsed.value() == parsed.value());

        auto bytes_again = tsunami::data::serialise_dataset_manifest(reparsed.value());
        REQUIRE(bytes_again.has_value());
        REQUIRE(bytes_again.value() == bytes.value());
    }

    const auto forward = tsunami::data::parse_dataset_manifest(read_text(manifest_fixture("valid", "forward_minor_with_extensions.json")), "forward");
    REQUIRE(forward.has_value());
    REQUIRE(forward.value().compatibility() == tsunami::data::DatasetManifestCompatibility::forward_compatible_minor);
    REQUIRE(forward.value().extensions().values.front().name == "alpha_extension");
    REQUIRE(forward.value().extensions().values.back().name == "zeta_extension");
}

TEST_CASE("Invalid and migration dataset manifest fixtures return deterministic diagnostics", "[data][dataset manifest][validation][migration]")
{
    struct Expectation
    {
        std::string_view category;
        std::string_view name;
        std::string_view code;
        std::string_view pointer;
    };
    const auto expectations = std::vector<Expectation>{
        {"invalid", "malformed_json.json", "data.dataset_manifest.json_invalid", ""},
        {"invalid", "root_array.json", "data.dataset_manifest.root_type_invalid", "/"},
        {"invalid", "missing_schema_version.json", "data.dataset_manifest.required_field_missing", "/schema_version"},
        {"invalid", "invalid_schema_version.json", "data.dataset_manifest.schema_version_invalid", "/schema_version"},
        {"invalid", "unsupported_major.json", "data.dataset_manifest.schema_major_unsupported", "/schema_version"},
        {"invalid", "unknown_top_level_field.json", "data.dataset_manifest.unknown_field", "/typo"},
        {"invalid", "unknown_dataset_field.json", "data.dataset_manifest.unknown_field", "/datasets/0/typo"},
        {"invalid", "invalid_manifest_id.json", "data.dataset_manifest.identity_invalid", "/manifest/manifest_id"},
        {"invalid", "duplicate_provider_id.json", "data.dataset_manifest.provider_invalid", "/providers"},
        {"invalid", "missing_provider_reference.json", "data.dataset_manifest.reference_missing", "/datasets/provider_id"},
        {"invalid", "invalid_sha256.json", "data.dataset_manifest.digest_invalid", "/datasets/assets/digest"},
        {"invalid", "generated_external_asset.json", "data.dataset_manifest.asset_invalid", "/datasets/assets/location"},
        {"invalid", "multiple_producers.json", "data.dataset_manifest.multiple_producers", "/processes/output_dataset_ids"},
        {"invalid", "lineage_cycle.json", "data.dataset_manifest.lineage_cycle", "/processes"},
        {"migration", "legacy_manifest_0_1_0.json", "data.dataset_manifest.migration_required", "/schema_version"}};

    for (const auto &expectation : expectations) {
        INFO("fixture: " << expectation.category << "/" << expectation.name);
        auto parsed = tsunami::data::parse_dataset_manifest(read_text(manifest_fixture(expectation.category, expectation.name)), std::string{expectation.name});
        REQUIRE_FALSE(parsed.has_value());
        REQUIRE(parsed.error().code() == expectation.code);
        if (!expectation.pointer.empty()) {
            REQUIRE(context_value(parsed.error(), "json_pointer") == expectation.pointer);
        }
        REQUIRE(context_value(parsed.error(), "state_changed") == "false");
    }
}

TEST_CASE("All invalid dataset manifest fixtures are rejected or fail case binding without exceptions", "[data][dataset manifest][fixtures]")
{
    const auto case_only = std::set<std::string>{
        "binding_role_mismatch.json",
        "case_id_mismatch.json",
        "case_revision_mismatch.json",
        "missing_case_binding.json"};
    const auto invalid_dir = source_root() / "tests/fixtures/manifests/invalid";
    auto count = std::size_t{0U};
    for (const auto &entry : std::filesystem::directory_iterator{invalid_dir}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        const auto name = entry.path().filename().generic_string();
        INFO("fixture: " << name);
        auto parsed = tsunami::data::parse_dataset_manifest(read_text(entry.path()), name);
        if (case_only.contains(name)) {
            REQUIRE(parsed.has_value());
        } else {
            REQUIRE_FALSE(parsed.has_value());
            REQUIRE(context_value(parsed.error(), "state_changed") == "false");
        }
        ++count;
    }
    REQUIRE(count >= 50U);
}

TEST_CASE("Dataset manifest case bindings validate against case configuration", "[data][dataset manifest][case binding]")
{
    auto configuration = tsunami::data::parse_case_configuration(
        read_text(case_fixture("valid", "illustrative_tohoku_kamaishi_corridor_case.json")),
        "illustrative case");
    REQUIRE(configuration.has_value());
    auto minimal_configuration = tsunami::data::parse_case_configuration(
        read_text(case_fixture("valid", "minimal_regional_2d.json")),
        "minimal case");
    REQUIRE(minimal_configuration.has_value());

    auto manifest = tsunami::data::parse_dataset_manifest(
        read_text(manifest_fixture("valid", "illustrative_tohoku_kamaishi_corridor_manifest.json")),
        "illustrative manifest");
    REQUIRE(manifest.has_value());
    REQUIRE(tsunami::data::validate_dataset_manifest_for_case(manifest.value(), configuration.value()).has_value());
    auto minimal_manifest = tsunami::data::parse_dataset_manifest(
        read_text(manifest_fixture("valid", "minimal_source_manifest.json")),
        "minimal manifest");
    REQUIRE(minimal_manifest.has_value());
    REQUIRE(tsunami::data::validate_dataset_manifest_for_case(minimal_manifest.value(), minimal_configuration.value()).has_value());

    struct BindingExpectation
    {
        std::string_view name;
        std::string_view code;
    };
    const auto expectations = std::vector<BindingExpectation>{
        {"case_id_mismatch.json", "data.dataset_manifest.case_identity_mismatch"},
        {"case_revision_mismatch.json", "data.dataset_manifest.case_revision_mismatch"},
        {"missing_case_binding.json", "data.dataset_manifest.binding_missing"},
        {"binding_role_mismatch.json", "data.dataset_manifest.binding_role_mismatch"}};
    for (const auto &expectation : expectations) {
        INFO("fixture: " << expectation.name);
        auto parsed = tsunami::data::parse_dataset_manifest(read_text(manifest_fixture("invalid", expectation.name)), std::string{expectation.name});
        REQUIRE(parsed.has_value());
        auto result = tsunami::data::validate_dataset_manifest_for_case(parsed.value(), minimal_configuration.value());
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().code() == expectation.code);
        REQUIRE(context_value(result.error(), "state_changed") == "false");
    }
}

TEST_CASE("Dataset manifest read and write are transactional", "[data][dataset manifest][serialisation]")
{
    auto parsed = tsunami::data::parse_dataset_manifest(read_text(manifest_fixture("valid", "generated_lineage_manifest.json")), "generated");
    REQUIRE(parsed.has_value());

    const auto dir = std::filesystem::temp_directory_path() / "tsunami-dataset-manifest-tests";
    std::filesystem::create_directories(dir);
    const auto output = dir / "datasets.json";
    REQUIRE(tsunami::data::write_dataset_manifest(output, parsed.value()).has_value());
    auto read_back = tsunami::data::read_dataset_manifest(output);
    REQUIRE(read_back.has_value());
    REQUIRE(read_back.value() == parsed.value());

    const auto original = read_text(output);
    auto bad_schema = parsed.value().schema_identity();
    bad_schema.version = tsunami::core::SemanticVersion{2, 0, 0};
    auto invalid = tsunami::data::make_dataset_manifest(
        bad_schema,
        tsunami::data::DatasetManifestCompatibility::unsupported_major,
        std::string{parsed.value().policy_version()},
        parsed.value().identity(),
        parsed.value().providers(),
        parsed.value().licences(),
        parsed.value().datasets(),
        parsed.value().processes(),
        parsed.value().extensions());
    REQUIRE_FALSE(invalid.has_value());
    REQUIRE(read_text(output) == original);
}
