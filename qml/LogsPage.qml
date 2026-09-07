pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var backend

    function levelColor(level) {
        if (level === "error") return "#ff6b7a"
        if (level === "warning") return "#f1bf62"
        if (level === "success") return "#3ddc97"
        return "#62a2ff"
    }

    function accepted(level) {
        return filterBox.currentIndex === 0
                || filterBox.currentText.toLowerCase() === level
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: qsTr("Activity log"); color: "#eef4ff"; font.pixelSize: 26; font.bold: true }
                Label { text: qsTr("Downloads, publications, retries, updates and errors"); color: "#9dadc7" }
            }

            ComboBox {
                id: filterBox
                model: [qsTr("All"), "info", "success", "warning", "error"]
            }

            AppButton {
                compact: true
                danger: true
                text: qsTr("Clear")
                onClicked: root.backend.clearLogs()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 15
            color: "#111c2f"
            border.color: "#263751"

            Label {
                anchors.centerIn: parent
                text: qsTr("No activity has been recorded in this session.")
                color: "#9dadc7"
                visible: root.backend.logs.length === 0
            }

            ListView {
                anchors.fill: parent
                anchors.margins: 14
                model: root.backend.logs
                spacing: 7
                clip: true
                visible: count > 0
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: logRow
                    required property var modelData
                    width: ListView.view.width
                    height: root.accepted(logRow.modelData.level) ? 62 : 0
                    visible: height > 0
                    radius: 9
                    color: "#0e192b"
                    border.color: "#263751"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 11
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 9
                            Layout.preferredHeight: 9
                            radius: 5
                            color: root.levelColor(logRow.modelData.level)
                        }

                        Label {
                            Layout.preferredWidth: 140
                            text: logRow.modelData.time
                            color: "#71829f"
                            font.pixelSize: 11
                        }

                        Label {
                            Layout.preferredWidth: 70
                            text: logRow.modelData.level.toUpperCase()
                            color: root.levelColor(logRow.modelData.level)
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Label {
                            Layout.fillWidth: true
                            text: logRow.modelData.message
                            color: "#dce7fa"
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
