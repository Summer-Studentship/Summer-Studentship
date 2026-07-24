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
            horizontalAlignment: Text.AlignHCenter
            text: "Tsunami Barrier Studentship\nService backend: " + serviceStatus.backend
                  + "\nSolver available: " + (serviceStatus.solverAvailable ? "yes" : "no")
            color: "#1f2a37"
            font.pixelSize: 28
        }
    }
}
