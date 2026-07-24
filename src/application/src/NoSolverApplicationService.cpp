#include <tsunami/application/ServiceFactory.hpp>

#include <filesystem>
#include <string>

namespace tsunami::application {
namespace {

constexpr const char* unavailable_reason = "no solver backend is configured";

auto unavailable_error(std::string code, std::string operation) -> tsunami::core::Error
{
    return tsunami::core::Error{
               std::move(code),
               unavailable_reason,
               tsunami::core::DiagnosticCategory::unsupported,
               tsunami::core::Severity::error}
        .add_context("operation", std::move(operation))
        .add_context("state_changed", "false");
}

auto cancellation_error(std::string operation) -> tsunami::core::Error
{
    return tsunami::core::Error{
               "application.service.cancelled",
               "operation cancelled before service dispatch",
               tsunami::core::DiagnosticCategory::cancellation,
               tsunami::core::Severity::info}
        .add_context("operation", std::move(operation))
        .add_context("cancellation_stage", "requested")
        .add_context("state_changed", "false");
}

auto validation_error(const ValidationRequest& request) -> tsunami::core::Error
{
    return tsunami::core::Error{
               "application.validation.case_location_required",
               "case location is required",
               tsunami::core::DiagnosticCategory::validation,
               tsunami::core::Severity::error}
        .add_context("operation", "validate_case")
        .add_context("rule_id", "case.location.required")
        .add_context("case_location", request.case_location.empty() ? "<empty>" : request.case_location.generic_string())
        .add_context("case_revision", std::to_string(request.case_revision))
        .add_context("state_changed", "false");
}

void observe_failure(tsunami::core::IObserver& observer, const char* operation, const tsunami::core::Error& error)
{
    observer.observe(tsunami::core::failed_status(operation, error));
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
        const ValidationRequest& request,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer) -> tsunami::core::Result<ValidationResponse> override
    {
        if (cancellation.stop_requested()) {
            return fail<ValidationResponse>("validate_case", observer, cancellation_error("validate_case"));
        }
        if (request.case_location.empty()) {
            return fail<ValidationResponse>("validate_case", observer, validation_error(request));
        }
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
            return fail<ResultDiscoveryResponse>("discover_results", observer, cancellation_error("discover_results"));
        }
        return ResultDiscoveryResponse{{}, {}, "no backend configured; no results discovered"};
    }

private:
    template <class Response>
    auto fail(const char* operation, tsunami::core::IObserver& observer, tsunami::core::Error error) -> tsunami::core::Result<Response>
    {
        observe_failure(observer, operation, error);
        return tsunami::core::failure<Response>(std::move(error));
    }

    template <class Response>
    auto unsupported(
        const char* operation,
        tsunami::core::CancellationTokenRef cancellation,
        tsunami::core::IObserver& observer,
        const char* error_code) -> tsunami::core::Result<Response>
    {
        if (cancellation.stop_requested()) {
            return fail<Response>(operation, observer, cancellation_error(operation));
        }
        return fail<Response>(operation, observer, unavailable_error(error_code, operation));
    }
};

} // namespace

auto make_no_solver_application_service() -> std::unique_ptr<IApplicationService>
{
    return std::make_unique<NoSolverApplicationService>();
}

} // namespace tsunami::application
