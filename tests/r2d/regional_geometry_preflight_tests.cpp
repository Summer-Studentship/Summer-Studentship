#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/adapters/gmsh/GmshMeshImporter.hpp>
#include <tsunami/fvm/FiniteVolumeMesh.hpp>
#include <tsunami/geo/ConditionedTerrainRaster.hpp>
#include <tsunami/r2d/RegionalGeometryPreflight.hpp>

using Catch::Approx;

namespace
{
    struct CorridorFixture
    {
        tsunami::geo::ConstructedCorridor corridor;
        tsunami::geo::CorridorConstructionRecord record;
    };

    struct TerrainFixture
    {
        tsunami::geo::ConditionedTerrainRaster terrain;
        tsunami::geo::TerrainConditioningRecord record;
    };

    struct PreflightFixture
    {
        CorridorFixture corridor;
        TerrainFixture terrain;
        tsunami::adapters::gmsh::GmshMeshImportResult imported;
    };

    [[nodiscard]] auto reference(std::string code = "EN-METRIC-1")
        -> tsunami::geo::CoordinateReferenceDescriptor
    {
        return tsunami::geo::CoordinateReferenceDescriptor{
            std::string{"TEST"},
            std::move(code),
            "Synthetic east-north metric reference",
            std::string{"LOCAL_CS[\"Synthetic metric\"]"},
            std::nullopt,
            std::string{"Synthetic datum"},
            std::string{"Synthetic realisation"},
            2026.0,
            {"Easting", "Northing"},
            {"east", "north"},
            {"metre", "metre"}};
    }

    [[nodiscard]] auto target_reference(std::string code = "EN-METRIC-1")
        -> tsunami::geo::ComputationalTargetReference
    {
        return tsunami::geo::ComputationalTargetReference{
            reference(std::move(code)),
            std::nullopt,
            tsunami::geo::ComputationalAxisConvention::east_north,
            "m",
            std::string{"m"},
            std::string{"up"}};
    }

    [[nodiscard]] auto case_revision() -> tsunami::data::CaseRevisionRef
    {
        return tsunami::data::CaseRevisionRef{
            tsunami::core::CaseId::from_string("preflight-case").value(),
            1U};
    }

    [[nodiscard]] auto transformation_identity(std::string id)
        -> tsunami::geo::CoordinateTransformationIdentity
    {
        return tsunami::geo::CoordinateTransformationIdentity{
            std::move(id),
            1U,
            case_revision(),
            "preflight-manifest",
            1U,
            "source-import",
            1U,
            "source-dataset",
            "source-asset",
            "transformed-dataset",
            "transform-process",
            "2026-07-31T00:00:00Z"};
    }

    [[nodiscard]] auto corridor_policy() -> tsunami::geo::CorridorConstructionPolicy
    {
        return tsunami::geo::CorridorConstructionPolicy{
            1.0,
            0.001,
            0.001,
            1.0e-12,
            1.0e-7,
            1.0e-12,
            "preflight synthetic metric tolerances"};
    }

