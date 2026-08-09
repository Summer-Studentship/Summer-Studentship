#include <tsunami/r2d_case/RegionalFileCaseRunner.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include <tsunami/adapters/gmsh/GmshMeshImporter.hpp>
#include <tsunami/coupling/SectionExport.hpp>
#include <tsunami/data/CaseConfigurationParsing.hpp>
#include <tsunami/data/DatasetManifestParsing.hpp>
#include <tsunami/data/DatasetManifestValidation.hpp>
#include <tsunami/geo/ConstructedCorridor.hpp>
#include <tsunami/geo/CorridorConstructionParsing.hpp>
#include <tsunami/geo/TerrainConditioningParsing.hpp>
#include <tsunami/geo_gdal/GdalConditionedTerrainArtifacts.hpp>
#include <tsunami/geo_gdal/GdalEarthquakeDisplacementArtifacts.hpp>
#include <tsunami/r2d/RegionalPerformanceTiming.hpp>
#include <tsunami/r2d_io/RegionalCsvOutputWriter.hpp>

namespace tsunami::r2d_case
{
    namespace
    {
        constexpr auto operation_name = "run_regional_case_from_files";

        [[nodiscard]] auto bool_text(bool value) noexcept -> std::string_view
        {
            return value ? "true" : "false";
        }

        [[nodiscard]] auto run_id_text(const RegionalFileCaseRunRequest &request) -> std::string
        {
            return request.run_id.valid() ? request.run_id.str() : std::string{};
        }

        [[nodiscard]] auto termination_reason_text(tsunami::r2d::RegionalSolveTerminationReason reason) noexcept -> std::string_view
        {
            switch (reason) {
            case tsunami::r2d::RegionalSolveTerminationReason::final_time_reached:
                return "final_time_reached";
            case tsunami::r2d::RegionalSolveTerminationReason::maximum_steps_reached:
                return "maximum_steps_reached";
            case tsunami::r2d::RegionalSolveTerminationReason::minimum_timestep_reached:
                return "minimum_timestep_reached";
            case tsunami::r2d::RegionalSolveTerminationReason::cancelled:
                return "cancelled";
            case tsunami::r2d::RegionalSolveTerminationReason::callback_failed:
                return "callback_failed";
            case tsunami::r2d::RegionalSolveTerminationReason::numerical_failure:
                return "numerical_failure";
            }
            return "unknown";
        }

        [[nodiscard]] auto reconstruction_scheme_text(tsunami::r2d::RegionalReconstructionScheme scheme) noexcept -> std::string_view
        {
            switch (scheme) {
            case tsunami::r2d::RegionalReconstructionScheme::first_order:
                return "first_order";
            case tsunami::r2d::RegionalReconstructionScheme::limited_linear:
                return "limited_linear";
            case tsunami::r2d::RegionalReconstructionScheme::unlimited_linear_for_verification:
                return "unlimited_linear_for_verification";
            }
            return "unknown";
        }

        [[nodiscard]] auto timing_output_path() -> std::filesystem::path
        {
            const auto *path = std::getenv("TSUNAMI_R2D_TIMING_JSON");
            if (path == nullptr || std::string_view{path}.empty()) {
                return {};
            }
            return std::filesystem::path{path};
        }

