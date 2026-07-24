import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: header
    property string sectionTitle: ""
    property string backend: ""
    Accessible.name: "Application header"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 18

        Label {
            text: "Tsunami Barrier Studentship"
            font.pixelSize: 20
            font.bold: true
        }

        Label {
            text: "Application shell"
            color: palette.mid
        }

        Item { Layout.fillWidth: true }

        Label {
            text: header.sectionTitle
            font.bold: true
        }

        Label {
            text: "Backend: " + header.backend
            Accessible.name: "Backend " + header.backend
        }
    }
}
