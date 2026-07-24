#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QVariantMap>
#include <QString>
#include <QTimer>

#include <tsunami/application/ServiceFactory.hpp>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    bool smoke_startup = false;

    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == "--smoke-startup") {
            smoke_startup = true;
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

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("Tsunami.Studentship", "Main");

    if (smoke_startup) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
