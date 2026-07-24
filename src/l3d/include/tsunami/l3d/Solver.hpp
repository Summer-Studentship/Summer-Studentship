#pragma once

#include <vector>

#include <tsunami/core/Cancellation.hpp>
#include <tsunami/core/Observer.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/data/References.hpp>

namespace tsunami::l3d {

enum class LocalCompletion {
    completed,
    cancelled
};

struct LocalRunRequest {
    tsunami::data::CaseRevisionRef case_revision;
    tsunami::core::PreparationId preparation_id;
    tsunami::core::RunId run_id;
    std::vector<tsunami::data::ArtifactRef> resolved_inputs;
};

struct LocalRunSummary {
    tsunami::core::RunId run_id;
    LocalCompletion completion{LocalCompletion::completed};
    std::vector<tsunami::data::ArtifactRef> produced_artifacts;
};

class ILocalSolver {
public:
    virtual ~ILocalSolver() = default;

    virtual auto run(
        const LocalRunRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<LocalRunSummary> = 0;
};

} // namespace tsunami::l3d
