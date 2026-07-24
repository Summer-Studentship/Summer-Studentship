#pragma once

#include <tsunami/application/ServiceCapabilities.hpp>
#include <tsunami/application/ServiceOperations.hpp>
#include <tsunami/core/Cancellation.hpp>
#include <tsunami/core/Observer.hpp>
#include <tsunami/core/Result.hpp>

namespace tsunami::application {

class IApplicationService {
public:
    virtual ~IApplicationService() = default;

    virtual auto describe() const -> ServiceCapabilities = 0;

    virtual auto validate_case(
        const ValidationRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<ValidationResponse> = 0;

    virtual auto prepare_run(
        const PreparationRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<PreparationResponse> = 0;

    virtual auto launch_run(
        const LaunchRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<LaunchResponse> = 0;

    virtual auto request_cancellation(
        const CancellationRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<CancellationResponse> = 0;

    virtual auto discover_results(
        const ResultDiscoveryRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<ResultDiscoveryResponse> = 0;
};

} // namespace tsunami::application
