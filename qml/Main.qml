import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root

    required property var backend

    readonly property color pageColor: "#0b1220"
    readonly property color cardColor: "#111c2f"
    readonly property color cardBorderColor: "#24324a"
    readonly property color primaryColor: "#5b8cff"
    readonly property color textColor: "#eef4ff"
    readonly property color mutedTextColor: "#9dadc7"
    readonly property color successColor: "#3ddc97"
    readonly property color errorColor: "#ff6b7a"

    width: 820
    height: 760
    minimumWidth: 640
    minimumHeight: 600
    visible: true
    title: qsTr("TikTok Telegram Bot")
    color: pageColor

    palette.window: pageColor
    palette.windowText: textColor
    palette.base: "#0d1728"
    palette.alternateBase: cardColor
    palette.text: textColor
    palette.placeholderText: mutedTextColor
    palette.button: "#1b2a43"
    palette.buttonText: textColor
    palette.highlight: primaryColor
    palette.highlightedText: "#ffffff"

    component AppButton: Button {
        id: buttonControl

        property bool primary: false
        property bool danger: false

        implicitWidth: Math.max(104, buttonLabel.implicitWidth + 34)
        implicitHeight: 40
        leftPadding: 17
        rightPadding: 17
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus

        contentItem: Text {
            id: buttonLabel
            text: buttonControl.text
            color: !buttonControl.enabled ? "#6f7d95"
                                          : (buttonControl.danger ? "#ff9baa" : "#ffffff")
            font.family: buttonControl.font.family
            font.pixelSize: buttonControl.font.pixelSize
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 8
            color: !buttonControl.enabled
                   ? "#151f31"
                   : (buttonControl.primary
                      ? (buttonControl.down ? "#3e67d5"
                                            : (buttonControl.hovered ? "#7199ff"
                                                                     : "#5b8cff"))
                      : (buttonControl.danger
                         ? (buttonControl.down ? "#44202d"
                                               : (buttonControl.hovered ? "#382231"
                                                                        : "#291c29"))
                         : (buttonControl.down ? "#243a5b"
                                               : (buttonControl.hovered ? "#253958"
                                                                        : "#1a2942"))))
            border.width: buttonControl.activeFocus ? 2 : 1
            border.color: !buttonControl.enabled
                          ? "#27344b"
                          : (buttonControl.primary
                             ? "#8baaff"
                             : (buttonControl.danger ? "#a94f68" : "#496388"))
        }
    }

    component CatalogTabButton: TabButton {
        id: tabControl

        implicitHeight: 42
        hoverEnabled: true

        contentItem: Text {
            text: tabControl.text
            color: tabControl.checked ? "#ffffff" : "#9dadc7"
            font.pixelSize: 13
            font.weight: tabControl.checked ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: 8
            color: tabControl.checked ? "#263d66"
                                      : (tabControl.hovered ? "#1c2d48" : "transparent")
            border.width: tabControl.activeFocus ? 2 : 0
            border.color: "#8baaff"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                height: 3
                radius: 2
                color: "#5b8cff"
                visible: tabControl.checked
            }
        }
    }

    component VideoCatalogRow: Rectangle {
        id: videoRow

        required property var entry
        property bool sent: false

        width: ListView.view ? ListView.view.width : 0
        implicitHeight: 88
        radius: 11
        color: rowHover.hovered ? "#182944" : "#0e192b"
        border.color: rowHover.hovered ? "#4c6b9b" : "#263751"

        HoverHandler {
            id: rowHover
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: Qt.openUrlExternally(videoRow.entry.url)
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 13
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                radius: 12
                color: videoRow.sent ? "#143a32" : "#1d3154"

                Text {
                    anchors.centerIn: parent
                    text: videoRow.sent ? "✓" : "▶"
                    color: videoRow.sent ? "#3ddc97" : "#7fa4ff"
                    font.pixelSize: 17
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5

                Text {
                    Layout.fillWidth: true
                    text: videoRow.entry.url
                    color: "#eef4ff"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }

                Text {
                    Layout.fillWidth: true
                    text: videoRow.sent
                          ? qsTr("Sent %1").arg(videoRow.entry.publishedAt)
                          : (videoRow.entry.status === "Scheduled"
                             ? qsTr("Scheduled for %1").arg(videoRow.entry.scheduledAt)
                             : qsTr("%1 now").arg(videoRow.entry.status))
                    color: videoRow.sent ? "#65e4ad" : "#9dadc7"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: videoRow.sent ? qsTr("Published to the Telegram channel")
                                        : qsTr("Added %1").arg(videoRow.entry.requestedAt)
                    color: "#71829f"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            Text {
                text: qsTr("Open ↗")
                color: "#8eaeff"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }
    }

    FileDialog {
        id: ytDlpDialog
        title: qsTr("Select yt-dlp executable")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Executable files (*.exe)"), qsTr("All files (*)")]
        onAccepted: root.backend.setYtDlpFromUrl(selectedFile)
    }

    header: Rectangle {
        color: "#0e192b"
        height: 86

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 32
            anchors.rightMargin: 32
            spacing: 16

            Rectangle {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                radius: 14
                color: root.primaryColor

                Label {
                    anchors.centerIn: parent
                    text: "▶"
                    color: "white"
                    font.pixelSize: 19
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    text: qsTr("TikTok Telegram Bot")
                    color: root.textColor
                    font.pixelSize: 21
                    font.bold: true
                }

                Label {
                    text: qsTr("Settings and publication scheduler")
                    color: root.mutedTextColor
                    font.pixelSize: 13
                }
            }

            RowLayout {
                spacing: 9

                Rectangle {
                    Layout.preferredWidth: 10
                    Layout.preferredHeight: 10
                    radius: 5
                    color: root.backend.botRunning ? root.successColor : "#73809a"
                }

                Label {
                    text: root.backend.botRunning ? qsTr("Running") : qsTr("Stopped")
                    color: root.backend.botRunning ? root.successColor : root.mutedTextColor
                    font.bold: true
                }
            }
        }
    }

    ScrollView {
        id: settingsScroll
        anchors.fill: parent
        anchors.margins: 24
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: settingsScroll.availableWidth
            spacing: 18

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: telegramLayout.implicitHeight + 40
                radius: 16
                color: root.cardColor
                border.color: root.cardBorderColor

                ColumnLayout {
                    id: telegramLayout
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 14

                    Label {
                        text: qsTr("Telegram")
                        color: root.textColor
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Label {
                        text: qsTr("Bot token")
                        color: root.mutedTextColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: botTokenField
                            Layout.fillWidth: true
                            text: root.backend.botToken
                            placeholderText: qsTr("Token issued by @BotFather")
                            echoMode: revealTokenButton.checked ? TextInput.Normal
                                                                : TextInput.Password
                            selectByMouse: true
                            onTextEdited: root.backend.botToken = text
                        }

                        AppButton {
                            id: revealTokenButton
                            Layout.preferredWidth: 84
                            checkable: true
                            text: checked ? qsTr("Hide") : qsTr("Show")
                            Accessible.name: checked ? qsTr("Hide bot token")
                                                     : qsTr("Show bot token")
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.backend.secureSecretStorage
                              ? qsTr("The token is stored in %1.")
                                    .arg(root.backend.secretStorageDescription)
                              : qsTr("Warning: the token is stored in %1.")
                                    .arg(root.backend.secretStorageDescription)
                        color: root.backend.secureSecretStorage ? root.mutedTextColor
                                                               : root.errorColor
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: width >= 620 ? 2 : 1
                        columnSpacing: 14
                        rowSpacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Label {
                                text: qsTr("Channel ID")
                                color: root.mutedTextColor
                            }

                            TextField {
                                Layout.fillWidth: true
                                text: root.backend.channelId
                                placeholderText: qsTr("-1001234567890 or @channel_name")
                                selectByMouse: true
                                onTextEdited: root.backend.channelId = text
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Label {
                                text: qsTr("Administrator user ID")
                                color: root.mutedTextColor
                            }

                            TextField {
                                Layout.fillWidth: true
                                text: root.backend.adminUserId
                                placeholderText: qsTr("Positive numeric Telegram user ID")
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: RegularExpressionValidator {
                                    regularExpression: /[0-9]*/
                                }
                                selectByMouse: true
                                onTextEdited: root.backend.adminUserId = text
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: deliveryLayout.implicitHeight + 40
                radius: 16
                color: root.cardColor
                border.color: root.cardBorderColor

                ColumnLayout {
                    id: deliveryLayout
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 14

                    Label {
                        text: qsTr("Downloads and publishing")
                        color: root.textColor
                        font.pixelSize: 18
                        font.bold: true
                    }

                    Label {
                        text: qsTr("yt-dlp executable")
                        color: root.mutedTextColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            Layout.fillWidth: true
                            text: root.backend.ytDlpExecutable
                            placeholderText: qsTr("yt-dlp or C:\\Tools\\yt-dlp.exe")
                            selectByMouse: true
                            onTextEdited: root.backend.ytDlpExecutable = text
                        }

                        AppButton {
                            Layout.preferredWidth: 108
                            text: qsTr("Browse…")
                            onClicked: ytDlpDialog.open()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

                            Label {
                                text: qsTr("Interval between posts, minutes")
                                color: root.mutedTextColor
                            }

                            Label {
                                text: qsTr("120 minutes = one publication every two hours")
                                color: root.mutedTextColor
                                font.pixelSize: 12
                            }
                        }

                        SpinBox {
                            id: intervalSpinBox
                            Layout.preferredWidth: 150
                            from: 1
                            to: 10080
                            editable: true
                            value: root.backend.publicationIntervalMinutes
                            onValueModified: root.backend.publicationIntervalMinutes = value
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: catalogLayout.implicitHeight + 40
                radius: 16
                color: root.cardColor
                border.color: root.cardBorderColor

                ColumnLayout {
                    id: catalogLayout
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Label {
                                text: qsTr("Video catalog")
                                color: root.textColor
                                font.pixelSize: 18
                                font.bold: true
                            }

                            Label {
                                text: qsTr("Scheduled queue and publication history")
                                color: root.mutedTextColor
                                font.pixelSize: 12
                            }
                        }

                        Label {
                            text: qsTr("%1 total")
                                  .arg(root.backend.scheduledVideos.length
                                       + root.backend.sentVideos.length)
                            color: root.mutedTextColor
                            font.pixelSize: 12
                        }
                    }

                    TabBar {
                        id: catalogTabs
                        Layout.fillWidth: true
                        background: Rectangle {
                            radius: 10
                            color: "#0d1829"
                            border.color: "#263751"
                        }

                        CatalogTabButton {
                            text: qsTr("Scheduled (%1)")
                                  .arg(root.backend.scheduledVideos.length)
                        }

                        CatalogTabButton {
                            text: qsTr("Sent (%1)").arg(root.backend.sentVideos.length)
                        }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 280
                        currentIndex: catalogTabs.currentIndex

                        Item {
                            Label {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 40, 440)
                                text: qsTr("There are no scheduled videos. Send a TikTok link to the bot to add it to the queue.")
                                color: root.mutedTextColor
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                visible: root.backend.scheduledVideos.length === 0
                            }

                            ListView {
                                id: scheduledVideosList
                                anchors.fill: parent
                                clip: true
                                spacing: 8
                                model: root.backend.scheduledVideos
                                visible: count > 0
                                ScrollBar.vertical: ScrollBar {}

                                delegate: VideoCatalogRow {
                                    required property var modelData
                                    entry: modelData
                                }
                            }
                        }

                        Item {
                            Label {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 40, 440)
                                text: qsTr("Successfully published videos will appear here.")
                                color: root.mutedTextColor
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                visible: root.backend.sentVideos.length === 0
                            }

                            ListView {
                                id: sentVideosList
                                anchors.fill: parent
                                clip: true
                                spacing: 8
                                model: root.backend.sentVideos
                                visible: count > 0
                                ScrollBar.vertical: ScrollBar {}

                                delegate: VideoCatalogRow {
                                    required property var modelData
                                    entry: modelData
                                    sent: true
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: statusLayout.implicitHeight + 32
                radius: 14
                color: root.backend.statusIsError ? "#351b28"
                                                  : (root.backend.botRunning ? "#102d29"
                                                                             : "#172237")
                border.color: root.backend.statusIsError ? root.errorColor
                                                         : (root.backend.botRunning
                                                            ? root.successColor
                                                            : root.cardBorderColor)

                RowLayout {
                    id: statusLayout
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 10
                        Layout.preferredHeight: 10
                        radius: 5
                        color: root.backend.statusIsError ? root.errorColor
                                                         : (root.backend.botRunning
                                                            ? root.successColor
                                                            : root.mutedTextColor)
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.backend.statusMessage
                        color: root.textColor
                        wrapMode: Text.WordWrap
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                spacing: 12

                Item {
                    Layout.fillWidth: true
                }

                AppButton {
                    Layout.preferredWidth: 116
                    danger: true
                    text: qsTr("Stop bot")
                    enabled: root.backend.botRunning
                    onClicked: root.backend.stopBot()
                }

                AppButton {
                    id: startButton
                    Layout.preferredWidth: root.backend.botRunning ? 170 : 150
                    primary: true
                    text: root.backend.botRunning ? qsTr("Save and restart")
                                                  : qsTr("Save and start")
                    onClicked: root.backend.saveAndStart()
                }
            }
        }
    }
}
