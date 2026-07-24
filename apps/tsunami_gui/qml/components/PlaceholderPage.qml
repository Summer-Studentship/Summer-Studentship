import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: page
    property string title: ""
    property string sectionId: ""
    property string description: ""
    property string downstreamOwners: ""
    property string availabilityText: "Backend unavailable: placeholder remains navigable."
    Accessible.name: title + " placeholder page"

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: page.title
            font.pixelSize: 26
            font.bold: true
            Accessible.name: page.title + " heading"
        }
        Label { text: "G0 shell placeholder"; font.bold: true }
        Label { text: "No scientific workflow is implemented in this page."; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Label { text: page.description; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Label { text: "Section ID: " + page.sectionId }
        Label { text: "Downstream handoff: " + page.downstreamOwners; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Label { text: page.availabilityText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Item { Layout.fillHeight: true }
    }
}
