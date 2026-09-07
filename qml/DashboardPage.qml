import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var backend

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 18

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    text: qsTr("Dashboard")
                    color: "#eef4ff"
                    font.pixelSize: 26
                    font.bold: true
                }

                Label {
                    text: qsTr("Publication pipeline, storage and bot health")
                    color: "#9dadc7"
                    font.pixelSize: 13
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width >= 860 ? 4 : 2
                columnSpacing: 14
                rowSpacing: 14

                MetricCard {
                    Layout.fillWidth: true
                    title: qsTr("Active queue")
                    value: String(root.backend.metrics.activeCount || 0)
                    subtitle: qsTr("downloaded and scheduled")
                    accent: "#5b8cff"
                    iconText: "▶"
                }

                MetricCard {
                    Layout.fillWidth: true
                    title: qsTr("Published")
                    value: String(root.backend.metrics.sentCount || 0)
                    subtitle: qsTr("successful channel posts")
                    accent: "#3ddc97"
                    iconText: "✓"
                }

                MetricCard {
                    Layout.fillWidth: true
                    title: qsTr("Failed")
                    value: String(root.backend.metrics.failedCount || 0)
                    subtitle: qsTr("waiting for retry")
                    accent: "#ff6b7a"
                    iconText: "!"
                }

                MetricCard {
                    Layout.fillWidth: true
                    title: qsTr("Free disk")
                    value: (root.backend.metrics.freeDiskGiB || "—") + " GB"
                    subtitle: qsTr("Cache: %1 MB").arg(root.backend.metrics.cacheMiB || "0")
                    accent: "#c68cff"
                    iconText: "▣"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: pipelineLayout.implicitHeight + 36
                radius: 15
                color: "#111c2f"
                border.color: "#263751"

                ColumnLayout {
                    id: pipelineLayout
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Label {
                                text: qsTr("Current pipeline")
                                color: "#eef4ff"
                                font.pixelSize: 17
                                font.bold: true
                            }

                            Label {
                                text: root.backend.metrics.downloading
                                      ? qsTr("Downloading metadata and video")
                                      : (root.backend.metrics.uploading
                                         ? qsTr("Uploading to Telegram")
                                         : qsTr("Waiting for work"))
                                color: "#9dadc7"
                            }
                        }

                        Rectangle {
                            radius: 8
                            color: root.backend.botRunning ? "#143a32" : "#2b2632"
                            border.color: root.backend.botRunning ? "#3ddc97" : "#71829f"
                            implicitWidth: pipelineStatus.implicitWidth + 20
                            implicitHeight: 30

                            Label {
                                id: pipelineStatus
                                anchors.centerIn: parent
                                text: root.backend.botRunning ? qsTr("Bot running")
                                                              : qsTr("Bot stopped")
                                color: root.backend.botRunning ? "#65e4ad" : "#9dadc7"
                                font.bold: true
                            }
                        }
                    }

                    ProgressBar {
                        id: downloadProgress
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: Number(root.backend.metrics.downloadProgress || 0)
                        background: Rectangle { radius: 4; color: "#24324a" }
                        contentItem: Item {
                            implicitHeight: 9
                            Rectangle {
                                width: parent.width * downloadProgress.position
                                height: parent.height
                                radius: 4
                                color: "#5b8cff"
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: root.backend.metrics.downloadDetail || qsTr("No active download")
                            color: "#71829f"
                            elide: Text.ElideRight
                        }

                        Label {
                            text: qsTr("Next: %1").arg(root.backend.metrics.nextPublication || "—")
                            color: "#9dadc7"
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: quickLayout.implicitHeight + 36
                radius: 15
                color: "#111c2f"
                border.color: "#263751"

                ColumnLayout {
                    id: quickLayout
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12

                    Label {
                        text: qsTr("Quick controls")
                        color: "#eef4ff"
                        font.pixelSize: 17
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.backend.statusMessage
                        color: root.backend.statusIsError ? "#ff8c98" : "#9dadc7"
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }

                        AppButton {
                            danger: true
                            text: qsTr("Stop bot")
                            enabled: root.backend.botRunning
                            onClicked: root.backend.stopBot()
                        }

                        AppButton {
                            primary: true
                            text: root.backend.botRunning ? qsTr("Save and restart")
                                                          : qsTr("Save and start")
                            onClicked: root.backend.saveAndStart()
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
