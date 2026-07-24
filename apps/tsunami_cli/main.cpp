#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <tsunami/application/ServiceFactory.hpp>

namespace {

class CollectingObserver final : public tsunami::core::IObserver {
public:
    void observe(const tsunami::core::Observation& observation) override
    {
        observations.push_back(observation);
    }

    std::vector<tsunami::core::Observation> observations;
};

auto context_or_empty(const tsunami::core::Diagnostic& diagnostic, std::string_view key) -> std::string
{
    return diagnostic.context_value(key).value_or("");
}

auto diagnostic_matches(const tsunami::core::Error& error, const tsunami::core::Observation& observed) -> bool
{
    return observed.operation == "validate_case"
        && observed.state == tsunami::core::OperationState::failed
        && observed.diagnostic.code == error.code()
        && observed.diagnostic.message == error.message()
        && observed.diagnostic.category == error.category()
        && observed.diagnostic.severity == error.severity()
        && observed.diagnostic.context == error.context();
}

auto run_diagnostic_smoke() -> int
{
    const auto service = tsunami::application::make_no_solver_application_service();
    tsunami::core::NeverCancelToken cancellation;
    CollectingObserver observer;
    const tsunami::application::ValidationRequest request{std::filesystem::path{}, {}, 0, "case"};

    const auto result = service->validate_case(request, cancellation, observer);
    if (result || observer.observations.size() != 1) {
        return 1;
    }

    const auto& error = result.error();
    const auto& observed = observer.observations.front();
    const auto preserved = diagnostic_matches(error, observed)
        && error.code() == "application.validation.case_location_required"
        && error.category() == tsunami::core::DiagnosticCategory::validation
        && error.severity() == tsunami::core::Severity::error
        && error.context_value("operation") == "validate_case"
        && error.context_value("rule_id") == "case.location.required"
        && error.context_value("case_location") == "<empty>"
        && error.context_value("case_revision") == "0"
        && error.context_value("state_changed") == "false";

    std::cout
        << "diagnostic_operation=" << observed.operation << '\n'
        << "diagnostic_state=" << tsunami::core::to_string(observed.state) << '\n'
        << "diagnostic_severity=" << tsunami::core::to_string(error.severity()) << '\n'
        << "diagnostic_category=" << tsunami::core::to_string(error.category()) << '\n'
        << "diagnostic_code=" << error.code() << '\n'
        << "diagnostic_message=" << error.message() << '\n'
        << "diagnostic_context.operation=" << context_or_empty(error.diagnostic(), "operation") << '\n'
        << "diagnostic_context.rule_id=" << context_or_empty(error.diagnostic(), "rule_id") << '\n'
        << "diagnostic_context.case_location=" << context_or_empty(error.diagnostic(), "case_location") << '\n'
        << "diagnostic_context.case_revision=" << context_or_empty(error.diagnostic(), "case_revision") << '\n'
        << "diagnostic_context.state_changed=" << context_or_empty(error.diagnostic(), "state_changed") << '\n'
        << "diagnostic_context_preserved=" << (preserved ? "true" : "false") << '\n';

    return preserved ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    CLI::App app{"Tsunami Barrier Simulation command-line scaffold"};
    app.set_version_flag("--version", "Tsunami Barrier Simulation 0.1.0");
    bool service_status = false;
    bool diagnostic_smoke = false;
    app.add_flag("--service-status", service_status, "Print shared application-service status");
    app.add_flag("--diagnostic-smoke", diagnostic_smoke, "Verify representative diagnostic propagation");

    CLI11_PARSE(app, argc, argv);

    if (diagnostic_smoke) {
        return run_diagnostic_smoke();
    }

    if (service_status) {
        const auto service = tsunami::application::make_no_solver_application_service();
        const auto description = service->describe();
        std::cout
            << "service_backend=" << description.implementation_id << '\n'
            << "service_boundary=available\n"
            << "solver_available=" << (description.solver_available ? "true" : "false") << '\n'
            << "validation_available=" << (description.validation.available ? "true" : "false") << '\n'
            << "preparation_available=" << (description.preparation.available ? "true" : "false") << '\n'
            << "launch_available=" << (description.launch.available ? "true" : "false") << '\n'
            << "cancellation_available=" << (description.cancellation.available ? "true" : "false") << '\n'
            << "result_discovery_available=" << (description.result_discovery.available ? "true" : "false") << '\n';
    }

    return 0;
}
