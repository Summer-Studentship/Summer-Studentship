#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <tsunami/application/ServiceFactory.hpp>

namespace {

class RequestedCancellation final : public tsunami::core::ICancellationToken {
public:
    auto stop_requested() const noexcept -> bool override { return true; }
};

class CapturingObserver final : public tsunami::core::IObserver {
public:
    void observe(const tsunami::core::Observation& observation) override
    {
        observations.push_back(observation);
    }

    std::vector<tsunami::core::Observation> observations;
};

auto context_value(const tsunami::core::Error& error, std::string_view key) -> std::string
{
    return error.context_value(key).value_or("");
}

auto observed_matches_error(const tsunami::core::Observation& observation, const tsunami::core::Error& error) -> bool
{
    return observation.diagnostic.code == error.code()
        && observation.diagnostic.message == error.message()
        && observation.diagnostic.category == error.category()
        && observation.diagnostic.severity == error.severity()
        && observation.diagnostic.context == error.context();
}

auto case_id() -> tsunami::core::CaseId
{
    return tsunami::core::CaseId::from_string("case-service-smoke").value();
}

auto run_id() -> tsunami::core::RunId
{
    return tsunami::core::RunId::from_string("run-service-smoke").value();
}

auto preparation_id() -> tsunami::core::PreparationId
{
    return tsunami::core::PreparationId::from_string("prep-service-smoke").value();
}

} // namespace

TEST_CASE("no-solver service reports deterministic unavailable capabilities")
{
    std::unique_ptr<tsunami::application::IApplicationService> service =
        tsunami::application::make_no_solver_application_service();

    REQUIRE(service);
    const auto description = service->describe();
    REQUIRE(description.implementation_id == "no-solver");
    REQUIRE(description.implementation_version.text() == "0.1.0");
    REQUIRE_FALSE(description.solver_available);
    REQUIRE_FALSE(description.validation.available);
    REQUIRE_FALSE(description.preparation.available);
    REQUIRE_FALSE(description.launch.available);
    REQUIRE_FALSE(description.cancellation.available);
    REQUIRE_FALSE(description.result_discovery.available);
}

TEST_CASE("unsupported no-solver operations fail without fabricating run state")
{
    auto service = tsunami::application::make_no_solver_application_service();
    tsunami::core::NeverCancelToken cancellation;
    CapturingObserver observer;
    const auto revision = tsunami::data::CaseRevisionRef{case_id(), 1};

    auto validation = service->validate_case({std::filesystem::path{"case-root"}, case_id(), 1, "case"}, cancellation, observer);
    REQUIRE_FALSE(validation);
    REQUIRE(validation.error().code() == "application.service.operation_unavailable");
    REQUIRE(validation.error().category() == tsunami::core::DiagnosticCategory::unsupported);
    REQUIRE(validation.error().severity() == tsunami::core::Severity::error);
    REQUIRE(context_value(validation.error(), "operation") == "validate_case");
    REQUIRE(context_value(validation.error(), "state_changed") == "false");

    auto preparation = service->prepare_run({revision, "execution"}, cancellation, observer);
    REQUIRE_FALSE(preparation);
    REQUIRE(preparation.error().code() == "application.service.operation_unavailable");
    REQUIRE(preparation.error().category() == tsunami::core::DiagnosticCategory::unsupported);
    REQUIRE(context_value(preparation.error(), "operation") == "prepare_run");

    auto launch = service->launch_run({revision, preparation_id(), tsunami::application::WorkflowKind::regional_2d, {}}, cancellation, observer);
    REQUIRE_FALSE(launch);
    REQUIRE(launch.error().code() == "application.service.solver_unavailable");
    REQUIRE(launch.error().category() == tsunami::core::DiagnosticCategory::unsupported);
    REQUIRE(context_value(launch.error(), "operation") == "launch_run");

    auto cancel = service->request_cancellation({run_id(), "smoke"}, cancellation, observer);
    REQUIRE_FALSE(cancel);
    REQUIRE(cancel.error().code() == "application.service.operation_unavailable");
    REQUIRE(cancel.error().category() == tsunami::core::DiagnosticCategory::unsupported);
    REQUIRE(context_value(cancel.error(), "operation") == "request_cancellation");

    REQUIRE(observer.observations.size() == 4);
    for (const auto& observation : observer.observations) {
        REQUIRE(observation.state == tsunami::core::OperationState::failed);
        REQUIRE(observation.diagnostic.category == tsunami::core::DiagnosticCategory::unsupported);
        REQUIRE(observation.diagnostic.context_value("state_changed") == "false");
    }
}