    [[nodiscard]] auto corridor_fixture() -> CorridorFixture
    {
        const auto polygon = tsunami::geo::Polygon2D{
            {
                {-10.0, 5.0},
                {-10.0, -5.0},
                {30.0, -5.0},
                {30.0, 5.0},
                {-10.0, 5.0},
            },
            {}};
        const auto extent = tsunami::geo::BoundingBox2D{-10.0, -5.0, 30.0, 5.0};
        const auto basis = tsunami::geo::CorridorLocalBasis{{1.0, 0.0}, {0.0, 1.0}, 20.0, 90.0};
        const auto stations = tsunami::geo::CorridorLongitudinalStations{-10.0, 0.0, 20.0, 30.0};
        const auto sponge = tsunami::geo::CorridorSpongeLimits{-10.0, -10.0, 0.0, 10.0};
        auto corridor = tsunami::geo::ConstructedCorridor{
            polygon,
            extent,
            basis,
            stations,
            sponge,
            10.0,
            10.0,
            40.0,
            400.0,
            100.0};

        auto record = tsunami::geo::CorridorConstructionRecord{};
        record.schema = tsunami::data::SchemaIdentity{
            std::string{tsunami::geo::corridor_construction_record_schema_name},
            tsunami::geo::supported_corridor_construction_record_version};
        record.policy_version = tsunami::geo::supported_corridor_construction_record_policy_version;
        record.identity = tsunami::geo::CorridorConstructionIdentity{
            "corridor-preflight",
            1U,
            case_revision(),
            "preflight-axis",
            "corridor-dataset",
            "corridor-process",
            "2026-07-31T00:00:00Z"};
        record.scenario_id = "preflight-scenario";
        record.target_site = "kamaishi";
        record.epicentre = tsunami::geo::CorridorReferencePointEvidence{
            tsunami::geo::CorridorReferencePointRole::epicentre,
            "epicentre-point",
            "synthetic epicentre",
            {0.0, 0.0, -5.0},
            0U,
            std::string{"feature-epicentre"},
            transformation_identity("epicentre-transform"),
            reference("SOURCE"),
            target_reference(),
            "Synthetic preflight fixture",
            "https://example.test/preflight/epicentre",
            "2026-07-31T00:00:00Z"};
        record.target = tsunami::geo::CorridorReferencePointEvidence{
            tsunami::geo::CorridorReferencePointRole::target,
            "target-point",
            "synthetic target",
            {20.0, 0.0, 2.0},
            0U,
            std::string{"feature-target"},
            transformation_identity("target-transform"),
            reference("SOURCE"),
            target_reference(),
            "Synthetic preflight fixture",
            "https://example.test/preflight/target",
            "2026-07-31T00:00:00Z"};
        record.target_reference = target_reference();
        record.policy = corridor_policy();
        record.configured_origin = {0.0, 0.0};
        record.configured_bearing_degrees = 90.0;
        record.derived_bearing_degrees = 90.0;
        record.origin_residual_m = 0.0;
        record.bearing_residual_degrees = 0.0;
        record.offshore_extent_m = 10.0;
        record.epicentre_target_distance_m = 20.0;
        record.inland_extent_m = 10.0;
        record.total_length_m = 40.0;
        record.offshore_width_m = 10.0;
        record.inland_width_m = 10.0;
        record.narrowing_enabled = false;
        record.narrowing_rule = "constant_width";
        record.local_basis = basis;
        record.stations = stations;
        record.sponge_limits = sponge;
        record.polygon = polygon;
        record.vertex_order_convention = "counter_clockwise_closed_offshore_left_offshore_right_inland_right_inland_left";
        record.extent = extent;
        record.area_m2 = 400.0;
        record.perimeter_m = 100.0;
        record.diagnostics.analytic_area_m2 = 400.0;
        record.diagnostics.polygon_area_m2 = 400.0;
        record.diagnostics.analytic_perimeter_m = 100.0;
        record.diagnostics.polygon_perimeter_m = 100.0;
        record.configured_field_paths = {"/regional_2d/corridor"};
        return CorridorFixture{std::move(corridor), std::move(record)};
    }

    [[nodiscard]] auto terrain_grid(tsunami::geo::ComputationalTargetReference target = target_reference())
        -> tsunami::geo::TerrainTargetGrid
    {
        return tsunami::geo::TerrainTargetGrid{
            4U,
            2U,
            10.0,
            tsunami::geo::RasterAffineTransform{-10.0, 10.0, 0.0, 10.0, 0.0, -10.0},
            tsunami::geo::BoundingBox2D{-10.0, -10.0, 30.0, 10.0},
            std::move(target),
            -10.0,
            30.0,
            -10.0,
            10.0,
            0.0,
            10.0};
    }

    [[nodiscard]] auto grid_policy() -> tsunami::geo::TerrainTargetGridPolicy
    {
        return tsunami::geo::TerrainTargetGridPolicy{
            10.0,
            0.5,
            4.0,
            4096U,
            1.0e-9,
            1.0e-12,
            "preflight synthetic grid policy"};
    }

