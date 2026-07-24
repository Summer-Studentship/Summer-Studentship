#include <tsunami/application/ServiceFactory.hpp>

#include <string>

namespace tsunami::application {
namespace {

constexpr const char* unavailable_reason = "no solver backend is configured";

auto unavailable_error(std::string code, std::string operation) -> tsunami::core::Error
{
    return tsunami::core::Error{std::move(code), unavailable_reason}
        .add_context("operation", std::move(operation))
        .add_context("state_changed", "false");
}

auto cancellation_error(std::string operation) -> tsunami::core::Error
{
    return tsunami::core::Error{"application.service.cancelled", "operation cancelled before service dispatch"}
        .add_context("operation", std::move(operation))
        .add_context("state_changed", "false");
}

void observe_rejection(tsunami::core::IObserver& observer, const char* operation)
{
    observer.observe({
        "application.service.no_solver",
        "operation inspected and rejected because no solver backend is configured",
        {},
        {{"operation", operation}, {"state_changed", "false"}}});
}

class NoSolverApplicationService final : public IApplicationService {
public:
    auto describe() const -> ServiceCapabilities override
    {
        return {
            "no-solver",
            {0, 1, 0},
            "deterministic smoke-test service with no scientific backend",
            {false, unavailable_reason},
            {false, unavailable_reason},
            {false, unavailable_reason},
            {false, unavailable_reason},
            {false, unavailable_reason},
            false};
    }

    auto validate_case(
        const ValidationRequest&,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<ValidationResponse> override
    {
        return unsupported<ValidationResponse>("validate_case", cancellation, observer, "application.service.operation_unavailable");
    }

    auto prepare_run(
        const PreparationRequest&,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<PreparationResponse> override
    {
        return unsupported<PreparationResponse>("prepare_run", cancellation, observer, "application.service.operation_unavailable");
    }

    auto launch_run(
        const LaunchRequest&,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<LaunchResponse> override
    {
        return unsupported<LaunchResponse>("launch_run", cancellation, observer, "application.service.solver_unavailable");
    }

    auto request_cancellation(
        const CancellationRequest&,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<CancellationResponse> override
    {
        return unsupported<CancellationResponse>("request_cancellation", cancellation, observer, "application.service.operation_unavailable");
    }

    auto discover_results(
        const ResultDiscoveryRequest&,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<ResultDiscoveryResponse> override
    {
        if (cancellation.stop_requested()) {
            return tsunami::core::failure<ResultDiscoveryResponse>(cancellation_error("discover_results"));
        }
        observe_rejection(observer, "discover_results");
        return ResultDiscoveryResponse{{}, {}, "no backend configured; no results discovered"};
    }

private:
    template <class Response>
    auto unsupported(
        const char* operation,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer,
        const char* error_code) -> tsunami::core::Result<Response>
    {
        if (cancellation.stop_requested()) {
            return tsunami::core::failure<Response>(cancellation_error(operation));
        }
        observe_rejection(observer, operation);
        return tsunami::core::failure<Response>(unavailable_error(error_code, operation));
    }
};

} // namespace

auto make_no_solver_application_service() -> std::unique_ptr<IApplicationService>
{
    return std::make_unique<NoSolverApplicationService>();
}

} // namespace tsunami::application
