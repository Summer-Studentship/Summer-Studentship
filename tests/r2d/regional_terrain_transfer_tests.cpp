#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <tsunami/fvm/FiniteVolumeMesh.hpp>
#include <tsunami/r2d/RegionalTerrainTransfer.hpp>

#include "geospatial_record_fixtures.hpp"

using Catch::Approx;

namespace
{
    [[nodiscard]] auto triangle_mesh(
        tsunami::fvm::Point3 first = {0.0, 0.0, 0.0},
        tsunami::fvm::Point3 second = {2.0, 0.0, 0.0},
        tsunami::fvm::Point3 third = {0.0, 1.0, 0.0},
        std::string id = "terrain-transfer-mesh") -> tsunami::fvm::FiniteVolumeMesh
    {
        const auto area = 0.5 * std::abs(
            (second.x - first.x) * (third.y - first.y) - (third.x - first.x) * (second.y - first.y));
        return tsunami::fvm::FiniteVolumeMesh{
            tsunami::fvm::MeshTopology{
                tsunami::fvm::MeshId{std::move(id)},
                2U,
                {{{0U}, first}, {{1U}, second}, {{2U}, third}},
                {
                    {{0U}, {{0U}, {1U}}, {0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
                    {{1U}, {{1U}, {2U}}, {0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
                    {{2U}, {{2U}, {0U}}, {0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
                },
                {{{0U}, {{0U}, {1U}, {2U}}}},
                {{{0U}, "boundary.synthetic", {{0U}, {1U}, {2U}}}}},
            tsunami::fvm::MeshGeometry{
                {},
                {{{(first.x + second.x + third.x) / 3.0, (first.y + second.y + third.y) / 3.0, 0.0}, area}}}};
    }

    [[nodiscard]] auto two_cell_mesh() -> tsunami::fvm::FiniteVolumeMesh
    {
        return tsunami::fvm::FiniteVolumeMesh{
            tsunami::fvm::MeshTopology{
                tsunami::fvm::MeshId{"terrain-transfer-two-cell-mesh"},
                2U,
                {
                    {{0U}, {0.0, 0.0, 0.0}},
                    {{1U}, {1.0, 0.0, 0.0}},
                    {{2U}, {0.0, 1.0, 0.0}},
                    {{3U}, {1.0, 0.0, 0.0}},
                    {{4U}, {2.0, 0.0, 0.0}},
                    {{5U}, {1.0, 1.0, 0.0}},
                },
                {
                    {{0U}, {{0U}, {1U}}, {0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
                    {{1U}, {{1U}, {2U}}, {0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
                    {{2U}, {{2U}, {0U}}, {0U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
                    {{3U}, {{3U}, {4U}}, {1U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
                    {{4U}, {{4U}, {5U}}, {1U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
                    {{5U}, {{5U}, {3U}}, {1U}, std::nullopt, tsunami::fvm::BoundaryPatchId{0U}},
                },
                {{{0U}, {{0U}, {1U}, {2U}}}, {{1U}, {{3U}, {4U}, {5U}}}},
                {{{0U}, "boundary.synthetic", {{0U}, {1U}, {2U}, {3U}, {4U}, {5U}}}}},
            tsunami::fvm::MeshGeometry{
                {},
                {
                    {{1.0 / 3.0, 1.0 / 3.0, 0.0}, 0.5},
                    {{4.0 / 3.0, 1.0 / 3.0, 0.0}, 0.5},
                }}};
    }

    [[nodiscard]] auto grid(
        tsunami::geo::RasterAffineTransform transform = {0.0, 1.0, 0.0, 0.0, 0.0, 1.0},
        std::uint64_t width = 2U,
        std::uint64_t height = 1U) -> tsunami::geo::TerrainTargetGrid
    {
        const auto corner = [&](double column, double row) {
            return tsunami::geo::Point2D{
                transform.origin_x + (column * transform.pixel_width) + (row * transform.row_rotation),
                transform.origin_y + (column * transform.column_rotation) + (row * transform.pixel_height)};
        };
        const auto corners = std::array{
            corner(0.0, 0.0),
            corner(static_cast<double>(width), 0.0),
            corner(0.0, static_cast<double>(height)),
            corner(static_cast<double>(width), static_cast<double>(height))};
        const auto minmax_x = std::minmax_element(corners.begin(), corners.end(), [](const auto &left, const auto &right) {
            return left.x < right.x;
        });
        const auto minmax_y = std::minmax_element(corners.begin(), corners.end(), [](const auto &left, const auto &right) {
            return left.y < right.y;
        });
        return tsunami::geo::TerrainTargetGrid{
            width,
            height,
            1.0,
            transform,
            {minmax_x.first->x, minmax_y.first->y, minmax_x.second->x, minmax_y.second->y},
            tsunami::tests::r2d_fixtures::target_reference(),
            0.0,
            static_cast<double>(width),
            0.0,
            static_cast<double>(height),
            0.0,
            0.0};
    }

    struct TerrainInput
    {
        tsunami::geo::ConditionedTerrainRaster terrain;
        tsunami::geo::TerrainConditioningRecord record;
    };

    [[nodiscard]] auto terrain_input(
        const tsunami::geo::TerrainTargetGrid &target_grid,
        std::vector<double> values,
        std::vector<std::uint8_t> mask = {},
        std::vector<tsunami::geo::TerrainCellLineage> lineage = {}) -> TerrainInput
    {
        const auto count = values.size();
        if (mask.empty()) {
            mask.assign(count, 1U);
        }
        if (lineage.empty()) {
            lineage.assign(count, tsunami::geo::TerrainCellLineage::bathymetry_selected);
        }
        auto input = tsunami::tests::r2d_fixtures::valid_terrain_record(
            target_grid,
            std::move(values),
            std::move(mask),
            std::vector<double>(count, 1.0),
            std::move(lineage),
            tsunami::geo::CorridorConstructionIdentity{
                "corridor-transfer",
                1U,
                tsunami::tests::r2d_fixtures::case_revision("terrain-transfer-case"),
                "corridor-transfer-axis",
                "corridor-transfer-output",
                "corridor-transfer-process",
                "2026-07-31T00:00:00Z"},
            "conditioned-terrain-1",
            "terrain-transfer-case",
            "conditioned-terrain-manifest");
        return TerrainInput{std::move(input.terrain), std::move(input.record)};
    }

    [[nodiscard]] auto preflight(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        std::string terrain_id = "conditioned-terrain-1") -> tsunami::r2d::RegionalGeometryPreflightReport
    {
        const auto summary = mesh.summary();
        auto report = tsunami::r2d::RegionalGeometryPreflightReport{};
        report.validation_status = "accepted";
        report.mesh_id = summary.id.value;
        report.terrain_id = std::move(terrain_id);
        report.vertex_count = summary.vertex_count;
        report.face_count = summary.face_count;
        report.cell_count = summary.cell_count;
        return report;
    }

    [[nodiscard]] auto policy(std::size_t maximum = 8U)
        -> tsunami::r2d::RegionalRasterCellTransferPolicy
    {
        return {1.0e-12, 1.0e-12, maximum};
    }

    [[nodiscard]] auto stencil_for(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const tsunami::geo::TerrainTargetGrid &target_grid,
        std::size_t maximum = 8U) -> tsunami::r2d::RegionalRasterCellTransferStencil
    {
        auto result = tsunami::r2d::make_regional_raster_cell_transfer_stencil(mesh, target_grid, policy(maximum));
        REQUIRE(result.has_value());
        return std::move(result).value();
    }

    [[nodiscard]] auto apply(
        const tsunami::fvm::FiniteVolumeMesh &mesh,
        const TerrainInput &input,
        const tsunami::r2d::RegionalRasterCellTransferStencil &stencil,
        const tsunami::r2d::RegionalGeometryPreflightReport *report = nullptr)
        -> tsunami::core::Result<tsunami::r2d::RegionalTerrainTransferResult>
    {
        const auto accepted = report == nullptr ? preflight(mesh) : *report;
        return tsunami::r2d::transfer_conditioned_terrain_to_regional_bathymetry(
            mesh,
            input.terrain,
            input.record,
            accepted,
            stencil,
            tsunami::fvm::FieldId{"regional-bed"},
            "Regional bed elevation");
    }
}

static_assert(!std::is_aggregate_v<tsunami::r2d::RegionalRasterCellTransferStencil>);
static_assert(!std::is_default_constructible_v<tsunami::r2d::RegionalRasterCellTransferStencil>);
static_assert(std::is_same_v<
    decltype(std::declval<const tsunami::r2d::RegionalRasterCellTransferStencil &>().cell_ranges()),
    std::span<const tsunami::r2d::RegionalRasterCellContributionRange>>);
static_assert(std::is_same_v<
    decltype(std::declval<const tsunami::r2d::RegionalRasterCellTransferStencil &>().mapped_area_m2()),
    std::span<const double>>);
static_assert(std::is_same_v<
    decltype(std::declval<const tsunami::r2d::RegionalRasterCellTransferStencil &>().contributions()),
    std::span<const tsunami::r2d::RegionalRasterCellContribution>>);
static_assert(!std::is_assignable_v<
    decltype((std::declval<const tsunami::r2d::RegionalRasterCellTransferStencil &>().contributions()[0].weight)),
    double>);

TEST_CASE("Regional terrain transfer conservatively preserves values and evidence", "[terrain-transfer]")
{
    const auto mesh = triangle_mesh();
    const auto target_grid = grid();
    const auto stencil = stencil_for(mesh, target_grid);

    SECTION("a constant piecewise-constant raster is preserved exactly")
    {
        const auto input = terrain_input(target_grid, {-4.25, -4.25});
        auto transferred = apply(mesh, input, stencil);
        REQUIRE(transferred.has_value());
        CHECK(transferred.value().bathymetry.local_bed_elevation({0U}) == -4.25);
        CHECK(transferred.value().bathymetry.is_bound_to(mesh));
    }

    SECTION("multiple raster cells produce the analytic area-weighted value")
    {
        const auto input = terrain_input(target_grid, {2.0, 6.0});
        auto transferred = apply(mesh, input, stencil);
        REQUIRE(transferred.has_value());
        CHECK(transferred.value().bathymetry.local_bed_elevation({0U}) == Approx(3.0));
        const auto &diagnostics = transferred.value().diagnostics;
        CHECK(diagnostics.method_id == "area_weighted_piecewise_constant_v1");
        CHECK(diagnostics.total_mesh_area_m2 == Approx(1.0));
        CHECK(diagnostics.total_mapped_terrain_area_m2 == Approx(1.0));
        CHECK(diagnostics.maximum_cell_area_residual_m2 == Approx(0.0).margin(1.0e-14));
        CHECK(diagnostics.minimum_bed_elevation_m == Approx(3.0));
        CHECK(diagnostics.maximum_bed_elevation_m == Approx(3.0));
        CHECK(diagnostics.minimum_contributors_per_cell == 2U);
        CHECK(diagnostics.maximum_contributors_per_cell == 2U);
        CHECK(diagnostics.contributor_lineage_counts.at("bathymetry_selected") == 2U);
    }

    SECTION("weights and mapped area reproduce the authoritative cell")
    {
        REQUIRE(stencil.cell_ranges().size() == 1U);
        CHECK(stencil.mapped_area_m2()[0] == Approx(1.0));
        auto sum = 0.0;
        for (const auto &contribution : stencil.contributions()) {
            sum += contribution.weight;
        }
        CHECK(sum == Approx(1.0));
        REQUIRE(stencil.contributions().size() == 2U);
        CHECK(stencil.contributions()[0].raster_cell_index == 0U);
        CHECK(stencil.contributions()[1].raster_cell_index == 1U);
        CHECK(stencil.contributions()[0].weight == Approx(0.75));
        CHECK(stencil.contributions()[1].weight == Approx(0.25));
    }

    SECTION("rebuilding produces an equal deterministic stencil")
    {
        CHECK(stencil == stencil_for(mesh, target_grid));
    }
}

TEST_CASE("Regional terrain transfer supports rotated affine grids", "[terrain-transfer]")
{
    const auto rotated_grid = grid({0.0, 0.0, -1.0, 0.0, 1.0, 0.0});
    const auto mesh = triangle_mesh({0.0, 0.0, 0.0}, {0.0, 2.0, 0.0}, {-1.0, 0.0, 0.0});
    const auto input = terrain_input(rotated_grid, {2.0, 6.0});
    const auto stencil = stencil_for(mesh, rotated_grid);
    auto transferred = apply(mesh, input, stencil);
    REQUIRE(transferred.has_value());
    CHECK(transferred.value().bathymetry.local_bed_elevation({0U}) == Approx(3.0));
}

TEST_CASE("Regional terrain transfer stencil rejects invalid geometry", "[terrain-transfer]")
{
    const auto mesh = triangle_mesh();

    SECTION("singular affine")
    {
        const auto singular = grid({0.0, 1.0, 2.0, 0.0, 0.5, 1.0});
        const auto result = tsunami::r2d::make_regional_raster_cell_transfer_stencil(mesh, singular, policy());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.affine_invalid");
    }

    SECTION("near-singular affine")
    {
        const auto nearly_singular = grid({
            0.0,
            1.0,
            1.0,
            0.0,
            1.0,
            1.0 + (32.0 * std::numeric_limits<double>::epsilon())});
        const auto result = tsunami::r2d::make_regional_raster_cell_transfer_stencil(mesh, nearly_singular, policy());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.affine_invalid");
    }

    SECTION("incomplete raster coverage")
    {
        const auto extended = triangle_mesh({0.0, 0.0, 0.0}, {2.5, 0.0, 0.0}, {0.0, 1.0, 0.0});
        const auto result = tsunami::r2d::make_regional_raster_cell_transfer_stencil(extended, grid(), policy());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.cell_uncovered");
    }

    SECTION("contributor maximum")
    {
        const auto result = tsunami::r2d::make_regional_raster_cell_transfer_stencil(mesh, grid(), policy(1U));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.contributor_limit_exceeded");
    }
}

TEST_CASE("Regional terrain transfer stencil exposes immutable contiguous ranges", "[terrain-transfer]")
{
    const auto mesh = two_cell_mesh();
    const auto target_grid = grid();
    const auto stencil = stencil_for(mesh, target_grid, 1U);
    const auto input = terrain_input(target_grid, {2.0, 6.0});

    REQUIRE(stencil.cell_ranges().size() == 2U);
    REQUIRE(stencil.contributions().size() == 2U);
    CHECK((stencil.cell_ranges()[0] == tsunami::r2d::RegionalRasterCellContributionRange{0U, 1U}));
    CHECK((stencil.cell_ranges()[1] == tsunami::r2d::RegionalRasterCellContributionRange{1U, 1U}));
    CHECK(stencil.mapped_area_m2()[0] == Approx(0.5));
    CHECK(stencil.mapped_area_m2()[1] == Approx(0.5));
    CHECK((stencil.contributions()[0] == tsunami::r2d::RegionalRasterCellContribution{0U, 0.5, 1.0}));
    CHECK((stencil.contributions()[1] == tsunami::r2d::RegionalRasterCellContribution{1U, 0.5, 1.0}));

    const auto transferred = apply(mesh, input, stencil);
    REQUIRE(transferred.has_value());
    CHECK(transferred.value().diagnostics.minimum_contributors_per_cell == 1U);
    CHECK(transferred.value().diagnostics.maximum_contributors_per_cell == 1U);
    CHECK(transferred.value().bathymetry.local_bed_elevation({0U}) == Approx(2.0));
    CHECK(transferred.value().bathymetry.local_bed_elevation({1U}) == Approx(6.0));
}

TEST_CASE("Regional terrain transfer rejects stale and unacceptable sources without mutation", "[terrain-transfer]")
{
    const auto mesh = triangle_mesh();
    const auto target_grid = grid();
    const auto stencil = stencil_for(mesh, target_grid);

    SECTION("valid terrain record succeeds")
    {
        const auto input = terrain_input(target_grid, {2.0, 6.0});
        const auto result = apply(mesh, input, stencil);
        REQUIRE(result.has_value());
        CHECK(result.value().bathymetry.local_bed_elevation({0U}) == Approx(3.0));
    }

    SECTION("invalid terrain record")
    {
        auto input = terrain_input(target_grid, {2.0, 6.0});
        input.record.schema.schema_name = "stale.terrain_record";
        const auto result = apply(mesh, input, stencil);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.record_invalid");
        REQUIRE(result.error().cause_code().has_value());
        CHECK(*result.error().cause_code() == "geo.terrain.record_invalid");
    }

    SECTION("zero active coverage threshold")
    {
        auto input = terrain_input(target_grid, {2.0, 6.0});
        input.record.grid_policy.active_coverage_threshold = 0.0;
        const auto result = apply(mesh, input, stencil);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.record_invalid");
        REQUIRE(result.error().cause_code().has_value());
        CHECK(*result.error().cause_code() == "geo.terrain.record_invalid");
    }

    SECTION("nonfinite contributing value")
    {
        auto input = terrain_input(target_grid, {2.0, std::numeric_limits<double>::quiet_NaN()});
        const auto grid_before = input.terrain.grid();
        const auto mask_before = input.terrain.valid_mask();
        const auto coverage_before = input.terrain.corridor_coverage_fraction();
        const auto lineage_before = input.terrain.cell_lineage();
        const auto stencil_before = stencil;
        auto result = apply(mesh, input, stencil);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.source_value_nonfinite");
        CHECK(input.terrain.grid() == grid_before);
        CHECK(input.terrain.values()[0] == 2.0);
        CHECK(std::isnan(input.terrain.values()[1]));
        CHECK(input.terrain.valid_mask() == mask_before);
        CHECK(input.terrain.corridor_coverage_fraction() == coverage_before);
        CHECK(input.terrain.cell_lineage() == lineage_before);
        CHECK(stencil == stencil_before);
    }

    SECTION("invalid mask")
    {
        const auto input = terrain_input(target_grid, {2.0, 6.0}, {1U, 0U});
        const auto result = apply(mesh, input, stencil);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.source_nodata");
    }

    SECTION("outside and excluded lineage")
    {
        const auto invalid_lineage = GENERATE(
            tsunami::geo::TerrainCellLineage::outside_corridor,
            tsunami::geo::TerrainCellLineage::excluded_boundary_fraction);
        const auto input = terrain_input(
            target_grid,
            {2.0, 6.0},
            {},
            {tsunami::geo::TerrainCellLineage::bathymetry_selected, invalid_lineage});
        const auto result = apply(mesh, input, stencil);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.source_lineage_invalid");
    }

    SECTION("stale preflight mesh")
    {
        const auto input = terrain_input(target_grid, {2.0, 6.0});
        auto stale = preflight(mesh);
        stale.mesh_id = "stale-mesh";
        const auto result = apply(mesh, input, stencil, &stale);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.preflight_mismatch");
    }

    SECTION("stale terrain identity")
    {
        const auto input = terrain_input(target_grid, {2.0, 6.0});
        auto stale = preflight(mesh, "stale-terrain");
        const auto result = apply(mesh, input, stencil, &stale);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.preflight_mismatch");
    }

    SECTION("stale terrain grid")
    {
        auto input = terrain_input(target_grid, {2.0, 6.0});
        input.record.grid = grid({0.0, 1.0, 0.0, 0.0, 0.0, -1.0});
        const auto result = apply(mesh, input, stencil);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.terrain_transfer.grid_mismatch");
    }
}