    [[nodiscard]] auto terrain_fixture(
        const tsunami::geo::CorridorConstructionRecord &corridor_record,
        std::vector<std::uint8_t> mask = std::vector<std::uint8_t>(8U, 1U),
        std::vector<double> coverage = std::vector<double>(8U, 1.0),
        std::vector<tsunami::geo::TerrainCellLineage> lineage =
            std::vector<tsunami::geo::TerrainCellLineage>(8U, tsunami::geo::TerrainCellLineage::bathymetry_selected),
        tsunami::geo::ComputationalTargetReference target = target_reference()) -> TerrainFixture
    {
        auto grid = terrain_grid(target);
        auto values = std::vector<double>(8U, -3.0);
        auto terrain = tsunami::geo::make_conditioned_terrain_raster(
            grid,
            values,
            std::move(mask),
            std::move(coverage),
            std::move(lineage));
        REQUIRE(terrain.has_value());

        auto record = tsunami::geo::TerrainConditioningRecord{};
        record.schema = tsunami::data::SchemaIdentity{
            std::string{tsunami::geo::terrain_conditioning_record_schema_name},
            tsunami::geo::supported_terrain_conditioning_record_version};
        record.policy_version = tsunami::geo::supported_terrain_conditioning_record_policy_version;
        record.formula_version = tsunami::geo::terrain_conditioning_formula_version;
        record.identity = tsunami::geo::TerrainConditioningIdentity{
            "terrain-preflight",
            1U,
            case_revision(),
            "preflight-manifest",
            1U,
            "conditioned-terrain",
            "terrain-conditioning-process",
            "2026-07-31T00:00:00Z"};
        record.scenario_id = "preflight-scenario";
        record.target_site = "kamaishi";
        record.bathymetry_dataset_id = "bathymetry-primary";
        record.bathymetry_asset_id = "bathymetry-asset";
        record.bathymetry_transformation_identity = transformation_identity("bathymetry-transform");
        record.topography_dataset_id = "topography-primary";
        record.topography_asset_id = "topography-asset";
        record.topography_transformation_identity = transformation_identity("topography-transform");
        record.corridor_identity = corridor_record.identity;
        record.target_reference = grid.target_reference();
        record.grid = grid;
        record.grid_policy = grid_policy();
        record.diagnostics.total_cell_count = grid.cell_count();
        record.diagnostics.active_cell_count = grid.cell_count();
        record.diagnostics.minimum_elevation_m = -3.0;
        record.diagnostics.maximum_elevation_m = -3.0;
        record.output_uncertainty = tsunami::data::DatasetUncertainty{
            tsunami::data::UncertaintyStatus::not_reported,
            {},
            std::string{"not_reported"}};
        record.output_media_type = "image/tiff";
        record.output_path = std::filesystem::path{"outputs/terrain/preflight.tif"};
        record.digest_status = "not_computed_by_terrain_conditioning";
        return TerrainFixture{std::move(terrain).value(), std::move(record)};
    }

