#pragma once

#include <string>
#include <vector>

#include <tsunami/core/Cancellation.hpp>
#include <tsunami/core/Observer.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/data/References.hpp>
#include <tsunami/l3d/Solver.hpp>
#include <tsunami/r2d/Solver.hpp>

namespace tsunami::coupling {

struct CoordinateConvention {
    std::string spatial_reference_id;
    std::string vertical_datum_id;
    std::string time_basis_id;
};

struct ReplayRequest {
    tsunami::data::ArtifactRef regional_output;
    tsunami::data::PreparationRef local_preparation;
    CoordinateConvention convention;
};

struct MappingRequest {
    tsunami::data::ArtifactRef regional_output;
    tsunami::data::ArtifactRef local_boundary;
    CoordinateConvention convention;
};

struct InletReconstructionRequest {
    MappingRequest mapping;
    std::string inlet_code;
};

struct ReplayArtifact {
    tsunami::data::ArtifactRef artifact;
    CoordinateConvention convention;
};

struct CouplingValidationResult {
    bool compatible{};
    std::vector<std::string> messages;
};

class IReplayProvider {
public:
    virtual ~IReplayProvider() = default;

    virtual auto validate(const ReplayRequest& request) const -> tsunami::core::Result<CouplingValidationResult> = 0;
    virtual auto replay(
        const ReplayRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<ReplayArtifact> = 0;
};

class ICouplingRunner {
public:
    virtual ~ICouplingRunner() = default;

    virtual auto reconstruct_local_inlet(
        const InletReconstructionRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<ReplayArtifact> = 0;
};

} // namespace tsunami::coupling
