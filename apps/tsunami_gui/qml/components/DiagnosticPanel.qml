import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: panel
    property bool active: false
    property string severity: ""
    property string category: ""
    property string code: ""
    property string message: ""
    property string operation: ""
    property string ruleId: ""
    property string stateChanged: ""
    visible: active
    Accessible.name: active ? "Diagnostic " + code : "No active diagnostic"

    GridLayout {
        anchors.fill: parent
        columns: 4
        rowSpacing: 4
        columnSpacing: 18

        Label { text: "Diagnostic:"; font.bold: true }
        TextField { text: panel.code; readOnly: true; selectByMouse: true; Layout.fillWidth: true; Accessible.name: "Diagnostic code" }
        Label { text: panel.message; Layout.columnSpan: 2 }
        Label { text: "Severity: " + panel.severity }
        Label { text: "Category: " + panel.category }
        Label { text: "Operation: " + panel.operation }
        Label { text: "Rule ID: " + panel.ruleId }
        Label { text: "State changed: " + panel.stateChanged }
    }
}