    [[nodiscard]] auto gmsh_text() -> std::string
    {
        return R"($MeshFormat
4.1 0 8
$EndMeshFormat
$PhysicalNames
5
2 1 "region.domain"
1 2 "boundary.offshore"
1 3 "boundary.inland"
1 4 "boundary.left_side"
1 5 "boundary.right_side"
$EndPhysicalNames
$Entities
0 4 1 0
1 0 -4 0 0 4 0 1 2 0
2 20 -4 0 20 4 0 1 3 0
3 0 4 0 20 4 0 1 4 0
4 0 -4 0 20 -4 0 1 5 0
1 0 -4 0 20 4 0 1 1 0
$EndEntities
$Nodes
1 4 10 40
2 1 0 4
10
20
30
40
0 -4 0
20 -4 0
20 4 0
0 4 0
$EndNodes
$Elements
5 6 100 201
1 1 1 1
100 10 40
1 2 1 1
101 20 30
1 3 1 1
102 30 40
1 4 1 1
103 10 20
2 1 2 2
200 10 20 30
201 10 30 40
$EndElements
)";
    }

    [[nodiscard]] auto write_mesh_fixture(std::string name = "preflight-square.msh") -> std::filesystem::path
    {
        const auto dir = std::filesystem::temp_directory_path() / "tsunami-regional-preflight-tests";
        std::filesystem::create_directories(dir);
        const auto path = dir / std::move(name);
        auto out = std::ofstream{path};
        REQUIRE(out);
        out << gmsh_text();
        return path;
    }

    [[nodiscard]] auto fixture() -> PreflightFixture
    {
        auto corridor = corridor_fixture();
        auto terrain = terrain_fixture(corridor.record);
        auto imported = tsunami::adapters::gmsh::import_gmsh_msh41_ascii_mesh(write_mesh_fixture());
        REQUIRE(imported.has_value());
        return PreflightFixture{std::move(corridor), std::move(terrain), std::move(imported).value()};
    }

    [[nodiscard]] auto request_for(const PreflightFixture &data) -> tsunami::r2d::RegionalGeometryPreflightRequest
    {
        return tsunami::r2d::RegionalGeometryPreflightRequest{
            &data.corridor.corridor,
            &data.corridor.record,
            &data.terrain.terrain,
            &data.terrain.record,
            &data.imported.mesh,
            tsunami::r2d::RegionalMeshImportPhysicalGroups{data.imported.metadata.physical_name_tags}};
    }

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        const auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }

    [[nodiscard]] auto cloned_mesh_with(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::vector<tsunami::fvm::VertexRecord> vertices,
        std::vector<tsunami::fvm::FaceRecord> faces,
        std::vector<tsunami::fvm::CellRecord> cells,
        std::vector<tsunami::fvm::BoundaryPatchRecord> patches) -> tsunami::fvm::FiniteVolumeMesh
    {
        auto face_geometry = std::vector<tsunami::fvm::FaceGeometry>{
            mesh.geometry().faces().begin(),
            mesh.geometry().faces().end()};
        auto cell_geometry = std::vector<tsunami::fvm::CellGeometry>{
            mesh.geometry().cells().begin(),
            mesh.geometry().cells().end()};
        return tsunami::fvm::FiniteVolumeMesh{
            tsunami::fvm::MeshTopology{
                mesh.summary().id,
                mesh.summary().spatial_dimension,
                std::move(vertices),
                std::move(faces),
                std::move(cells),
                std::move(patches)},
            tsunami::fvm::MeshGeometry{std::move(face_geometry), std::move(cell_geometry)}};
    }

    [[nodiscard]] auto topology_input_from(
        const tsunami::fvm::FiniteVolumeMesh &mesh) -> tsunami::fvm::MeshTopologyInput
    {
        return tsunami::fvm::MeshTopologyInput{
            mesh.summary().id,
            mesh.summary().spatial_dimension,
            {mesh.topology().vertices().begin(), mesh.topology().vertices().end()},
            {mesh.topology().faces().begin(), mesh.topology().faces().end()},
            {mesh.topology().cells().begin(), mesh.topology().cells().end()},
            {mesh.topology().boundary_patches().begin(), mesh.topology().boundary_patches().end()}};
    }

    [[nodiscard]] auto reconstructed_mesh_from(tsunami::fvm::MeshTopologyInput input)
        -> tsunami::fvm::FiniteVolumeMesh
    {
        auto mesh = tsunami::fvm::make_finite_volume_mesh(std::move(input));
        REQUIRE(mesh.has_value());
        return std::move(mesh).value();
    }

    [[nodiscard]] auto mesh_with_vertices(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::vector<tsunami::fvm::VertexRecord> vertices) -> tsunami::fvm::FiniteVolumeMesh
    {
        auto input = topology_input_from(mesh);
        input.vertices = std::move(vertices);
        return reconstructed_mesh_from(std::move(input));
    }

    [[nodiscard]] auto invalid_mesh_with(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::vector<tsunami::fvm::FaceRecord> faces,
        std::vector<tsunami::fvm::CellRecord> cells,
        std::vector<tsunami::fvm::BoundaryPatchRecord> patches) -> tsunami::fvm::FiniteVolumeMesh
    {
        return cloned_mesh_with(
            mesh,
            {mesh.topology().vertices().begin(), mesh.topology().vertices().end()},
            std::move(faces),
            std::move(cells),
            std::move(patches));
    }

    auto expect_mesh_invalid_with_cause(
        const PreflightFixture &data,
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::string_view cause_code) -> void
    {
        auto request = request_for(data);
        request.mesh = &mesh;
        const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.preflight.mesh_invalid");
        REQUIRE(result.error().cause_code().has_value());
        CHECK(*result.error().cause_code() == cause_code);
    }
}

TEST_CASE("Regional2D geometry preflight accepts a corridor terrain and imported triangular mesh", "[r2d][preflight]")
{
    auto data = fixture();
    const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request_for(data));
    REQUIRE(result.has_value());
    CHECK(result.value().validation_status == "accepted");
    CHECK(result.value().corridor_id == "corridor-preflight");
    CHECK(result.value().terrain_id == "terrain-preflight");
    CHECK(result.value().vertex_count == 4U);
    CHECK(result.value().cell_count == 2U);
    CHECK(result.value().face_count == 5U);
    CHECK(result.value().internal_face_count == 1U);
    CHECK(result.value().boundary_face_count == 4U);
    CHECK(result.value().patches.size() == 4U);
    CHECK(result.value().minimum_cell_measure == Approx(80.0));
    CHECK(result.value().minimum_face_length == Approx(8.0));
    CHECK(result.value().mesh_bounds.minimum_x == Approx(0.0));
    CHECK(result.value().mesh_bounds.maximum_y == Approx(4.0));
    CHECK(result.value().terrain_support_bounds.maximum_x == Approx(30.0));
}

