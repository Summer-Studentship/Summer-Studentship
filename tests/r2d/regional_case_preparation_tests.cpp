#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <tsunami/data/CaseConfiguration.hpp>
#include <tsunami/fvm/MeshField.hpp>
#include <tsunami/r2d/RegionalCasePreparation.hpp>

#include "geospatial_record_fixtures.hpp"

using Catch::Approx;

namespace
{
    [[nodiscard]] auto case_id() -> tsunami::core::CaseId
    {
        auto id = tsunami::core::CaseId::from_string("case-preparation");
        REQUIRE(id.has_value());
        return *id;
    }

    [[nodiscard]] auto horizontal_reference_descriptor() -> tsunami::geo::CoordinateReferenceDescriptor
    {
        auto descriptor = tsunami::geo::CoordinateReferenceDescriptor{};
        descriptor.authority_name = "EPSG";
        descriptor.authority_code = "0000";
        descriptor.name = "Synthetic horizontal CRS";
        descriptor.axis_names = {"Easting", "Northing"};
        descriptor.axis_directions = {"east", "north"};
        descriptor.axis_units = {"m", "m"};
        return descriptor;
    }

    [[nodiscard]] auto vertical_reference_descriptor() -> tsunami::geo::CoordinateReferenceDescriptor
    {
        auto descriptor = tsunami::geo::CoordinateReferenceDescriptor{};
        descriptor.name = "synthetic-datum";
        descriptor.canonical_wkt2 = "VERTCRS[\"synthetic-datum\"]";
        descriptor.axis_names = {"Gravity-related height"};
        descriptor.axis_directions = {"up"};
        descriptor.axis_units = {"m"};
        return descriptor;
    }

    [[nodiscard]] auto source_reference_descriptor() -> tsunami::geo::CoordinateReferenceDescriptor
    {
        auto descriptor = tsunami::geo::CoordinateReferenceDescriptor{};
        descriptor.name = "source";
        descriptor.canonical_wkt2 = "LOCAL_CS[\"source\"]";
        return descriptor;
    }

    [[nodiscard]] auto target_reference() -> tsunami::geo::ComputationalTargetReference
    {
        return tsunami::geo::ComputationalTargetReference{
            horizontal_reference_descriptor(),
            vertical_reference_descriptor(),
            tsunami::geo::ComputationalAxisConvention::east_north_up,
            "m",
            "m",
            "up"};
    }

