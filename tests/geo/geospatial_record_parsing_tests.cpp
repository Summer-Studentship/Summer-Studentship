#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/geo/CorridorConstructionParsing.hpp>
#include <tsunami/geo/CorridorConstructionSerialisation.hpp>
#include <tsunami/geo/TerrainConditioningParsing.hpp>
#include <tsunami/geo/TerrainConditioningSerialisation.hpp>

namespace
{
    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        auto value = error.context_value(key);
        return value ? *value : std::string{};
    }

    [[nodiscard]] auto case_ref(std::string id = "record-case") -> tsunami::data::CaseRevisionRef
    {
        return tsunami::data::CaseRevisionRef{tsunami::core::CaseId::from_string(std::move(id)).value(), 1U};
    }

    [[nodiscard]] auto reference(std::string code = "EN-METRIC-1") -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"TEST"},
            std::move(code),
            "Synthetic east north metric reference",
            std::string{"LOCAL_CS[\"Synthetic metric\"]"},
            std::nullopt,
            std::string{"Synthetic datum"},
            std::string{"Synthetic realisation"},
            2026.0,
            {"Easting", "Northing"},
            {"east", "north"},
            {"metre", "metre"}};
    }

    [[nodiscard]] auto target_reference() -> tsunami::geo::ComputationalTargetReference
    {
        return tsunami::geo::ComputationalTargetReference{
            reference(),
            std::nullopt,
            tsunami::geo::ComputationalAxisConvention::east_north,
            "m",
            std::nullopt,
            std::nullopt};
    }

    [[nodiscard]] auto transformation_identity(std::string id, std::string dataset_id) -> tsunami::geo::CoordinateTransformationIdentity
    {
        return tsunami::geo::CoordinateTransformationIdentity{
            std::move(id),
            1U,
            case_ref(),
            "record-manifest",
            1U,
            "source-import",
            1U,
            "source-" + dataset_id,
            "asset-" + dataset_id,
            std::move(dataset_id),
            "transform-process",
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard]] auto evidence(
        tsunami::geo::CorridorReferencePointRole role,
        std::string point_id,
        tsunami::geo::Coordinate3D coordinate) -> tsunami::geo::CorridorReferencePointEvidence
    {
        const auto dataset = role == tsunami::geo::CorridorReferencePointRole::epicentre ? "epicentre-dataset" : "target-dataset";
        return tsunami::geo::CorridorReferencePointEvidence{
            role,
            std::move(point_id),
            role == tsunami::geo::CorridorReferencePointRole::epicentre ? "reported earthquake epicentre" : "reported target point",
            coordinate,
            0U,
            std::nullopt,
            transformation_identity(role == tsunami::geo::CorridorReferencePointRole::epicentre ? "epicentre-transform" : "target-transform", dataset),
            reference("SOURCE"),
            target_reference(),
            "Synthetic record fixture",
            "https://example.test/source",
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard]] auto corridor_record() -> tsunami::geo::CorridorConstructionRecord
    {
        auto record = tsunami::geo::CorridorConstructionRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{tsunami::geo::corridor_construction_record_schema_name}, tsunami::geo::supported_corridor_construction_record_version};
        record.policy_version = tsunami::geo::supported_corridor_construction_record_policy_version;
        record.identity = tsunami::geo::CorridorConstructionIdentity{"corridor-axis", 1U, case_ref(), "axis", "corridor-dataset", "corridor-process", "2026-07-31T00:00:00Z"};
        record.scenario_id = "record-scenario";
        record.target_site = "synthetic-site";
        record.epicentre = evidence(tsunami::geo::CorridorReferencePointRole::epicentre, "epicentre-point", {0.0, 0.0, -5.0});
        record.target = evidence(tsunami::geo::CorridorReferencePointRole::target, "target-point", {100.0, 0.0, 2.0});
        record.target_reference = target_reference();
        record.policy = tsunami::geo::CorridorConstructionPolicy{1.0, 0.001, 0.001, 1.0e-12, 1.0e-7, 1.0e-12, "fixture tolerance"};
        record.configured_origin = {0.0, 0.0};
        record.configured_bearing_degrees = 90.0;
        record.derived_bearing_degrees = 90.0;
        record.offshore_extent_m = 20.0;
        record.epicentre_target_distance_m = 100.0;
        record.inland_extent_m = 30.0;
        record.total_length_m = 150.0;
        record.offshore_width_m = 40.0;
        record.inland_width_m = 40.0;
        record.narrowing_enabled = false;
        record.narrowing_rule = "constant_width";
        record.local_basis = tsunami::geo::CorridorLocalBasis{{1.0, 0.0}, {-0.0, 1.0}, 100.0, 90.0};
        record.stations = tsunami::geo::CorridorLongitudinalStations{-20.0, 0.0, 100.0, 130.0};
        record.sponge_limits = tsunami::geo::CorridorSpongeLimits{-20.0, -20.0, 0.0, 40.0};
        record.polygon = tsunami::geo::Polygon2D{{{-20.0, 20.0}, {-20.0, -20.0}, {130.0, -20.0}, {130.0, 20.0}, {-20.0, 20.0}}, {}};
        record.vertex_order_convention = "counter_clockwise_closed_exterior_ring";
        record.extent = tsunami::geo::BoundingBox2D{-20.0, -20.0, 130.0, 20.0};
        record.area_m2 = 6000.0;
        record.perimeter_m = 380.0;
        record.diagnostics = tsunami::geo::CorridorConstructionDiagnostics{0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 6000.0, 6000.0, 0.0, 380.0, 380.0, 0.0, {"alpha-warning", "zeta-warning"}};
        record.configured_field_paths = {"/regional_2d/corridor"};
        record.warnings = {"alpha-warning", "zeta-warning"};
        return record;
    }

    [[nodiscard]] auto resampling(std::string dataset_id, tsunami::geo::TerrainSourceRole role) -> tsunami::geo::RasterResamplingRecord
    {
        auto record = tsunami::geo::RasterResamplingRecord{};
        record.dataset_id = dataset_id;
        record.asset_id = dataset_id + "-asset";
        record.import_identity.import_id = dataset_id + "-import";
        record.import_identity.dataset_id = dataset_id;
        record.import_identity.asset_id = record.asset_id;
        record.transformation_identity.transformation_id = dataset_id + "-transform";
        record.transformation_identity.source_dataset_id = dataset_id;
        record.transformation_identity.source_asset_id = record.asset_id;
        record.role = role;
        record.kernel = tsunami::geo::RasterResamplingKernel::bilinear;
        record.source_registration = tsunami::geo::RasterCellRegistration::pixel_is_area;
        record.target_registration = tsunami::geo::RasterCellRegistration::pixel_is_area;
        record.source_scale = std::nullopt;
        record.source_offset = std::nullopt;
        record.minimum_source_spacing_m = 10.0;
        record.maximum_source_spacing_m = 10.0;
        record.nominal_source_spacing_m = 10.0;
        record.target_spacing_m = 10.0;
        record.maximum_upsampling_factor = 4.0;
        record.source_valid_cell_count = 4U;
        record.output_valid_cell_count = 4U;
        record.operation.operation_name = "Synthetic identity operation";
        record.adapter_name = "fixture";
        record.adapter_version = "1.0";
        return record;
    }

    [[nodiscard]] auto terrain_record() -> tsunami::geo::TerrainConditioningRecord
    {
        auto record = tsunami::geo::TerrainConditioningRecord{};
        record.schema = tsunami::data::SchemaIdentity{std::string{tsunami::geo::terrain_conditioning_record_schema_name}, tsunami::geo::supported_terrain_conditioning_record_version};
        record.policy_version = tsunami::geo::supported_terrain_conditioning_record_policy_version;
        record.formula_version = tsunami::geo::terrain_conditioning_formula_version;
        record.identity = tsunami::geo::TerrainConditioningIdentity{"terrain-fixture", 1U, case_ref(), "record-manifest", 1U, "conditioned-terrain", "terrain-process", "2026-07-31T00:00:00Z"};
        record.scenario_id = "record-scenario";
        record.target_site = "synthetic-site";
        record.bathymetry_dataset_id = "bathymetry-primary";
        record.bathymetry_asset_id = "bathymetry-primary-asset";
        record.topography_dataset_id = "topography-primary";
        record.topography_asset_id = "topography-primary-asset";
        record.corridor_identity.corridor_id = "corridor-axis";
        const auto transform = tsunami::geo::RasterAffineTransform{0.0, 10.0, 0.0, 20.0, 0.0, -10.0};
        record.grid = tsunami::geo::TerrainTargetGrid{2U, 2U, 10.0, transform, {0.0, 0.0, 20.0, 20.0}, {}, -20.0, 0.0, -10.0, 10.0, 0.0, 0.0};
        record.target_reference = record.grid.target_reference();
        record.grid_policy = tsunami::geo::TerrainTargetGridPolicy{10.0, 0.5, 4.0, 16U, 1.0e-9, 1.0e-12, "fixture grid policy"};
        record.bathymetry_resampling = resampling("bathymetry-primary", tsunami::geo::TerrainSourceRole::bathymetry);
        record.topography_resampling = resampling("topography-primary", tsunami::geo::TerrainSourceRole::topography);
        record.bathymetry_import_identity = record.bathymetry_resampling.import_identity;
        record.topography_import_identity = record.topography_resampling.import_identity;
        record.bathymetry_transformation_identity = record.bathymetry_resampling.transformation_identity;
        record.topography_transformation_identity = record.topography_resampling.transformation_identity;
        record.merge_policy = tsunami::geo::TerrainMergePolicy{"bathymetry-primary", "topography-primary", 100.0, tsunami::geo::TerrainOverlapConflictPolicy::accept_priority_with_warning, "bathymetry priority"};
        record.gap_policy = tsunami::geo::TerrainGapResolutionPolicy{tsunami::geo::TerrainGapResolutionKind::reject, 0.0, 0.0, 0U, 0U, 0.0, 0.0, "reject gaps"};
        record.diagnostics.total_cell_count = 4U;
        record.diagnostics.active_cell_count = 4U;
        record.diagnostics.bathymetry_selected_cell_count = 4U;
        record.diagnostics.minimum_elevation_m = -3.0;
        record.diagnostics.maximum_elevation_m = -1.0;
        record.output_uncertainty = tsunami::data::DatasetUncertainty{tsunami::data::UncertaintyStatus::not_reported, {}, std::nullopt};
        record.output_media_type = "image/tiff";
        record.output_path = std::filesystem::path{"outputs/terrain/conditioned-terrain.tif"};
        record.digest_status = "not_computed_by_terrain_conditioning";
        record.warnings = {"alpha-warning", "zeta-warning"};
        return record;
    }

    [[nodiscard]] auto replace_once(std::string text, std::string_view needle, std::string_view replacement) -> std::string
    {
        const auto position = text.find(needle);
        REQUIRE(position != std::string::npos);
        text.replace(position, needle.size(), replacement);
        return text;
    }

    [[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string
    {
        auto file = std::ifstream{path, std::ios::binary};
        REQUIRE(file);
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }
}

TEST_CASE("canonical corridor records parse and reserialise byte-identically", "[geo-record-parsing]")
{
    const auto original = corridor_record();
    const auto first = tsunami::geo::serialise_corridor_construction_record(original).value();
    const auto parsed = tsunami::geo::parse_corridor_construction_record(first, "corridor-memory");
    REQUIRE(parsed.has_value());
    const auto second = tsunami::geo::serialise_corridor_construction_record(parsed.value()).value();
    CHECK(second == first);
    CHECK(parsed.value().identity == original.identity);
    CHECK(parsed.value().epicentre == original.epicentre);
    CHECK(parsed.value().target == original.target);
    CHECK(parsed.value().target_reference == original.target_reference);
    CHECK(parsed.value().polygon.exterior_ring == original.polygon.exterior_ring);
    CHECK(parsed.value().diagnostics.warnings == original.diagnostics.warnings);
    CHECK(parsed.value().warnings == original.warnings);
    CHECK(second.ends_with('\n'));
}

TEST_CASE("canonical terrain records parse and reserialise byte-identically", "[geo-record-parsing]")
{
    const auto original = terrain_record();
    const auto first = tsunami::geo::serialise_terrain_conditioning_record(original).value();
    const auto parsed = tsunami::geo::parse_terrain_conditioning_record(first, "terrain-memory");
    REQUIRE(parsed.has_value());
    const auto second = tsunami::geo::serialise_terrain_conditioning_record(parsed.value()).value();
    CHECK(second == first);
    CHECK(parsed.value().identity == original.identity);
    CHECK(parsed.value().grid == original.grid);
    CHECK(parsed.value().bathymetry_resampling.dataset_id == original.bathymetry_resampling.dataset_id);
    CHECK(parsed.value().topography_resampling.dataset_id == original.topography_resampling.dataset_id);
    CHECK(parsed.value().warnings == original.warnings);
    CHECK(second.ends_with('\n'));
}

TEST_CASE("geospatial record filesystem readers round-trip regular files", "[geo-record-parsing]")
{
    const auto directory = std::filesystem::temp_directory_path() / "tsunami-geospatial-record-parsing-tests";
    std::filesystem::create_directories(directory);
    const auto corridor_path = directory / "corridor.json";
    const auto terrain_path = directory / "terrain.json";

    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    REQUIRE(tsunami::geo::write_corridor_construction_record(corridor_path, corridor_record()).has_value());
    auto read_corridor = tsunami::geo::read_corridor_construction_record(corridor_path);
    REQUIRE(read_corridor.has_value());
    CHECK(tsunami::geo::serialise_corridor_construction_record(read_corridor.value()).value() == corridor_json);

    const auto terrain_json = tsunami::geo::serialise_terrain_conditioning_record(terrain_record()).value();
    REQUIRE(tsunami::geo::write_terrain_conditioning_record(terrain_path, terrain_record()).has_value());
    auto read_terrain = tsunami::geo::read_terrain_conditioning_record(terrain_path);
    REQUIRE(read_terrain.has_value());
    CHECK(tsunami::geo::serialise_terrain_conditioning_record(read_terrain.value()).value() == terrain_json);
}

TEST_CASE("explicit null optionals survive geospatial record round-trips", "[geo-record-parsing]")
{
    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    REQUIRE(corridor_json.find("\"source_feature_id\": null") != std::string::npos);
    auto corridor = tsunami::geo::parse_corridor_construction_record(corridor_json);
    REQUIRE(corridor.has_value());
    CHECK_FALSE(corridor.value().epicentre.source_feature_id.has_value());
    CHECK_FALSE(corridor.value().target_reference.vertical.has_value());

    const auto terrain_json = tsunami::geo::serialise_terrain_conditioning_record(terrain_record()).value();
    REQUIRE(terrain_json.find("\"source_scale\": null") != std::string::npos);
    auto terrain = tsunami::geo::parse_terrain_conditioning_record(terrain_json);
    REQUIRE(terrain.has_value());
    CHECK_FALSE(terrain.value().bathymetry_resampling.source_scale.has_value());
    CHECK_FALSE(terrain.value().topography_resampling.source_offset.has_value());
}

TEST_CASE("ordered geospatial arrays retain canonical order", "[geo-record-parsing]")
{
    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    auto corridor = tsunami::geo::parse_corridor_construction_record(corridor_json).value();
    REQUIRE(corridor.polygon.exterior_ring.size() == 5U);
    CHECK(corridor.polygon.exterior_ring[0] == tsunami::geo::Point2D{-20.0, 20.0});
    CHECK(corridor.polygon.exterior_ring[1] == tsunami::geo::Point2D{-20.0, -20.0});
    CHECK(corridor.epicentre.source_reference.axis_names == std::vector<std::string>{"Easting", "Northing"});
    CHECK(corridor.diagnostics.warnings == std::vector<std::string>{"alpha-warning", "zeta-warning"});

    const auto terrain_json = tsunami::geo::serialise_terrain_conditioning_record(terrain_record()).value();
    auto terrain = tsunami::geo::parse_terrain_conditioning_record(terrain_json).value();
    CHECK(terrain.warnings == std::vector<std::string>{"alpha-warning", "zeta-warning"});
}

TEST_CASE("geospatial record parsers reject malformed and non-object JSON", "[geo-record-parsing]")
{
    auto malformed = tsunami::geo::parse_corridor_construction_record("{\"schema\":", "bad-json");
    REQUIRE_FALSE(malformed.has_value());
    CHECK(malformed.error().code() == "geo.corridor.record_parse.malformed");
    CHECK(context_value(malformed.error(), "source_name") == "bad-json");

    auto non_object = tsunami::geo::parse_terrain_conditioning_record("[]", "array-json");
    REQUIRE_FALSE(non_object.has_value());
    CHECK(non_object.error().code() == "geo.terrain.record_parse.type_invalid");
    CHECK(context_value(non_object.error(), "json_pointer") == "/");
    CHECK(context_value(non_object.error(), "expected_type") == "object");
    CHECK(context_value(non_object.error(), "actual_type") == "array");
}

TEST_CASE("geospatial record parsers report missing wrong unknown and duplicate fields deterministically", "[geo-record-parsing]")
{
    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    auto missing = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "  \"policy_version\": \"0.1\",\n", ""), "missing-field");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code() == "geo.corridor.record_parse.field_missing");
    CHECK(context_value(missing.error(), "json_pointer") == "/policy_version");
    CHECK(context_value(missing.error(), "field") == "policy_version");

    auto wrong_type = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "\"policy_version\": \"0.1\"", "\"policy_version\": 1"), "wrong-type");
    REQUIRE_FALSE(wrong_type.has_value());
    CHECK(wrong_type.error().code() == "geo.corridor.record_parse.type_invalid");
    CHECK(context_value(wrong_type.error(), "expected_type") == "string");
    CHECK(context_value(wrong_type.error(), "actual_type") == "unsigned integer");

    auto unknown_top = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "{\n", "{\n  \"unexpected\": true,\n"), "unknown-top");
    REQUIRE_FALSE(unknown_top.has_value());
    CHECK(unknown_top.error().code() == "geo.corridor.record_parse.field_unknown");
    CHECK(context_value(unknown_top.error(), "json_pointer") == "/unexpected");

    auto unknown_nested = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "    \"schema_name\":", "    \"extra\": 0,\n    \"schema_name\":"), "unknown-nested");
    REQUIRE_FALSE(unknown_nested.has_value());
    CHECK(unknown_nested.error().code() == "geo.corridor.record_parse.field_unknown");
    CHECK(context_value(unknown_nested.error(), "json_pointer") == "/schema/extra");

    auto duplicate_top = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "  \"policy_version\": \"0.1\",\n", "  \"policy_version\": \"0.1\",\n  \"policy_version\": \"0.1\",\n"), "duplicate-top");
    REQUIRE_FALSE(duplicate_top.has_value());
    CHECK(duplicate_top.error().code() == "geo.corridor.record_parse.duplicate_key");
    CHECK(context_value(duplicate_top.error(), "json_pointer") == "/policy_version");

    auto duplicate_nested = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "      \"minor\": 0,\n", "      \"minor\": 0,\n      \"minor\": 0,\n"), "duplicate-nested");
    REQUIRE_FALSE(duplicate_nested.has_value());
    CHECK(duplicate_nested.error().code() == "geo.corridor.record_parse.duplicate_key");
    CHECK(context_value(duplicate_nested.error(), "json_pointer") == "/schema/version/minor");
}