TEST_CASE("Regional2D geometry preflight rejects mesh vertex outside corridor", "[r2d][preflight]")
{
    auto data = fixture();
    auto vertices = std::vector<tsunami::fvm::VertexRecord>{
        data.imported.mesh.topology().vertices().begin(),
        data.imported.mesh.topology().vertices().end()};
    vertices[0U].position.x = -20.0;
    auto mesh = mesh_with_vertices(data.imported.mesh, std::move(vertices));
    auto request = request_for(data);
    request.mesh = &mesh;
    const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == "r2d.preflight.mesh_outside_corridor");
    CHECK(context_value(result.error(), "vertex_id") == "0");
}

TEST_CASE("Regional2D geometry preflight rejects cell centroid outside terrain support", "[r2d][preflight]")
{
    auto data = fixture();
    auto coverage = std::vector<double>(8U, 1.0);
    coverage[6U] = 0.0;
    auto lineage = std::vector<tsunami::geo::TerrainCellLineage>(8U, tsunami::geo::TerrainCellLineage::bathymetry_selected);
    lineage[6U] = tsunami::geo::TerrainCellLineage::outside_corridor;
    auto terrain = terrain_fixture(data.corridor.record, std::vector<std::uint8_t>(8U, 1U), std::move(coverage), std::move(lineage));
    auto request = request_for(data);
    request.terrain = &terrain.terrain;
    request.terrain_record = &terrain.record;
    const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == "r2d.preflight.terrain_support_missing");
    CHECK(context_value(result.error(), "support_kind") == "cell_centroid");
    CHECK(context_value(result.error(), "terrain_cell_index") == "6");
}

TEST_CASE("Regional2D geometry preflight rejects unexplained nodata at required support", "[r2d][preflight]")
{
    auto data = fixture();
    auto mask = std::vector<std::uint8_t>(8U, 1U);
    mask[6U] = 0U;
    auto terrain = terrain_fixture(data.corridor.record, std::move(mask));
    auto request = request_for(data);
    request.terrain = &terrain.terrain;
    request.terrain_record = &terrain.record;
    const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == "r2d.preflight.terrain_nodata");
    CHECK(context_value(result.error(), "support_kind") == "cell_centroid");
}

TEST_CASE("Regional2D geometry preflight rejects mismatched corridor identity", "[r2d][preflight]")
{
    auto data = fixture();
    auto terrain = data.terrain;
    terrain.record.corridor_identity.corridor_id = "corridor-other";
    auto request = request_for(data);
    request.terrain = &terrain.terrain;
    request.terrain_record = &terrain.record;
    const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == "r2d.preflight.corridor_identity_mismatch");
}

TEST_CASE("Regional2D geometry preflight rejects CRS target mismatch", "[r2d][preflight]")
{
    auto data = fixture();
    auto terrain = terrain_fixture(
        data.corridor.record,
        std::vector<std::uint8_t>(8U, 1U),
        std::vector<double>(8U, 1.0),
        std::vector<tsunami::geo::TerrainCellLineage>(8U, tsunami::geo::TerrainCellLineage::bathymetry_selected),
        target_reference("EN-METRIC-OTHER"));
    auto request = request_for(data);
    request.terrain = &terrain.terrain;
    request.terrain_record = &terrain.record;
    const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == "r2d.preflight.crs_mismatch");
}

