#include <charconv>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <tsunami/r2d/RegionalSolveLoop.hpp>
#include <tsunami/r2d_benchmarks/RegionalBenchmarkCases.hpp>
#include <tsunami/r2d_io/RegionalCsvOutputWriter.hpp>

namespace
{
    struct Options
    {
        std::string case_id{"lake_at_rest_flat"};
        tsunami::r2d::ExplicitIntegrationScheme scheme{tsunami::r2d::ExplicitIntegrationScheme::ssprk3};
        tsunami::core::Time final_time{-1.0};
        std::size_t maximum_steps{1000U};
        tsunami::core::Real courant_number{0.45};
        tsunami::core::Real positivity_safety_factor{0.95};
        tsunami::core::Real maximum_timestep{0.01};
        tsunami::core::Real minimum_timestep{1.0e-10};
        std::optional<tsunami::core::Real> snapshot_interval;
        std::string output_dir{"r2d-benchmark-output"};
        bool output_enabled{true};
        bool overwrite{true};
    };

    auto usage() -> void
    {
        std::cout
            << "Usage: tsunami_r2d_benchmark [options]\n"
            << "  --case <id>\n"
            << "  --scheme <forward_euler|ssprk2|ssprk3>\n"
            << "  --final-time <seconds>\n"
            << "  --max-steps <count>\n"
            << "  --courant <number>\n"
            << "  --positivity <number>\n"
            << "  --max-dt <seconds>\n"
            << "  --min-dt <seconds>\n"
            << "  --snapshot-interval <seconds>\n"
            << "  --output-dir <path>\n"
            << "  --no-output\n"
            << "  --help\n";
    }

    template <class Value>
    [[nodiscard]] auto parse_number(std::string_view text, Value &value) -> bool
    {
        const auto *begin = text.data();
        const auto *end = text.data() + text.size();
        const auto parsed = std::from_chars(begin, end, value);
        return parsed.ec == std::errc{} && parsed.ptr == end;
    }

    [[nodiscard]] auto parse_options(int argc, char **argv, Options &options) -> bool
    {
        for (int index = 1; index < argc; ++index) {
            const auto arg = std::string_view{argv[index]};
            auto require_value = [&](std::string_view name) -> const char * {
                if (index + 1 >= argc) {
                    std::cerr << "missing value for " << name << '\n';
                    return nullptr;
                }
                return argv[++index];
            };
            if (arg == "--help" || arg == "-h") {
                usage();
                return false;
            }
            if (arg == "--no-output") {
                options.output_enabled = false;
                continue;
            }
            if (arg == "--case") {
                const auto *value = require_value(arg);
                if (value == nullptr) {
                    return false;
                }
                options.case_id = value;
                continue;
            }
            if (arg == "--scheme") {
                const auto *value = require_value(arg);
                if (value == nullptr) {
                    return false;
                }
                auto scheme = tsunami::r2d::parse_explicit_integration_scheme(value);
                if (!scheme) {
                    std::cerr << scheme.error().message() << '\n';
                    return false;
                }
                options.scheme = scheme.value();
                continue;
            }
            if (arg == "--final-time" || arg == "--courant" || arg == "--positivity" ||
                arg == "--max-dt" || arg == "--min-dt" || arg == "--snapshot-interval") {
                const auto *value = require_value(arg);
                if (value == nullptr) {
                    return false;
                }
                tsunami::core::Real parsed{};
                if (!parse_number(std::string_view{value}, parsed)) {
                    std::cerr << "invalid numeric value for " << arg << '\n';
                    return false;
                }
                if (arg == "--final-time") {
                    options.final_time = parsed;
                } else if (arg == "--courant") {
                    options.courant_number = parsed;
                } else if (arg == "--positivity") {
                    options.positivity_safety_factor = parsed;
                } else if (arg == "--max-dt") {
                    options.maximum_timestep = parsed;
                } else if (arg == "--min-dt") {
                    options.minimum_timestep = parsed;
                } else {
                    options.snapshot_interval = parsed;
                }
                continue;
            }
            if (arg == "--max-steps") {
                const auto *value = require_value(arg);
                if (value == nullptr || !parse_number(std::string_view{value}, options.maximum_steps)) {
                    std::cerr << "invalid max-steps value\n";
                    return false;
                }
                continue;
            }
            if (arg == "--output-dir") {
                const auto *value = require_value(arg);
                if (value == nullptr) {
                    return false;
                }
                options.output_dir = value;
                continue;
            }
            std::cerr << "unknown option: " << arg << '\n';
            return false;
        }
        return true;
    }
} // namespace

auto main(int argc, char **argv) -> int
{
    auto options = Options{};
    if (!parse_options(argc, argv, options)) {
        return argc == 2 && std::string_view{argv[1]} == "--help" ? 0 : 2;
    }

    auto benchmark = tsunami::r2d_benchmarks::make_regional_benchmark_case(options.case_id);
    if (!benchmark) {
        std::cerr << benchmark.error().message() << '\n';
        return 1;
    }
    auto problem = std::move(benchmark).value();
    problem.time_policy.scheme = options.scheme;
    problem.time_policy.courant_number = options.courant_number;
    problem.time_policy.positivity_safety_factor = options.positivity_safety_factor;
    problem.time_policy.maximum_timestep = options.maximum_timestep;
    problem.time_policy.minimum_timestep = options.minimum_timestep;
    const auto final_time = options.final_time >= 0.0 ? options.final_time : problem.default_final_time;

    auto workspace = tsunami::r2d::make_regional_time_integration_workspace(problem.mesh, problem.simulation_state.conserved_state());
    if (!workspace) {
        std::cerr << workspace.error().message() << '\n';
        return 1;
    }

    auto output_writer = tsunami::r2d_io::RegionalCsvOutputWriter{options.output_dir, options.overwrite};
    tsunami::r2d::RegionalStepDiagnosticsSink diagnostics_sink;
    tsunami::r2d::RegionalSnapshotSink snapshot_sink;
    if (options.output_enabled) {
        auto prepared = output_writer.prepare();
        if (!prepared) {
            std::cerr << prepared.error().message() << '\n';
            return 1;
        }
        diagnostics_sink = [&](const auto &diagnostics) { return output_writer.write_diagnostics(diagnostics); };
        snapshot_sink = [&](const auto &snapshot) { return output_writer.write_snapshot(snapshot); };
    }

    auto request = tsunami::r2d::RegionalSolveRequest{
        &problem.mesh,
        &problem.bathymetry,
        &problem.depth_boundaries,
        &problem.momentum_x_boundaries,
        &problem.momentum_y_boundaries,
        &problem.bathymetry_boundaries,
        problem.state_policy,
        problem.time_policy,
        tsunami::r2d::RegionalSnapshotOutputPolicy{true, true, options.snapshot_interval},
        final_time,
        options.maximum_steps,
        diagnostics_sink,
        snapshot_sink};
    auto summary = tsunami::r2d::solve_regional_model(request, problem.simulation_state, workspace.value());
    if (!summary) {
        std::cerr << summary.error().message() << '\n';
        return 1;
    }

    std::cout << "case=" << problem.id
              << " scheme=" << tsunami::r2d::to_string(problem.time_policy.scheme)
              << " steps=" << summary.value().accepted_step_count
              << " rejected_attempts=" << summary.value().rejected_attempt_count
              << " final_time=" << summary.value().final_time
              << " water_volume=" << summary.value().final_integrals.water_volume
              << '\n';
    return summary.value().completed_successfully ? 0 : 1;
}
