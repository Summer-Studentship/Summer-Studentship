#include <limits>

#include <catch2/catch_test_macros.hpp>

#include <tsunami/core/Diagnostic.hpp>
#include <tsunami/core/Error.hpp>
#include <tsunami/core/Observer.hpp>
#include <tsunami/core/Result.hpp>
#include <tsunami/core/Status.hpp>

TEST_CASE("diagnostic primitives expose stable string conversions")
{
    REQUIRE(tsunami::core::to_string(tsunami::core::Severity::trace) == "trace");
    REQUIRE(tsunami::core::to_string(tsunami::core::Severity::debug) == "debug");
    REQUIRE(tsunami::core::to_string(tsunami::core::Severity::info) == "info");
    REQUIRE(tsunami::core::to_string(tsunami::core::Severity::warning) == "warning");
    REQUIRE(tsunami::core::to_string(tsunami::core::Severity::error) == "error");
    REQUIRE(tsunami::core::to_string(tsunami::core::Severity::critical) == "critical");

    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::validation) == "validation");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::configuration) == "configuration");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::input_data) == "input_data");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::preparation) == "preparation");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::execution) == "execution");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::persistence) == "persistence");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::geospatial) == "geospatial");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::numerical) == "numerical");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::coupling) == "coupling");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::cancellation) == "cancellation");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::resource) == "resource");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::unsupported) == "unsupported");
    REQUIRE(tsunami::core::to_string(tsunami::core::DiagnosticCategory::internal) == "internal");

    REQUIRE(tsunami::core::to_string(tsunami::core::OperationState::idle) == "idle");
    REQUIRE(tsunami::core::to_string(tsunami::core::OperationState::accepted) == "accepted");
    REQUIRE(tsunami::core::to_string(tsunami::core::OperationState::running) == "running");
    REQUIRE(tsunami::core::to_string(tsunami::core::OperationState::succeeded) == "succeeded");
    REQUIRE(tsunami::core::to_string(tsunami::core::OperationState::failed) == "failed");
    REQUIRE(tsunami::core::to_string(tsunami::core::OperationState::cancelled) == "cancelled");
}

TEST_CASE("diagnostic context remains unique, searchable and deterministic")
{
    tsunami::core::Diagnostic diagnostic;
    diagnostic.add_context("state_changed", "true")
        .add_context("operation", "validate_case")
        .add_context("state_changed", "false");

    REQUIRE(diagnostic.context.size() == 2);
    REQUIRE(diagnostic.context[0].key == "operation");
    REQUIRE(diagnostic.context[1].key == "state_changed");
    REQUIRE(diagnostic.context_value("state_changed") == "false");
    REQUIRE(tsunami::core::context_value(diagnostic, "missing") == std::nullopt);
}

TEST_CASE("operation progress is finite and bounded")
{
    REQUIRE(tsunami::core::Progress{0.0}.valid());
    REQUIRE(tsunami::core::Progress{0.5}.valid());
    REQUIRE(tsunami::core::Progress{1.0}.valid());
    REQUIRE_FALSE(tsunami::core::Progress{-0.1}.valid());
    REQUIRE_FALSE(tsunami::core::Progress{1.1}.valid());
    REQUIRE_FALSE(tsunami::core::Progress{std::numeric_limits<double>::infinity()}.valid());
}

TEST_CASE("Error and Result preserve complete diagnostic fields")
{
    tsunami::core::Error error{
        "application.validation.case_location_required",
        "case location is required",
        tsunami::core::DiagnosticCategory::validation,
        tsunami::core::Severity::error};
    error.add_context("operation", "validate_case")
        .add_context("rule_id", "case.location.required")
        .with_cause_code("application.validation.request_envelope");

    REQUIRE(error.valid());
    REQUIRE(error.code() == "application.validation.case_location_required");
    REQUIRE(error.message() == "case location is required");
    REQUIRE(error.category() == tsunami::core::DiagnosticCategory::validation);
    REQUIRE(error.severity() == tsunami::core::Severity::error);
    REQUIRE(error.context_value("rule_id") == "case.location.required");
    REQUIRE(error.cause_code() == "application.validation.request_envelope");

    const auto failed = tsunami::core::failed_status("validate_case", error);
    REQUIRE(failed.operation == "validate_case");
    REQUIRE(failed.state == tsunami::core::OperationState::failed);
    REQUIRE(failed.diagnostic.code == error.code());
    REQUIRE(failed.diagnostic.context == error.context());

    auto result = tsunami::core::failure<int>(error);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().diagnostic().context == error.context());

    auto void_result = tsunami::core::failure(tsunami::core::Error{"core.operation.cancelled", "operation cancelled"});
    REQUIRE_FALSE(void_result);
    REQUIRE(void_result.error().category() == tsunami::core::DiagnosticCategory::internal);
    REQUIRE(void_result.error().severity() == tsunami::core::Severity::error);
}

TEST_CASE("observer consumes the canonical operation status")
{
    class CapturingObserver final : public tsunami::core::IObserver {
    public:
        void observe(const tsunami::core::Observation& observation) override { status = observation; }
        tsunami::core::Observation status;
    };

    CapturingObserver observer;
    tsunami::core::NullObserver null_observer;
    const tsunami::core::Error error{
        "application.service.operation_unavailable",
        "no solver backend is configured",
        tsunami::core::DiagnosticCategory::unsupported,
        tsunami::core::Severity::error};

    observer.observe(tsunami::core::failed_status("prepare_run", error));
    null_observer.observe(observer.status);

    REQUIRE(observer.status.operation == "prepare_run");
    REQUIRE(observer.status.state == tsunami::core::OperationState::failed);
    REQUIRE(observer.status.diagnostic.category == tsunami::core::DiagnosticCategory::unsupported);
}