TEST_CASE("Regional2D geometry preflight enforces required patch set", "[r2d][preflight]")
{
    auto data = fixture();
    auto input = topology_input_from(data.imported.mesh);

    auto missing_input = input;
    const auto missing_face = missing_input.boundary_patches[1U].faces.front();
    missing_input.faces[missing_face.value].boundary_patch = tsunami::fvm::BoundaryPatchId{0U};
    missing_input.boundary_patches[0U].faces.push_back(missing_face);
    missing_input.boundary_patches.erase(std::remove_if(missing_input.boundary_patches.begin(), missing_input.boundary_patches.end(), [](const auto &patch) {
        return patch.name == "boundary.inland";
    }), missing_input.boundary_patches.end());
    for (std::size_t index = 0U; index < missing_input.boundary_patches.size(); ++index) {
        missing_input.boundary_patches[index].id = tsunami::fvm::BoundaryPatchId{index};
        for (const auto face_id : missing_input.boundary_patches[index].faces) {
            missing_input.faces[face_id.value].boundary_patch = missing_input.boundary_patches[index].id;
        }
    }
    auto missing_mesh = reconstructed_mesh_from(std::move(missing_input));
    auto missing_request = request_for(data);
    missing_request.mesh = &missing_mesh;
    auto missing = tsunami::r2d::validate_regional2d_geometry_preflight(missing_request);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code() == "r2d.preflight.patch_missing");
    CHECK(context_value(missing.error(), "patch_name") == "boundary.inland");

    auto empty_input = input;
    const auto empty_face = empty_input.boundary_patches.front().faces.front();
    empty_input.faces[empty_face.value].boundary_patch = tsunami::fvm::BoundaryPatchId{1U};
    empty_input.boundary_patches[1U].faces.push_back(empty_face);
    empty_input.boundary_patches.front().faces.clear();
    auto empty_mesh = reconstructed_mesh_from(std::move(empty_input));
    auto empty_request = request_for(data);
    empty_request.mesh = &empty_mesh;
    auto empty = tsunami::r2d::validate_regional2d_geometry_preflight(empty_request);
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().code() == "r2d.preflight.patch_empty");

    auto extra_input = input;
    extra_input.boundary_patches.push_back(tsunami::fvm::BoundaryPatchRecord{tsunami::fvm::BoundaryPatchId{4U}, "boundary.extra", {}});
    auto extra_mesh = reconstructed_mesh_from(std::move(extra_input));
    auto extra_request = request_for(data);
    extra_request.mesh = &extra_mesh;
    auto extra = tsunami::r2d::validate_regional2d_geometry_preflight(extra_request);
    REQUIRE_FALSE(extra.has_value());
    CHECK(extra.error().code() == "r2d.preflight.patch_unsupported");
}

TEST_CASE("Regional2D geometry preflight rejects noncanonical internal owner neighbour order", "[r2d][preflight]")
{
    auto data = fixture();
    auto input = topology_input_from(data.imported.mesh);
    auto internal = std::find_if(input.faces.begin(), input.faces.end(), [](const auto &face) {
        return face.neighbour.has_value();
    });
    REQUIRE(internal != input.faces.end());
    std::swap(internal->owner, *internal->neighbour);
    const auto internal_face_id = internal->id;
    auto mesh = reconstructed_mesh_from(std::move(input));
    auto request = request_for(data);
    request.mesh = &mesh;
    const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == "r2d.preflight.internal_owner_not_canonical");
    CHECK(context_value(result.error(), "face_id") == std::to_string(internal_face_id.value));
}

