#include <memory>

#include <catch2/catch_test_macros.hpp>

#include <tsunami/core/Cancellation.hpp>
#include <tsunami/core/Error.hpp>
#include <tsunami/core/Identity.hpp>
#include <tsunami/core/Observer.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/data/Io.hpp>
#include <tsunami/fvm/Field.hpp>
#include <tsunami/fvm/Mesh.hpp>
#include <tsunami/l3d/Solver.hpp>
#include <tsunami/r2d/Solver.hpp>
#include <tsunami/coupling/Replay.hpp>

namespace {

class DummyMeshView final : public tsunami::fvm::IMeshView {
public:
    auto summary() const -> tsunami::fvm::MeshSummary override
    {
        return {{"mesh"}, 2, 4, 8, 5, 1};
    }
};

class DummyFieldView final : public tsunami::fvm::IFieldView {
public:
    auto descriptor() const -> tsunami::fvm::FieldDescriptor override
    {
        return {{"depth"}, "depth", tsunami::fvm::FieldLocation::cell, 1, 4, "m"};
    }
};

class DummyTransaction final : public tsunami::data::IArtifactTransaction {
public:
    auto commit() -> tsunami::core::Result<void> override { return tsunami::core::success(); }
    auto abort() noexcept -> void override { aborted = true; }

    bool aborted{};
};

class DummyArtifactPort final : public tsunami::data::IArtifactPort {
public:
    auto inspect(const tsunami::data::ArtifactRef& artifact) const -> tsunami::core::Result<tsunami::data::ArtifactMetadata> override
    {
        return tsunami::data::ArtifactMetadata{artifact, {"case", {0, 1, 0}}, true};
    }

    auto read_metadata(const tsunami::data::ReadRequest& request) const -> tsunami::core::Result<tsunami::data::ArtifactMetadata> override
    {
        return inspect(request.artifact);
    }

    auto write_metadata(const tsunami::data::WriteRequest&) -> tsunami::core::Result<void> override
    {
        return tsunami::core::success();
    }

    auto begin_transaction(const tsunami::data::WriteRequest&)
        -> tsunami::core::Result<std::unique_ptr<tsunami::data::IArtifactTransaction>> override
    {
        std::unique_ptr<tsunami::data::IArtifactTransaction> transaction = std::make_unique<DummyTransaction>();
        return transaction;
    }
};

class DummyRegionalSolver final : public tsunami::r2d::IRegionalSolver {
public:
    auto run(
        const tsunami::r2d::RegionalRunRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<tsunami::r2d::RegionalRunSummary> override
    {
        observer.observe({
            "run",
            tsunami::core::OperationState::running,
            {"r2d.started", "regional solver contract invoked", tsunami::core::DiagnosticCategory::execution, tsunami::core::Severity::info, {}, {}},
            {}});
        return tsunami::r2d::RegionalRunSummary{
            request.run_id,
            cancellation.stop_requested() ? tsunami::r2d::RegionalCompletion::cancelled : tsunami::r2d::RegionalCompletion::completed,
            request.resolved_inputs};
    }
};

class DummyLocalSolver final : public tsunami::l3d::ILocalSolver {
public:
    auto run(
        const tsunami::l3d::LocalRunRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<tsunami::l3d::LocalRunSummary> override
    {
        observer.observe({
            "run",
            tsunami::core::OperationState::running,
            {"l3d.started", "local solver contract invoked", tsunami::core::DiagnosticCategory::execution, tsunami::core::Severity::info, {}, {}},
            {}});
        return tsunami::l3d::LocalRunSummary{
            request.run_id,
            cancellation.stop_requested() ? tsunami::l3d::LocalCompletion::cancelled : tsunami::l3d::LocalCompletion::completed,
            request.resolved_inputs};
    }
};

class DummyReplayProvider final : public tsunami::coupling::IReplayProvider {
public:
    auto validate(const tsunami::coupling::ReplayRequest&) const
        -> tsunami::core::Result<tsunami::coupling::CouplingValidationResult> override
    {
        return tsunami::coupling::CouplingValidationResult{true, {}};
    }

