#include <tsunami/r2d_case/RegionalFileCaseRunner.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
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
                             .add_context("run_id", request.run_id)
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

        [[nodiscard]] auto run_id_valid(std::string_view value) noexcept -> bool
        {
            if (value.empty() || value == "." || value == "..") {
                return false;
            }
            return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                return std::isalnum(ch) || ch == '-' || ch == '_';
            });
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

        [[nodiscard]] auto resolve_input_file(
            const std::filesystem::path &case_root,
            const std::filesystem::path &relative_path,
            const RegionalFileCaseRunRequest &request,
            std::string stage) -> tsunami::core::Result<std::filesystem::path>
        {
            if (!path_is_case_relative(relative_path)) {
                return tsunami::core::failure<std::filesystem::path>(file_error(
                    "r2d.file_case.path_invalid",
                    "input paths must be non-empty case-relative paths without parent traversal",
                    tsunami::core::DiagnosticCategory::validation,
                    std::move(stage),
                    request,
                    false,
                    relative_path));
            }
            std::error_code ec;
            auto resolved = std::filesystem::weakly_canonical(case_root / relative_path, ec);
            if (ec || resolved.empty() || !path_is_under(case_root, resolved) ||
                !std::filesystem::is_regular_file(resolved, ec)) {
                return tsunami::core::failure<std::filesystem::path>(file_error(
                    "r2d.file_case.path_invalid",
                    "input path must resolve to a regular file inside the case root",
                    tsunami::core::DiagnosticCategory::validation,
                    std::move(stage),
                    request,
                    false,
                    relative_path));
            }
            return tsunami::core::success(resolved);
        }

        [[nodiscard]] auto check_request(
            const RegionalFileCaseRunRequest &request) -> tsunami::core::Result<void>
        {
            if (!run_id_valid(request.run_id) ||
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

        [[nodiscard]] auto ensure_output_allowed(
            const std::filesystem::path &directory,
            const RegionalFileCaseRunRequest &request) -> tsunami::core::Result<void>
        {
            std::error_code ec;
            if (std::filesystem::exists(directory, ec) && !request.overwrite_existing_outputs &&
                !std::filesystem::is_empty(directory, ec)) {
                return tsunami::core::failure(file_error(
                    "r2d.file_case.output_prepare_failed",
                    "regional output directory already exists and is not empty",
                    tsunami::core::DiagnosticCategory::persistence,
                    "output_prepare",
                    request,
                    false,
                    directory));
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto file_readable(const std::filesystem::path &path) -> bool
        {
            auto input = std::ifstream{path, std::ios::binary};
            return input.good();
        }

        [[nodiscard]] auto unsupported_modes(
            const tsunami::data::CaseConfiguration &configuration,
            const RegionalFileCaseRunRequest &request) -> tsunami::core::Result<void>
        {
            const auto &physics = configuration.regional_2d().physics;
            if (physics.earthquake.enabled) {
                return tsunami::core::failure(file_error(
                    "r2d.file_case.unsupported_earthquake_artifact",
                    "file-driven Regional2D runs do not yet support earthquake displacement artifacts",
                    tsunami::core::DiagnosticCategory::unsupported,
                    "contract",
                    request,
                    false));
            }
            if (physics.manning.kind == tsunami::data::ManningConfigurationKind::dataset ||
                physics.coriolis.kind == tsunami::data::CoriolisConfigurationKind::dataset ||
                physics.earthquake.surface_transfer == tsunami::data::SurfaceTransfer::prescribed ||
                physics.earthquake.prescribed_surface_binding.has_value() ||
                configuration.datasets().prescribed_surface.has_value()) {
                return tsunami::core::failure(file_error(
                    "r2d.file_case.unsupported_dataset_source",
                    "file-driven Regional2D runs require uniform/constant sources and passive free-surface transfer",
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
            const auto case_ref = tsunami::data::CaseRevisionRef{
                configuration.identity().case_id,
                configuration.identity().revision};
            if (configuration.scenario().model_family != tsunami::data::CaseModelFamily::regional_2d ||
                manifest.identity().case_revision != case_ref ||
                corridor.identity.case_revision != case_ref ||
                terrain.identity.case_revision != case_ref ||
                corridor.identity.trajectory_id != configuration.regional_2d().corridor.trajectory_id ||
                corridor.scenario_id != configuration.scenario().scenario_id ||
                corridor.target_site != configuration.scenario().target_site ||
                terrain.scenario_id != configuration.scenario().scenario_id ||
                terrain.target_site != configuration.scenario().target_site ||
                terrain.identity.manifest_id != manifest.identity().manifest_id ||
                terrain.identity.manifest_revision != manifest.identity().manifest_revision ||
                terrain.corridor_identity != corridor.identity ||
                terrain.target_reference != corridor.target_reference ||
                terrain.bathymetry_dataset_id != configuration.datasets().bathymetry ||
                terrain.topography_dataset_id != configuration.datasets().topography) {
                return tsunami::core::failure(file_error(
                    "r2d.file_case.contract_mismatch",
                    "case, manifest, corridor and terrain records do not describe the same Regional2D run contract",
                    tsunami::core::DiagnosticCategory::validation,
                    "contract",
                    request,
                    false));
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

        [[nodiscard]] auto final_time_matches(tsunami::core::Time actual, tsunami::core::Time expected) -> bool
        {
            const auto scale = std::max<tsunami::core::Real>(1.0, std::abs(expected));
            return std::abs(actual - expected) <= 1.0e-10 * scale;
        }
    } // namespace

    auto run_regional_case_from_files(const RegionalFileCaseRunRequest &request)
        -> tsunami::core::Result<RegionalFileCaseRunResult>
    {
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

            const auto case_path = case_root / tsunami::data::authoritative_case_configuration_path;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(case_path, ec)) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(file_error(
                    "r2d.file_case.case_read_failed",
                    "case.json must be a regular file under the case root",
                    tsunami::core::DiagnosticCategory::input_data,
                    "case",
                    request,
                    false,
                    case_path));
            }
            auto configuration = tsunami::data::read_case_configuration(case_path);
            if (!configuration) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.case_read_failed",
                    "case configuration could not be read",
                    tsunami::core::DiagnosticCategory::input_data,
                    "case",
                    request,
                    configuration.error(),
                    false,
                    case_path));
            }
            completed_steps.push_back("case_read");

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

            if (auto unsupported = unsupported_modes(configuration.value(), request); !unsupported) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(unsupported.error());
            }
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
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.mesh_import_failed",
                    "Gmsh MSH 4.1 Regional2D mesh could not be imported",
                    tsunami::core::DiagnosticCategory::input_data,
                    "mesh",
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

            const auto outputs = output_directory(case_root, request.run_id);
            if (auto allowed = ensure_output_allowed(outputs, request); !allowed) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(allowed.error());
            }

            auto writer = tsunami::r2d_io::RegionalCsvOutputWriter{outputs, request.overwrite_existing_outputs};
            auto output_prepared = writer.prepare();
            if (!output_prepared) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.output_prepare_failed",
                    "regional CSV output directory could not be prepared",
                    tsunami::core::DiagnosticCategory::persistence,
                    "output_prepare",
                    request,
                    output_prepared.error(),
                    true,
                    outputs));
            }
            completed_steps.push_back("outputs_prepared");

            auto diagnostics_sink = [&](const tsunami::r2d::RegionalStepDiagnostics &diagnostics) {
                return writer.write_diagnostics(diagnostics);
            };
            auto snapshot_sink = [&](const tsunami::r2d::RegionalSnapshot &snapshot) {
                return writer.write_snapshot(snapshot);
            };
            if (prepared.value().earthquake_diagnostics()) {
                auto written = writer.write_earthquake_initialisation(*prepared.value().earthquake_diagnostics());
                if (!written) {
                    return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                        "r2d.file_case.output_prepare_failed",
                        "regional earthquake initialisation CSV could not be written",
                        tsunami::core::DiagnosticCategory::persistence,
                        "output_prepare",
                        request,
                        written.error(),
                        true,
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
                    true,
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
                    true,
                    outputs));
            }
            if (!solve.value().completed_successfully ||
                solve.value().accepted_step_count > solve_request.value().maximum_steps ||
                !final_time_matches(solve.value().final_time, solve_request.value().final_time) ||
                !prepared.value().simulation_state().conserved_state().is_bound_to(imported_mesh.value().mesh)) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(file_error(
                    "r2d.file_case.solve_failed",
                    "Regional2D solve did not reach the requested final state",
                    tsunami::core::DiagnosticCategory::execution,
                    "solve",
                    request,
                    true,
                    outputs));
            }
            auto final_state_check = tsunami::r2d::validate_and_canonicalise(
                prepared.value().simulation_state().conserved_state(),
                solve_request.value().state_policy);
            if (!final_state_check) {
                return tsunami::core::failure<RegionalFileCaseRunResult>(wrap_failure(
                    "r2d.file_case.solve_failed",
                    "Regional2D final state failed canonical validation",
                    tsunami::core::DiagnosticCategory::execution,
                    "solve",
                    request,
                    final_state_check.error(),
                    true,
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
                    true,
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
            diagnostics.run_id = request.run_id;
            diagnostics.terrain_artifacts = artifact_summary(terrain_artifacts.value().diagnostics);
            diagnostics.preflight = preflight.value();
            diagnostics.terrain_transfer = transfer.value().diagnostics;
            diagnostics.preparation = prepared.value().diagnostics();
            diagnostics.solve = solve.value();
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
                    false));
        } catch (...) {
            return tsunami::core::failure<RegionalFileCaseRunResult>(
                file_error(
                    "r2d.file_case.request_invalid",
                    "unexpected exception escaped Regional2D file case orchestration",
                    tsunami::core::DiagnosticCategory::internal,
                    "exception",
                    request,
                    false));
        }
    }

} // namespace tsunami::r2d_case
