import QtQuick
import QtQuick.Controls

ItemDelegate {
    id: delegate
    required property string sectionId
    required property string label
    required property string description
    required property bool selected
    width: parent ? parent.width : 220
    height: 64
    text: label + "\n" + description
    checkable: true
    checked: selected
    focusPolicy: Qt.StrongFocus
    Accessible.name: "Navigate to " + label
    Accessible.description: description
}