    [[nodiscard]] auto mesh(std::string id = "case-preparation-mesh") -> tsunami::fvm::FiniteVolumeMesh
    {
        return tsunami::fvm::FiniteVolumeMesh{
            tsunami::fvm::MeshTopology{
                tsunami::fvm::MeshId{std::move(id)},
                2U,
                {
                    {{0U}, {0.0, 0.0, 0.0}},
                    {{1U}, {1.0, 0.0, 0.0}},
                    {{2U}, {1.0, 1.0, 0.0}},
                    {{3U}, {0.0, 1.0, 0.0}},
                },
                {
                    {{0U}, {{0U}, {1U}}, {0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
                    {{1U}, {{1U}, {2U}}, {0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{3U}},
                    {{2U}, {{2U}, {0U}}, {0U}, tsunami::fvm::CellId{1U}, std::nullopt},
                    {{3U}, {{2U}, {3U}}, {1U}, std::nullopt, tsunami::fvm::BoundaryPatchId{1U}},
                    {{4U}, {{3U}, {0U}}, {1U}, std::nullopt, tsunami::fvm::BoundaryPatchId{2U}},
                },
                {{{0U}, {{0U}, {1U}, {2U}}}, {{1U}, {{2U}, {3U}, {4U}}}},
                {
                    {{0U}, "boundary.offshore", {{0U}}},
                    {{1U}, "boundary.inland", {{3U}}},
                    {{2U}, "boundary.left_side", {{4U}}},
                    {{3U}, "boundary.right_side", {{1U}}},
                }},
            tsunami::fvm::MeshGeometry{
                {
                    {{0.5, 0.0, 0.0}, {0.0, -1.0, 0.0}},
                    {{1.0, 0.5, 0.0}, {1.0, 0.0, 0.0}},
                    {{0.5, 0.5, 0.0}, {-1.0, 1.0, 0.0}},
                    {{0.5, 1.0, 0.0}, {0.0, 1.0, 0.0}},
                    {{0.0, 0.5, 0.0}, {-1.0, 0.0, 0.0}},
                },
                {
                    {{2.0 / 3.0, 1.0 / 3.0, 0.0}, 0.5},
                    {{1.0 / 3.0, 2.0 / 3.0, 0.0}, 0.5},
                }}};
    }

    [[nodiscard]] auto case_configuration(
        bool relaxation = false,
        tsunami::data::ManningConfiguration manning = {},
        tsunami::data::CoriolisConfiguration coriolis = {},
        tsunami::data::EarthquakeConfiguration earthquake = {},
        std::optional<double> snapshot_interval = std::nullopt) -> tsunami::data::CaseConfiguration
    {
        auto datasets = tsunami::data::DatasetBindings{};
        datasets.manifest_path = std::filesystem::path{"manifests/case-preparation.json"};
        datasets.bathymetry = "bathymetry";
        datasets.topography = "topography";
        if (manning.kind == tsunami::data::ManningConfigurationKind::dataset) {
            datasets.manning = "manning";
            manning.dataset_binding = "manning";
        }
        if (coriolis.kind == tsunami::data::CoriolisConfigurationKind::dataset) {
            datasets.coriolis = "coriolis";
            coriolis.dataset_binding = "coriolis";
        }
        if (earthquake.enabled) {
            datasets.earthquake_displacement = "earthquake-displacement";
            earthquake.displacement_binding = "earthquake-displacement";
            if (earthquake.surface_transfer == tsunami::data::SurfaceTransfer::prescribed) {
                datasets.prescribed_surface = "prescribed-surface";
                earthquake.prescribed_surface_binding = "prescribed-surface";
            }
        }

        auto made = tsunami::data::make_case_configuration(
            tsunami::data::SchemaIdentity{
                std::string{tsunami::data::case_configuration_schema_name},
                tsunami::data::supported_case_configuration_version},
            tsunami::data::CaseSchemaCompatibility::exact,
            std::string{tsunami::data::supported_case_policy_version},
            tsunami::data::CaseIdentity{
                case_id(),
                "case-preparation",
                1U,
                "2026-07-31T00:00:00Z",
                "codex"},
            tsunami::data::ScenarioConfiguration{
                "scenario-preparation",
                "event-preparation",
                "target-preparation",
                tsunami::data::CaseModelFamily::regional_2d},
            tsunami::data::CoordinateFrameConfiguration{
                "EPSG:0000",
                "synthetic-datum",
                "m",
                "m",
                tsunami::data::HorizontalAxisOrder::east_north,
                tsunami::data::VerticalPositiveDirection::up},
            std::move(datasets),
            tsunami::data::Regional2DCaseConfiguration{
                tsunami::data::CorridorRequest{
                    "trajectory-preparation",
                    {0.0, 0.0},
                    90.0,
                    1.0,
                    1.0,
                    1.0,
                    {false, std::nullopt},
                    {relaxation ? 0.4 : 0.0, relaxation ? 0.4 : 0.0}},
                tsunami::data::RegionalPhysicsConfiguration{9.81, std::move(manning), std::move(coriolis), std::move(earthquake)},
                tsunami::data::RegionalNumericsConfiguration{
                    tsunami::data::RegionalTimeScheme::ssprk3,
                    0.45,
                    0.95,
                    0.9,
                    0.9,
                    1.0e-8,
                    0.001,
                    0.001,
                    20U},
                tsunami::data::CorridorBoundaryConfiguration{
                    tsunami::data::RegionalBoundaryKind::radiation,
                    tsunami::data::RegionalBoundaryKind::transmissive,
                    tsunami::data::RegionalBoundaryKind::radiation,
                    tsunami::data::RegionalBoundaryKind::radiation},
                relaxation
                    ? tsunami::data::RelaxationConfiguration{true, 0.5, 2.0}
                    : tsunami::data::RelaxationConfiguration{}},
            tsunami::data::CaseOutputConfiguration{snapshot_interval, true, true, std::nullopt},
            tsunami::data::CaseExtensions{});
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto point_evidence(
        tsunami::geo::CorridorReferencePointRole role,
        std::string id,
        double x) -> tsunami::geo::CorridorReferencePointEvidence
    {
        const auto point_id = id;
        const auto dataset_id = point_id + "-source-dataset";
        const auto asset_id = point_id + "-source-asset";
        return tsunami::geo::CorridorReferencePointEvidence{
            role,
            std::move(id),
            role == tsunami::geo::CorridorReferencePointRole::epicentre ? "synthetic epicentre" : "synthetic target",
            {x, 0.0, 0.0},
            0U,
            std::nullopt,
            tsunami::tests::r2d_fixtures::transformation_identity(
                point_id + "-case-prep-transform",
                point_id + "-source-import",
                dataset_id,
                asset_id,
                tsunami::data::CaseRevisionRef{case_id(), 1U},
                "case-preparation-manifest",
                1U,
                point_id + "-target-dataset",
                point_id + "-transform-process"),
            source_reference_descriptor(),
            target_reference(),
            "synthetic",
            "memory://synthetic",
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard]] auto corridor_record(const tsunami::data::CaseConfiguration &config) -> tsunami::geo::CorridorConstructionRecord
    {
        const auto &corridor = config.regional_2d().corridor;
        const auto inland_width = corridor.narrowing.enabled ? *corridor.narrowing.inland_width_m : corridor.width_m;
        auto record = tsunami::geo::CorridorConstructionRecord{};
        record.schema = tsunami::data::SchemaIdentity{
            std::string{tsunami::geo::corridor_construction_record_schema_name},
            tsunami::geo::supported_corridor_construction_record_version};
        record.policy_version = tsunami::geo::supported_corridor_construction_record_policy_version;
        record.identity = tsunami::geo::CorridorConstructionIdentity{
            "corridor-preparation",
            1U,
            tsunami::data::CaseRevisionRef{case_id(), 1U},
            "trajectory-preparation",
            "corridor-output",
            "corridor-process",
            "2026-07-31T00:00:00Z"};
        record.scenario_id = "scenario-preparation";
        record.target_site = "target-preparation";
        record.epicentre = point_evidence(tsunami::geo::CorridorReferencePointRole::epicentre, "epicentre", -1.0);
        record.target = point_evidence(tsunami::geo::CorridorReferencePointRole::target, "target", 1.0);
        record.target_reference = target_reference();
        record.policy = tsunami::geo::CorridorConstructionPolicy{0.1, 1.0e-12, 1.0e-12, 1.0e-12, 1.0e-12, 1.0e-12, "test"};
        record.configured_origin = {0.0, 0.0};
        record.configured_bearing_degrees = 90.0;
        record.derived_bearing_degrees = 90.0;
        record.offshore_extent_m = corridor.offshore_extent_m;
        record.epicentre_target_distance_m = 2.0;
        record.inland_extent_m = corridor.inland_extent_m;
        record.total_length_m = corridor.offshore_extent_m + corridor.inland_extent_m;
        record.offshore_width_m = corridor.width_m;
        record.inland_width_m = inland_width;
        record.narrowing_enabled = corridor.narrowing.enabled;
        record.narrowing_rule = corridor.narrowing.enabled ? "configured" : "none";
        record.local_basis = {{1.0, 0.0}, {0.0, 1.0}, 2.0, 90.0};
        record.stations = {-corridor.offshore_extent_m, 0.0, corridor.inland_extent_m, corridor.inland_extent_m};
        record.sponge_limits = {
            record.stations.offshore_xi_m,
            record.stations.offshore_xi_m + corridor.sponge.offshore_width_m,
            corridor.sponge.side_width_m,
            std::max(0.0, corridor.width_m - (2.0 * corridor.sponge.side_width_m))};
        record.polygon = {{{-1.0, -0.5}, {1.0, -0.5}, {1.0, 0.5}, {-1.0, 0.5}, {-1.0, -0.5}}, {}};
        record.vertex_order_convention = "counter_clockwise_closed";
        record.extent = {-1.0, -0.5, 1.0, 0.5};
        record.area_m2 = 2.0;
        record.perimeter_m = 6.0;
        record.configured_field_paths = {"/regional_2d/corridor"};
        return record;
    }

    [[nodiscard]] auto bathymetry(
        const tsunami::fvm::FiniteVolumeMesh &m,
        std::vector<tsunami::core::Real> values = {-1.0, -1.0}) -> tsunami::r2d::RegionalBathymetry
    {
        auto made = tsunami::r2d::make_regional_bathymetry(
            m,
            tsunami::fvm::FieldId{"case-prep-bed"},
            "case prep bed",
            std::move(values));
        REQUIRE(made.has_value());
        return std::move(made).value();
    }

    [[nodiscard]] auto preflight(const tsunami::fvm::FiniteVolumeMesh &m) -> tsunami::r2d::RegionalGeometryPreflightReport
    {
        const auto summary = m.summary();
        return tsunami::r2d::RegionalGeometryPreflightReport{
            "accepted",
            "corridor-preparation",
            "terrain-preparation",
            summary.id.value,
            summary.vertex_count,
            summary.cell_count,
            summary.face_count,
            1U,
            4U,
            {},
            {-1.0, -1.0, 1.0, 1.0},
            {-1.0, -1.0, 1.0, 1.0},
            0.5,
            1.0};
    }

    [[nodiscard]] auto transfer(
        const tsunami::fvm::FiniteVolumeMesh &m,
        const tsunami::r2d::RegionalBathymetry &bed) -> tsunami::r2d::RegionalTerrainTransferDiagnostics
    {
        const auto summary = m.summary();
        auto total_area = 0.0;
        auto minimum_bed = std::numeric_limits<tsunami::core::Real>::infinity();
        auto maximum_bed = -std::numeric_limits<tsunami::core::Real>::infinity();
        for (std::size_t index = 0; index < summary.cell_count; ++index) {
            const auto cell_id = tsunami::fvm::CellId{index};
            total_area += m.cell_geometry(cell_id).measure;
            minimum_bed = std::min(minimum_bed, bed.local_bed_elevation(cell_id));
            maximum_bed = std::max(maximum_bed, bed.local_bed_elevation(cell_id));
        }
        return tsunami::r2d::RegionalTerrainTransferDiagnostics{
            std::string{tsunami::r2d::regional_terrain_transfer_method_id},
            summary.id.value,
            "terrain-preparation",
            summary.cell_count,
            summary.cell_count,
            1U,
            1U,
            total_area,
            total_area,
            0.0,
            minimum_bed,
            maximum_bed,
            {{"bathymetry_selected", summary.cell_count}}};
    }

    [[nodiscard]] auto policy(double eta = 0.0) -> tsunami::r2d::RegionalCasePreparationPolicy
    {
        return {eta, 1.0e-6, 1.0e-8, 1.0e-12, 1.0e-12};
    }

    struct Fixture
    {
        tsunami::fvm::FiniteVolumeMesh m;
        tsunami::data::CaseConfiguration config;
        tsunami::geo::CorridorConstructionRecord corridor;
        tsunami::r2d::RegionalGeometryPreflightReport report;
        tsunami::r2d::RegionalTerrainTransferDiagnostics terrain;
        tsunami::r2d::RegionalBathymetry bed;
    };

    [[nodiscard]] auto fixture(
        tsunami::data::CaseConfiguration config = case_configuration(),
        std::vector<tsunami::core::Real> bed_values = {-1.0, -1.0}) -> Fixture
    {
        auto m = mesh();
        auto report = preflight(m);
        auto bed = bathymetry(m, std::move(bed_values));
        auto terrain = transfer(m, bed);
        auto corridor = corridor_record(config);
        return Fixture{std::move(m), std::move(config), std::move(corridor), std::move(report), std::move(terrain), std::move(bed)};
    }

    [[nodiscard]] auto request(
        Fixture &f,
        tsunami::r2d::RegionalCasePreparationPolicy prep_policy = policy()) -> tsunami::r2d::RegionalCasePreparationRequest
    {
        auto r = tsunami::r2d::RegionalCasePreparationRequest{};
        r.configuration = &f.config;
        r.corridor_record = &f.corridor;
        r.preflight = &f.report;
        r.terrain_transfer = &f.terrain;
        r.mesh = &f.m;
        r.pre_event_bathymetry = &f.bed;
        r.policy = prep_policy;
        return r;
    }

    [[nodiscard]] auto prepare(Fixture &f, tsunami::r2d::RegionalCasePreparationPolicy prep_policy = policy())
        -> tsunami::core::Result<tsunami::r2d::RegionalPreparedCase>
    {
        return tsunami::r2d::prepare_regional_case(request(f, prep_policy));
    }

    [[nodiscard]] auto patch_kind(
        const tsunami::r2d::RegionalPreparedCase &prepared,
        tsunami::fvm::BoundaryPatchId patch) -> tsunami::r2d::RegionalBoundaryConditionKind
    {
        const auto *condition = prepared.regional_boundaries().condition(patch);
        REQUIRE(condition != nullptr);
        return std::visit([](const auto &value) { return value.kind(); }, *condition);
    }

    [[nodiscard]] auto radiation_reference(
        const tsunami::r2d::RegionalPreparedCase &prepared,
        tsunami::fvm::BoundaryPatchId patch) -> tsunami::r2d::RegionalFarFieldState
    {
        const auto *condition = prepared.regional_boundaries().condition(patch);
        REQUIRE(condition != nullptr);
        const auto *radiation = std::get_if<tsunami::r2d::RegionalRadiationBoundary>(condition);
        REQUIRE(radiation != nullptr);
        return radiation->reference_state();
    }
}

TEST_CASE("Regional case preparation composes still-water runtime objects", "[case-preparation]")
{
    SECTION("valid no-earthquake case prepares successfully")
    {
        auto f = fixture();
        auto result = prepare(f);
        REQUIRE(result.has_value());
        CHECK(result.value().is_bound_to(f.m));
        CHECK(result.value().diagnostics().wet_cell_count == 2U);
        CHECK(result.value().diagnostics().dry_cell_count == 0U);
    }

    SECTION("a short solve reaches final time and preserves a lake at rest")
    {
        auto f = fixture();
        auto result = prepare(f);
        REQUIRE(result.has_value());
        auto prepared = std::move(result).value();
        auto snapshots = std::size_t{};
        auto solve_request = tsunami::r2d::make_regional_solve_request(
            f.m,
            prepared,
            {},
            [&](const auto &snapshot) {
                ++snapshots;
                for (const auto h : snapshot.depth) {
                    CHECK(h == Approx(1.0));
                }
                return tsunami::core::success();
            });
        REQUIRE(solve_request.has_value());
        auto summary = tsunami::r2d::solve_regional_model(solve_request.value(), prepared.simulation_state(), prepared.workspace());
        REQUIRE(summary.has_value());
        CHECK(summary.value().termination_reason == tsunami::r2d::RegionalSolveTerminationReason::final_time_reached);
        CHECK(prepared.simulation_state().time() == Approx(prepared.final_time()));
        CHECK(snapshots == 2U);
        for (std::size_t index = 0; index < prepared.simulation_state().conserved_state().size(); ++index) {
            CHECK(prepared.simulation_state().conserved_state().local_state({index}).depth == Approx(1.0));
        }
    }

    SECTION("wet and dry depths follow max still-water minus bed")
    {
        auto f = fixture(case_configuration(), {-1.0, 0.5});
        auto result = prepare(f, policy(0.0));
        REQUIRE(result.has_value());
        CHECK(result.value().simulation_state().conserved_state().local_state({0U}).depth == Approx(1.0));
        CHECK(result.value().simulation_state().conserved_state().local_state({1U}).depth == Approx(0.0));
        CHECK(result.value().diagnostics().wet_cell_count == 1U);
        CHECK(result.value().diagnostics().dry_cell_count == 1U);
    }

    SECTION("canonical dry-depth diagnostics ignore sub-dry raw water")
    {
        auto f = fixture(case_configuration(), {-5.0e-7, -1.0});
        auto result = prepare(f, policy(0.0));
        REQUIRE(result.has_value());
        CHECK(result.value().simulation_state().conserved_state().local_state({0U}).depth == Approx(0.0));
        CHECK(result.value().simulation_state().conserved_state().local_state({1U}).depth == Approx(1.0));
        CHECK(result.value().diagnostics().dry_cell_count == 1U);
        CHECK(result.value().diagnostics().wet_cell_count == 1U);
        CHECK(result.value().diagnostics().minimum_depth_m == Approx(0.0));
        CHECK(result.value().diagnostics().maximum_depth_m == Approx(1.0));
        CHECK(result.value().diagnostics().total_water_volume_m3 == Approx(0.5));
    }

    SECTION("configured patches map to physical kinds and radiation states")
    {
        auto f = fixture();
        auto result = prepare(f, policy(2.5));
        REQUIRE(result.has_value());
        CHECK(patch_kind(result.value(), {0U}) == tsunami::r2d::RegionalBoundaryConditionKind::radiation);
        CHECK(patch_kind(result.value(), {1U}) == tsunami::r2d::RegionalBoundaryConditionKind::transmissive);
        CHECK(patch_kind(result.value(), {2U}) == tsunami::r2d::RegionalBoundaryConditionKind::radiation);
        CHECK(patch_kind(result.value(), {3U}) == tsunami::r2d::RegionalBoundaryConditionKind::radiation);
        for (const auto patch : {tsunami::fvm::BoundaryPatchId{0U}, tsunami::fvm::BoundaryPatchId{2U}, tsunami::fvm::BoundaryPatchId{3U}}) {
            const auto reference = radiation_reference(result.value(), patch);
            CHECK(reference.free_surface_elevation == Approx(2.5));
            CHECK(reference.velocity_x == Approx(0.0));
            CHECK(reference.velocity_y == Approx(0.0));
        }
    }
}

TEST_CASE("Regional case preparation maps relaxation and local sources", "[case-preparation]")
{
    SECTION("relaxation creates offshore and side zones but no inland zone")
    {
        auto f = fixture(case_configuration(true));
        auto result = prepare(f);
        REQUIRE(result.has_value());
        REQUIRE(result.value().relaxation_zones().size() == 3U);
        CHECK(result.value().relaxation_zones().zones()[0].patch_id() == tsunami::fvm::BoundaryPatchId{0U});
        CHECK(result.value().relaxation_zones().zones()[1].patch_id() == tsunami::fvm::BoundaryPatchId{2U});
        CHECK(result.value().relaxation_zones().zones()[2].patch_id() == tsunami::fvm::BoundaryPatchId{3U});
    }

    SECTION("disabled relaxation creates an empty bound set")
    {
        auto f = fixture(case_configuration(false));
        auto result = prepare(f);
        REQUIRE(result.has_value());
        CHECK(result.value().relaxation_zones().empty());
        CHECK(result.value().relaxation_zones().is_bound_to(f.m));
    }

    SECTION("uniform Manning and constant Coriolis map correctly")
    {
        auto f = fixture(case_configuration(
            false,
            {tsunami::data::ManningConfigurationKind::uniform, 0.03, std::nullopt},
            {tsunami::data::CoriolisConfigurationKind::constant, 1.0e-4, std::nullopt}));
        auto result = prepare(f);
        REQUIRE(result.has_value());
        REQUIRE(result.value().local_sources().manning_coefficient() != nullptr);
        REQUIRE(result.value().local_sources().coriolis_parameter() != nullptr);
        CHECK(result.value().local_sources().manning_coefficient()->values()[0] == Approx(0.03));
        CHECK(result.value().local_sources().coriolis_parameter()->values()[1] == Approx(1.0e-4));
    }

    SECTION("dataset Manning and Coriolis values map correctly")
    {
        auto f = fixture(case_configuration(
            false,
            {tsunami::data::ManningConfigurationKind::dataset, std::nullopt, std::nullopt},
            {tsunami::data::CoriolisConfigurationKind::dataset, std::nullopt, std::nullopt}));
        const auto manning = std::vector<tsunami::core::Real>{0.02, 0.04};
        const auto coriolis = std::vector<tsunami::core::Real>{1.0e-4, -1.0e-4};
        auto r = request(f);
        r.manning_values = std::span<const tsunami::core::Real>{manning};
        r.coriolis_values = std::span<const tsunami::core::Real>{coriolis};
        auto result = tsunami::r2d::prepare_regional_case(r);
        REQUIRE(result.has_value());
        CHECK(result.value().local_sources().manning_coefficient()->values()[1] == Approx(0.04));
        CHECK(result.value().local_sources().coriolis_parameter()->values()[1] == Approx(-1.0e-4));
    }

    SECTION("missing or wrong-sized dataset values fail")
    {
        auto f = fixture(case_configuration(
            false,
            {tsunami::data::ManningConfigurationKind::dataset, std::nullopt, std::nullopt}));
        auto missing = prepare(f);
        REQUIRE_FALSE(missing.has_value());
        CHECK(missing.error().code() == "r2d.case_preparation.manning_dataset_invalid");
        const auto values = std::vector<tsunami::core::Real>{0.02};
        auto r = request(f);
        r.manning_values = std::span<const tsunami::core::Real>{values};
        auto wrong = tsunami::r2d::prepare_regional_case(r);
        REQUIRE_FALSE(wrong.has_value());
        CHECK(wrong.error().code() == "r2d.case_preparation.manning_dataset_invalid");
    }

    SECTION("extraneous Manning and Coriolis spans fail")
    {
        const auto values = std::vector<tsunami::core::Real>{0.02, 0.03};
        auto manning = fixture(case_configuration(false));
        auto manning_request = request(manning);
        manning_request.manning_values = std::span<const tsunami::core::Real>{values};
        auto manning_result = tsunami::r2d::prepare_regional_case(manning_request);
        REQUIRE_FALSE(manning_result.has_value());
        CHECK(manning_result.error().code() == "r2d.case_preparation.source_input_invalid");

        auto coriolis = fixture(case_configuration(false));
        auto coriolis_request = request(coriolis);
        coriolis_request.coriolis_values = std::span<const tsunami::core::Real>{values};
        auto coriolis_result = tsunami::r2d::prepare_regional_case(coriolis_request);
        REQUIRE_FALSE(coriolis_result.has_value());
        CHECK(coriolis_result.error().code() == "r2d.case_preparation.source_input_invalid");
    }
}

TEST_CASE("Regional case preparation composes earthquake initialisation", "[case-preparation]")
{
    auto metadata = [] {
        return tsunami::r2d::RegionalEarthquakeSourceMetadata{
            tsunami::r2d::RegionalEarthquakeSourceKind::synthetic,
            "event-preparation",
            "synthetic-model",
            "programmatic",
            "mesh",
            0U};
    };

    SECTION("enabled earthquake initialisation composes the existing implementation")
    {
        auto f = fixture(case_configuration(
            false,
            {},
            {},
            {true, std::nullopt, tsunami::data::BedDeformationMapping::vertical_only, tsunami::data::SurfaceTransfer::passive_equal_to_effective_bed, std::nullopt}));
        auto displacement = tsunami::r2d::make_filled_regional_seabed_displacement(f.m, 0.0, 0.0, 0.05);
        REQUIRE(displacement.has_value());
        const auto meta = metadata();
        auto r = request(f);
        r.seabed_displacement = &displacement.value();
        r.earthquake_metadata = &meta;
        auto result = tsunami::r2d::prepare_regional_case(r);
        REQUIRE(result.has_value());
        REQUIRE(result.value().earthquake_diagnostics().has_value());
        CHECK(result.value().diagnostics().earthquake_initialised);
    }

    SECTION("missing displacement or prescribed surface fails when required")
    {
        auto f = fixture(case_configuration(
            false,
            {},
            {},
            {true, std::nullopt, tsunami::data::BedDeformationMapping::vertical_only, tsunami::data::SurfaceTransfer::passive_equal_to_effective_bed, std::nullopt}));
        const auto meta = metadata();
        auto r = request(f);
        r.earthquake_metadata = &meta;
        auto missing_displacement = tsunami::r2d::prepare_regional_case(r);
        REQUIRE_FALSE(missing_displacement.has_value());
        CHECK(missing_displacement.error().code() == "r2d.case_preparation.earthquake_input_invalid");

        auto prescribed_case = fixture(case_configuration(
            false,
            {},
            {},
            {true, std::nullopt, tsunami::data::BedDeformationMapping::vertical_only, tsunami::data::SurfaceTransfer::prescribed, std::nullopt}));
        auto displacement = tsunami::r2d::make_filled_regional_seabed_displacement(prescribed_case.m, 0.0, 0.0, 0.01);
        REQUIRE(displacement.has_value());
        auto prescribed_request = request(prescribed_case);
        prescribed_request.seabed_displacement = &displacement.value();
        prescribed_request.earthquake_metadata = &meta;
        auto missing_prescribed = tsunami::r2d::prepare_regional_case(prescribed_request);
        REQUIRE_FALSE(missing_prescribed.has_value());
        CHECK(missing_prescribed.error().code() == "r2d.case_preparation.earthquake_input_invalid");
    }

    SECTION("extraneous earthquake inputs fail when earthquake is disabled")
    {
        auto f = fixture();
        auto displacement = tsunami::r2d::make_filled_regional_seabed_displacement(f.m, 0.0, 0.0, 0.01);
        REQUIRE(displacement.has_value());
        auto r = request(f);
        r.seabed_displacement = &displacement.value();
        auto result = tsunami::r2d::prepare_regional_case(r);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.case_preparation.earthquake_input_invalid");
    }
}

TEST_CASE("Regional case preparation rejects stale contracts without mutation", "[case-preparation]")
{
    SECTION("stale case, corridor, preflight, transfer or mesh identity fails")
    {
        auto f = fixture();
        auto stale_case = request(f);
        f.corridor.identity.case_revision.revision = 2U;
        auto case_result = tsunami::r2d::prepare_regional_case(stale_case);
        REQUIRE_FALSE(case_result.has_value());
        CHECK(case_result.error().code() == "r2d.case_preparation.identity_mismatch");

        auto corridor = fixture();
        corridor.corridor.configured_origin.x = 0.1;
        auto corridor_result = prepare(corridor);
        REQUIRE_FALSE(corridor_result.has_value());
        CHECK(corridor_result.error().code() == "r2d.case_preparation.corridor_mismatch");

        auto stale_preflight = fixture();
        stale_preflight.report.validation_status = "rejected";
        auto preflight_result = prepare(stale_preflight);
        REQUIRE_FALSE(preflight_result.has_value());
        CHECK(preflight_result.error().code() == "r2d.case_preparation.preflight_mismatch");

        auto stale_transfer = fixture();
        stale_transfer.terrain.mesh_id = "stale-mesh";
        auto transfer_result = prepare(stale_transfer);
        REQUIRE_FALSE(transfer_result.has_value());
        CHECK(transfer_result.error().code() == "r2d.case_preparation.terrain_transfer_mismatch");

        auto stale_evidence = fixture();
        stale_evidence.terrain.minimum_bed_elevation_m = -2.0;
        auto evidence_result = prepare(stale_evidence);
        REQUIRE_FALSE(evidence_result.has_value());
        CHECK(evidence_result.error().code() == "r2d.case_preparation.terrain_transfer_mismatch");

        auto stale_mesh = fixture();
        auto other_mesh = mesh("other-mesh");
        auto stale_request = request(stale_mesh);
        stale_request.mesh = &other_mesh;
        auto mesh_result = tsunami::r2d::prepare_regional_case(stale_request);
        REQUIRE_FALSE(mesh_result.has_value());
        CHECK(mesh_result.error().code() == "r2d.case_preparation.preflight_mismatch");
    }

    SECTION("stale coordinate frame, narrowing and sponge evidence fails")
    {
        auto frame = fixture();
        frame.corridor.target_reference.horizontal.authority_code = "9999";
        auto frame_result = prepare(frame);
        REQUIRE_FALSE(frame_result.has_value());
        CHECK(frame_result.error().code() == "r2d.case_preparation.coordinate_frame_mismatch");
        REQUIRE(frame_result.error().cause_code().has_value());
        CHECK(*frame_result.error().cause_code() == "geo.crs.case_target_mismatch");

        auto point_target = fixture();
        point_target.corridor.epicentre.target_reference.vertical_unit = "ft";
        auto point_result = prepare(point_target);
        REQUIRE_FALSE(point_result.has_value());
        CHECK(point_result.error().code() == "r2d.case_preparation.coordinate_frame_mismatch");

        auto narrowing = fixture();
        narrowing.corridor.narrowing_enabled = true;
        auto narrowing_result = prepare(narrowing);
        REQUIRE_FALSE(narrowing_result.has_value());
        CHECK(narrowing_result.error().code() == "r2d.case_preparation.corridor_mismatch");

        auto offshore = fixture(case_configuration(true));
        offshore.corridor.sponge_limits.offshore_end_xi_m += 0.1;
        auto offshore_result = prepare(offshore);
        REQUIRE_FALSE(offshore_result.has_value());
        CHECK(offshore_result.error().code() == "r2d.case_preparation.corridor_mismatch");

        auto side = fixture(case_configuration(true));
        side.corridor.sponge_limits.side_width_m += 0.1;
        auto side_result = prepare(side);
        REQUIRE_FALSE(side_result.has_value());
        CHECK(side_result.error().code() == "r2d.case_preparation.corridor_mismatch");
    }

    SECTION("prepared solve request uses physical boundary mode only")
    {
        auto f = fixture();
        auto result = prepare(f);
        REQUIRE(result.has_value());
        auto prepared = std::move(result).value();
        auto solve_request = tsunami::r2d::make_regional_solve_request(f.m, prepared);
        REQUIRE(solve_request.has_value());
        CHECK(solve_request.value().regional_boundaries == &prepared.regional_boundaries());
        CHECK(solve_request.value().depth_boundaries == nullptr);
        CHECK(solve_request.value().momentum_x_boundaries == nullptr);
        CHECK(solve_request.value().momentum_y_boundaries == nullptr);
        CHECK(solve_request.value().bathymetry_boundaries == nullptr);
    }

    SECTION("prepared runtime components are all bound to the same mesh")
    {
        auto f = fixture();
        auto result = prepare(f);
        REQUIRE(result.has_value());
        CHECK(result.value().bathymetry().is_bound_to(f.m));
        CHECK(result.value().simulation_state().conserved_state().is_bound_to(f.m));
        CHECK(result.value().regional_boundaries().is_complete_for(f.m));
        CHECK(result.value().relaxation_zones().is_bound_to(f.m));
        CHECK(result.value().local_sources().is_bound_to(f.m));
        CHECK(result.value().workspace().is_bound_to(f.m));
    }

    SECTION("preparation failure does not mutate supplied input")
    {
        auto f = fixture();
        const auto before = f.bed.local_bed_elevation({0U});
        auto r = request(f);
        r.policy.dry_depth_m = -1.0;
        auto result = tsunami::r2d::prepare_regional_case(r);
        REQUIRE_FALSE(result.has_value());
        CHECK(f.bed.local_bed_elevation({0U}) == before);
        CHECK(f.config.identity().revision == 1U);
        CHECK(f.corridor.identity.corridor_revision == 1U);
    }
}
