#include "ShellController.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

#include <tsunami/core/Cancellation.hpp>

namespace {

class CollectingObserver final : public tsunami::core::IObserver {
public:
    void observe(const tsunami::core::Observation& observation) override { observations.push_back(observation); }
    std::vector<tsunami::core::Observation> observations;
};

auto to_qstring(std::string_view value) -> QString
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

auto context_or_empty(const tsunami::core::Diagnostic& diagnostic, std::string_view key) -> QString
{
    return QString::fromStdString(diagnostic.context_value(key).value_or(""));
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

} // namespace

ShellController::ShellController(std::unique_ptr<tsunami::application::IApplicationService> service, QObject* parent)
    : QObject(parent), service_(std::move(service))
{
    refreshServiceStatus();
}

auto ShellController::serviceBackend() const -> QString { return service_backend_; }
auto ShellController::serviceBoundaryAvailable() const -> bool { return service_boundary_available_; }
auto ShellController::solverAvailable() const -> bool { return solver_available_; }
auto ShellController::validationAvailable() const -> bool { return validation_available_; }
auto ShellController::preparationAvailable() const -> bool { return preparation_available_; }
auto ShellController::launchAvailable() const -> bool { return launch_available_; }
auto ShellController::cancellationAvailable() const -> bool { return cancellation_available_; }
auto ShellController::resultDiscoveryAvailable() const -> bool { return result_discovery_available_; }
auto ShellController::diagnosticActive() const -> bool { return diagnostic_active_; }
auto ShellController::diagnosticOperation() const -> QString { return diagnostic_operation_; }
auto ShellController::diagnosticState() const -> QString { return diagnostic_state_; }
auto ShellController::diagnosticSeverity() const -> QString { return diagnostic_severity_; }
auto ShellController::diagnosticCategory() const -> QString { return diagnostic_category_; }
auto ShellController::diagnosticCode() const -> QString { return diagnostic_code_; }
auto ShellController::diagnosticMessage() const -> QString { return diagnostic_message_; }
auto ShellController::diagnosticRuleId() const -> QString { return diagnostic_rule_id_; }
auto ShellController::diagnosticCaseLocation() const -> QString { return diagnostic_case_location_; }
auto ShellController::diagnosticCaseRevision() const -> QString { return diagnostic_case_revision_; }
auto ShellController::diagnosticStateChanged() const -> QString { return diagnostic_state_changed_; }
auto ShellController::diagnosticContextPreserved() const -> bool { return diagnostic_context_preserved_; }

void ShellController::refreshServiceStatus()
{
    const auto description = service_->describe();
    service_backend_ = QString::fromStdString(description.implementation_id);
    service_boundary_available_ = true;
    solver_available_ = description.solver_available;
    validation_available_ = description.validation.available;
    preparation_available_ = description.preparation.available;
    launch_available_ = description.launch.available;
    cancellation_available_ = description.cancellation.available;
    result_discovery_available_ = description.result_discovery.available;
    emit serviceStatusChanged();
}

bool ShellController::runDiagnosticSmoke()
{
    tsunami::core::NeverCancelToken cancellation;
    CollectingObserver observer;
    const tsunami::application::ValidationRequest request{std::filesystem::path{}, {}, 0, "case"};
    const auto result = service_->validate_case(request, cancellation, observer);

    clearDiagnostic();
    if (result || observer.observations.size() != 1) {
        return false;
    }

    const auto& error = result.error();
    const auto& observed = observer.observations.front();
    diagnostic_context_preserved_ = diagnostic_matches(error, observed)
        && error.code() == "application.validation.case_location_required"
        && error.category() == tsunami::core::DiagnosticCategory::validation
        && error.severity() == tsunami::core::Severity::error
        && error.context_value("operation") == "validate_case"
        && error.context_value("rule_id") == "case.location.required"
        && error.context_value("case_location") == "<empty>"
        && error.context_value("case_revision") == "0"
        && error.context_value("state_changed") == "false";

    diagnostic_active_ = true;
    diagnostic_operation_ = QString::fromStdString(observed.operation);
    diagnostic_state_ = to_qstring(tsunami::core::to_string(observed.state));
    diagnostic_severity_ = to_qstring(tsunami::core::to_string(error.severity()));
    diagnostic_category_ = to_qstring(tsunami::core::to_string(error.category()));
    diagnostic_code_ = QString::fromStdString(error.code());
    diagnostic_message_ = QString::fromStdString(error.message());
    diagnostic_rule_id_ = context_or_empty(error.diagnostic(), "rule_id");
    diagnostic_case_location_ = context_or_empty(error.diagnostic(), "case_location");
    diagnostic_case_revision_ = context_or_empty(error.diagnostic(), "case_revision");
    diagnostic_state_changed_ = context_or_empty(error.diagnostic(), "state_changed");
    emit diagnosticChanged();
    return diagnostic_context_preserved_;
}

void ShellController::clearDiagnostic()
{
    diagnostic_active_ = false;
    diagnostic_operation_.clear();
    diagnostic_state_.clear();
    diagnostic_severity_.clear();
    diagnostic_category_.clear();
    diagnostic_code_.clear();
    diagnostic_message_.clear();
    diagnostic_rule_id_.clear();
    diagnostic_case_location_.clear();
    diagnostic_case_revision_.clear();
    diagnostic_state_changed_.clear();
    diagnostic_context_preserved_ = false;
    emit diagnosticChanged();
}
