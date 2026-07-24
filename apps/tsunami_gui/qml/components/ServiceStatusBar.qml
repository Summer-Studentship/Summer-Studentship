import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: status
    property string backend: ""
    property bool boundaryAvailable: false
    property bool solverAvailable: false
    property bool validationAvailable: false
    property bool preparationAvailable: false
    property bool launchAvailable: false
    property bool cancellationAvailable: false
    property bool resultDiscoveryAvailable: false
    Accessible.name: "Service status"

    RowLayout {
        anchors.fill: parent
        Label { text: "Backend: " + status.backend; font.bold: true }
        Label { text: "Service boundary: " + (status.boundaryAvailable ? "available" : "unavailable") }
        Label { text: "Solver: " + (status.solverAvailable ? "available" : "unavailable") }
        Label { text: "Validation: " + (status.validationAvailable ? "available" : "unavailable") }
        Label { text: "Preparation: " + (status.preparationAvailable ? "available" : "unavailable") }
        Label { text: "Launch: " + (status.launchAvailable ? "available" : "unavailable") }
        Label { text: "Cancellation: " + (status.cancellationAvailable ? "available" : "unavailable") }
        Label { text: "Results: " + (status.resultDiscoveryAvailable ? "available" : "unavailable") }
    }
}
