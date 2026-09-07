import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var backend
    required property var entry
    property string mode: "active"

    readonly property bool activeMode: mode === "active"
    readonly property bool failedMode: mode === "failed"
    readonly property bool serverScheduled: entry.status === "server_scheduled"
    readonly property color statusColor: {
        if (entry.status === "ready") return "#3ddc97"
        if (entry.status === "downloading") return "#62a2ff"
        if (entry.status === "uploading") return "#c68cff"
        if (entry.status === "server_scheduled") return "#50d5ff"
        if (entry.status === "failed") return "#ff6b7a"
        if (entry.status === "sent") return "#3ddc97"
        return "#f1bf62"
    }

    function channelIndex() {
        for (let index = 0; index < backend.channels.length; ++index) {
            if (Number(backend.channels[index].id) === Number(entry.channelId))
                return index
        }
        return -1
    }

    implicitHeight: activeMode ? 264 : (failedMode ? 206 : 166)
    radius: 15
    color: hover.hovered ? "#14233a" : "#111c2f"
    border.color: hover.hovered ? "#496b9f" : "#263751"

    HoverHandler {
        id: hover
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 11

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 112
                Layout.preferredHeight: 82
                radius: 11
                color: "#0a1424"
                clip: true

                Image {
                    anchors.fill: parent
                    source: root.entry.thumbnail || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    visible: status === Image.Ready
                }

                Text {
                    anchors.centerIn: parent
                    text: root.entry.status === "sent" ? "✓"
                          : (root.serverScheduled ? "◷" : "▶")
                    color: root.statusColor
                    font.pixelSize: 27
                    font.bold: true
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 7
                    radius: 5
                    color: "#cc08111f"
                    width: durationText.implicitWidth + 10
                    height: 22
                    visible: (root.entry.duration || "").length > 0

                    Text {
                        id: durationText
                        anchors.centerIn: parent
                        text: root.entry.duration || ""
                        color: "white"
                        font.pixelSize: 11
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: root.entry.title || qsTr("TikTok video")
                        color: "#eef4ff"
                        font.pixelSize: 15
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        radius: 7
                        color: Qt.rgba(root.statusColor.r, root.statusColor.g,
                                       root.statusColor.b, 0.15)
                        border.color: root.statusColor
                        implicitWidth: statusText.implicitWidth + 16
                        implicitHeight: 27

                        Text {
                            id: statusText
                            anchors.centerIn: parent
                            text: root.entry.status
                            color: root.statusColor
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: (root.entry.author ? "@" + root.entry.author + "  •  " : "")
                          + root.entry.url
                    color: "#9dadc7"
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }

                Label {
                    Layout.fillWidth: true
                    text: root.entry.channelName + "  •  "
                          + (root.entry.fileSizeMiB ? root.entry.fileSizeMiB + " MB  •  " : "")
                          + (root.entry.status === "sent"
                             ? qsTr("Sent %1").arg(root.entry.publishedAt)
                             : (root.serverScheduled
                                ? qsTr("Stored by Telegram • Sends %1").arg(root.entry.scheduledAt)
                                : qsTr("Planned %1").arg(root.entry.scheduledAt)))
                    color: "#71829f"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: root.entry.error || ""
                    color: "#ff8c98"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    visible: text.length > 0
                }
            }
        }

        ProgressBar {
            id: videoProgress
            Layout.fillWidth: true
            from: 0
            to: 100
            value: Number(root.entry.progress || 0)
            visible: root.entry.status === "downloading"
            background: Rectangle {
                radius: 3
                color: "#24324a"
            }
            contentItem: Item {
                implicitHeight: 7
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, videoProgress.position))
                    height: parent.height
                    radius: 3
                    color: "#5b8cff"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.activeMode

            ComboBox {
                id: channelBox
                Layout.preferredWidth: 170
                model: root.backend.channels
                textRole: "name"
                valueRole: "id"
                enabled: !root.serverScheduled
                Component.onCompleted: currentIndex = root.channelIndex()
                onActivated: root.backend.setVideoChannel(
                                 root.entry.id, Number(currentValue))

                Connections {
                    target: root.backend
                    ignoreUnknownSignals: true
                    function onDataModelsChanged() {
                        channelBox.currentIndex = root.channelIndex()
                    }
                }
            }

            TextField {
                id: scheduleField
                Layout.fillWidth: true
                text: root.entry.scheduledAtChannelIso || root.entry.scheduledAtIso || ""
                placeholderText: qsTr("2026-08-25T18:00")
                selectByMouse: true
            }

            AppButton {
                compact: true
                text: qsTr("Apply")
                onClicked: root.backend.setVideoSchedule(root.entry.id,
                                                         scheduleField.text)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppButton {
                compact: true
                text: qsTr("Open ↗")
                onClicked: Qt.openUrlExternally(root.entry.url)
            }

            Item { Layout.fillWidth: true }

            AppButton {
                compact: true
                text: "↑"
                Accessible.name: qsTr("Move earlier in queue")
                visible: root.activeMode && !root.serverScheduled
                onClicked: root.backend.moveVideo(root.entry.id, -1)
            }

            AppButton {
                compact: true
                text: "↓"
                Accessible.name: qsTr("Move later in queue")
                visible: root.activeMode && !root.serverScheduled
                onClicked: root.backend.moveVideo(root.entry.id, 1)
            }

            AppButton {
                compact: true
                primary: true
                text: qsTr("Publish now")
                enabled: !!root.entry.approved
                visible: root.activeMode
                onClicked: root.backend.publishVideoNow(root.entry.id)
            }

            AppButton {
                compact: true
                primary: true
                text: qsTr("Retry")
                enabled: root.entry.status !== "delivery_unknown"
                visible: root.failedMode
                onClicked: root.backend.retryVideo(root.entry.id)
            }

            AppButton {
                compact: true
                danger: true
                text: qsTr("Cancel")
                enabled: root.entry.status !== "delivery_unknown"
                visible: root.activeMode || root.failedMode
                onClicked: root.backend.cancelVideo(root.entry.id)
            }
        }
    }
}
