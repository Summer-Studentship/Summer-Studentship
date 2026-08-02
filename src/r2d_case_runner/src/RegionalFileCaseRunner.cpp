#include <tsunami/r2d_case/RegionalFileCaseRunner.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

#include <tsunami/adapters/gmsh/GmshMeshImporter.hpp>
#include <tsunami/data/CaseConfigurationParsing.hpp>
#include <tsunami/data/DatasetManifestParsing.hpp>
#include <tsunami/data/DatasetManifestValidation.hpp>
#include <tsunami/geo/ConstructedCorridor.hpp>
#include <tsunami/geo/CorridorConstructionParsing.hpp>
#include <tsunami/geo/TerrainConditioningParsing.hpp>
#include <tsunami/geo_gdal/GdalConditionedTerrainArtifacts.hpp>
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
            if (physics.earthquake.enabled) {
                return tsunami::core::failure(file_error(
                    "r2d.file_case.unsupported_earthquake_artifact",
                    "file-driven Regional2D runs do not yet support earthquake displacement artifacts",
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

        [[nodiscard]] auto output_artifacts(
            const std::filesystem::path &directory,
            bool has_earthquake_diagnostics) -> RegionalFileCaseRunOutputArtifacts
        {
            return RegionalFileCaseRunOutputArtifacts{
                directory / "diagnostics.csv",
                directory / "snapshots.csv",
                has_earthquake_diagnostics
                    ? std::optional<std::filesystem::path>{directory / "earthquake_initialisation.csv"}
                    : std::nullopt};
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

            auto prepared = tsunami::r2d::prepare_regional_case(
                tsunami::r2d::RegionalCasePreparationRequest{
                    &configuration.value(),
                    &corridor_record.value(),
                    &preflight.value(),
                    &transfer.value().diagnostics,
                    &imported_mesh.value().mesh,
                    &transfer.value().bathymetry,
                    request.policy.preparation,
                    nullptr,
                    nullptr,
                    std::nullopt,
                    std::nullopt,
                    nullptr});
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
                return written;
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
            const auto time_scale = std::max<tsunami::core::Real>(1.0, std::abs(solve_request.value().final_time));
            const auto time_tolerance = solve_request.value().time_policy.timestep_comparison_tolerance * time_scale;
            if (!solve.value().completed_successfully ||
                solve.value().accepted_step_count > solve_request.value().maximum_steps ||
                std::abs(solve.value().final_time - solve_request.value().final_time) > time_tolerance) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(file_error(
                    "r2d.file_case.solve_failed",
                    "Regional2D solve did not reach the requested final state",
                    tsunami::core::DiagnosticCategory::execution,
                    "solve",
                    request,
                    output_state_changed,
                    outputs));
            }
            auto final_evidence = validate_final_lake_at_rest(
                imported_mesh.value().mesh,
                prepared.value(),
                request.policy.preparation,
                solve.value());
            if (!final_evidence) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.solve_failed",
                    "Regional2D solve did not leave a local lake-at-rest final state",
                    tsunami::core::DiagnosticCategory::execution,
                    "solve",
                    request,
                    final_evidence.error(),
                    output_state_changed,
                    outputs));
            }

            const auto artifacts = output_artifacts(outputs, prepared.value().earthquake_diagnostics().has_value());
            if (!file_readable(artifacts.diagnostics_csv) || !file_readable(artifacts.snapshots_csv) ||
                (artifacts.earthquake_initialisation_csv && !file_readable(*artifacts.earthquake_initialisation_csv))) {
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
