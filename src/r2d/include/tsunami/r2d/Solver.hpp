#pragma once

#include <string>
#include <vector>

#include <tsunami/core/Cancellation.hpp>
#include <tsunami/core/Observer.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/data/References.hpp>

namespace tsunami::r2d {

enum class RegionalCompletion {
    completed,
    cancelled
};

struct RegionalRunRequest {
    tsunami::data::CaseRevisionRef case_revision;
    tsunami::core::PreparationId preparation_id;
    tsunami::core::RunId run_id;
    std::vector<tsunami::data::ArtifactRef> resolved_inputs;
};

struct RegionalRunSummary {
    tsunami::core::RunId run_id;
    RegionalCompletion completion{RegionalCompletion::completed};
    std::vector<tsunami::data::ArtifactRef> produced_artifacts;
};

class IRegionalSolver {
public:
    virtual ~IRegionalSolver() = default;

    virtual auto run(
        const RegionalRunRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<RegionalRunSummary> = 0;
};

} // namespace tsunami::r2d
