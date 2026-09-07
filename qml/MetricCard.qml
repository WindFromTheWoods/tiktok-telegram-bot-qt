import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string title: ""
    property string value: "—"
    property string subtitle: ""
    property color accent: "#5b8cff"
    property string iconText: "●"

    implicitHeight: 128
    radius: 15
    color: "#111c2f"
    border.color: "#263751"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        Rectangle {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            radius: 13
            color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.16)

            Text {
                anchors.centerIn: parent
                text: root.iconText
                color: root.accent
                font.pixelSize: 18
                font.bold: true
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Label {
                text: root.title
                color: "#9dadc7"
                font.pixelSize: 12
            }

            Label {
                text: root.value
                color: "#eef4ff"
                font.pixelSize: 23
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                text: root.subtitle
                color: "#71829f"
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }
}
