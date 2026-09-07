pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    required property var backend
    property int initialPage: 0
    property string testDialog: ""

    width: 1220
    height: 820
    minimumWidth: 920
    minimumHeight: 660
    visible: true
    title: qsTr("TikTok Telegram Bot")
    color: "#09111f"

    palette.window: "#09111f"
    palette.windowText: "#eef4ff"
    palette.base: "#0d1728"
    palette.alternateBase: "#111c2f"
    palette.text: "#eef4ff"
    palette.placeholderText: "#71829f"
    palette.button: "#1b2a43"
    palette.buttonText: "#eef4ff"
    palette.highlight: "#5b8cff"
    palette.highlightedText: "#ffffff"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 224
            Layout.fillHeight: true
            color: "#0d1829"
            border.color: "#1d2a40"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 18
                    spacing: 11

                    Rectangle {
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 42
                        radius: 13
                        color: "#5b8cff"
                        Text {
                            anchors.centerIn: parent
                            text: "▶"
                            color: "white"
                            font.pixelSize: 17
                            font.bold: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            text: "TikTok Bot"
                            color: "#eef4ff"
                            font.pixelSize: 16
                            font.bold: true
                        }
                        Label {
                            text: qsTr("Publisher 2.0")
                            color: "#71829f"
                            font.pixelSize: 10
                        }
                    }
                }

                Repeater {
                    model: ListModel {
                        ListElement { label: qsTr("Dashboard"); glyph: "◆" }
                        ListElement { label: qsTr("Videos"); glyph: "▶" }
                        ListElement { label: qsTr("Schedule"); glyph: "◷" }
                        ListElement { label: qsTr("Settings"); glyph: "⚙" }
                        ListElement { label: qsTr("Logs"); glyph: "≡" }
                    }

                    delegate: Button {
                        id: navButton
                        required property int index
                        required property string label
                        required property string glyph
                        Layout.fillWidth: true
                        implicitHeight: 46
                        hoverEnabled: true
                        onClicked: pageStack.currentIndex = index

                        contentItem: RowLayout {
                            spacing: 12
                            Text {
                                Layout.preferredWidth: 24
                                text: navButton.glyph
                                color: pageStack.currentIndex === navButton.index
                                       ? "#8eaeff" : "#71829f"
                                font.pixelSize: 15
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                Layout.fillWidth: true
                                text: navButton.label
                                color: pageStack.currentIndex === navButton.index
                                       ? "#ffffff" : "#a7b6cf"
                                font.pixelSize: 13
                                font.weight: pageStack.currentIndex === navButton.index
                                             ? Font.DemiBold : Font.Normal
                            }
                        }

                        background: Rectangle {
                            radius: 10
                            color: pageStack.currentIndex === navButton.index
                                   ? "#20365b"
                                   : (navButton.hovered ? "#17273f" : "transparent")
                            border.color: pageStack.currentIndex === navButton.index
                                          ? "#3d5e91" : "transparent"
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 78
                    radius: 12
                    color: root.backend.botRunning ? "#102d29" : "#151f31"
                    border.color: root.backend.botRunning ? "#2f9d72" : "#2d3b53"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4
                        RowLayout {
                            spacing: 8
                            Rectangle {
                                Layout.preferredWidth: 9
                                Layout.preferredHeight: 9
                                radius: 5
                                color: root.backend.botRunning ? "#3ddc97" : "#71829f"
                            }
                            Label {
                                text: root.backend.botRunning ? qsTr("Bot running")
                                                              : qsTr("Bot stopped")
                                color: root.backend.botRunning ? "#65e4ad" : "#9dadc7"
                                font.bold: true
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Queue: %1  •  Failed: %2")
                                  .arg(root.backend.metrics.activeCount || 0)
                                  .arg(root.backend.metrics.failedCount || 0)
                            color: "#71829f"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#09111f"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 62
                    color: "#0b1525"
                    border.color: "#1d2a40"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 26
                        anchors.rightMargin: 26
                        spacing: 12

                        Label {
                            Layout.fillWidth: true
                            text: root.backend.statusMessage
                            color: root.backend.statusIsError ? "#ff8c98" : "#9dadc7"
                            elide: Text.ElideRight
                        }

                        Label {
                            text: qsTr("Next publication: %1")
                                  .arg(root.backend.metrics.nextPublication || "—")
                            color: "#71829f"
                            font.pixelSize: 11
                        }
                    }
                }

                StackLayout {
                    id: pageStack
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 24
                    currentIndex: root.initialPage

                    DashboardPage { backend: root.backend }
                    CatalogPage { backend: root.backend; testDialog: root.testDialog }
                    SchedulePage { backend: root.backend }
                    SettingsPage { backend: root.backend }
                    LogsPage { backend: root.backend }
                }
            }
        }
    }
}