TEST_CASE("geospatial record parsers compose semantic validation failures", "[geo-record-parsing]")
{
    const auto corridor_json = tsunami::geo::serialise_corridor_construction_record(corridor_record()).value();
    auto bad_schema = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "\"major\": 1", "\"major\": 2"), "bad-schema");
    REQUIRE_FALSE(bad_schema.has_value());
    CHECK(bad_schema.error().code() == "geo.corridor.record_parse.validation_failed");
    CHECK(bad_schema.error().cause_code() == std::optional<std::string>{"geo.corridor.record_invalid"});

    auto bad_formula = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "flat-ended-epicentre-target-v1", "unsupported-formula"), "bad-formula");
    REQUIRE_FALSE(bad_formula.has_value());
    CHECK(bad_formula.error().code() == "geo.corridor.record_parse.validation_failed");
    CHECK(bad_formula.error().cause_code().has_value());

    auto bad_nested_evidence = tsunami::geo::parse_corridor_construction_record(replace_once(corridor_json, "\"point_id\": \"epicentre-point\"", "\"point_id\": \"BAD ID\""), "bad-evidence");
    REQUIRE_FALSE(bad_nested_evidence.has_value());
    CHECK(bad_nested_evidence.error().code() == "geo.corridor.record_parse.validation_failed");
    CHECK(bad_nested_evidence.error().cause_code() == std::optional<std::string>{"geo.corridor.record_invalid"});

    const auto terrain_json = tsunami::geo::serialise_terrain_conditioning_record(terrain_record()).value();
    auto bad_policy = tsunami::geo::parse_terrain_conditioning_record(replace_once(terrain_json, "\"policy_version\": \"0.1\"", "\"policy_version\": \"9.9\""), "bad-policy");
    REQUIRE_FALSE(bad_policy.has_value());
    CHECK(bad_policy.error().code() == "geo.terrain.record_parse.validation_failed");
    CHECK(bad_policy.error().cause_code() == std::optional<std::string>{"geo.terrain.record_invalid"});

    auto bad_terrain_formula = tsunami::geo::parse_terrain_conditioning_record(replace_once(terrain_json, "corridor-grid-priority-merge-v1", "unsupported-formula"), "bad-terrain-formula");
    REQUIRE_FALSE(bad_terrain_formula.has_value());
    CHECK(bad_terrain_formula.error().code() == "geo.terrain.record_parse.validation_failed");
    CHECK(bad_terrain_formula.error().cause_code() == std::optional<std::string>{"geo.terrain.record_invalid"});
}

