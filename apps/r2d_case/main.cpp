#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include <tsunami/r2d_case/RegionalFileCaseRunner.hpp>

namespace
{
    [[nodiscard]] auto context_value(const tsunami::core::Error &error, std::string_view key) -> std::string
    {
        return error.context_value(key).value_or("");
    }

    [[nodiscard]] auto make_request(
        const std::filesystem::path &case_root,
        const std::filesystem::path &terrain_record,
        const std::filesystem::path &mesh,
        const std::optional<std::filesystem::path> &corridor_record,
        const std::string &run_id,
        const tsunami::r2d::RegionalCasePreparationPolicy &preparation,
        const tsunami::r2d::RegionalRasterCellTransferPolicy &transfer,
        bool overwrite) -> tsunami::r2d_case::RegionalFileCaseRunRequest
    {
        return tsunami::r2d_case::RegionalFileCaseRunRequest{
            case_root,
            terrain_record,
            mesh,
            corridor_record,
            run_id,
            tsunami::r2d_case::RegionalFileCaseRunPolicy{preparation, transfer},
            overwrite,
            {}};
    }
} // namespace

auto main(int argc, char **argv) -> int
{
    auto case_root = std::filesystem::path{};
    auto terrain_record = std::filesystem::path{};
    auto mesh = std::filesystem::path{};
    auto corridor_record = std::optional<std::filesystem::path>{};
    auto run_id = std::string{};
    auto overwrite = false;
    auto preparation = tsunami::r2d::RegionalCasePreparationPolicy{};
    auto transfer = tsunami::r2d::RegionalRasterCellTransferPolicy{};

    CLI::App app{"Run a file-driven Regional2D case"};
    app.add_option("--case-root", case_root, "Case root containing case.json")->required();
    app.add_option("--terrain-record", terrain_record, "Case-relative terrain conditioning record path")->required();
    app.add_option("--mesh", mesh, "Case-relative Gmsh MSH 4.1 ASCII mesh path")->required();
    app.add_option("--corridor-record", corridor_record, "Case-relative corridor construction record path");
    app.add_option("--run-id", run_id, "Run identifier used under runs/<run-id>/outputs/regional2d")->required();
    app.add_option(
           "--pre-event-free-surface-elevation-m",
           preparation.pre_event_free_surface_elevation_m,
           "Pre-event free-surface elevation in metres")
        ->required();
    app.add_option("--dry-depth-m", preparation.dry_depth_m, "Dry-cell depth threshold in metres")->required();
    app.add_option("--depth-tolerance-m", preparation.depth_tolerance_m, "Depth validation tolerance in metres")->required();
    app.add_option("--normal-tolerance", preparation.normal_tolerance, "Boundary normal comparison tolerance")->required();
    app.add_option(
           "--zero-momentum-tolerance",
           preparation.zero_momentum_tolerance,
           "Zero-momentum validation tolerance")
        ->required();
    app.add_option(
           "--transfer-absolute-area-tolerance-m2",
           transfer.absolute_area_tolerance_m2,
           "Absolute cell-area transfer tolerance in square metres")
        ->required();
    app.add_option(
           "--transfer-relative-area-tolerance",
           transfer.relative_area_tolerance,
           "Relative cell-area transfer tolerance")
        ->required();
    app.add_option(
           "--transfer-maximum-contributors",
           transfer.maximum_contributors_per_cell,
           "Maximum terrain cells contributing to one mesh cell")
        ->required();
    app.add_flag("--overwrite", overwrite, "Allow existing Regional2D CSV outputs to be replaced");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return app.exit(error);
    }

    auto result = tsunami::r2d_case::run_regional_case_from_files(
        make_request(case_root, terrain_record, mesh, corridor_record, run_id, preparation, transfer, overwrite));
    if (!result) {
        const auto &error = result.error();
        std::cerr << "error_code=" << error.code() << '\n'
                  << "error_message=" << error.message() << '\n'
                  << "stage=" << context_value(error, "stage") << '\n'
                  << "state_changed=" << context_value(error, "state_changed") << '\n';
        if (error.cause_code()) {
            std::cerr << "cause_code=" << *error.cause_code() << '\n';
        }
        return 1;
    }

    const auto &run = result.value();
    std::cout << "run_id=" << run.diagnostics.run_id << '\n'
              << "case_id=" << run.diagnostics.case_id << '\n'
              << "mesh_id=" << run.diagnostics.mesh_id << '\n'
              << "steps=" << run.diagnostics.solve.accepted_step_count << '\n'
              << "final_time=" << run.final_simulation_time << '\n'
              << "output_dir=" << run.output_directory.generic_string() << '\n';
    return 0;
}
