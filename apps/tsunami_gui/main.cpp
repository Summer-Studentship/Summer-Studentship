#include "ShellController.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <tsunami/application/ServiceFactory.hpp>

namespace {

struct SmokeFlags {
    bool smoke_startup{};
    bool diagnostic_smoke{};
    bool shell_smoke{};
};

auto parse_flags(int argc, char* argv[]) -> SmokeFlags
{
    SmokeFlags flags;
    for (int index = 1; index < argc; ++index) {
        const auto flag = QString::fromLocal8Bit(argv[index]);
        if (flag == "--smoke-startup") {
            flags.smoke_startup = true;
        } else if (flag == "--diagnostic-smoke") {
            flags.diagnostic_smoke = true;
        } else if (flag == "--shell-smoke") {
            flags.shell_smoke = true;
        }
    }
    return flags;
}

auto root_property(QObject* root, const char* name) -> QVariant
{
    return root == nullptr ? QVariant{} : root->property(name);
}

auto run_shell_checks(QObject* root, ShellController& controller, bool expect_diagnostic) -> bool
{
    if (root == nullptr) {
        return false;
    }
    const QStringList expected_ids{
        "data", "domain", "mesh", "physics", "solver", "run", "post_processing"};
    if (!root_property(root, "shellReady").toBool()
        || root_property(root, "navigationCount").toInt() != expected_ids.size()
        || root_property(root, "activeSectionId").toString() != "data"
        || controller.serviceBackend() != "no-solver"
        || !controller.serviceBoundaryAvailable()
        || controller.solverAvailable()) {
        return false;
    }
    if (expect_diagnostic) {
        if (!controller.diagnosticActive()
            || controller.diagnosticCode() != "application.validation.case_location_required"
            || controller.diagnosticCategory() != "validation"
            || controller.diagnosticSeverity() != "error"
            || !controller.diagnosticContextPreserved()
            || !root_property(root, "diagnosticPanelVisible").toBool()) {
            return false;
        }
    } else if (controller.diagnosticActive()) {
        return false;
    }
    for (int index = 0; index < expected_ids.size(); ++index) {
        if (!QMetaObject::invokeMethod(root, "activateSection", Q_ARG(QVariant, index))) {
            return false;
        }
        if (root_property(root, "activeSectionId").toString() != expected_ids[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    const auto flags = parse_flags(argc, argv);
    auto service = tsunami::application::make_no_solver_application_service();
    ShellController controller(std::move(service));
    if (flags.diagnostic_smoke && !controller.runDiagnosticSmoke()) {
        return 2;
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("shellController", &controller);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("Tsunami.Studentship", "Main");
    QObject* root = engine.rootObjects().isEmpty() ? nullptr : engine.rootObjects().front();

    if (flags.shell_smoke && !run_shell_checks(root, controller, flags.diagnostic_smoke)) {
        return 3;
    }

    if (flags.diagnostic_smoke && !flags.shell_smoke
        && (!controller.diagnosticActive() || controller.diagnosticCode() != "application.validation.case_location_required")) {
        return 4;
    }

    if (flags.smoke_startup) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