    auto replay(
        const tsunami::coupling::ReplayRequest& request,
        tsunami::core::CancellationTokenRef,
        tsunami::core::IObserver&) -> tsunami::core::Result<tsunami::coupling::ReplayArtifact> override
    {
        return tsunami::coupling::ReplayArtifact{request.regional_output, request.convention};
    }
};

class DummyCouplingRunner final : public tsunami::coupling::ICouplingRunner {
public:
    auto reconstruct_local_inlet(
        const tsunami::coupling::InletReconstructionRequest& request,
        tsunami::core::CancellationTokenRef,
        tsunami::core::IObserver&) -> tsunami::core::Result<tsunami::coupling::ReplayArtifact> override
    {
        return tsunami::coupling::ReplayArtifact{request.mapping.local_boundary, request.mapping.convention};
    }
};

} // namespace

TEST_CASE("strong identifiers require explicit valid text")
{
    auto invalid = tsunami::core::CaseId::from_string("");
    auto case_id = tsunami::core::CaseId::from_string("case-001");
    auto other_case_id = tsunami::core::CaseId::from_string("case-002");

    REQUIRE_FALSE(invalid.has_value());
    REQUIRE(case_id.has_value());
    REQUIRE(case_id->valid());
    REQUIRE(*case_id < *other_case_id);
    REQUIRE(case_id->str() == "case-001");
}

TEST_CASE("Result supports value, void, failure and move-only payloads")
{
    auto ok = tsunami::core::Result<int>(42);
    REQUIRE(ok);
    REQUIRE(ok.value() == 42);

    auto failed = tsunami::core::failure<int>(tsunami::core::Error{"data.missing", "missing input"});
    REQUIRE_FALSE(failed);
    REQUIRE(failed.error().code() == "data.missing");

    auto void_ok = tsunami::core::success();
    REQUIRE(void_ok);

    auto void_failed = tsunami::core::failure(tsunami::core::Error{"cancelled", "operation cancelled"});
    REQUIRE_FALSE(void_failed);

    auto move_only = tsunami::core::Result<std::unique_ptr<int>>(std::make_unique<int>(7));
    REQUIRE(*std::move(move_only).value() == 7);
}

TEST_CASE("dummy contract implementations compile and use neutral boundaries")
{
    DummyMeshView mesh;
    DummyFieldView field;
    DummyArtifactPort port;
    DummyRegionalSolver regional;
    DummyLocalSolver local;
    DummyReplayProvider replay;
    DummyCouplingRunner coupling;
    tsunami::core::NeverCancelToken cancellation;
    tsunami::core::NullObserver observer;

    REQUIRE(mesh.summary().cell_count == 4);
    REQUIRE(field.descriptor().component_count == 1);

    auto case_id = tsunami::core::CaseId::from_string("case-001").value();
    auto preparation_id = tsunami::core::PreparationId::from_string("prep-001").value();
    auto run_id = tsunami::core::RunId::from_string("run-001").value();
    auto artifact_id = tsunami::core::ArtifactId::from_string("artifact-001").value();

    tsunami::data::CaseRevisionRef case_revision{case_id, 1};
    tsunami::data::ArtifactRef artifact{artifact_id, "runs/run-001/outputs/depth.h5", "application/octet-stream"};
    tsunami::data::WriteRequest write{artifact, {"output", {0, 1, 0}}, tsunami::data::WriteDisposition::reject_overwrite, {}};

    REQUIRE(port.write_metadata(write));
    auto transaction = port.begin_transaction(write);
    REQUIRE(transaction);
    REQUIRE(transaction.value()->commit());

    tsunami::r2d::RegionalRunRequest regional_request{case_revision, preparation_id, run_id, {artifact}};
    REQUIRE(regional.run(regional_request, cancellation, observer));

    tsunami::l3d::LocalRunRequest local_request{case_revision, preparation_id, run_id, {artifact}};
    REQUIRE(local.run(local_request, cancellation, observer));

    tsunami::coupling::ReplayRequest replay_request{
        artifact,
        tsunami::data::PreparationRef{preparation_id, case_revision},
        {"EPSG:4326", "MSL", "UTC"}};
    REQUIRE(replay.validate(replay_request).value().compatible);
    REQUIRE(replay.replay(replay_request, cancellation, observer));

    tsunami::coupling::InletReconstructionRequest inlet{{artifact, artifact, {"EPSG:4326", "MSL", "UTC"}}, "inlet-west"};
    REQUIRE(coupling.reconstruct_local_inlet(inlet, cancellation, observer));
}
