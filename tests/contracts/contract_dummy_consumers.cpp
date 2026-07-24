#include <memory>
#include <utility>

#include <tsunami/core/Cancellation.hpp>
#include <tsunami/core/Observer.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/coupling/Replay.hpp>
#include <tsunami/data/Io.hpp>
#include <tsunami/fvm/Field.hpp>
#include <tsunami/fvm/Mesh.hpp>
#include <tsunami/l3d/Solver.hpp>
#include <tsunami/r2d/Solver.hpp>

namespace tsunami::contracts_test {

class MeshConsumer final : public tsunami::fvm::IMeshView {
public:
    auto summary() const -> tsunami::fvm::MeshSummary override { return {{"mesh"}, 2, 1, 1, 1, 1}; }
};

class FieldConsumer final : public tsunami::fvm::IFieldView {
public:
    auto descriptor() const -> tsunami::fvm::FieldDescriptor override
    {
        return {{"field"}, "eta", tsunami::fvm::FieldLocation::cell, 1, 1, "m"};
    }
};

class TransactionConsumer final : public tsunami::data::IArtifactTransaction {
public:
    auto commit() -> tsunami::core::Result<void> override { return tsunami::core::success(); }
    auto abort() noexcept -> void override {}
};

class ArtifactPortConsumer final : public tsunami::data::IArtifactPort {
public:
    auto inspect(const tsunami::data::ArtifactRef& artifact) const -> tsunami::core::Result<tsunami::data::ArtifactMetadata> override
    {
        return tsunami::data::ArtifactMetadata{artifact, {"artifact", {0, 1, 0}}, true};
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
        std::unique_ptr<tsunami::data::IArtifactTransaction> transaction = std::make_unique<TransactionConsumer>();
        return transaction;
    }
};

class RegionalConsumer final : public tsunami::r2d::IRegionalSolver {
public:
    auto run(
        const tsunami::r2d::RegionalRunRequest& request,
        tsunami::core::CancellationTokenRef,
        tsunami::core::IObserver&) -> tsunami::core::Result<tsunami::r2d::RegionalRunSummary> override
    {
        return tsunami::r2d::RegionalRunSummary{request.run_id, tsunami::r2d::RegionalCompletion::completed, {}};
    }
};

class LocalConsumer final : public tsunami::l3d::ILocalSolver {
public:
    auto run(
        const tsunami::l3d::LocalRunRequest& request,
        tsunami::core::CancellationTokenRef,
        tsunami::core::IObserver&) -> tsunami::core::Result<tsunami::l3d::LocalRunSummary> override
    {
        return tsunami::l3d::LocalRunSummary{request.run_id, tsunami::l3d::LocalCompletion::completed, {}};
    }
};

class ReplayConsumer final : public tsunami::coupling::IReplayProvider {
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

class CouplingConsumer final : public tsunami::coupling::ICouplingRunner {
public:
    auto reconstruct_local_inlet(
        const tsunami::coupling::InletReconstructionRequest& request,
        tsunami::core::CancellationTokenRef,
        tsunami::core::IObserver&) -> tsunami::core::Result<tsunami::coupling::ReplayArtifact> override
    {
        return tsunami::coupling::ReplayArtifact{request.mapping.local_boundary, request.mapping.convention};
    }
};

void compile_contract_consumers()
{
    MeshConsumer mesh;
    FieldConsumer field;
    ArtifactPortConsumer artifacts;
    RegionalConsumer regional;
    LocalConsumer local;
    ReplayConsumer replay;
    CouplingConsumer coupling;
    tsunami::core::NeverCancelToken cancellation;
    tsunami::core::NullObserver observer;
    (void)mesh.summary();
    (void)field.descriptor();
    (void)artifacts;
    (void)regional;
    (void)local;
    (void)replay;
    (void)coupling;
    (void)cancellation;
    (void)observer;
}

} // namespace tsunami::contracts_test