TEST_CASE("Regional2D geometry preflight wraps authoritative FVM topology failures", "[r2d][preflight]")
{
    auto data = fixture();
    const auto base = topology_input_from(data.imported.mesh);

    SECTION("internal face is not addressed by its declared neighbour")
    {
        auto faces = base.faces;
        auto cells = base.cells;
        const auto internal = std::find_if(faces.begin(), faces.end(), [](const auto &face) {
            return face.neighbour.has_value();
        });
        REQUIRE(internal != faces.end());
        auto &neighbour_faces = cells[internal->neighbour->value].faces;
        const auto internal_slot = std::find(neighbour_faces.begin(), neighbour_faces.end(), internal->id);
        REQUIRE(internal_slot != neighbour_faces.end());
        const auto replacement = std::find_if(base.faces.begin(), base.faces.end(), [&](const auto &face) {
            return face.is_boundary() && face.owner != cells[internal->neighbour->value].id &&
                std::find(neighbour_faces.begin(), neighbour_faces.end(), face.id) == neighbour_faces.end();
        });
        REQUIRE(replacement != base.faces.end());
        *internal_slot = replacement->id;
        auto mesh = invalid_mesh_with(data.imported.mesh, std::move(faces), std::move(cells), base.boundary_patches);
        expect_mesh_invalid_with_cause(data, mesh, "fvm.mesh.cell_face_membership_invalid");
    }

    SECTION("boundary face is referenced by a cell other than its declared owner")
    {
        auto cells = base.cells;
        auto &target_faces = cells[1U].faces;
        const auto replacement = std::find_if(base.faces.begin(), base.faces.end(), [&](const auto &face) {
            return face.is_boundary() && face.owner != cells[1U].id &&
                std::find(target_faces.begin(), target_faces.end(), face.id) == target_faces.end();
        });
        REQUIRE(replacement != base.faces.end());
        target_faces.front() = replacement->id;
        auto mesh = invalid_mesh_with(data.imported.mesh, base.faces, std::move(cells), base.boundary_patches);
        expect_mesh_invalid_with_cause(data, mesh, "fvm.mesh.cell_face_membership_invalid");
    }

    SECTION("owner is outside the cell range")
    {
        auto faces = base.faces;
        faces.front().owner = tsunami::fvm::CellId{99U};
        auto mesh = invalid_mesh_with(data.imported.mesh, std::move(faces), base.cells, base.boundary_patches);
        expect_mesh_invalid_with_cause(data, mesh, "fvm.mesh.face_owner_out_of_range");
    }

    SECTION("neighbour is outside the cell range")
    {
        auto faces = base.faces;
        const auto internal = std::find_if(faces.begin(), faces.end(), [](const auto &face) {
            return face.neighbour.has_value();
        });
        REQUIRE(internal != faces.end());
        internal->neighbour = tsunami::fvm::CellId{99U};
        auto mesh = invalid_mesh_with(data.imported.mesh, std::move(faces), base.cells, base.boundary_patches);
        expect_mesh_invalid_with_cause(data, mesh, "fvm.mesh.face_neighbour_out_of_range");
    }

    SECTION("internal face appears in a boundary patch")
    {
        auto patches = base.boundary_patches;
        const auto internal = std::find_if(base.faces.begin(), base.faces.end(), [](const auto &face) {
            return face.neighbour.has_value();
        });
        REQUIRE(internal != base.faces.end());
        patches.front().faces.push_back(internal->id);
        auto mesh = invalid_mesh_with(data.imported.mesh, base.faces, base.cells, std::move(patches));
        expect_mesh_invalid_with_cause(data, mesh, "fvm.mesh.internal_face_in_patch");
    }

    SECTION("boundary face appears under a different patch")
    {
        auto faces = base.faces;
        faces[base.boundary_patches.front().faces.front().value].boundary_patch = tsunami::fvm::BoundaryPatchId{1U};
        auto mesh = invalid_mesh_with(data.imported.mesh, std::move(faces), base.cells, base.boundary_patches);
        expect_mesh_invalid_with_cause(data, mesh, "fvm.mesh.boundary_face_unassigned");
    }

    SECTION("boundary face is omitted from patches")
    {
        auto patches = base.boundary_patches;
        patches.front().faces.clear();
        auto mesh = invalid_mesh_with(data.imported.mesh, base.faces, base.cells, std::move(patches));
        expect_mesh_invalid_with_cause(data, mesh, "fvm.mesh.boundary_face_unassigned");
    }

    SECTION("patch references an out-of-range face")
    {
        auto patches = base.boundary_patches;
        patches.front().faces.push_back(tsunami::fvm::FaceId{99U});
        auto mesh = invalid_mesh_with(data.imported.mesh, base.faces, base.cells, std::move(patches));
        expect_mesh_invalid_with_cause(data, mesh, "fvm.mesh.boundary_face_unassigned");
    }
}

TEST_CASE("Regional2D geometry preflight rejects stale supplied geometry", "[r2d][preflight]")
{
    auto data = fixture();
    auto input = topology_input_from(data.imported.mesh);
    auto face_geometry = std::vector<tsunami::fvm::FaceGeometry>{
        data.imported.mesh.geometry().faces().begin(),
        data.imported.mesh.geometry().faces().end()};
    auto cell_geometry = std::vector<tsunami::fvm::CellGeometry>{
        data.imported.mesh.geometry().cells().begin(),
        data.imported.mesh.geometry().cells().end()};

    SECTION("face geometry mismatch")
    {
        face_geometry.front().centroid.x += 0.01;
        auto mesh = tsunami::fvm::FiniteVolumeMesh{
            tsunami::fvm::MeshTopology{
                input.id,
                input.spatial_dimension,
                std::move(input.vertices),
                std::move(input.faces),
                std::move(input.cells),
                std::move(input.boundary_patches)},
            tsunami::fvm::MeshGeometry{std::move(face_geometry), std::move(cell_geometry)}};
        auto request = request_for(data);
        request.mesh = &mesh;
        const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.preflight.mesh_geometry_mismatch");
        CHECK(context_value(result.error(), "face_id") == "0");
    }

    SECTION("cell geometry mismatch")
    {
        cell_geometry.front().measure += 0.01;
        auto mesh = tsunami::fvm::FiniteVolumeMesh{
            tsunami::fvm::MeshTopology{
                input.id,
                input.spatial_dimension,
                std::move(input.vertices),
                std::move(input.faces),
                std::move(input.cells),
                std::move(input.boundary_patches)},
            tsunami::fvm::MeshGeometry{std::move(face_geometry), std::move(cell_geometry)}};
        auto request = request_for(data);
        request.mesh = &mesh;
        const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.preflight.mesh_geometry_mismatch");
        CHECK(context_value(result.error(), "cell_id") == "0");
    }
}

