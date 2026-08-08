#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace tsunami::r2d
{
    enum class RegionalTimingRegion
    {
        solve_loop,
        timestep_attempt,
        source_timestep,
        boundary_application,
        face_residual,
        residual_reduction,
        cfl_reduction,
        positivity_reduction,
        relaxation_timestep,
        source_update,
        state_update,
        state_combination,
        integrals,
        diagnostic_output,
        snapshot_output,
        final_snapshot_output,
        output_total
    };

    struct RegionalPerformanceTimingSnapshot
    {
        bool enabled{};
        std::uint64_t solve_loop_count{};
        std::uint64_t timestep_attempt_count{};
        std::uint64_t source_timestep_count{};
        std::uint64_t boundary_application_count{};
        std::uint64_t face_residual_count{};
        std::uint64_t residual_reduction_count{};
        std::uint64_t cfl_reduction_count{};
        std::uint64_t positivity_reduction_count{};
        std::uint64_t relaxation_timestep_count{};
        std::uint64_t source_update_count{};
        std::uint64_t state_update_count{};
        std::uint64_t state_combination_count{};
        std::uint64_t integrals_count{};
        std::uint64_t diagnostic_output_count{};
        std::uint64_t snapshot_output_count{};
        std::uint64_t final_snapshot_output_count{};
        std::uint64_t observed_openmp_threads{};
        double solve_loop_wall_s{};
        double timestep_attempt_wall_s{};
        double source_timestep_wall_s{};
        double boundary_application_wall_s{};
        double face_residual_wall_s{};
        double residual_reduction_wall_s{};
        double cfl_reduction_wall_s{};
        double positivity_reduction_wall_s{};
        double relaxation_timestep_wall_s{};
        double source_update_wall_s{};
        double state_update_wall_s{};
        double state_combination_wall_s{};
        double integrals_wall_s{};
        double diagnostic_output_wall_s{};
        double snapshot_output_wall_s{};
        double final_snapshot_output_wall_s{};
        double output_total_wall_s{};
    };

    [[nodiscard]] auto regional_performance_timing_enabled() -> bool;
    auto reset_regional_performance_timing() -> void;
    auto record_regional_timing(RegionalTimingRegion region, double wall_seconds) -> void;
    auto record_regional_observed_openmp_threads(std::uint64_t thread_count) -> void;
    [[nodiscard]] auto snapshot_regional_performance_timing() -> RegionalPerformanceTimingSnapshot;
    auto write_regional_performance_timing_json(const std::filesystem::path &path) -> void;
    [[nodiscard]] auto timing_region_name(RegionalTimingRegion region) -> std::string_view;

    class RegionalScopedTimer
    {
    public:
        explicit RegionalScopedTimer(RegionalTimingRegion region);
        RegionalScopedTimer(const RegionalScopedTimer &) = delete;
        auto operator=(const RegionalScopedTimer &) -> RegionalScopedTimer & = delete;
        RegionalScopedTimer(RegionalScopedTimer &&) = delete;
        auto operator=(RegionalScopedTimer &&) -> RegionalScopedTimer & = delete;
        ~RegionalScopedTimer();

    private:
        RegionalTimingRegion region_;
        bool enabled_{};
        double start_s_{};
    };

} // namespace tsunami::r2d
