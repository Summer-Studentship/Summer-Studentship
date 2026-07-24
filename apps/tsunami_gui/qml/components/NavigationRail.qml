import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: rail
    property var sections: []
    property int activeIndex: 0
    signal sectionRequested(int index)
    Accessible.name: "Primary navigation"

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        Label {
            text: "Navigation"
            font.bold: true
            Layout.margins: 8
        }

        Repeater {
            model: rail.sections
            NavigationDelegate {
                Layout.fillWidth: true
                sectionId: modelData.id
                label: modelData.title
                description: modelData.description
                selected: index === rail.activeIndex
                onClicked: rail.sectionRequested(index)
                Keys.onReturnPressed: rail.sectionRequested(index)
                Keys.onEnterPressed: rail.sectionRequested(index)
                Keys.onSpacePressed: rail.sectionRequested(index)
            }
        }

        Item { Layout.fillHeight: true }
    }
}
