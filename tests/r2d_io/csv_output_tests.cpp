#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <tsunami/r2d_io/RegionalCsvOutputWriter.hpp>

namespace
{
    class ScopedEnvironmentFlag
    {
    public:
        explicit ScopedEnvironmentFlag(const char *name)
            : name_{name}
        {
#ifdef _WIN32
            _putenv_s(name_, "1");
#else
            setenv(name_, "1", 1);
#endif
        }

        ScopedEnvironmentFlag(const ScopedEnvironmentFlag &) = delete;
        auto operator=(const ScopedEnvironmentFlag &) -> ScopedEnvironmentFlag & = delete;

        ~ScopedEnvironmentFlag()
        {
#ifdef _WIN32
            _putenv_s(name_, "");
#else
            unsetenv(name_);
#endif
        }

    private:
        const char *name_{};
    };

    [[nodiscard]] auto diagnostics_row() -> tsunami::r2d::RegionalStepDiagnostics
    {
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
        return diagnostics;
    }

    [[nodiscard]] auto snapshot_row() -> tsunami::r2d::RegionalSnapshot
    {
        auto snapshot = tsunami::r2d::RegionalSnapshot{};
        snapshot.step_index = 3U;
        snapshot.time = 0.2;
        snapshot.depth = {1.0};
        snapshot.momentum_x = {0.0};
        snapshot.momentum_y = {0.0};
        snapshot.bed_elevation = {2.0};
        snapshot.free_surface_elevation = {3.0};
        return snapshot;
    }

    [[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string
    {
        auto input = std::ifstream{path, std::ios::binary};
        REQUIRE(input);
        return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        auto value = error.context_value(key);
        REQUIRE(value.has_value());
        return *value;
    }
}

TEST_CASE("Regional CSV writer emits diagnostics and snapshot rows", "[r2d][io]")
{
    const auto output_dir = std::filesystem::temp_directory_path() / "tsunami-r2d-csv-output-test";
    std::filesystem::remove_all(output_dir);
    auto writer = tsunami::r2d_io::RegionalCsvOutputWriter{output_dir, true};
    REQUIRE(writer.prepare().has_value());

    auto diagnostics = diagnostics_row();
    REQUIRE(writer.write_diagnostics(diagnostics).has_value());

    auto snapshot = snapshot_row();
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
    std::filesystem::remove_all(output_dir);
}

TEST_CASE("Regional CSV writer reports preparation and write failures", "[r2d][io]")
{
    SECTION("diagnostics open failure is reported")
    {
        const auto output_dir = std::filesystem::temp_directory_path() / "tsunami-r2d-csv-diagnostics-open-failure";
        std::filesystem::remove_all(output_dir);
        auto writer = tsunami::r2d_io::RegionalCsvOutputWriter{output_dir, true};
        REQUIRE(writer.prepare().has_value());
        std::filesystem::create_directory(output_dir / "diagnostics.csv");

        auto result = writer.write_diagnostics(diagnostics_row());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.io.csv.open_failed");
        std::filesystem::remove_all(output_dir);
    }

    SECTION("snapshot open failure is reported")
    {
        const auto output_dir = std::filesystem::temp_directory_path() / "tsunami-r2d-csv-snapshot-open-failure";
        std::filesystem::remove_all(output_dir);
        auto writer = tsunami::r2d_io::RegionalCsvOutputWriter{output_dir, true};
        REQUIRE(writer.prepare().has_value());
        std::filesystem::create_directory(output_dir / "snapshots.csv");

        auto result = writer.write_snapshot(snapshot_row());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.io.csv.open_failed");
        std::filesystem::remove_all(output_dir);
    }

    SECTION("overwrite removal failure is reported before append")
    {
        const auto output_dir = std::filesystem::temp_directory_path() / "tsunami-r2d-csv-remove-failure";
        std::filesystem::remove_all(output_dir);
        std::filesystem::create_directories(output_dir / "diagnostics.csv");
        {
            auto child = std::ofstream{output_dir / "diagnostics.csv/child.txt"};
            child << "blocks remove\n";
        }

        auto writer = tsunami::r2d_io::RegionalCsvOutputWriter{output_dir, true};
        auto result = writer.prepare();
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code() == "r2d.io.csv.remove_failed");
        std::filesystem::remove_all(output_dir);
    }

    SECTION("forced write failure is stable and does not mark the header persisted")
    {
        const auto output_dir = std::filesystem::temp_directory_path() / "tsunami-r2d-csv-forced-write-failure";
        std::filesystem::remove_all(output_dir);
        auto writer = tsunami::r2d_io::RegionalCsvOutputWriter{output_dir, true};
        REQUIRE(writer.prepare().has_value());
        {
            const auto seam = ScopedEnvironmentFlag{"TSUNAMI_R2D_CSV_FAIL_DIAGNOSTICS_WRITE"};
            auto result = writer.write_diagnostics(diagnostics_row());
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == "r2d.io.csv.write_failed");
        }
        REQUIRE(writer.write_diagnostics(diagnostics_row()).has_value());

        std::ifstream diagnostics_file(output_dir / "diagnostics.csv");
        std::string diagnostics_text((std::istreambuf_iterator<char>(diagnostics_file)), std::istreambuf_iterator<char>());
        const auto first_header = diagnostics_text.find("step,start_time,end_time");
        REQUIRE(first_header != std::string::npos);
        CHECK(diagnostics_text.find("step,start_time,end_time", first_header + 1U) == std::string::npos);
        std::filesystem::remove_all(output_dir);
    }
}