TEST_CASE("empty validation request reports deterministic contextual diagnostic")
{
    auto service = tsunami::application::make_no_solver_application_service();
    tsunami::core::NeverCancelToken cancellation;
    CapturingObserver observer;

    const auto before = std::filesystem::exists(std::filesystem::temp_directory_path() / "tsunami-empty-validation-smoke");
    auto response = service->validate_case({std::filesystem::path{}, {}, 0, "case"}, cancellation, observer);
    const auto after = std::filesystem::exists(std::filesystem::temp_directory_path() / "tsunami-empty-validation-smoke");

    REQUIRE_FALSE(response);
    const auto& error = response.error();
    REQUIRE(error.code() == "application.validation.case_location_required");
    REQUIRE(error.message() == "case location is required");
    REQUIRE(error.category() == tsunami::core::DiagnosticCategory::validation);
    REQUIRE(error.severity() == tsunami::core::Severity::error);
    REQUIRE(context_value(error, "operation") == "validate_case");
    REQUIRE(context_value(error, "rule_id") == "case.location.required");
    REQUIRE(context_value(error, "case_location") == "<empty>");
    REQUIRE(context_value(error, "case_revision") == "0");
    REQUIRE(context_value(error, "state_changed") == "false");
    REQUIRE(before == after);

    REQUIRE(observer.observations.size() == 1);
    REQUIRE(observer.observations.front().operation == "validate_case");
    REQUIRE(observer.observations.front().state == tsunami::core::OperationState::failed);
    REQUIRE(observed_matches_error(observer.observations.front(), error));
}

TEST_CASE("result discovery is empty and non-mutating for no-solver backend")
{
    auto service = tsunami::application::make_no_solver_application_service();
    tsunami::core::NeverCancelToken cancellation;
    CapturingObserver observer;

    const auto before = std::filesystem::directory_iterator(std::filesystem::temp_directory_path()) == std::filesystem::directory_iterator{};
    (void)before;

    auto response = service->discover_results({case_id(), 1, run_id(), "outputs"}, cancellation, observer);
    REQUIRE(response);
    REQUIRE(response.value().runs.empty());
    REQUIRE(response.value().artifacts.empty());
    REQUIRE(response.value().metadata == "no backend configured; no results discovered");
    REQUIRE(observer.observations.empty());
}

TEST_CASE("pre-cancelled no-solver operation returns cancellation diagnostic")
{
    auto service = tsunami::application::make_no_solver_application_service();
    RequestedCancellation cancellation;
    CapturingObserver observer;

    auto response = service->launch_run(
        {{case_id(), 1}, preparation_id(), tsunami::application::WorkflowKind::regional_2d, {}},
        cancellation,
        observer);

    REQUIRE_FALSE(response);
    REQUIRE(response.error().code() == "application.service.cancelled");
    REQUIRE(response.error().category() == tsunami::core::DiagnosticCategory::cancellation);
    REQUIRE(response.error().severity() == tsunami::core::Severity::info);
    REQUIRE(context_value(response.error(), "operation") == "launch_run");
    REQUIRE(context_value(response.error(), "cancellation_stage") == "requested");
    REQUIRE(context_value(response.error(), "state_changed") == "false");
    REQUIRE(observer.observations.size() == 1);
    REQUIRE(observer.observations.front().operation == "launch_run");
    REQUIRE(observed_matches_error(observer.observations.front(), response.error()));
}

TEST_CASE("pre-cancelled validation remains cancellation rather than validation failure")
{
    auto service = tsunami::application::make_no_solver_application_service();
    RequestedCancellation cancellation;
    CapturingObserver observer;

    auto response = service->validate_case({std::filesystem::path{}, {}, 0, "case"}, cancellation, observer);

    REQUIRE_FALSE(response);
    REQUIRE(response.error().code() == "application.service.cancelled");
    REQUIRE(response.error().category() == tsunami::core::DiagnosticCategory::cancellation);
    REQUIRE(response.error().severity() == tsunami::core::Severity::info);
    REQUIRE(context_value(response.error(), "operation") == "validate_case");
    REQUIRE(context_value(response.error(), "state_changed") == "false");
    REQUIRE(observer.observations.size() == 1);
}