TEST_CASE("geospatial record file readers reject empty missing and oversized files deterministically", "[geo-record-parsing]")
{
    const auto directory = std::filesystem::temp_directory_path() / "tsunami-geospatial-record-read-failures";
    std::filesystem::create_directories(directory);
    const auto missing_path = directory / "missing.json";
    auto missing = tsunami::geo::read_corridor_construction_record(missing_path);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code() == "geo.corridor.record_read.failed");
    CHECK(context_value(missing.error(), "path") == missing_path.generic_string());

    const auto empty_path = directory / "empty.json";
    { auto out = std::ofstream{empty_path, std::ios::binary | std::ios::trunc}; }
    auto empty = tsunami::geo::read_terrain_conditioning_record(empty_path);
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().code() == "geo.terrain.record_parse.malformed");
    CHECK(read_text(empty_path).empty());

    const auto oversized_path = directory / "oversized.json";
    {
        auto out = std::ofstream{oversized_path, std::ios::binary | std::ios::trunc};
        const auto bytes = std::string(tsunami::geo::maximum_corridor_construction_record_document_bytes + 1U, ' ');
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    auto oversized = tsunami::geo::read_corridor_construction_record(oversized_path);
    REQUIRE_FALSE(oversized.has_value());
    CHECK(oversized.error().code() == "geo.corridor.record_read.too_large");
}

