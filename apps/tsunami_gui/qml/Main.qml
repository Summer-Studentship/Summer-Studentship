import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 960
    height: 640
    visible: true
    title: "Tsunami Barrier Studentship"

    Rectangle {
        anchors.fill: parent
        color: "#f5f7fa"

        Text {
            anchors.centerIn: parent
            text: "Tsunami Barrier Studentship"
            color: "#1f2a37"
            font.pixelSize: 28
        }
    }
}
