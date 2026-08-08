#include <tsunami/r2d/RegionalPerformanceTiming.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>

namespace tsunami::r2d
{
    namespace
    {
        struct Accumulator
        {
            std::uint64_t count{};
            double seconds{};
        };

        struct TimingState
        {
            Accumulator solve_loop;
            Accumulator timestep_attempt;
            Accumulator source_timestep;
            Accumulator boundary_application;
            Accumulator face_residual;
            Accumulator residual_reduction;
            Accumulator cfl_reduction;
            Accumulator positivity_reduction;
            Accumulator relaxation_timestep;
            Accumulator source_update;
            Accumulator state_update;
            Accumulator state_combination;
            Accumulator integrals;
            Accumulator diagnostic_output;
            Accumulator snapshot_output;
            Accumulator final_snapshot_output;
            Accumulator output_total;
            std::uint64_t observed_openmp_threads{};
        };

        auto mutex = std::mutex{};
        auto state = TimingState{};

        [[nodiscard]] auto monotonic_seconds() -> double
        {
            using Clock = std::chrono::steady_clock;
            return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
        }

        [[nodiscard]] auto env_enabled() -> bool
        {
            const auto *path = std::getenv("TSUNAMI_R2D_TIMING_JSON");
            if (path != nullptr && std::string_view{path}.size() > 0U) {
                return true;
            }
            const auto *enabled = std::getenv("TSUNAMI_R2D_TIMING");
            return enabled != nullptr && std::string_view{enabled} == "1";
        }

        [[nodiscard]] auto accumulator(RegionalTimingRegion region, TimingState &target) -> Accumulator &
        {
            switch (region) {
            case RegionalTimingRegion::solve_loop:
                return target.solve_loop;
            case RegionalTimingRegion::timestep_attempt:
                return target.timestep_attempt;
            case RegionalTimingRegion::source_timestep:
                return target.source_timestep;
            case RegionalTimingRegion::boundary_application:
                return target.boundary_application;
            case RegionalTimingRegion::face_residual:
                return target.face_residual;
            case RegionalTimingRegion::residual_reduction:
                return target.residual_reduction;
            case RegionalTimingRegion::cfl_reduction:
                return target.cfl_reduction;
            case RegionalTimingRegion::positivity_reduction:
                return target.positivity_reduction;
            case RegionalTimingRegion::relaxation_timestep:
                return target.relaxation_timestep;
            case RegionalTimingRegion::source_update:
                return target.source_update;
            case RegionalTimingRegion::state_update:
                return target.state_update;
            case RegionalTimingRegion::state_combination:
                return target.state_combination;
            case RegionalTimingRegion::integrals:
                return target.integrals;
            case RegionalTimingRegion::diagnostic_output:
                return target.diagnostic_output;
            case RegionalTimingRegion::snapshot_output:
                return target.snapshot_output;
            case RegionalTimingRegion::final_snapshot_output:
                return target.final_snapshot_output;
            case RegionalTimingRegion::output_total:
                return target.output_total;
            }
            return target.solve_loop;
        }

        auto write_accumulator(std::ofstream &out, std::string_view name, const Accumulator &acc, bool comma) -> void
        {
            out << "    \"" << name << "\": {"
                << "\"count\": " << acc.count << ", "
                << "\"wall_s\": " << acc.seconds << "}";
            if (comma) {
                out << ',';
            }
            out << '\n';
        }
    } // namespace

    auto regional_performance_timing_enabled() -> bool
    {
        return env_enabled();
    }

    auto reset_regional_performance_timing() -> void
    {
        std::scoped_lock lock{mutex};
        state = TimingState{};
    }

    auto record_regional_timing(RegionalTimingRegion region, double wall_seconds) -> void
    {
        if (!env_enabled()) {
            return;
        }
        std::scoped_lock lock{mutex};
        auto &acc = accumulator(region, state);
        ++acc.count;
        acc.seconds += wall_seconds;
    }

    auto record_regional_observed_openmp_threads(std::uint64_t thread_count) -> void
    {
        if (!env_enabled()) {
            return;
        }
        std::scoped_lock lock{mutex};
        state.observed_openmp_threads = std::max(state.observed_openmp_threads, thread_count);
    }