TEST_CASE("geospatial record parse failures do not mutate source strings or files", "[geo-record-parsing]")
{
    auto source = std::string{"{\"schema\": 1}"};
    const auto before = source;
    auto parsed = tsunami::geo::parse_corridor_construction_record(source, "stable-source");
    REQUIRE_FALSE(parsed.has_value());
    CHECK(source == before);

    const auto directory = std::filesystem::temp_directory_path() / "tsunami-geospatial-record-no-mutation";
    std::filesystem::create_directories(directory);
    const auto path = directory / "invalid.json";
    {
        auto out = std::ofstream{path, std::ios::binary | std::ios::trunc};
        out << source;
    }
    auto read = tsunami::geo::read_corridor_construction_record(path);
    REQUIRE_FALSE(read.has_value());
    CHECK(read_text(path) == before);
}

TEST_CASE("public geospatial parsing headers expose no JSON or adapter implementation types", "[geo-record-parsing]")
{
    const auto root = std::filesystem::path{__FILE__}.parent_path().parent_path().parent_path();
    const auto headers = std::vector<std::filesystem::path>{
        root / "src/geo/include/tsunami/geo/CorridorConstructionParsing.hpp",
        root / "src/geo/include/tsunami/geo/TerrainConditioningParsing.hpp"};
    const auto forbidden = std::vector<std::string>{"nlohmann", "QObject", "QString", "QVariant", "GDAL", "OGR", "PROJ", "H5", "OpenFOAM"};
    for (const auto &header : headers) {
        const auto text = read_text(header);
        for (const auto &token : forbidden) {
            CHECK(text.find(token) == std::string::npos);
        }
    }
}
