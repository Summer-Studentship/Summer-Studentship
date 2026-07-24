#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QVariantMap>
#include <QString>
#include <QTimer>

#include <filesystem>
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

auto make_empty_diagnostic_status() -> QVariantMap
{
    QVariantMap status;
    status.insert("active", false);
    status.insert("contextPreserved", false);
    return status;
}

auto make_diagnostic_status(bool& preserved) -> QVariantMap
{
    const auto service = tsunami::application::make_no_solver_application_service();
    tsunami::core::NeverCancelToken cancellation;
    CollectingObserver observer;
    const tsunami::application::ValidationRequest request{std::filesystem::path{}, {}, 0, "case"};
    const auto result = service->validate_case(request, cancellation, observer);

    QVariantMap status = make_empty_diagnostic_status();
    if (result || observer.observations.size() != 1) {
        preserved = false;
        return status;
    }

    const auto& error = result.error();
    const auto& observed = observer.observations.front();
    preserved = diagnostic_matches(error, observed)
        && error.code() == "application.validation.case_location_required"
        && error.category() == tsunami::core::DiagnosticCategory::validation
        && error.severity() == tsunami::core::Severity::error
        && error.context_value("operation") == "validate_case"
        && error.context_value("rule_id") == "case.location.required"
        && error.context_value("case_location") == "<empty>"
        && error.context_value("case_revision") == "0"
        && error.context_value("state_changed") == "false";

    status.insert("active", true);
    status.insert("operation", QString::fromStdString(observed.operation));
    status.insert("state", QString::fromUtf8(tsunami::core::to_string(observed.state).data(), static_cast<int>(tsunami::core::to_string(observed.state).size())));
    status.insert("severity", QString::fromUtf8(tsunami::core::to_string(error.severity()).data(), static_cast<int>(tsunami::core::to_string(error.severity()).size())));
    status.insert("category", QString::fromUtf8(tsunami::core::to_string(error.category()).data(), static_cast<int>(tsunami::core::to_string(error.category()).size())));
    status.insert("code", QString::fromStdString(error.code()));
    status.insert("message", QString::fromStdString(error.message()));
    status.insert("ruleId", context_or_empty(error.diagnostic(), "rule_id"));
    status.insert("caseLocation", context_or_empty(error.diagnostic(), "case_location"));
    status.insert("caseRevision", context_or_empty(error.diagnostic(), "case_revision"));
    status.insert("stateChanged", context_or_empty(error.diagnostic(), "state_changed"));
    status.insert("contextPreserved", preserved);
    return status;
}

auto qml_diagnostic_verified(const QList<QObject*>& roots) -> bool
{
    if (roots.isEmpty()) {
        return false;
    }
    const auto* root = roots.front();
    return root->property("diagnosticActive").toBool()
        && root->property("diagnosticOperation").toString() == "validate_case"
        && root->property("diagnosticState").toString() == "failed"
        && root->property("diagnosticSeverity").toString() == "error"
        && root->property("diagnosticCategory").toString() == "validation"
        && root->property("diagnosticCode").toString() == "application.validation.case_location_required"
        && root->property("diagnosticMessage").toString() == "case location is required"
        && root->property("diagnosticRuleId").toString() == "case.location.required"
        && root->property("diagnosticCaseLocation").toString() == "<empty>"
        && root->property("diagnosticCaseRevision").toString() == "0"
        && root->property("diagnosticStateChanged").toString() == "false"
        && root->property("diagnosticContextPreserved").toBool();
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    bool smoke_startup = false;
    bool diagnostic_smoke = false;

    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == "--smoke-startup") {
            smoke_startup = true;
        } else if (QString::fromLocal8Bit(argv[index]) == "--diagnostic-smoke") {
            diagnostic_smoke = true;
        }
    }

    QQmlApplicationEngine engine;
    const auto service = tsunami::application::make_no_solver_application_service();
    const auto description = service->describe();
    QVariantMap service_status;
    service_status.insert("backend", QString::fromStdString(description.implementation_id));
    service_status.insert("boundaryAvailable", true);
    service_status.insert("solverAvailable", description.solver_available);
    service_status.insert("validationAvailable", description.validation.available);
    service_status.insert("preparationAvailable", description.preparation.available);
    service_status.insert("launchAvailable", description.launch.available);
    service_status.insert("cancellationAvailable", description.cancellation.available);
    service_status.insert("resultDiscoveryAvailable", description.result_discovery.available);
    engine.rootContext()->setContextProperty("serviceStatus", service_status);

    bool diagnostic_preserved = false;
    const auto diagnostic_status = diagnostic_smoke ? make_diagnostic_status(diagnostic_preserved) : make_empty_diagnostic_status();
    engine.rootContext()->setContextProperty("diagnosticStatusModel", diagnostic_status);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("Tsunami.Studentship", "Main");

    if (diagnostic_smoke && (!diagnostic_preserved || !qml_diagnostic_verified(engine.rootObjects()))) {
        return 2;
    }

    if (smoke_startup) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
