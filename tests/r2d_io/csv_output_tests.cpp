#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <tsunami/r2d_io/RegionalCsvOutputWriter.hpp>

TEST_CASE("Regional CSV writer emits diagnostics and snapshot rows", "[r2d][io]")
{
    const auto output_dir = std::filesystem::temp_directory_path() / "tsunami-r2d-csv-output-test";
    auto writer = tsunami::r2d_io::RegionalCsvOutputWriter{output_dir, true};
    REQUIRE(writer.prepare().has_value());

    auto diagnostics = tsunami::r2d::RegionalStepDiagnostics{};
    diagnostics.step_index = 3U;
    diagnostics.start_time = 0.1;
    diagnostics.end_time = 0.2;
    diagnostics.timestep = 0.1;
    diagnostics.scheme = tsunami::r2d::ExplicitIntegrationScheme::ssprk3;
    diagnostics.integrals.water_volume = 1.25;
    diagnostics.stable_timestep.restriction = tsunami::r2d::TimestepRestrictionKind::source;
    diagnostics.sources.maximum_manning_rate = 0.5;
    diagnostics.sources.manning_limiting_cell = tsunami::fvm::CellId{0};
    REQUIRE(writer.write_diagnostics(diagnostics).has_value());

    auto snapshot = tsunami::r2d::RegionalSnapshot{};
    snapshot.step_index = 3U;
    snapshot.time = 0.2;
    snapshot.depth = {1.0};
    snapshot.momentum_x = {0.0};
    snapshot.momentum_y = {0.0};
    snapshot.bed_elevation = {2.0};
    snapshot.free_surface_elevation = {3.0};
    REQUIRE(writer.write_snapshot(snapshot).has_value());

    REQUIRE(std::filesystem::exists(output_dir / "diagnostics.csv"));
    REQUIRE(std::filesystem::exists(output_dir / "snapshots.csv"));

    std::ifstream diagnostics_file(output_dir / "diagnostics.csv");
    std::string diagnostics_text((std::istreambuf_iterator<char>(diagnostics_file)), std::istreambuf_iterator<char>());
    REQUIRE(diagnostics_text.find("step,start_time,end_time") != std::string::npos);
    REQUIRE(diagnostics_text.find("source_restriction,manning_active_cells,coriolis_active_cells") != std::string::npos);
    REQUIRE(diagnostics_text.find("ssprk3") != std::string::npos);
    REQUIRE(diagnostics_text.find("source,0,0") != std::string::npos);

    std::ifstream snapshots_file(output_dir / "snapshots.csv");
    std::string snapshots_text((std::istreambuf_iterator<char>(snapshots_file)), std::istreambuf_iterator<char>());
    REQUIRE(snapshots_text.find("step,time,cell,depth") != std::string::npos);
    REQUIRE(snapshots_text.find("3,0.2") != std::string::npos);
    REQUIRE(snapshots_text.find(",0,1,0,0,2,3") != std::string::npos);
}