    auto snapshot_regional_performance_timing() -> RegionalPerformanceTimingSnapshot
    {
        std::scoped_lock lock{mutex};
        return RegionalPerformanceTimingSnapshot{
            env_enabled(),
            state.solve_loop.count,
            state.timestep_attempt.count,
            state.source_timestep.count,
            state.boundary_application.count,
            state.face_residual.count,
            state.residual_reduction.count,
            state.cfl_reduction.count,
            state.positivity_reduction.count,
            state.relaxation_timestep.count,
            state.source_update.count,
            state.state_update.count,
            state.state_combination.count,
            state.integrals.count,
            state.diagnostic_output.count,
            state.snapshot_output.count,
            state.final_snapshot_output.count,
            state.observed_openmp_threads,
            state.solve_loop.seconds,
            state.timestep_attempt.seconds,
            state.source_timestep.seconds,
            state.boundary_application.seconds,
            state.face_residual.seconds,
            state.residual_reduction.seconds,
            state.cfl_reduction.seconds,
            state.positivity_reduction.seconds,
            state.relaxation_timestep.seconds,
            state.source_update.seconds,
            state.state_update.seconds,
            state.state_combination.seconds,
            state.integrals.seconds,
            state.diagnostic_output.seconds,
            state.snapshot_output.seconds,
            state.final_snapshot_output.seconds,
            state.output_total.seconds};
    }

    auto write_regional_performance_timing_json(const std::filesystem::path &path) -> void
    {
        if (path.empty()) {
            return;
        }
        const auto snapshot = snapshot_regional_performance_timing();
        std::filesystem::create_directories(path.parent_path());
        auto out = std::ofstream{path};
        out << "{\n";
        out << "  \"schema\": {\"name\": \"tsunami.r2d.performance_timing\", \"version\": \"1.0.0\"},\n";
        out << "  \"enabled\": " << (snapshot.enabled ? "true" : "false") << ",\n";
        out << "  \"observed_openmp_threads\": " << snapshot.observed_openmp_threads << ",\n";
        out << "  \"regions\": {\n";
        write_accumulator(out, "solve_loop", state.solve_loop, true);
        write_accumulator(out, "timestep_attempt", state.timestep_attempt, true);
        write_accumulator(out, "source_timestep", state.source_timestep, true);
        write_accumulator(out, "boundary_application", state.boundary_application, true);
        write_accumulator(out, "face_residual", state.face_residual, true);
        write_accumulator(out, "residual_reduction", state.residual_reduction, true);
        write_accumulator(out, "cfl_reduction", state.cfl_reduction, true);
        write_accumulator(out, "positivity_reduction", state.positivity_reduction, true);
        write_accumulator(out, "relaxation_timestep", state.relaxation_timestep, true);
        write_accumulator(out, "source_update", state.source_update, true);
        write_accumulator(out, "state_update", state.state_update, true);
        write_accumulator(out, "state_combination", state.state_combination, true);
        write_accumulator(out, "integrals", state.integrals, true);
        write_accumulator(out, "diagnostic_output", state.diagnostic_output, true);
        write_accumulator(out, "snapshot_output", state.snapshot_output, true);
        write_accumulator(out, "final_snapshot_output", state.final_snapshot_output, true);
        write_accumulator(out, "output_total", state.output_total, false);
        out << "  }\n";
        out << "}\n";
    }

    auto timing_region_name(RegionalTimingRegion region) -> std::string_view
    {
        switch (region) {
        case RegionalTimingRegion::solve_loop:
            return "solve_loop";
        case RegionalTimingRegion::timestep_attempt:
            return "timestep_attempt";
        case RegionalTimingRegion::source_timestep:
            return "source_timestep";
        case RegionalTimingRegion::boundary_application:
            return "boundary_application";
        case RegionalTimingRegion::face_residual:
            return "face_residual";
        case RegionalTimingRegion::residual_reduction:
            return "residual_reduction";
        case RegionalTimingRegion::cfl_reduction:
            return "cfl_reduction";
        case RegionalTimingRegion::positivity_reduction:
            return "positivity_reduction";
        case RegionalTimingRegion::relaxation_timestep:
            return "relaxation_timestep";
        case RegionalTimingRegion::source_update:
            return "source_update";
        case RegionalTimingRegion::state_update:
            return "state_update";
        case RegionalTimingRegion::state_combination:
            return "state_combination";
        case RegionalTimingRegion::integrals:
            return "integrals";
        case RegionalTimingRegion::diagnostic_output:
            return "diagnostic_output";
        case RegionalTimingRegion::snapshot_output:
            return "snapshot_output";
        case RegionalTimingRegion::final_snapshot_output:
            return "final_snapshot_output";
        case RegionalTimingRegion::output_total:
            return "output_total";
        }
        return "unknown";
    }

    RegionalScopedTimer::RegionalScopedTimer(RegionalTimingRegion region)
        : region_{region}
        , enabled_{env_enabled()}
        , start_s_{enabled_ ? monotonic_seconds() : 0.0}
    {
    }

    RegionalScopedTimer::~RegionalScopedTimer()
    {
        if (enabled_) {
            record_regional_timing(region_, monotonic_seconds() - start_s_);
        }
    }

} // namespace tsunami::r2d