TEST_CASE("Regional CSV writer rejects malformed snapshots before filesystem mutation", "[r2d][io]")
{
    const auto require_snapshot_invalid =
        [](std::string_view name, std::string_view field, auto mutate, std::size_t expected, std::size_t actual) {
            const auto output_dir = std::filesystem::temp_directory_path() / ("tsunami-r2d-csv-invalid-snapshot-" + std::string{name});
            std::filesystem::remove_all(output_dir);
            std::filesystem::create_directories(output_dir);
            {
                auto sentinel = std::ofstream{output_dir / "snapshots.csv", std::ios::binary};
                REQUIRE(sentinel);
                sentinel << "existing snapshot sentinel\n";
                REQUIRE(sentinel.good());
            }
            const auto before = read_text(output_dir / "snapshots.csv");
            auto writer = tsunami::r2d_io::RegionalCsvOutputWriter{output_dir, false};
            auto snapshot = snapshot_row();
            mutate(snapshot);

            auto result = writer.write_snapshot(snapshot);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().code() == "r2d.io.csv.snapshot_invalid");
            CHECK(context_value(result.error(), "field") == field);
            CHECK(context_value(result.error(), "expected") == std::to_string(expected));
            CHECK(context_value(result.error(), "actual") == std::to_string(actual));
            CHECK(read_text(output_dir / "snapshots.csv") == before);
            CHECK_FALSE(writer.output_state_changed());
            std::filesystem::remove_all(output_dir);
        };

    SECTION("short momentum_x")
    {
        require_snapshot_invalid("short-momentum-x", "momentum_x", [](auto &snapshot) {
            snapshot.depth = {1.0, 2.0};
            snapshot.momentum_x = {0.0};
            snapshot.momentum_y = {0.0, 0.0};
            snapshot.bed_elevation = {2.0, 3.0};
            snapshot.free_surface_elevation = {3.0, 4.0};
        },
                                  2U,
                                  1U);
    }

    SECTION("short momentum_y")
    {
        require_snapshot_invalid("short-momentum-y", "momentum_y", [](auto &snapshot) {
            snapshot.depth = {1.0, 2.0};
            snapshot.momentum_x = {0.0, 0.0};
            snapshot.momentum_y = {0.0};
            snapshot.bed_elevation = {2.0, 3.0};
            snapshot.free_surface_elevation = {3.0, 4.0};
        },
                                  2U,
                                  1U);
    }

    SECTION("short bed elevation")
    {
        require_snapshot_invalid("short-bed-elevation", "bed_elevation", [](auto &snapshot) {
            snapshot.depth = {1.0, 2.0};
            snapshot.momentum_x = {0.0, 0.0};
            snapshot.momentum_y = {0.0, 0.0};
            snapshot.bed_elevation = {2.0};
            snapshot.free_surface_elevation = {3.0, 4.0};
        },
                                  2U,
                                  1U);
    }

    SECTION("short free-surface elevation")
    {
        require_snapshot_invalid("short-free-surface", "free_surface_elevation", [](auto &snapshot) {
            snapshot.depth = {1.0, 2.0};
            snapshot.momentum_x = {0.0, 0.0};
            snapshot.momentum_y = {0.0, 0.0};
            snapshot.bed_elevation = {2.0, 3.0};
            snapshot.free_surface_elevation = {3.0};
        },
                                  2U,
                                  1U);
    }

    SECTION("array longer than depth")
    {
        require_snapshot_invalid("long-momentum-x", "momentum_x", [](auto &snapshot) {
            snapshot.depth = {1.0};
            snapshot.momentum_x = {0.0, 0.0};
            snapshot.momentum_y = {0.0};
            snapshot.bed_elevation = {2.0};
            snapshot.free_surface_elevation = {3.0};
        },
                                  1U,
                                  2U);
    }
}