TEST_CASE("Regional2D geometry preflight relies on FVM factory for degenerate cells", "[r2d][preflight][fvm]")
{
    auto input = tsunami::fvm::MeshTopologyInput{};
    input.id = tsunami::fvm::MeshId{"degenerate"};
    input.spatial_dimension = 2U;
    input.vertices = {
        {tsunami::fvm::VertexId{0U}, {0.0, 0.0, 0.0}},
        {tsunami::fvm::VertexId{1U}, {1.0, 0.0, 0.0}},
        {tsunami::fvm::VertexId{2U}, {2.0, 0.0, 0.0}},
    };
    input.faces = {
        {tsunami::fvm::FaceId{0U}, {tsunami::fvm::VertexId{0U}, tsunami::fvm::VertexId{1U}}, tsunami::fvm::CellId{0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
        {tsunami::fvm::FaceId{1U}, {tsunami::fvm::VertexId{1U}, tsunami::fvm::VertexId{2U}}, tsunami::fvm::CellId{0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
        {tsunami::fvm::FaceId{2U}, {tsunami::fvm::VertexId{0U}, tsunami::fvm::VertexId{2U}}, tsunami::fvm::CellId{0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
    };
    input.cells = {{tsunami::fvm::CellId{0U}, {tsunami::fvm::FaceId{0U}, tsunami::fvm::FaceId{1U}, tsunami::fvm::FaceId{2U}}}};
    input.boundary_patches = {{tsunami::fvm::BoundaryPatchId{0U}, "boundary.offshore", {tsunami::fvm::FaceId{0U}, tsunami::fvm::FaceId{1U}, tsunami::fvm::FaceId{2U}}}};
    const auto mesh = tsunami::fvm::make_finite_volume_mesh(std::move(input));
    REQUIRE_FALSE(mesh.has_value());
    CHECK(mesh.error().code() == "fvm.mesh.cell_degenerate");
}

TEST_CASE("Regional2D geometry preflight failure diagnostics carry deterministic context", "[r2d][preflight]")
{
    auto data = fixture();
    auto groups = data.imported.metadata.physical_name_tags;
    groups.erase("boundary.left_side");
    auto request = request_for(data);
    request.import_physical_groups.physical_name_tags = std::move(groups);
    const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code() == "r2d.preflight.import_physical_group_missing");
    CHECK(result.error().severity() == tsunami::core::Severity::error);
    CHECK(result.error().category() == tsunami::core::DiagnosticCategory::validation);
    CHECK(context_value(result.error(), "operation") == "validate_regional2d_geometry_preflight");
    CHECK(context_value(result.error(), "rule_id") == "SWE-GEO-CHK-WP1");
    CHECK(context_value(result.error(), "patch_name") == "boundary.left_side");
    CHECK(context_value(result.error(), "state_changed") == "false");
}

TEST_CASE("Regional2D geometry preflight does not mutate supplied inputs", "[r2d][preflight]")
{
    auto data = fixture();
    const auto corridor_before = data.corridor.corridor;
    const auto terrain_before = data.terrain.terrain;
    const auto mesh_summary_before = data.imported.mesh.summary();
    const auto patch_count_before = data.imported.mesh.topology().boundary_patches().size();
    const auto result = tsunami::r2d::validate_regional2d_geometry_preflight(request_for(data));
    REQUIRE(result.has_value());
    CHECK(data.corridor.corridor == corridor_before);
    CHECK(data.terrain.terrain == terrain_before);
    CHECK(data.imported.mesh.summary().id == mesh_summary_before.id);
    CHECK(data.imported.mesh.summary().cell_count == mesh_summary_before.cell_count);
    CHECK(data.imported.mesh.topology().boundary_patches().size() == patch_count_before);
}
