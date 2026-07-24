#pragma once

#include <QObject>
#include <QString>

#include <memory>

#include <tsunami/application/ServiceFactory.hpp>

class ShellController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString serviceBackend READ serviceBackend NOTIFY serviceStatusChanged)
    Q_PROPERTY(bool serviceBoundaryAvailable READ serviceBoundaryAvailable NOTIFY serviceStatusChanged)
    Q_PROPERTY(bool solverAvailable READ solverAvailable NOTIFY serviceStatusChanged)
    Q_PROPERTY(bool validationAvailable READ validationAvailable NOTIFY serviceStatusChanged)
    Q_PROPERTY(bool preparationAvailable READ preparationAvailable NOTIFY serviceStatusChanged)
    Q_PROPERTY(bool launchAvailable READ launchAvailable NOTIFY serviceStatusChanged)
    Q_PROPERTY(bool cancellationAvailable READ cancellationAvailable NOTIFY serviceStatusChanged)
    Q_PROPERTY(bool resultDiscoveryAvailable READ resultDiscoveryAvailable NOTIFY serviceStatusChanged)
    Q_PROPERTY(bool diagnosticActive READ diagnosticActive NOTIFY diagnosticChanged)
    Q_PROPERTY(QString diagnosticOperation READ diagnosticOperation NOTIFY diagnosticChanged)
    Q_PROPERTY(QString diagnosticState READ diagnosticState NOTIFY diagnosticChanged)
    Q_PROPERTY(QString diagnosticSeverity READ diagnosticSeverity NOTIFY diagnosticChanged)
    Q_PROPERTY(QString diagnosticCategory READ diagnosticCategory NOTIFY diagnosticChanged)
    Q_PROPERTY(QString diagnosticCode READ diagnosticCode NOTIFY diagnosticChanged)
    Q_PROPERTY(QString diagnosticMessage READ diagnosticMessage NOTIFY diagnosticChanged)
    Q_PROPERTY(QString diagnosticRuleId READ diagnosticRuleId NOTIFY diagnosticChanged)
    Q_PROPERTY(QString diagnosticCaseLocation READ diagnosticCaseLocation NOTIFY diagnosticChanged)
    Q_PROPERTY(QString diagnosticCaseRevision READ diagnosticCaseRevision NOTIFY diagnosticChanged)
    Q_PROPERTY(QString diagnosticStateChanged READ diagnosticStateChanged NOTIFY diagnosticChanged)
    Q_PROPERTY(bool diagnosticContextPreserved READ diagnosticContextPreserved NOTIFY diagnosticChanged)

public:
    explicit ShellController(std::unique_ptr<tsunami::application::IApplicationService> service, QObject* parent = nullptr);

    [[nodiscard]] auto serviceBackend() const -> QString;
    [[nodiscard]] auto serviceBoundaryAvailable() const -> bool;
    [[nodiscard]] auto solverAvailable() const -> bool;
    [[nodiscard]] auto validationAvailable() const -> bool;
    [[nodiscard]] auto preparationAvailable() const -> bool;
    [[nodiscard]] auto launchAvailable() const -> bool;
    [[nodiscard]] auto cancellationAvailable() const -> bool;
    [[nodiscard]] auto resultDiscoveryAvailable() const -> bool;
    [[nodiscard]] auto diagnosticActive() const -> bool;
    [[nodiscard]] auto diagnosticOperation() const -> QString;
    [[nodiscard]] auto diagnosticState() const -> QString;
    [[nodiscard]] auto diagnosticSeverity() const -> QString;
    [[nodiscard]] auto diagnosticCategory() const -> QString;
    [[nodiscard]] auto diagnosticCode() const -> QString;
    [[nodiscard]] auto diagnosticMessage() const -> QString;
    [[nodiscard]] auto diagnosticRuleId() const -> QString;
    [[nodiscard]] auto diagnosticCaseLocation() const -> QString;
    [[nodiscard]] auto diagnosticCaseRevision() const -> QString;
    [[nodiscard]] auto diagnosticStateChanged() const -> QString;
    [[nodiscard]] auto diagnosticContextPreserved() const -> bool;

    Q_INVOKABLE void refreshServiceStatus();
    Q_INVOKABLE bool runDiagnosticSmoke();
    Q_INVOKABLE void clearDiagnostic();

signals:
    void serviceStatusChanged();
    void diagnosticChanged();

private:
    std::unique_ptr<tsunami::application::IApplicationService> service_;
    QString service_backend_;
    bool service_boundary_available_{false};
    bool solver_available_{false};
    bool validation_available_{false};
    bool preparation_available_{false};
    bool launch_available_{false};
    bool cancellation_available_{false};
    bool result_discovery_available_{false};
    bool diagnostic_active_{false};
    QString diagnostic_operation_;
    QString diagnostic_state_;
    QString diagnostic_severity_;
    QString diagnostic_category_;
    QString diagnostic_code_;
    QString diagnostic_message_;
    QString diagnostic_rule_id_;
    QString diagnostic_case_location_;
    QString diagnostic_case_revision_;
    QString diagnostic_state_changed_;
    bool diagnostic_context_preserved_{false};
};
