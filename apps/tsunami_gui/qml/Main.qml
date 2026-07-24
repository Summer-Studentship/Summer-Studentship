import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 960
    height: 640
    visible: true
    title: "Tsunami Barrier Studentship"
    property var diagnosticStatus: diagnosticStatusModel
    property bool diagnosticActive: diagnosticStatus.active === true
    property string diagnosticOperation: diagnosticStatus.operation || ""
    property string diagnosticState: diagnosticStatus.state || ""
    property string diagnosticSeverity: diagnosticStatus.severity || ""
    property string diagnosticCategory: diagnosticStatus.category || ""
    property string diagnosticCode: diagnosticStatus.code || ""
    property string diagnosticMessage: diagnosticStatus.message || ""
    property string diagnosticRuleId: diagnosticStatus.ruleId || ""
    property string diagnosticCaseLocation: diagnosticStatus.caseLocation || ""
    property string diagnosticCaseRevision: diagnosticStatus.caseRevision || ""
    property string diagnosticStateChanged: diagnosticStatus.stateChanged || ""
    property bool diagnosticContextPreserved: diagnosticStatus.contextPreserved === true

    Rectangle {
        anchors.fill: parent
        color: "#f5f7fa"

        Text {
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
            text: "Tsunami Barrier Studentship\nService backend: " + serviceStatus.backend
                  + "\nSolver available: " + (serviceStatus.solverAvailable ? "yes" : "no")
                  + (root.diagnosticActive
                     ? "\nDiagnostic: " + root.diagnosticCode + "\n" + root.diagnosticMessage
                     : "")
            color: "#1f2a37"
            font.pixelSize: 24
        }
    }
}