        [[nodiscard]] auto file_error(
            std::string code,
            std::string message,
            tsunami::core::DiagnosticCategory category,
            std::string stage,
            const RegionalFileCaseRunRequest &request,
            bool state_changed,
            const std::filesystem::path &path = {}) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                             std::move(code),
                             std::move(message),
                             category,
                             tsunami::core::Severity::error}
                             .add_context("operation", operation_name)
                             .add_context("stage", std::move(stage))
                             .add_context("case_root", request.case_root.generic_string())
                             .add_context("run_id", run_id_text(request))
                             .add_context("state_changed", std::string{bool_text(state_changed)});
            if (!path.empty()) {
                error.add_context("path", path.generic_string());
            }
            return error;
        }

        [[nodiscard]] auto io_error(std::string code, std::string message) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::persistence,
                tsunami::core::Severity::error};
            error.add_context("operation", operation_name)
                .add_context("state_changed", "true");
            return error;
        }

        [[nodiscard]] auto wrap_failure(
            std::string code,
            std::string message,
            tsunami::core::DiagnosticCategory category,
            std::string stage,
            const RegionalFileCaseRunRequest &request,
            const tsunami::core::Error &cause,
            bool state_changed,
            const std::filesystem::path &path = {}) -> tsunami::core::Error
        {
            auto error = file_error(std::move(code), std::move(message), category, std::move(stage), request, state_changed, path);
            error.with_cause_code(cause.code());
            for (const auto &entry : cause.context()) {
                if (entry.key == "operation") {
                    error.add_context("cause_operation", entry.value);
                }
            }
            return error;
        }

        [[nodiscard]] auto has_embedded_null(std::string_view text) noexcept -> bool
        {
            return text.find('\0') != std::string_view::npos;
        }

        [[nodiscard]] auto logical_id_valid(std::string_view value) noexcept -> bool
        {
            constexpr auto max_logical_id_length = std::size_t{128U};
            if (value.empty() || value.size() > max_logical_id_length || has_embedded_null(value)) {
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
                    previous_separator = true;
                    expect_alnum = true;
                } else {
                    return false;
                }
            }
            return !expect_alnum && !previous_separator;
        }

        [[nodiscard]] auto path_is_case_relative(const std::filesystem::path &path) -> bool
        {
            if (path.empty() || path.is_absolute()) {
                return false;
            }
            for (const auto &part : path) {
                if (part == "..") {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] auto path_is_under(
            const std::filesystem::path &root,
            const std::filesystem::path &path) -> bool
        {
            auto root_it = root.begin();
            auto path_it = path.begin();
            for (; root_it != root.end() && path_it != path.end(); ++root_it, ++path_it) {
                if (*root_it != *path_it) {
                    return false;
                }
            }
            return root_it == root.end();
        }

        [[nodiscard]] auto path_query_failure(
            std::string stage,
            const RegionalFileCaseRunRequest &request,
            const std::filesystem::path &path,
            std::string reason,
            std::error_code ec = {}) -> tsunami::core::Error
        {
            auto error = file_error(
                "r2d.file_case.path_invalid",
                "filesystem path failed Regional2D case containment validation",
                tsunami::core::DiagnosticCategory::validation,
                std::move(stage),
                request,
                false,
                path);
            error.add_context("path_failure", std::move(reason));
            if (ec) {
                error.add_context("filesystem_error", ec.message());
            }
            return error;
        }

        [[nodiscard]] auto canonical_case_root(
            const RegionalFileCaseRunRequest &request) -> tsunami::core::Result<std::filesystem::path>
        {
            std::error_code ec;
            auto root = std::filesystem::weakly_canonical(request.case_root, ec);
            if (ec || root.empty() || !std::filesystem::is_directory(root, ec)) {
                return tsunami::core::failure<std::filesystem::path>(file_error(
                    "r2d.file_case.path_invalid",
                    "case root must resolve to an existing directory",
                    tsunami::core::DiagnosticCategory::validation,
                    "request",
                    request,
                    false,
                    request.case_root));
            }
            return tsunami::core::success(root);
        }

        [[nodiscard]] auto validate_existing_file_under_root(
            const std::filesystem::path &case_root,
            const std::filesystem::path &path,
            const RegionalFileCaseRunRequest &request,
            std::string stage) -> tsunami::core::Result<std::filesystem::path>
        {
            std::error_code ec;
            auto status = std::filesystem::symlink_status(path, ec);
            if (ec) {
                if (ec == std::errc::no_such_file_or_directory) {
                    return tsunami::core::failure<std::filesystem::path>(
                        path_query_failure(std::move(stage), request, path, "missing_file", ec));
                }
                return tsunami::core::failure<std::filesystem::path>(
                    path_query_failure(std::move(stage), request, path, "query_failed", ec));
            }
            if (!std::filesystem::exists(status)) {
                return tsunami::core::failure<std::filesystem::path>(
                    path_query_failure(std::move(stage), request, path, "missing_file"));
            }
            auto resolved = std::filesystem::weakly_canonical(path, ec);
            if (ec || resolved.empty()) {
                return tsunami::core::failure<std::filesystem::path>(
                    path_query_failure(std::move(stage), request, path, "query_failed", ec));
            }
            if (!path_is_under(case_root, resolved)) {
                return tsunami::core::failure<std::filesystem::path>(
                    path_query_failure(std::move(stage), request, path, "path_escape"));
            }
            const auto regular = std::filesystem::is_regular_file(resolved, ec);
            if (ec) {
                return tsunami::core::failure<std::filesystem::path>(
                    path_query_failure(std::move(stage), request, path, "query_failed", ec));
            }
            if (!regular) {
                return tsunami::core::failure<std::filesystem::path>(
                    path_query_failure(std::move(stage), request, path, "non_regular_file"));
            }
            return tsunami::core::success(resolved);
        }

        [[nodiscard]] auto resolve_input_file(
            const std::filesystem::path &case_root,
            const std::filesystem::path &relative_path,
            const RegionalFileCaseRunRequest &request,
            std::string stage) -> tsunami::core::Result<std::filesystem::path>
        {
            if (!path_is_case_relative(relative_path)) {
                return tsunami::core::failure<std::filesystem::path>(
                    path_query_failure(std::move(stage), request, relative_path, "path_escape"));
            }
            return validate_existing_file_under_root(case_root, case_root / relative_path, request, std::move(stage));
        }

        [[nodiscard]] auto check_request(
            const RegionalFileCaseRunRequest &request) -> tsunami::core::Result<void>
        {
            if (!request.run_id.valid() || !logical_id_valid(request.run_id.str()) ||
                request.policy.transfer.maximum_contributors_per_cell == 0U ||
                request.policy.transfer.absolute_area_tolerance_m2 < 0.0 ||
                request.policy.transfer.relative_area_tolerance < 0.0) {
                return tsunami::core::failure(file_error(
                    "r2d.file_case.request_invalid",
                    "regional file case request is incomplete or contains invalid policy values",
                    tsunami::core::DiagnosticCategory::validation,
                    "request",
                    request,
                    false));
            }
            auto reconstruction_valid = tsunami::r2d::validate_reconstruction_policy(request.reconstruction);
            if (!reconstruction_valid) {
                return tsunami::core::failure(wrap_failure(
                    "r2d.file_case.request_invalid",
                    "regional file case reconstruction policy is invalid",
                    tsunami::core::DiagnosticCategory::validation,
                    "request",
                    request,
                    reconstruction_valid.error(),
                    false));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto output_directory(
            const std::filesystem::path &case_root,
            std::string_view run_id) -> std::filesystem::path
        {
            return case_root / "runs" / std::string{run_id} / "outputs" / "regional2d";
        }

        [[nodiscard]] auto validate_output_directory(
            const std::filesystem::path &case_root,
            const RegionalFileCaseRunRequest &request) -> tsunami::core::Result<void>
        {
            if (!request.run_id.valid() || !logical_id_valid(request.run_id.str())) {
                return tsunami::core::failure(file_error(
                    "r2d.file_case.request_invalid",
                    "regional file case request contains an unsafe run identifier",
                    tsunami::core::DiagnosticCategory::validation,
                    "output_path",
                    request,
                    false));
            }
            const auto relative = std::filesystem::path{"runs"} / request.run_id.str() / "outputs" / "regional2d";
            const auto directory = output_directory(case_root, request.run_id.str());
            if (directory == case_root || !path_is_under(case_root, directory.lexically_normal())) {
                return tsunami::core::failure(path_query_failure("output_path", request, directory, "path_escape"));
            }
            std::error_code ec;
            auto current = case_root;
            for (const auto &part : relative) {
                current /= part;
                auto status = std::filesystem::symlink_status(current, ec);
                if (ec) {
                    if (ec == std::errc::no_such_file_or_directory) {
                        ec.clear();
                        continue;
                    }
                    return tsunami::core::failure(path_query_failure("output_path", request, current, "query_failed", ec));
                }
                if (!std::filesystem::exists(status)) {
                    continue;
                }
                auto resolved = std::filesystem::weakly_canonical(current, ec);
                if (ec || resolved.empty()) {
                    return tsunami::core::failure(path_query_failure("output_path", request, current, "query_failed", ec));
                }
                if (!path_is_under(case_root, resolved)) {
                    return tsunami::core::failure(path_query_failure("output_path", request, current, "path_escape"));
                }
                if (!std::filesystem::is_directory(resolved, ec)) {
                    if (ec) {
                        return tsunami::core::failure(path_query_failure("output_path", request, current, "query_failed", ec));
                    }
                    return tsunami::core::failure(path_query_failure("output_path", request, current, "non_directory_component"));
                }
            }
            if (std::filesystem::exists(directory, ec)) {
                if (ec) {
                    return tsunami::core::failure(path_query_failure("output_path", request, directory, "query_failed", ec));
                }
                if (!request.overwrite_existing_outputs && !std::filesystem::is_empty(directory, ec)) {
                    if (ec) {
                        return tsunami::core::failure(path_query_failure("output_path", request, directory, "query_failed", ec));
                    }
                    return tsunami::core::failure(file_error(
                    "r2d.file_case.output_prepare_failed",
                    "regional output directory already exists and is not empty",
                    tsunami::core::DiagnosticCategory::persistence,
                    "output_prepare",
                    request,
                    false,
                    directory));
                }
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto file_readable(const std::filesystem::path &path) -> bool
        {
            auto input = std::ifstream{path, std::ios::binary};
            return input.good();
        }

        [[nodiscard]] auto has_role(const tsunami::data::DatasetRecord &dataset, tsunami::data::DatasetRole role) -> bool
        {
            return std::find(dataset.roles.begin(), dataset.roles.end(), role) != dataset.roles.end();
        }

        [[nodiscard]] auto dataset_primary_asset(const tsunami::data::DatasetRecord &dataset)
            -> const tsunami::data::DatasetAsset *
        {
            const auto found = std::find_if(dataset.assets.begin(), dataset.assets.end(), [](const auto &asset) {
                return asset.role == tsunami::data::DatasetAssetRole::primary;
            });
            return found == dataset.assets.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] auto dataset_metadata_asset(const tsunami::data::DatasetRecord &dataset)
            -> const tsunami::data::DatasetAsset *
        {
            const auto found = std::find_if(dataset.assets.begin(), dataset.assets.end(), [](const auto &asset) {
                return asset.role == tsunami::data::DatasetAssetRole::metadata;
            });
            return found == dataset.assets.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] auto managed_asset_path(
            const tsunami::data::DatasetAsset &asset,
            const RegionalFileCaseRunRequest &request,
            std::string stage) -> tsunami::core::Result<std::filesystem::path>
        {
            if (asset.location.kind != tsunami::data::DatasetLocationKind::managed_path ||
                !asset.location.managed_path ||
                !path_is_case_relative(*asset.location.managed_path)) {
                return tsunami::core::failure<std::filesystem::path>(file_error(
                    "r2d.file_case.contract_mismatch",
                    "dataset asset must use a safe case-relative managed path",
                    tsunami::core::DiagnosticCategory::validation,
                    std::move(stage),
                    request,
                    false));
            }
            return tsunami::core::success(*asset.location.managed_path);
        }

        struct EarthquakeArtifactBinding
        {
            tsunami::r2d::RegionalSeabedDisplacement displacement;
            tsunami::r2d::RegionalEarthquakeSourceMetadata metadata;
            tsunami::geo_gdal::EarthquakeDisplacementArtifactReadDiagnostics diagnostics;
        };

        [[nodiscard]] auto mesh_import_failure_is_preflight_contract(const tsunami::core::Error &error) -> bool
        {
            return error.code() == "mesh.gmsh.physical_name_missing";
        }

        struct FinalLakeAtRestEvidence
        {
            tsunami::core::Real maximum_final_depth_residual_m{};
            tsunami::core::Real maximum_final_momentum_m2_per_s{};
            tsunami::core::Real final_water_volume_residual_m3{};
            tsunami::fvm::CellId limiting_final_depth_cell_id{};
            tsunami::fvm::CellId limiting_final_momentum_cell_id{};
        };

        [[nodiscard]] auto machine_tolerance(tsunami::core::Real scale) noexcept -> tsunami::core::Real
        {
            return 128.0 * std::numeric_limits<tsunami::core::Real>::epsilon() *
                   std::max(tsunami::core::Real{1.0}, std::abs(scale));
        }

        [[nodiscard]] auto final_state_error(
            std::string message,
            FinalLakeAtRestEvidence evidence = {}) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                "r2d.file_case.final_state_invalid",
                std::move(message),
                tsunami::core::DiagnosticCategory::execution,
                tsunami::core::Severity::error};
            error.add_context("maximum_final_depth_residual_m", std::to_string(evidence.maximum_final_depth_residual_m))
                .add_context("maximum_final_momentum_m2_per_s", std::to_string(evidence.maximum_final_momentum_m2_per_s))
                .add_context("final_water_volume_residual_m3", std::to_string(evidence.final_water_volume_residual_m3))
                .add_context("limiting_final_depth_cell_id", std::to_string(evidence.limiting_final_depth_cell_id.value))
                .add_context("limiting_final_momentum_cell_id", std::to_string(evidence.limiting_final_momentum_cell_id.value));
            return error;
        }

        [[nodiscard]] auto validate_final_lake_at_rest(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::r2d::RegionalPreparedCase &prepared_case,
            const tsunami::r2d::RegionalCasePreparationPolicy &preparation_policy,
            const tsunami::r2d::RegionalSolveSummary &solve_summary)
            -> tsunami::core::Result<FinalLakeAtRestEvidence>
        {
            const auto &state = prepared_case.simulation_state().conserved_state();
            const auto &bathymetry = prepared_case.bathymetry();
            if (!state.is_bound_to(mesh) || !bathymetry.is_bound_to(mesh) ||
                state.size() != mesh.summary().cell_count || bathymetry.size() != mesh.summary().cell_count) {
                return tsunami::core::failure<FinalLakeAtRestEvidence>(
                    final_state_error("Regional2D final state is not bound to the imported mesh"));
            }

            auto evidence = FinalLakeAtRestEvidence{};
            auto total_mesh_area = tsunami::core::Real{0.0};
            for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
                const auto cell_id = tsunami::fvm::CellId{index};
                const auto local_state = state.local_state(cell_id);
                const auto bed = bathymetry.local_bed_elevation(cell_id);
                const auto area = mesh.cell_geometry(cell_id).measure;
                if (!std::isfinite(local_state.depth) ||
                    !std::isfinite(local_state.momentum_x) ||
                    !std::isfinite(local_state.momentum_y) ||
                    !std::isfinite(bed) ||
                    !std::isfinite(area) ||
                    area <= 0.0) {
                    evidence.limiting_final_depth_cell_id = cell_id;
                    evidence.limiting_final_momentum_cell_id = cell_id;
                    return tsunami::core::failure<FinalLakeAtRestEvidence>(
                        final_state_error("Regional2D final state, bathymetry and cell areas must be finite", evidence));
                }

                total_mesh_area += area;
                const auto expected_depth = std::max<tsunami::core::Real>(
                    0.0,
                    preparation_policy.pre_event_free_surface_elevation_m - bed);
                const auto depth_scale = std::max(std::abs(local_state.depth), std::abs(expected_depth));
                const auto depth_residual = std::abs(local_state.depth - expected_depth);
                if (depth_residual > evidence.maximum_final_depth_residual_m) {
                    evidence.maximum_final_depth_residual_m = depth_residual;
                    evidence.limiting_final_depth_cell_id = cell_id;
                }
                const auto momentum = std::max(std::abs(local_state.momentum_x), std::abs(local_state.momentum_y));
                if (momentum > evidence.maximum_final_momentum_m2_per_s) {
                    evidence.maximum_final_momentum_m2_per_s = momentum;
                    evidence.limiting_final_momentum_cell_id = cell_id;
                }

                if (depth_residual > preparation_policy.depth_tolerance_m + machine_tolerance(depth_scale) ||
                    std::abs(local_state.momentum_x) > preparation_policy.zero_momentum_tolerance ||
                    std::abs(local_state.momentum_y) > preparation_policy.zero_momentum_tolerance) {
                    return tsunami::core::failure<FinalLakeAtRestEvidence>(
                        final_state_error("Regional2D final state is not locally a lake at rest", evidence));
                }
            }
            if (!std::isfinite(total_mesh_area) || total_mesh_area <= 0.0) {
                return tsunami::core::failure<FinalLakeAtRestEvidence>(
                    final_state_error("Regional2D final mesh area is invalid", evidence));
            }

            evidence.final_water_volume_residual_m3 = std::abs(
                solve_summary.final_integrals.water_volume -
                prepared_case.diagnostics().total_water_volume_m3);
            const auto volume_scale = std::max(
                std::abs(solve_summary.final_integrals.water_volume),
                std::abs(prepared_case.diagnostics().total_water_volume_m3));
            const auto volume_tolerance =
                preparation_policy.depth_tolerance_m * total_mesh_area +
                machine_tolerance(volume_scale);
            if (!std::isfinite(evidence.final_water_volume_residual_m3) ||
                evidence.final_water_volume_residual_m3 > volume_tolerance) {
                return tsunami::core::failure<FinalLakeAtRestEvidence>(
                    final_state_error("Regional2D final water volume is inconsistent with the prepared lake at rest", evidence));
            }
            return tsunami::core::success(evidence);
        }

        [[nodiscard]] auto validate_final_dynamic_physical_state(
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::r2d::RegionalPreparedCase &prepared_case,
            const tsunami::r2d::RegionalCasePreparationPolicy &preparation_policy,
            const tsunami::r2d::RegionalSolveSummary &solve_summary)
            -> tsunami::core::Result<FinalLakeAtRestEvidence>
        {
            const auto &state = prepared_case.simulation_state().conserved_state();
            const auto &bathymetry = prepared_case.bathymetry();
            if (!state.is_bound_to(mesh) || !bathymetry.is_bound_to(mesh) ||
                state.size() != mesh.summary().cell_count || bathymetry.size() != mesh.summary().cell_count) {
                return tsunami::core::failure<FinalLakeAtRestEvidence>(
                    final_state_error("Regional2D final dynamic state is not bound to the imported mesh"));
            }

            auto evidence = FinalLakeAtRestEvidence{};
            for (std::size_t index = 0; index < mesh.summary().cell_count; ++index) {
                const auto cell_id = tsunami::fvm::CellId{index};
                const auto local_state = state.local_state(cell_id);
                const auto bed = bathymetry.local_bed_elevation(cell_id);
                const auto area = mesh.cell_geometry(cell_id).measure;
                if (!std::isfinite(local_state.depth) ||
                    !std::isfinite(local_state.momentum_x) ||
                    !std::isfinite(local_state.momentum_y) ||
                    !std::isfinite(bed) ||
                    !std::isfinite(area) ||
                    area <= 0.0 ||
                    local_state.depth < -preparation_policy.depth_tolerance_m) {
                    evidence.limiting_final_depth_cell_id = cell_id;
                    evidence.limiting_final_momentum_cell_id = cell_id;
                    return tsunami::core::failure<FinalLakeAtRestEvidence>(
                        final_state_error("Regional2D final dynamic state must remain finite and physically admissible", evidence));
                }
                if (local_state.depth < 0.0) {
                    evidence.maximum_final_depth_residual_m = std::max(
                        evidence.maximum_final_depth_residual_m,
                        std::abs(local_state.depth));
                    evidence.limiting_final_depth_cell_id = cell_id;
                }
                const auto momentum = std::max(std::abs(local_state.momentum_x), std::abs(local_state.momentum_y));
                if (momentum > evidence.maximum_final_momentum_m2_per_s) {
                    evidence.maximum_final_momentum_m2_per_s = momentum;
                    evidence.limiting_final_momentum_cell_id = cell_id;
                }
            }
            if (!std::isfinite(solve_summary.final_integrals.water_volume)) {
                return tsunami::core::failure<FinalLakeAtRestEvidence>(
                    final_state_error("Regional2D final dynamic water volume must be finite", evidence));
            }
            evidence.final_water_volume_residual_m3 = std::abs(
                solve_summary.final_integrals.water_volume -
                prepared_case.diagnostics().total_water_volume_m3);
            if (!std::isfinite(evidence.final_water_volume_residual_m3)) {
                return tsunami::core::failure<FinalLakeAtRestEvidence>(
                    final_state_error("Regional2D final dynamic water-volume diagnostic is nonfinite", evidence));
            }
            return tsunami::core::success(evidence);
        }

        [[nodiscard]] auto unsupported_modes(
            const tsunami::data::CaseConfiguration &configuration,
            const RegionalFileCaseRunRequest &request) -> tsunami::core::Result<void>
        {
            const auto &physics = configuration.regional_2d().physics;
            if (physics.earthquake.surface_transfer == tsunami::data::SurfaceTransfer::prescribed ||
                physics.earthquake.prescribed_surface_binding.has_value() ||
                configuration.datasets().prescribed_surface.has_value()) {
                return tsunami::core::failure(file_error(
                    "r2d.file_case.unsupported_prescribed_surface_transfer",
                    "file-driven Regional2D runs do not yet support prescribed free-surface transfer artifacts",
                    tsunami::core::DiagnosticCategory::unsupported,
                    "contract",
                    request,
                    false));
            }
            if (physics.manning.kind == tsunami::data::ManningConfigurationKind::dataset) {
                return tsunami::core::failure(file_error(
                    "r2d.file_case.unsupported_manning_dataset_source",
                    "file-driven Regional2D runs do not yet support dataset-backed Manning source artifacts",
                    tsunami::core::DiagnosticCategory::unsupported,
                    "contract",
                    request,
                    false));
            }
            if (physics.coriolis.kind == tsunami::data::CoriolisConfigurationKind::dataset) {
                return tsunami::core::failure(file_error(
                    "r2d.file_case.unsupported_coriolis_dataset_source",
                    "file-driven Regional2D runs do not yet support dataset-backed Coriolis source artifacts",
                    tsunami::core::DiagnosticCategory::unsupported,
                    "contract",
                    request,
                    false));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto validate_cross_contracts(
            const tsunami::data::CaseConfiguration &configuration,
            const tsunami::data::DatasetManifest &manifest,
            const tsunami::geo::CorridorConstructionRecord &corridor,
            const tsunami::geo::TerrainConditioningRecord &terrain,
            const RegionalFileCaseRunRequest &request) -> tsunami::core::Result<void>
        {
            const auto mismatch = [&](std::string field, std::string expected, std::string actual) {
                auto error = file_error(
                    "r2d.file_case.contract_mismatch",
                    "case, manifest, corridor and terrain records do not describe the same Regional2D run contract",
                    tsunami::core::DiagnosticCategory::validation,
                    "contract",
                    request,
                    false);
                error.add_context("field", std::move(field))
                    .add_context("expected", std::move(expected))
                    .add_context("actual", std::move(actual));
                return error;
            };
            const auto case_ref_text = [](const tsunami::data::CaseRevisionRef &ref) {
                return ref.case_id.str() + "@" + std::to_string(ref.revision);
            };
            const auto manifest_ref_text = [](std::string_view id, std::uint64_t revision) {
                return std::string{id} + "@" + std::to_string(revision);
            };
            const auto require_dataset_asset = [&](std::string field, std::string_view dataset_id, std::string_view asset_id)
                -> tsunami::core::Result<void> {
                const auto *dataset = manifest.find_dataset(dataset_id);
                if (dataset == nullptr) {
                    return tsunami::core::failure(mismatch(
                        std::move(field),
                        std::string{"dataset present in loaded manifest"},
                        std::string{dataset_id}));
                }
                const auto asset_found = std::any_of(dataset->assets.begin(), dataset->assets.end(), [&](const auto &asset) {
                    return asset.asset_id == asset_id;
                });
                if (!asset_found) {
                    return tsunami::core::failure(mismatch(
                        std::move(field),
                        std::string{dataset_id} + "/" + std::string{asset_id},
                        std::string{"asset missing from loaded manifest"}));
                }
                return tsunami::core::success();
            };
            const auto require_transformation = [&](std::string field, const tsunami::geo::CoordinateTransformationIdentity &identity)
                -> tsunami::core::Result<void> {
                const auto case_ref = tsunami::data::CaseRevisionRef{
                    configuration.identity().case_id,
                    configuration.identity().revision};
                if (identity.case_revision != case_ref) {
                    return tsunami::core::failure(mismatch(field + ".case_revision", case_ref_text(case_ref), case_ref_text(identity.case_revision)));
                }
                if (identity.manifest_id != manifest.identity().manifest_id ||
                    identity.manifest_revision != manifest.identity().manifest_revision) {
                    return tsunami::core::failure(mismatch(
                        field + ".manifest",
                        manifest_ref_text(manifest.identity().manifest_id, manifest.identity().manifest_revision),
                        manifest_ref_text(identity.manifest_id, identity.manifest_revision)));
                }
                return require_dataset_asset(field + ".source_asset", identity.source_dataset_id, identity.source_asset_id);
            };
            const auto require_import = [&](std::string field, const tsunami::geo::GeospatialImportIdentity &identity)
                -> tsunami::core::Result<void> {
                const auto case_ref = tsunami::data::CaseRevisionRef{
                    configuration.identity().case_id,
                    configuration.identity().revision};
                if (identity.case_revision != case_ref) {
                    return tsunami::core::failure(mismatch(field + ".case_revision", case_ref_text(case_ref), case_ref_text(identity.case_revision)));
                }
                if (identity.manifest_id != manifest.identity().manifest_id ||
                    identity.manifest_revision != manifest.identity().manifest_revision) {
                    return tsunami::core::failure(mismatch(
                        field + ".manifest",
                        manifest_ref_text(manifest.identity().manifest_id, manifest.identity().manifest_revision),
                        manifest_ref_text(identity.manifest_id, identity.manifest_revision)));
                }
                return require_dataset_asset(field + ".asset", identity.dataset_id, identity.asset_id);
            };
            const auto case_ref = tsunami::data::CaseRevisionRef{
                configuration.identity().case_id,
                configuration.identity().revision};
            if (configuration.scenario().model_family != tsunami::data::CaseModelFamily::regional_2d) {
                return tsunami::core::failure(mismatch("scenario.model_family", "regional_2d", "non_regional_2d"));
            }
            if (manifest.identity().case_revision != case_ref) {
                return tsunami::core::failure(mismatch("manifest.case_revision", case_ref_text(case_ref), case_ref_text(manifest.identity().case_revision)));
            }
            if (corridor.identity.case_revision != case_ref) {
                return tsunami::core::failure(mismatch("corridor.identity.case_revision", case_ref_text(case_ref), case_ref_text(corridor.identity.case_revision)));
            }
            if (terrain.identity.case_revision != case_ref) {
                return tsunami::core::failure(mismatch("terrain.identity.case_revision", case_ref_text(case_ref), case_ref_text(terrain.identity.case_revision)));
            }
            if (corridor.identity.trajectory_id != configuration.regional_2d().corridor.trajectory_id) {
                return tsunami::core::failure(mismatch("corridor.identity.trajectory_id", configuration.regional_2d().corridor.trajectory_id, corridor.identity.trajectory_id));
            }
            if (corridor.scenario_id != configuration.scenario().scenario_id) {
                return tsunami::core::failure(mismatch("corridor.scenario_id", configuration.scenario().scenario_id, corridor.scenario_id));
            }
            if (corridor.target_site != configuration.scenario().target_site) {
                return tsunami::core::failure(mismatch("corridor.target_site", configuration.scenario().target_site, corridor.target_site));
            }
            if (terrain.scenario_id != configuration.scenario().scenario_id) {
                return tsunami::core::failure(mismatch("terrain.scenario_id", configuration.scenario().scenario_id, terrain.scenario_id));
            }
            if (terrain.target_site != configuration.scenario().target_site) {
                return tsunami::core::failure(mismatch("terrain.target_site", configuration.scenario().target_site, terrain.target_site));
            }
            if (terrain.identity.manifest_id != manifest.identity().manifest_id ||
                terrain.identity.manifest_revision != manifest.identity().manifest_revision) {
                return tsunami::core::failure(mismatch(
                    "terrain.identity.manifest",
                    manifest_ref_text(manifest.identity().manifest_id, manifest.identity().manifest_revision),
                    manifest_ref_text(terrain.identity.manifest_id, terrain.identity.manifest_revision)));
            }
            if (terrain.corridor_identity != corridor.identity) {
                return tsunami::core::failure(mismatch("terrain.corridor_identity", corridor.identity.corridor_id, terrain.corridor_identity.corridor_id));
            }
            if (terrain.target_reference != corridor.target_reference) {
                return tsunami::core::failure(mismatch("terrain.target_reference", "corridor.target_reference", "terrain.target_reference"));
            }
            if (terrain.bathymetry_dataset_id != configuration.datasets().bathymetry) {
                return tsunami::core::failure(mismatch("terrain.bathymetry_dataset_id", configuration.datasets().bathymetry, terrain.bathymetry_dataset_id));
            }
            if (terrain.topography_dataset_id != configuration.datasets().topography) {
                return tsunami::core::failure(mismatch("terrain.topography_dataset_id", configuration.datasets().topography, terrain.topography_dataset_id));
            }
            if (auto valid = require_transformation("corridor.epicentre.transformation", corridor.epicentre.transformation_identity); !valid) {
                return valid;
            }
            if (auto valid = require_transformation("corridor.target.transformation", corridor.target.transformation_identity); !valid) {
                return valid;
            }
            if (auto valid = require_dataset_asset("terrain.bathymetry", terrain.bathymetry_dataset_id, terrain.bathymetry_asset_id); !valid) {
                return valid;
            }
            if (auto valid = require_dataset_asset("terrain.topography", terrain.topography_dataset_id, terrain.topography_asset_id); !valid) {
                return valid;
            }
            if (auto valid = require_import("terrain.bathymetry_import", terrain.bathymetry_import_identity); !valid) {
                return valid;
            }
            if (auto valid = require_import("terrain.topography_import", terrain.topography_import_identity); !valid) {
                return valid;
            }
            if (auto valid = require_transformation("terrain.bathymetry_transformation", terrain.bathymetry_transformation_identity); !valid) {
                return valid;
            }
            if (auto valid = require_transformation("terrain.topography_transformation", terrain.topography_transformation_identity); !valid) {
                return valid;
            }
            if (terrain.bathymetry_resampling.dataset_id != terrain.bathymetry_dataset_id ||
                terrain.bathymetry_resampling.asset_id != terrain.bathymetry_asset_id ||
                terrain.bathymetry_resampling.import_identity != terrain.bathymetry_import_identity ||
                terrain.bathymetry_resampling.transformation_identity != terrain.bathymetry_transformation_identity) {
                return tsunami::core::failure(mismatch("terrain.bathymetry_resampling.identity", "record-level bathymetry identities", "resampling identities"));
            }
            if (terrain.topography_resampling.dataset_id != terrain.topography_dataset_id ||
                terrain.topography_resampling.asset_id != terrain.topography_asset_id ||
                terrain.topography_resampling.import_identity != terrain.topography_import_identity ||
                terrain.topography_resampling.transformation_identity != terrain.topography_transformation_identity) {
                return tsunami::core::failure(mismatch("terrain.topography_resampling.identity", "record-level topography identities", "resampling identities"));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto artifact_summary(
            const tsunami::geo_gdal::ConditionedTerrainArtifactReadDiagnostics &diagnostics)
            -> RegionalFileTerrainArtifactDiagnostics
        {
            return RegionalFileTerrainArtifactDiagnostics{
                diagnostics.artefact_contract_version,
                diagnostics.terrain_id,
                diagnostics.terrain_revision,
                diagnostics.width,
                diagnostics.height,
                diagnostics.cell_count,
                diagnostics.valid_terrain_cell_count,
                diagnostics.invalid_terrain_cell_count,
                diagnostics.minimum_bed_elevation_m,
                diagnostics.maximum_bed_elevation_m,
                diagnostics.validation_status};
        }

        [[nodiscard]] auto load_earthquake_artifact(
            const std::filesystem::path &case_root,
            const tsunami::data::CaseConfiguration &configuration,
            const tsunami::data::DatasetManifest &manifest,
            const tsunami::geo::TerrainConditioningRecord &terrain_record,
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const tsunami::r2d::RegionalRasterCellTransferStencil &stencil,
            const RegionalFileCaseRunRequest &request) -> tsunami::core::Result<std::optional<EarthquakeArtifactBinding>>
        {
            const auto &earthquake = configuration.regional_2d().physics.earthquake;
            if (!earthquake.enabled) {
                if (configuration.datasets().earthquake_displacement || earthquake.displacement_binding) {
                    return tsunami::core::failure<std::optional<EarthquakeArtifactBinding>>(file_error(
                        "r2d.file_case.contract_mismatch",
                        "earthquake displacement bindings are only valid when earthquake physics is enabled",
                        tsunami::core::DiagnosticCategory::validation,
                        "earthquake_artifact",
                        request,
                        false));
                }
                return tsunami::core::success(std::optional<EarthquakeArtifactBinding>{});
            }
            if (!configuration.datasets().earthquake_displacement || !earthquake.displacement_binding ||
                *configuration.datasets().earthquake_displacement != *earthquake.displacement_binding) {
                return tsunami::core::failure<std::optional<EarthquakeArtifactBinding>>(file_error(
                    "r2d.file_case.contract_mismatch",
                    "enabled earthquake physics requires a matching earthquake displacement dataset binding",
                    tsunami::core::DiagnosticCategory::validation,
                    "earthquake_artifact",
                    request,
                    false));
            }
            const auto dataset_id = *configuration.datasets().earthquake_displacement;
            const auto *dataset = manifest.find_dataset(dataset_id);
            if (dataset == nullptr ||
                dataset->origin_kind != tsunami::data::DatasetOriginKind::generated ||
                dataset->representation != tsunami::data::DatasetRepresentationKind::raster ||
                !has_role(*dataset, tsunami::data::DatasetRole::earthquake_displacement)) {
                return tsunami::core::failure<std::optional<EarthquakeArtifactBinding>>(file_error(
                    "r2d.file_case.contract_mismatch",
                    "earthquake displacement dataset must be a generated raster with the earthquake_displacement role",
                    tsunami::core::DiagnosticCategory::validation,
                    "earthquake_artifact",
                    request,
                    false));
            }
            const auto *primary = dataset_primary_asset(*dataset);
            const auto *metadata_asset = dataset_metadata_asset(*dataset);
            if (primary == nullptr || metadata_asset == nullptr) {
                return tsunami::core::failure<std::optional<EarthquakeArtifactBinding>>(file_error(
                    "r2d.file_case.contract_mismatch",
                    "earthquake displacement dataset must declare primary GeoTIFF and metadata assets",
                    tsunami::core::DiagnosticCategory::validation,
                    "earthquake_artifact",
                    request,
                    false));
            }
            auto primary_relative = managed_asset_path(*primary, request, "earthquake_artifact");
            auto metadata_relative = managed_asset_path(*metadata_asset, request, "earthquake_artifact");
            if (!primary_relative || !metadata_relative) {
                return tsunami::core::failure<std::optional<EarthquakeArtifactBinding>>(
                    !primary_relative ? primary_relative.error() : metadata_relative.error());
            }
            auto primary_path = validate_existing_file_under_root(case_root, case_root / primary_relative.value(), request, "earthquake_artifact");
            auto metadata_path = validate_existing_file_under_root(case_root, case_root / metadata_relative.value(), request, "earthquake_artifact");
            if (!primary_path || !metadata_path) {
                return tsunami::core::failure<std::optional<EarthquakeArtifactBinding>>(
                    !primary_path ? primary_path.error() : metadata_path.error());
            }
            auto artifact = tsunami::geo_gdal::read_earthquake_displacement_artifact_with_gdal(
                tsunami::geo_gdal::EarthquakeDisplacementArtifactPaths{primary_path.value(), metadata_path.value()},
                terrain_record,
                tsunami::geo_gdal::EarthquakeDisplacementArtifactReadPolicy{terrain_record.grid_policy.maximum_output_cells});
            if (!artifact) {
                return tsunami::core::failure<std::optional<EarthquakeArtifactBinding>>(wrap_failure(
                    "r2d.file_case.earthquake_artifact_read_failed",
                    "earthquake displacement artifact could not be read",
                    tsunami::core::DiagnosticCategory::input_data,
                    "earthquake_artifact",
                    request,
                    artifact.error(),
                    false,
                    primary_path.value()));
            }
            auto transfer = tsunami::r2d::transfer_scalar_raster_to_regional_cells(
                mesh,
                artifact.value().grid,
                artifact.value().vertical_displacement_m,
                artifact.value().valid_mask,
                stencil,
                dataset_id);
            if (!transfer) {
                return tsunami::core::failure<std::optional<EarthquakeArtifactBinding>>(wrap_failure(
                    "r2d.file_case.earthquake_transfer_failed",
                    "earthquake displacement raster could not be transferred to the Regional2D mesh",
                    tsunami::core::DiagnosticCategory::preparation,
                    "earthquake_transfer",
                    request,
                    transfer.error(),
                    false,
                    primary_path.value()));
            }
            auto displacement = tsunami::r2d::make_vertical_regional_seabed_displacement(mesh, std::move(transfer.value().values));
            if (!displacement) {
                return tsunami::core::failure<std::optional<EarthquakeArtifactBinding>>(wrap_failure(
                    "r2d.file_case.earthquake_transfer_failed",
                    "transferred earthquake displacement could not create a Regional2D seabed displacement",
                    tsunami::core::DiagnosticCategory::preparation,
                    "earthquake_transfer",
                    request,
                    displacement.error(),
                    false,
                    primary_path.value()));
            }
            auto metadata = tsunami::r2d::RegionalEarthquakeSourceMetadata{
                tsunami::r2d::RegionalEarthquakeSourceKind::finite_fault,
                artifact.value().metadata.event_id,
                artifact.value().metadata.model_id,
                artifact.value().metadata.source_format,
                artifact.value().metadata.coordinate_reference,
                static_cast<std::size_t>(artifact.value().metadata.subfault_count)};
            return tsunami::core::success(std::optional<EarthquakeArtifactBinding>{
                EarthquakeArtifactBinding{
                    std::move(displacement).value(),
                    std::move(metadata),
                    artifact.value().diagnostics}});
        }

        class CouplingSectionExporter
        {
        public:
            CouplingSectionExporter(
                std::filesystem::path output_directory,
                tsunami::coupling::RegionalCouplingSectionRequest request,
                tsunami::coupling::RegionalCouplingSectionExportMetadata metadata,
                bool overwrite)
                : output_directory_{std::move(output_directory)}
                , request_{std::move(request)}
                , metadata_{std::move(metadata)}
                , overwrite_{overwrite}
            {
            }

            [[nodiscard]] auto paths() const -> tsunami::coupling::RegionalCouplingSectionExportPaths
            {
                const auto directory = output_directory_ / "coupling" / request_.section_id;
                return tsunami::coupling::RegionalCouplingSectionExportPaths{
                    directory / "metadata.json",
                    directory / "samples.csv",
                    directory / "history.csv"};
            }

            [[nodiscard]] auto metadata() const noexcept -> const tsunami::coupling::RegionalCouplingSectionExportMetadata &
            {
                return metadata_;
            }

            [[nodiscard]] auto output_state_changed() const noexcept -> bool { return output_state_changed_; }

            auto prepare() -> tsunami::core::Result<void>
            {
                const auto export_paths = paths();
                std::error_code ec;
                std::filesystem::create_directories(export_paths.metadata_json.parent_path(), ec);
                if (ec) {
                    return tsunami::core::failure(io_error(
                        "r2d.coupling.export_prepare_failed",
                        "could not create Regional2D coupling section output directory"));
                }
                output_state_changed_ = true;
                if (overwrite_) {
                    for (const auto &path : {export_paths.metadata_json, export_paths.samples_csv, export_paths.history_csv}) {
                        ec.clear();
                        const auto removed = std::filesystem::remove(path, ec);
                        if (ec) {
                            return tsunami::core::failure(io_error(
                                "r2d.coupling.export_prepare_failed",
                                "could not remove existing coupling section output before overwrite"));
                        }
                        output_state_changed_ = output_state_changed_ || removed;
                    }
                }
                auto json = nlohmann::ordered_json{
                    {"contract_version", metadata_.contract_version},
                    {"section_id", metadata_.section_id},
                    {"boundary_patch_name", metadata_.boundary_patch_name},
                    {"mesh_id", metadata_.mesh_id},
                    {"sample_count", metadata_.sample_count},
                    {"fields", {"depth", "momentum_x", "momentum_y", "bed_elevation", "free_surface_elevation"}}};
                json["samples"] = nlohmann::ordered_json::array();
                for (const auto &sample : metadata_.samples) {
                    json["samples"].push_back(nlohmann::ordered_json{
                        {"local_index", sample.local_index},
                        {"cell", sample.cell_index},
                        {"face", sample.face_index},
                        {"x_m", sample.x_m},
                        {"y_m", sample.y_m}});
                }
                auto metadata_file = std::ofstream{export_paths.metadata_json, std::ios::binary | std::ios::trunc};
                if (!metadata_file) {
                    return tsunami::core::failure(io_error(
                        "r2d.coupling.export_write_failed",
                        "could not open Regional2D coupling metadata output"));
                }
                metadata_file << json.dump(2) << '\n';
                metadata_file.flush();
                if (!metadata_file.good()) {
                    return tsunami::core::failure(io_error(
                        "r2d.coupling.export_write_failed",
                        "could not write Regional2D coupling metadata output"));
                }
                samples_header_written_ = std::filesystem::exists(export_paths.samples_csv, ec);
                if (ec) {
                    return tsunami::core::failure(io_error(
                        "r2d.coupling.export_prepare_failed",
                        "could not query Regional2D coupling samples output"));
                }
                history_header_written_ = std::filesystem::exists(export_paths.history_csv, ec);
                if (ec) {
                    return tsunami::core::failure(io_error(
                        "r2d.coupling.export_prepare_failed",
                        "could not query Regional2D coupling history output"));
                }
                return tsunami::core::success();
            }

            auto write_snapshot(const tsunami::r2d::RegionalSnapshot &snapshot) -> tsunami::core::Result<void>
            {
                if (snapshot.depth.size() != snapshot.momentum_x.size() ||
                    snapshot.depth.size() != snapshot.momentum_y.size() ||
                    snapshot.depth.size() != snapshot.bed_elevation.size() ||
                    snapshot.depth.size() != snapshot.free_surface_elevation.size()) {
                    return tsunami::core::failure(io_error(
                        "r2d.coupling.snapshot_invalid",
                        "regional coupling snapshot arrays must have matching cell cardinality"));
                }
                const auto export_paths = paths();
                auto samples = std::ofstream{export_paths.samples_csv, std::ios::app};
                if (!samples) {
                    return tsunami::core::failure(io_error(
                        "r2d.coupling.export_write_failed",
                        "could not open Regional2D coupling samples output"));
                }
                samples << std::setprecision(17);
                if (!samples_header_written_) {
                    samples << "step,time,section_id,local_index,cell,face,x_m,y_m,depth,momentum_x,momentum_y,bed_elevation,free_surface_elevation\n";
                    samples_header_written_ = true;
                }
                auto maximum_depth = 0.0;
                auto maximum_speed = 0.0;
                for (const auto &sample : metadata_.samples) {
                    if (sample.cell_index >= snapshot.depth.size()) {
                        return tsunami::core::failure(io_error(
                            "r2d.coupling.snapshot_invalid",
                            "coupling section sample references a cell outside the snapshot"));
                    }
                    const auto depth = snapshot.depth[sample.cell_index];
                    const auto qx = snapshot.momentum_x[sample.cell_index];
                    const auto qy = snapshot.momentum_y[sample.cell_index];
                    maximum_depth = std::max(maximum_depth, depth);
                    maximum_speed = std::max(maximum_speed, std::hypot(qx, qy) / std::max(1.0e-12, depth));
                    samples << snapshot.step_index << ','
                            << snapshot.time << ','
                            << request_.section_id << ','
                            << sample.local_index << ','
                            << sample.cell_index << ','
                            << sample.face_index << ','
                            << sample.x_m << ','
                            << sample.y_m << ','
                            << depth << ','
                            << qx << ','
                            << qy << ','
                            << snapshot.bed_elevation[sample.cell_index] << ','
                            << snapshot.free_surface_elevation[sample.cell_index] << '\n';
                }
                samples.flush();
                if (!samples.good()) {
                    return tsunami::core::failure(io_error(
                        "r2d.coupling.export_write_failed",
                        "could not write Regional2D coupling samples output"));
                }

                auto history = std::ofstream{export_paths.history_csv, std::ios::app};
                if (!history) {
                    return tsunami::core::failure(io_error(
                        "r2d.coupling.export_write_failed",
                        "could not open Regional2D coupling history output"));
                }
                history << std::setprecision(17);
                if (!history_header_written_) {
                    history << "step,time,section_id,sample_count,maximum_depth,maximum_speed\n";
                    history_header_written_ = true;
                }
                history << snapshot.step_index << ','
                        << snapshot.time << ','
                        << request_.section_id << ','
                        << metadata_.sample_count << ','
                        << maximum_depth << ','
                        << maximum_speed << '\n';
                history.flush();
                if (!history.good()) {
                    return tsunami::core::failure(io_error(
                        "r2d.coupling.export_write_failed",
                        "could not write Regional2D coupling history output"));
                }
                output_state_changed_ = true;
                return tsunami::core::success();
            }

        private:
            std::filesystem::path output_directory_;
            tsunami::coupling::RegionalCouplingSectionRequest request_;
            tsunami::coupling::RegionalCouplingSectionExportMetadata metadata_;
            bool overwrite_{};
            bool samples_header_written_{};
            bool history_header_written_{};
            bool output_state_changed_{};
        };

        [[nodiscard]] auto make_coupling_exporter(
            const std::filesystem::path &outputs,
            const tsunami::fvm::FiniteVolumeMesh &mesh,
            const RegionalFileCaseRunRequest &request)
            -> tsunami::core::Result<std::optional<CouplingSectionExporter>>
        {
            if (!request.coupling_section) {
                return tsunami::core::success(std::optional<CouplingSectionExporter>{});
            }
            const auto &section = *request.coupling_section;
            if (!logical_id_valid(section.section_id) || section.boundary_patch_name.empty()) {
                return tsunami::core::failure<std::optional<CouplingSectionExporter>>(file_error(
                    "r2d.coupling.request_invalid",
                    "coupling section id and boundary patch name must be valid",
                    tsunami::core::DiagnosticCategory::validation,
                    "coupling_section",
                    request,
                    false,
                    outputs));
            }
            const auto *patch = static_cast<const tsunami::fvm::BoundaryPatchRecord *>(nullptr);
            for (std::size_t index = 0U; index < mesh.summary().boundary_patch_count; ++index) {
                const auto &candidate = mesh.boundary_patch(tsunami::fvm::BoundaryPatchId{index});
                if (candidate.name == section.boundary_patch_name) {
                    patch = std::addressof(candidate);
                    break;
                }
            }
            if (patch == nullptr || patch->faces.empty()) {
                return tsunami::core::failure<std::optional<CouplingSectionExporter>>(file_error(
                    "r2d.coupling.request_invalid",
                    "coupling section boundary patch is not present in the imported mesh",
                    tsunami::core::DiagnosticCategory::validation,
                    "coupling_section",
                    request,
                    false,
                    outputs));
            }
            auto metadata = tsunami::coupling::RegionalCouplingSectionExportMetadata{};
            metadata.section_id = section.section_id;
            metadata.boundary_patch_name = section.boundary_patch_name;
            metadata.mesh_id = mesh.summary().id.value;
            metadata.samples.reserve(patch->faces.size());
            for (std::size_t local = 0U; local < patch->faces.size(); ++local) {
                const auto face_id = patch->faces[local];
                const auto &face = mesh.face(face_id);
                const auto &centroid = mesh.face_geometry(face_id).centroid;
                metadata.samples.push_back(tsunami::coupling::RegionalCouplingSectionSample{
                    local,
                    face.owner.value,
                    face_id.value,
                    centroid.x,
                    centroid.y});
            }
            metadata.sample_count = metadata.samples.size();
            return tsunami::core::success(std::optional<CouplingSectionExporter>{
                CouplingSectionExporter{outputs, section, std::move(metadata), request.overwrite_existing_outputs}});
        }

        [[nodiscard]] auto output_artifacts(
            const std::filesystem::path &directory,
            bool has_earthquake_diagnostics,
            std::optional<tsunami::coupling::RegionalCouplingSectionExportPaths> coupling_paths)
            -> RegionalFileCaseRunOutputArtifacts
        {
            return RegionalFileCaseRunOutputArtifacts{
                directory / "diagnostics.csv",
                directory / "snapshots.csv",
                has_earthquake_diagnostics
                    ? std::optional<std::filesystem::path>{directory / "earthquake_initialisation.csv"}
                    : std::nullopt,
                std::move(coupling_paths)};
        }

    } // namespace

    auto run_regional_case_from_files(const RegionalFileCaseRunRequest &request)
        -> tsunami::core::Result<RegionalFileCaseRunResult>
    {
        auto output_state_changed = false;
        try {
            if (auto valid = check_request(request); !valid) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(valid.error());
            }
            auto case_root_result = canonical_case_root(request);
            if (!case_root_result) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(case_root_result.error());
            }
            const auto case_root = std::move(case_root_result).value();
            auto completed_steps = std::vector<std::string>{};

            auto case_path = resolve_input_file(
                case_root,
                std::filesystem::path{tsunami::data::authoritative_case_configuration_path},
                request,
                "case");
            if (!case_path) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(case_path.error());
            }
            auto configuration = tsunami::data::read_case_configuration(case_path.value());
            if (!configuration) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.case_read_failed",
                    "case configuration could not be read",
                    tsunami::core::DiagnosticCategory::input_data,
                    "case",
                    request,
                    configuration.error(),
                    false,
                    case_path.value()));
            }
            completed_steps.push_back("case_read");

            if (auto unsupported = unsupported_modes(configuration.value(), request); !unsupported) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(unsupported.error());
            }

            auto manifest_path = resolve_input_file(case_root, configuration.value().datasets().manifest_path, request, "manifest");
            if (!manifest_path) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(manifest_path.error());
            }
            auto manifest = tsunami::data::read_dataset_manifest(manifest_path.value());
            if (!manifest) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.manifest_read_failed",
                    "dataset manifest could not be read",
                    tsunami::core::DiagnosticCategory::input_data,
                    "manifest",
                    request,
                    manifest.error(),
                    false,
                    manifest_path.value()));
            }
            if (auto valid = tsunami::data::validate_dataset_manifest_for_case(manifest.value(), configuration.value()); !valid) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.manifest_read_failed",
                    "dataset manifest is not valid for the case configuration",
                    tsunami::core::DiagnosticCategory::validation,
                    "manifest",
                    request,
                    valid.error(),
                    false,
                    manifest_path.value()));
            }
            completed_steps.push_back("manifest_validated");

            const auto corridor_relative_path = request.corridor_record_path.value_or(
                tsunami::geo::default_corridor_construction_record_path(
                    configuration.value().regional_2d().corridor.trajectory_id));
            auto corridor_path = resolve_input_file(case_root, corridor_relative_path, request, "corridor");
            if (!corridor_path) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(corridor_path.error());
            }
            auto corridor_record = tsunami::geo::read_corridor_construction_record(corridor_path.value());
            if (!corridor_record) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.corridor_read_failed",
                    "corridor construction record could not be read",
                    tsunami::core::DiagnosticCategory::input_data,
                    "corridor",
                    request,
                    corridor_record.error(),
                    false,
                    corridor_path.value()));
            }
            auto corridor = tsunami::geo::make_constructed_corridor_from_record(corridor_record.value());
            if (!corridor) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.corridor_reconstruction_failed",
                    "constructed corridor could not be reconstructed from the accepted record",
                    tsunami::core::DiagnosticCategory::validation,
                    "corridor_reconstruction",
                    request,
                    corridor.error(),
                    false,
                    corridor_path.value()));
            }
            completed_steps.push_back("corridor_reconstructed");

            auto terrain_path = resolve_input_file(case_root, request.terrain_record_path, request, "terrain");
            if (!terrain_path) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(terrain_path.error());
            }
            auto terrain_record = tsunami::geo::read_terrain_conditioning_record(terrain_path.value());
            if (!terrain_record) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.terrain_read_failed",
                    "terrain conditioning record could not be read",
                    tsunami::core::DiagnosticCategory::input_data,
                    "terrain",
                    request,
                    terrain_record.error(),
                    false,
                    terrain_path.value()));
            }
            completed_steps.push_back("terrain_record_read");

            if (auto valid = validate_cross_contracts(configuration.value(), manifest.value(), corridor_record.value(), terrain_record.value(), request); !valid) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(valid.error());
            }
            completed_steps.push_back("contracts_validated");

            auto terrain_artifact_paths = tsunami::geo_gdal::make_conditioned_terrain_artifact_paths(case_root, terrain_record.value());
            if (!terrain_artifact_paths) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.terrain_artifact_read_failed",
                    "conditioned terrain artifact paths could not be derived from the accepted record",
                    tsunami::core::DiagnosticCategory::validation,
                    "terrain_artifacts",
                    request,
                    terrain_artifact_paths.error(),
                    false,
                    terrain_record.value().output_path));
            }
            auto terrain_artifact_primary = validate_existing_file_under_root(
                case_root,
                terrain_artifact_paths.value().terrain_path,
                request,
                "terrain_artifacts");
            if (!terrain_artifact_primary) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(terrain_artifact_primary.error());
            }
            auto terrain_artifact_coverage = validate_existing_file_under_root(
                case_root,
                terrain_artifact_paths.value().coverage_path,
                request,
                "terrain_artifacts");
            if (!terrain_artifact_coverage) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(terrain_artifact_coverage.error());
            }
            auto terrain_artifact_lineage = validate_existing_file_under_root(
                case_root,
                terrain_artifact_paths.value().lineage_path,
                request,
                "terrain_artifacts");
            if (!terrain_artifact_lineage) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(terrain_artifact_lineage.error());
            }
            auto terrain_artifacts = tsunami::geo_gdal::read_conditioned_terrain_artifacts_with_gdal(
                terrain_artifact_paths.value(),
                terrain_record.value(),
                tsunami::geo_gdal::ConditionedTerrainArtifactReadPolicy{
                    terrain_record.value().grid_policy.maximum_output_cells});
            if (!terrain_artifacts) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.terrain_artifact_read_failed",
                    "conditioned terrain artifact bundle could not be read",
                    tsunami::core::DiagnosticCategory::input_data,
                    "terrain_artifacts",
                    request,
                    terrain_artifacts.error(),
                    false,
                    terrain_record.value().output_path));
            }
            completed_steps.push_back("terrain_artifacts_read");

            auto mesh_path = resolve_input_file(case_root, request.mesh_path, request, "mesh");
            if (!mesh_path) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(mesh_path.error());
            }
            auto imported_mesh = tsunami::adapters::gmsh::import_gmsh_msh41_ascii_mesh(mesh_path.value());
            if (!imported_mesh) {
                const auto preflight_contract_failure = mesh_import_failure_is_preflight_contract(imported_mesh.error());
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    preflight_contract_failure ? "r2d.file_case.preflight_failed" : "r2d.file_case.mesh_import_failed",
                    preflight_contract_failure
                        ? "Regional2D mesh import metadata is missing a required preflight physical group"
                        : "Gmsh MSH 4.1 Regional2D mesh could not be imported",
                    preflight_contract_failure
                        ? tsunami::core::DiagnosticCategory::validation
                        : tsunami::core::DiagnosticCategory::input_data,
                    preflight_contract_failure ? "preflight" : "mesh",
                    request,
                    imported_mesh.error(),
                    false,
                    mesh_path.value()));
            }
            completed_steps.push_back("mesh_imported");

            auto preflight = tsunami::r2d::validate_regional2d_geometry_preflight(
                tsunami::r2d::RegionalGeometryPreflightRequest{
                    &corridor.value(),
                    &corridor_record.value(),
                    &terrain_artifacts.value().terrain,
                    &terrain_record.value(),
                    &imported_mesh.value().mesh,
                    tsunami::r2d::RegionalMeshImportPhysicalGroups{imported_mesh.value().metadata.physical_name_tags}});
            if (!preflight) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.preflight_failed",
                    "Regional2D geometry preflight failed",
                    tsunami::core::DiagnosticCategory::validation,
                    "preflight",
                    request,
                    preflight.error(),
                    false,
                    mesh_path.value()));
            }
            completed_steps.push_back("preflight_validated");

            auto stencil = tsunami::r2d::make_regional_raster_cell_transfer_stencil(
                imported_mesh.value().mesh,
                terrain_record.value().grid,
                request.policy.transfer);
            if (!stencil) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.terrain_transfer_failed",
                    "Regional2D raster-to-mesh terrain transfer stencil could not be built",
                    tsunami::core::DiagnosticCategory::preparation,
                    "terrain_transfer",
                    request,
                    stencil.error(),
                    false,
                    mesh_path.value()));
            }
            auto transfer = tsunami::r2d::transfer_conditioned_terrain_to_regional_bathymetry(
                imported_mesh.value().mesh,
                terrain_artifacts.value().terrain,
                terrain_record.value(),
                preflight.value(),
                stencil.value(),
                tsunami::fvm::FieldId{"regional-pre-event-bed-elevation"},
                "regional pre-event bed elevation");
            if (!transfer) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.terrain_transfer_failed",
                    "conditioned terrain could not be transferred to the Regional2D mesh",
                    tsunami::core::DiagnosticCategory::preparation,
                    "terrain_transfer",
                    request,
                    transfer.error(),
                    false,
                    mesh_path.value()));
            }
            completed_steps.push_back("terrain_transferred");

            auto earthquake_artifact = load_earthquake_artifact(
                case_root,
                configuration.value(),
                manifest.value(),
                terrain_record.value(),
                imported_mesh.value().mesh,
                stencil.value(),
                request);
            if (!earthquake_artifact) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(earthquake_artifact.error());
            }
            if (earthquake_artifact.value()) {
                completed_steps.push_back("earthquake_artifact_transferred");
            }

            auto prepared = tsunami::r2d::prepare_regional_case(
                tsunami::r2d::RegionalCasePreparationRequest{
                    &configuration.value(),
                    &corridor_record.value(),
                    &preflight.value(),
                    &transfer.value().diagnostics,
                    &imported_mesh.value().mesh,
                    &transfer.value().bathymetry,
                    request.policy.preparation,
                    earthquake_artifact.value()
                        ? &earthquake_artifact.value()->displacement
                        : nullptr,
                    nullptr,
                    std::nullopt,
                    std::nullopt,
                    earthquake_artifact.value()
                        ? &earthquake_artifact.value()->metadata
                        : nullptr});
            if (!prepared) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.preparation_failed",
                    "Regional2D case preparation failed",
                    tsunami::core::DiagnosticCategory::preparation,
                    "case_preparation",
                    request,
                    prepared.error(),
                    false,
                    mesh_path.value()));
            }
            completed_steps.push_back("case_prepared");

            if (auto allowed = validate_output_directory(case_root, request); !allowed) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(allowed.error());
            }
            const auto outputs = output_directory(case_root, request.run_id.str());

            auto writer = tsunami::r2d_io::RegionalCsvOutputWriter{outputs, request.overwrite_existing_outputs};
            auto output_prepared = writer.prepare();
            output_state_changed = writer.output_state_changed();
            if (!output_prepared) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.output_prepare_failed",
                    "regional CSV output directory could not be prepared",
                    tsunami::core::DiagnosticCategory::persistence,
                    "output_prepare",
                    request,
                    output_prepared.error(),
                    output_state_changed,
                    outputs));
            }
            completed_steps.push_back("outputs_prepared");
            auto coupling_exporter = make_coupling_exporter(outputs, imported_mesh.value().mesh, request);
            if (!coupling_exporter) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(coupling_exporter.error());
            }
            if (coupling_exporter.value()) {
                auto coupling_prepared = coupling_exporter.value()->prepare();
                output_state_changed = output_state_changed || coupling_exporter.value()->output_state_changed();
                if (!coupling_prepared) {
                    return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                        "r2d.file_case.output_prepare_failed",
                        "regional coupling section outputs could not be prepared",
                        tsunami::core::DiagnosticCategory::persistence,
                        "output_prepare",
                        request,
                        coupling_prepared.error(),
                        output_state_changed,
                        outputs));
                }
                completed_steps.push_back("coupling_section_prepared");
            }
            if (const auto *forced = std::getenv("TSUNAMI_R2D_FILE_RUNNER_THROW_AFTER_OUTPUT_PREPARE");
                forced != nullptr && std::string_view{forced} == "1") {
                throw std::runtime_error{"forced Regional2D file-runner exception after output preparation"};
            }

            auto diagnostics_sink = [&](const tsunami::r2d::RegionalStepDiagnostics &diagnostics) {
                auto written = writer.write_diagnostics(diagnostics);
                output_state_changed = output_state_changed || writer.output_state_changed();
                return written;
            };
            auto snapshot_sink = [&](const tsunami::r2d::RegionalSnapshot &snapshot) {
                auto written = writer.write_snapshot(snapshot);
                output_state_changed = output_state_changed || writer.output_state_changed();
                if (!written) {
                    return written;
                }
                if (coupling_exporter.value()) {
                    auto coupling_written = coupling_exporter.value()->write_snapshot(snapshot);
                    output_state_changed = output_state_changed || coupling_exporter.value()->output_state_changed();
                    return coupling_written;
                }
                return tsunami::core::success();
            };
            if (prepared.value().earthquake_diagnostics()) {
                auto written = writer.write_earthquake_initialisation(*prepared.value().earthquake_diagnostics());
                output_state_changed = output_state_changed || writer.output_state_changed();
                if (!written) {
                    return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                        "r2d.file_case.output_prepare_failed",
                        "regional earthquake initialisation CSV could not be written",
                        tsunami::core::DiagnosticCategory::persistence,
                        "output_prepare",
                        request,
                        written.error(),
                        output_state_changed,
                        outputs));
                }
            }

            auto solve_request = tsunami::r2d::make_regional_solve_request(
                imported_mesh.value().mesh,
                prepared.value(),
                diagnostics_sink,
                snapshot_sink,
                request.stop_token);
            if (!solve_request) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.preparation_failed",
                    "Regional2D solve request could not be constructed",
                    tsunami::core::DiagnosticCategory::preparation,
                    "solve_request",
                    request,
                    solve_request.error(),
                    output_state_changed,
                    outputs));
            }
            solve_request.value().time_policy.reconstruction = request.reconstruction;

            if (tsunami::r2d::regional_performance_timing_enabled()) {
                tsunami::r2d::reset_regional_performance_timing();
            }
            auto solve = tsunami::r2d::solve_regional_model(
                solve_request.value(),
                prepared.value().simulation_state(),
                prepared.value().workspace());
            if (!solve) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.solve_failed",
                    "Regional2D solve failed after output preparation",
                    tsunami::core::DiagnosticCategory::execution,
                    "solve",
                    request,
                    solve.error(),
                    output_state_changed,
                    outputs));
            }
            if (const auto timing_path = timing_output_path(); !timing_path.empty()) {
                tsunami::r2d::write_regional_performance_timing_json(timing_path);
            }
            const auto time_scale = std::max<tsunami::core::Real>(1.0, std::abs(solve_request.value().final_time));
            const auto time_tolerance = solve_request.value().time_policy.timestep_comparison_tolerance * time_scale;
            if (!solve.value().completed_successfully ||
                solve.value().accepted_step_count > solve_request.value().maximum_steps ||
                std::abs(solve.value().final_time - solve_request.value().final_time) > time_tolerance) {
                auto error = file_error(
                    "r2d.file_case.solve_failed",
                    "Regional2D solve did not reach the requested final state",
                    tsunami::core::DiagnosticCategory::execution,
                    "solve",
                    request,
                    output_state_changed,
                    outputs);
                error.add_context("termination_reason", std::string{termination_reason_text(solve.value().termination_reason)})
                    .add_context("accepted_step_count", std::to_string(solve.value().accepted_step_count))
                    .add_context("rejected_attempt_count", std::to_string(solve.value().rejected_attempt_count))
                    .add_context("achieved_final_time_s", std::to_string(solve.value().final_time))
                    .add_context("requested_final_time_s", std::to_string(solve_request.value().final_time))
                    .add_context("last_timestep_s", std::to_string(solve.value().last_timestep));
                return tsunami::core::failure<RegionalFileCaseRunResult>(std::move(error));
            }
            auto final_evidence = prepared.value().earthquake_diagnostics()
                ? validate_final_dynamic_physical_state(
                      imported_mesh.value().mesh,
                      prepared.value(),
                      request.policy.preparation,
                      solve.value())
                : validate_final_lake_at_rest(
                      imported_mesh.value().mesh,
                      prepared.value(),
                      request.policy.preparation,
                      solve.value());
            if (!final_evidence) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.solve_failed",
                    prepared.value().earthquake_diagnostics()
                        ? "Regional2D solve did not leave a finite admissible dynamic final state"
                        : "Regional2D solve did not leave a local lake-at-rest final state",
                    tsunami::core::DiagnosticCategory::execution,
                    "solve",
                    request,
                    final_evidence.error(),
                    output_state_changed,
                    outputs));
            }

            auto coupling_paths = coupling_exporter.value()
                ? std::optional<tsunami::coupling::RegionalCouplingSectionExportPaths>{coupling_exporter.value()->paths()}
                : std::nullopt;
            const auto artifacts = output_artifacts(
                outputs,
                prepared.value().earthquake_diagnostics().has_value(),
                coupling_paths);
            if (!file_readable(artifacts.diagnostics_csv) || !file_readable(artifacts.snapshots_csv) ||
                (artifacts.earthquake_initialisation_csv && !file_readable(*artifacts.earthquake_initialisation_csv)) ||
                (artifacts.coupling_section &&
                 (!file_readable(artifacts.coupling_section->metadata_json) ||
                  !file_readable(artifacts.coupling_section->samples_csv) ||
                  !file_readable(artifacts.coupling_section->history_csv)))) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(file_error(
                    "r2d.file_case.output_prepare_failed",
                    "regional CSV outputs were not closed into readable files",
                    tsunami::core::DiagnosticCategory::persistence,
                    "output_verify",
                    request,
                    output_state_changed,
                    outputs));
            }
            completed_steps.push_back("solve_completed");

            auto diagnostics = RegionalFileCaseRunDiagnostics{};
            diagnostics.case_id = configuration.value().identity().case_id.str();
            diagnostics.case_revision = configuration.value().identity().revision;
            diagnostics.scenario_id = configuration.value().scenario().scenario_id;
            diagnostics.target_site = configuration.value().scenario().target_site;
            diagnostics.manifest_id = manifest.value().identity().manifest_id;
            diagnostics.manifest_revision = manifest.value().identity().manifest_revision;
            diagnostics.corridor_id = corridor_record.value().identity.corridor_id;
            diagnostics.corridor_revision = corridor_record.value().identity.corridor_revision;
            diagnostics.terrain_id = terrain_record.value().identity.terrain_id;
            diagnostics.terrain_revision = terrain_record.value().identity.terrain_revision;
            diagnostics.mesh_id = imported_mesh.value().mesh.summary().id.value;
            diagnostics.run_id = request.run_id.str();
            diagnostics.reconstruction_scheme = std::string{reconstruction_scheme_text(request.reconstruction.scheme)};
            diagnostics.terrain_artifacts = artifact_summary(terrain_artifacts.value().diagnostics);
            diagnostics.preflight = preflight.value();
            diagnostics.terrain_transfer = transfer.value().diagnostics;
            diagnostics.preparation = prepared.value().diagnostics();
            diagnostics.solve = solve.value();
            diagnostics.maximum_final_depth_residual_m = final_evidence.value().maximum_final_depth_residual_m;
            diagnostics.maximum_final_momentum_m2_per_s = final_evidence.value().maximum_final_momentum_m2_per_s;
            diagnostics.final_water_volume_residual_m3 = final_evidence.value().final_water_volume_residual_m3;
            diagnostics.limiting_final_depth_cell_id = final_evidence.value().limiting_final_depth_cell_id.value;
            diagnostics.limiting_final_momentum_cell_id = final_evidence.value().limiting_final_momentum_cell_id.value;
            diagnostics.earthquake_initialised = prepared.value().earthquake_diagnostics().has_value();
            if (prepared.value().earthquake_diagnostics()) {
                diagnostics.earthquake_event_id = prepared.value().earthquake_diagnostics()->metadata.event_id;
                diagnostics.earthquake_model_id = prepared.value().earthquake_diagnostics()->metadata.model_id;
            }
            if (coupling_exporter.value()) {
                diagnostics.coupling_section = coupling_exporter.value()->metadata();
            }
            diagnostics.completed_steps = std::move(completed_steps);

            return tsunami::core::success(RegionalFileCaseRunResult{
                case_root,
                outputs,
                std::move(diagnostics),
                artifacts,
                solve.value().final_time});
        } catch (const std::exception &ex) {
            return tsunami::core::failure<RegionalFileCaseRunResult>(
                file_error(
                    "r2d.file_case.request_invalid",
                    ex.what(),
                    tsunami::core::DiagnosticCategory::internal,
                    "exception",
                    request,
                    output_state_changed));
        } catch (...) {
            return tsunami::core::failure<RegionalFileCaseRunResult>(
                file_error(
                    "r2d.file_case.request_invalid",
                    "unexpected exception escaped Regional2D file case orchestration",
                    tsunami::core::DiagnosticCategory::internal,
                    "exception",
                    request,
                    output_state_changed));
        }
    }

} // namespace tsunami::r2d_case
