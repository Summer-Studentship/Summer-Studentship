#include <CLI/CLI.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <tsunami/coupling/SectionExport.hpp>
#include <tsunami/r2d_case/RegionalFileCaseRunner.hpp>

namespace
{
    [[nodiscard]] auto logical_id_valid(std::string_view value) noexcept -> bool
    {
        constexpr auto max_logical_id_length = std::size_t{128U};
        if (value.empty() || value.size() > max_logical_id_length || value.find('\0') != std::string_view::npos) {
            return false;
        }
        auto expect_alnum = true;
        auto previous_separator = false;
        for (const auto ch : value) {
            const auto is_alnum = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
            const auto is_separator = ch == '.' || ch == '_' || ch == '-';
            if (is_alnum) {
                expect_alnum = false;
                previous_separator = false;
            } else if (is_separator && !expect_alnum && !previous_separator) {
                expect_alnum = true;
                previous_separator = true;
            } else {
                return false;
            }
        }
        return !expect_alnum && !previous_separator;
    }

    auto print_failure(const tsunami::core::Error &error) -> void
    {
        std::cerr << "error_code=" << error.code() << '\n'
                  << "error_message=" << error.message() << '\n';
        if (error.cause_code()) {
            std::cerr << "cause_code=" << *error.cause_code() << '\n';
        }
        auto context = std::vector<tsunami::core::ErrorContextEntry>{error.context().begin(), error.context().end()};
        std::sort(context.begin(), context.end(), [](const auto &left, const auto &right) {
            return left.key < right.key;
        });
        for (const auto &entry : context) {
            std::cerr << entry.key << '=' << entry.value << '\n';
        }
    }

    [[nodiscard]] auto make_request(
        const std::filesystem::path &case_root,
        const std::filesystem::path &terrain_record,
        const std::filesystem::path &mesh,
        const std::optional<std::filesystem::path> &corridor_record,
        tsunami::core::RunId run_id,
        const tsunami::r2d::RegionalCasePreparationPolicy &preparation,
        const tsunami::r2d::RegionalRasterCellTransferPolicy &transfer,
        bool overwrite,
        std::optional<tsunami::coupling::RegionalCouplingSectionRequest> coupling_section)
        -> tsunami::r2d_case::RegionalFileCaseRunRequest
    {
        return tsunami::r2d_case::RegionalFileCaseRunRequest{
            case_root,
            terrain_record,
            mesh,
            corridor_record,
            std::move(run_id),
            tsunami::r2d_case::RegionalFileCaseRunPolicy{preparation, transfer},
            overwrite,
            std::move(coupling_section),
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
    auto coupling_section = std::optional<std::string>{};
    auto coupling_patch = std::optional<std::string>{};
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
        "--coupling-section",
        coupling_section,
        "Coupling section identifier to export under coupling/<section-id> (for example boundary.offshore)");
    app.add_option(
        "--coupling-patch",
        coupling_patch,
        "Boundary patch name backing the coupling section; defaults to --coupling-section");
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

    if (!logical_id_valid(run_id)) {
        auto error = tsunami::core::Error{
            "r2d.file_case.request_invalid",
            "run-id must be a canonical logical identifier",
            tsunami::core::DiagnosticCategory::validation,
            tsunami::core::Severity::error};
        error.add_context("operation", "tsunami_r2d_case")
            .add_context("stage", "request")
            .add_context("state_changed", "false")
            .add_context("run_id", run_id);
        print_failure(error);
        return 1;
    }
    auto strong_run_id = tsunami::core::RunId::from_string(run_id);
    if (!strong_run_id) {
        auto error = tsunami::core::Error{
            "r2d.file_case.request_invalid",
            "run-id could not be constructed",
            tsunami::core::DiagnosticCategory::validation,
            tsunami::core::Severity::error};
        error.add_context("operation", "tsunami_r2d_case")
            .add_context("stage", "request")
            .add_context("state_changed", "false")
            .add_context("run_id", run_id);
        print_failure(error);
        return 1;
    }
    if (coupling_patch && !coupling_section) {
        auto error = tsunami::core::Error{
            "r2d.file_case.request_invalid",
            "coupling-patch requires coupling-section",
            tsunami::core::DiagnosticCategory::validation,
            tsunami::core::Severity::error};
        error.add_context("operation", "tsunami_r2d_case")
            .add_context("stage", "request")
            .add_context("state_changed", "false")
            .add_context("coupling_patch", *coupling_patch);
        print_failure(error);
        return 1;
    }

    auto result = tsunami::r2d_case::run_regional_case_from_files(
        make_request(
            case_root,
            terrain_record,
            mesh,
            corridor_record,
            *std::move(strong_run_id),
            preparation,
            transfer,
            overwrite,
            coupling_section
                ? std::optional<tsunami::coupling::RegionalCouplingSectionRequest>{
                      tsunami::coupling::RegionalCouplingSectionRequest{
                          *coupling_section,
                          coupling_patch.value_or(*coupling_section)}}
                : std::nullopt));
    if (!result) {
        print_failure(result.error());
        return 1;
    }

    const auto &run = result.value();
    std::cout << "case_id=" << run.diagnostics.case_id << '\n'
              << "case_revision=" << run.diagnostics.case_revision << '\n'
              << "manifest_id=" << run.diagnostics.manifest_id << '\n'
              << "manifest_revision=" << run.diagnostics.manifest_revision << '\n'
              << "run_id=" << run.diagnostics.run_id << '\n'
              << "corridor_id=" << run.diagnostics.corridor_id << '\n'
              << "terrain_id=" << run.diagnostics.terrain_id << '\n'
              << "mesh_id=" << run.diagnostics.mesh_id << '\n'
              << "steps=" << run.diagnostics.solve.accepted_step_count << '\n'
              << "final_time=" << run.final_simulation_time << '\n'
              << "output_dir=" << run.output_directory.generic_string() << '\n';
    if (run.output_artifacts.coupling_section) {
        std::cout << "coupling_metadata=" << run.output_artifacts.coupling_section->metadata_json.generic_string() << '\n'
                  << "coupling_samples=" << run.output_artifacts.coupling_section->samples_csv.generic_string() << '\n'
                  << "coupling_history=" << run.output_artifacts.coupling_section->history_csv.generic_string() << '\n';
    }
    return 0;
}
