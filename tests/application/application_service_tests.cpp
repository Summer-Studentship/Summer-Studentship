#include <filesystem>
#include <memory>
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

    auto preparation = service->prepare_run({revision, "execution"}, cancellation, observer);
    REQUIRE_FALSE(preparation);
    REQUIRE(preparation.error().code() == "application.service.operation_unavailable");

    auto launch = service->launch_run({revision, preparation_id(), tsunami::application::WorkflowKind::regional_2d, {}}, cancellation, observer);
    REQUIRE_FALSE(launch);
    REQUIRE(launch.error().code() == "application.service.solver_unavailable");

    auto cancel = service->request_cancellation({run_id(), "smoke"}, cancellation, observer);
    REQUIRE_FALSE(cancel);
    REQUIRE(cancel.error().code() == "application.service.operation_unavailable");

    REQUIRE(observer.observations.size() == 4);
    for (const auto& observation : observer.observations) {
        REQUIRE(observation.topic_code == "application.service.no_solver");
    }
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
    REQUIRE(observer.observations.size() == 1);
}

TEST_CASE("pre-cancelled no-solver operation returns cancellation")
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
    REQUIRE(observer.observations.empty());
}
