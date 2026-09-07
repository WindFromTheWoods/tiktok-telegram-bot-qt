pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    required property var backend

    FileDialog {
        id: ytDlpDialog
        title: qsTr("Select yt-dlp executable")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Executable files (*.exe)"), qsTr("All files (*)")]
        onAccepted: root.backend.setYtDlpFromUrl(selectedFile)
    }

    FileDialog {
        id: tdLibDialog
        title: qsTr("Select TDLib JSON library")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("TDLib JSON library (tdjson.dll)"), qsTr("DLL files (*.dll)"), qsTr("All files (*)")]
        onAccepted: root.backend.setTdLibFromUrl(selectedFile)
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export backup")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("Bot backup (*.json)")]
        onAccepted: root.backend.exportBackup(selectedFile)
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import backup")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Bot backup (*.json)")]
        onAccepted: root.backend.importBackup(selectedFile)
    }

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
                    text: qsTr("Settings")
                    color: "#eef4ff"
                    font.pixelSize: 26
                    font.bold: true
                }
                Label {
                    text: qsTr("Telegram, downloader, channels, updates and application behavior")
                    color: "#9dadc7"
                }
            }

            ProductivitySettings { Layout.fillWidth: true; backend: root.backend }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: telegramLayout.implicitHeight + 36
                radius: 15
                color: "#111c2f"
                border.color: "#263751"

                ColumnLayout {
                    id: telegramLayout
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 11

                    Label {
                        text: qsTr("Telegram credentials")
                        color: "#eef4ff"
                        font.pixelSize: 17
                        font.bold: true
                    }

                    Label { text: qsTr("Bot token"); color: "#9dadc7" }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: tokenField
                            Layout.fillWidth: true
                            text: root.backend.botToken
                            echoMode: revealButton.checked ? TextInput.Normal : TextInput.Password
                            placeholderText: qsTr("Token issued by @BotFather")
                            selectByMouse: true
                            onTextEdited: root.backend.botToken = text
                        }
                        AppButton {
                            id: revealButton
                            compact: true
                            checkable: true
                            text: checked ? qsTr("Hide") : qsTr("Show")
                        }
                    }
                    Label {
                        text: qsTr("Stored in %1").arg(root.backend.secretStorageDescription)
                        color: root.backend.secureSecretStorage ? "#71829f" : "#ff8c98"
                        font.pixelSize: 11
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: width >= 700 ? 2 : 1
                        columnSpacing: 12
                        rowSpacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Default channel ID"); color: "#9dadc7" }
                            TextField {
                                Layout.fillWidth: true
                                text: root.backend.channelId
                                placeholderText: "-1001234567890"
                                onTextEdited: root.backend.channelId = text
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Administrator user ID"); color: "#9dadc7" }
                            TextField {
                                Layout.fillWidth: true
                                text: root.backend.adminUserId
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: RegularExpressionValidator { regularExpression: /[0-9]*/ }
                                onTextEdited: root.backend.adminUserId = text
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: publisherLayout.implicitHeight + 36
                radius: 15
                color: "#111c2f"
                border.color: root.backend.publicationMode === "telegram_server"
                              ? "#5b8cff" : "#263751"

                ColumnLayout {
                    id: publisherLayout
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 11

                    Label {
                        text: qsTr("Publication mode")
                        color: "#eef4ff"
                        font.pixelSize: 17
                        font.bold: true
                    }

                    ComboBox {
                        id: publicationModeBox
                        Layout.fillWidth: true
                        model: [qsTr("Local queue — publish through Bot API"),
                                qsTr("Telegram server queue — publish through a user account")]
                        currentIndex: root.backend.publicationMode === "telegram_server" ? 1 : 0
                        onActivated: root.backend.publicationMode = currentIndex === 1
                                     ? "telegram_server" : "local"
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.backend.publicationMode === "telegram_server"
                              ? qsTr("Videos are uploaded in advance and stored as scheduled posts by Telegram. The user account must be an administrator of every destination channel.")
                              : qsTr("The application keeps the downloaded file and sends it at the planned time through the bot.")
                        color: "#9dadc7"
                        wrapMode: Text.WordWrap
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        visible: root.backend.publicationMode === "telegram_server"

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("TDLib is optional and is loaded dynamically. Use a 64-bit tdjson.dll for this 64-bit application. Obtain API credentials at my.telegram.org.")
                            color: "#f1bf62"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                        }

                        Label { text: qsTr("tdjson.dll"); color: "#9dadc7" }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                Layout.fillWidth: true
                                text: root.backend.tdLibPath
                                placeholderText: qsTr("Leave empty when tdjson.dll is next to the application")
                                selectByMouse: true
                                onTextEdited: root.backend.tdLibPath = text
                            }
                            AppButton {
                                compact: true
                                text: qsTr("Browse…")
                                onClicked: tdLibDialog.open()
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: width >= 700 ? 2 : 1
                            columnSpacing: 12
                            rowSpacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("API ID"); color: "#9dadc7" }
                                TextField {
                                    Layout.fillWidth: true
                                    text: root.backend.tdApiId
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    validator: RegularExpressionValidator { regularExpression: /[0-9]*/ }
                                    onTextEdited: root.backend.tdApiId = text
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { text: qsTr("API hash"); color: "#9dadc7" }
                                TextField {
                                    Layout.fillWidth: true
                                    text: root.backend.tdApiHash
                                    echoMode: TextInput.Password
                                    placeholderText: qsTr("32 hexadecimal characters")
                                    selectByMouse: true
                                    onTextEdited: root.backend.tdApiHash = text
                                }
                            }
                        }

                        Label { text: qsTr("User account phone number"); color: "#9dadc7" }
                        TextField {
                            Layout.fillWidth: true
                            text: root.backend.tdPhoneNumber
                            placeholderText: qsTr("+380…")
                            inputMethodHints: Qt.ImhDialableCharactersOnly
                            onTextEdited: root.backend.tdPhoneNumber = text
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: tdStatusLayout.implicitHeight + 24
                            radius: 10
                            color: root.backend.tdReady ? "#0b3029" : "#0e192b"
                            border.color: root.backend.tdReady ? "#3ddc97" : "#344968"

                            ColumnLayout {
                                id: tdStatusLayout
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 7
                                Label {
                                    Layout.fillWidth: true
                                    text: root.backend.tdAuthorizationStatus
                                    color: root.backend.tdReady ? "#75e8bd" : "#c4d2e8"
                                    wrapMode: Text.WordWrap
                                }
                                Label {
                                    text: qsTr("State: %1").arg(root.backend.tdAuthorizationState)
                                    color: "#71829f"
                                    font.pixelSize: 10
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: root.backend.tdAuthorizationState === "wait_registration" ? 2 : 1
                            visible: ["wait_phone", "wait_code", "wait_password",
                                      "wait_email", "wait_email_code",
                                      "wait_registration"].indexOf(root.backend.tdAuthorizationState) >= 0

                            TextField {
                                id: tdAuthField
                                Layout.fillWidth: true
                                echoMode: root.backend.tdAuthorizationState === "wait_password"
                                          ? TextInput.Password : TextInput.Normal
                                placeholderText: {
                                    const state = root.backend.tdAuthorizationState
                                    if (state === "wait_phone") return qsTr("Phone number")
                                    if (state === "wait_code") return qsTr("Telegram login code")
                                    if (state === "wait_password") return qsTr("Two-step verification password")
                                    if (state === "wait_email") return qsTr("Email address")
                                    if (state === "wait_email_code") return qsTr("Email code")
                                    return qsTr("First name")
                                }
                                onAccepted: tdSubmitButton.clicked()
                            }
                            TextField {
                                id: tdRegistrationLastName
                                Layout.fillWidth: true
                                visible: root.backend.tdAuthorizationState === "wait_registration"
                                placeholderText: qsTr("Last name (optional)")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            AppButton {
                                text: root.backend.tdReady ? qsTr("Reconnect TDLib")
                                                          : qsTr("Connect TDLib")
                                onClicked: root.backend.connectTdPublisher()
                            }
                            AppButton {
                                id: tdSubmitButton
                                primary: true
                                visible: tdAuthField.visible
                                text: qsTr("Submit")
                                enabled: tdAuthField.text.length > 0
                                onClicked: {
                                    root.backend.submitTdAuthorization(
                                                tdAuthField.text,
                                                tdRegistrationLastName.text)
                                    tdAuthField.clear()
                                    tdRegistrationLastName.clear()
                                }
                            }
                            AppButton {
                                danger: true
                                visible: root.backend.tdReady
                                text: qsTr("Log out user account")
                                onClicked: root.backend.logOutTdPublisher()
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: downloadLayout.implicitHeight + 36
                radius: 15
                color: "#111c2f"
                border.color: "#263751"

                ColumnLayout {
                    id: downloadLayout
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 11
                    Label {
                        text: qsTr("Downloader and fallback interval")
                        color: "#eef4ff"
                        font.pixelSize: 17
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            Layout.fillWidth: true
                            text: root.backend.ytDlpExecutable
                            placeholderText: "C:\\Tools\\yt-dlp.exe"
                            onTextEdited: root.backend.ytDlpExecutable = text
                        }
                        AppButton {
                            compact: true
                            text: qsTr("Browse…")
                            onClicked: ytDlpDialog.open()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("Minutes between posts when no calendar slots exist"); color: "#9dadc7" }
                            Label { text: qsTr("120 minutes = every two hours"); color: "#71829f"; font.pixelSize: 11 }
                        }
                        SpinBox {
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
                implicitHeight: channelsLayout.implicitHeight + 36
                radius: 15
                color: "#111c2f"
                border.color: "#263751"

                ColumnLayout {
                    id: channelsLayout
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 11
                    Label {
                        text: qsTr("Destination channels")
                        color: "#eef4ff"
                        font.pixelSize: 17
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: channelName; Layout.fillWidth: true; placeholderText: qsTr("Display name") }
                        TextField { id: channelTelegramId; Layout.fillWidth: true; placeholderText: qsTr("@channel or -100…") }
                        AppButton {
                            compact: true
                            primary: true
                            text: qsTr("Add")
                            onClicked: {
                                root.backend.addChannel(channelName.text, channelTelegramId.text)
                                channelName.clear()
                                channelTelegramId.clear()
                            }
                        }
                    }
                    Repeater {
                        model: root.backend.channels
                        delegate: Rectangle {
                            id: channelRow
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 52
                            radius: 9
                            color: "#0e192b"
                            border.color: channelRow.modelData.isDefault ? "#5b8cff" : "#263751"
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label { text: channelRow.modelData.name; color: "#eef4ff"; font.bold: true }
                                    Label { text: channelRow.modelData.telegramId; color: "#71829f"; font.pixelSize: 11 }
                                }
                                Label {
                                    text: qsTr("Default")
                                    color: "#7fa4ff"
                                    visible: channelRow.modelData.isDefault
                                }
                                AppButton {
                                    compact: true
                                    text: qsTr("Make default")
                                    visible: !channelRow.modelData.isDefault
                                    onClicked: root.backend.setDefaultChannel(channelRow.modelData.id)
                                }
                                AppButton {
                                    compact: true
                                    danger: true
                                    text: qsTr("Remove")
                                    visible: !channelRow.modelData.isDefault
                                    onClicked: root.backend.removeChannel(channelRow.modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: appLayout.implicitHeight + 36
                radius: 15
                color: "#111c2f"
                border.color: "#263751"

                ColumnLayout {
                    id: appLayout
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12
                    Label { text: qsTr("Application and backups"); color: "#eef4ff"; font.pixelSize: 17; font.bold: true }
                    CheckBox {
                        text: qsTr("Start with Windows and minimize to tray")
                        checked: root.backend.autostartEnabled
                        onToggled: root.backend.setAutostartEnabled(checked)
                    }
                    Label {
                        text: qsTr("SQLite: %1").arg(root.backend.databasePath)
                        color: "#71829f"
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton { text: qsTr("Export backup"); onClicked: exportDialog.open() }
                        AppButton { text: qsTr("Import backup"); onClicked: importDialog.open() }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: updateLayout.implicitHeight + 36
                radius: 15
                color: "#111c2f"
                border.color: "#263751"

                ColumnLayout {
                    id: updateLayout
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10
                    Label { text: qsTr("Automatic updates"); color: "#eef4ff"; font.pixelSize: 17; font.bold: true }
                    Label { Layout.fillWidth: true; text: root.backend.updateManager.applicationStatus; color: "#9dadc7"; wrapMode: Text.WordWrap }
                    Label { Layout.fillWidth: true; text: root.backend.updateManager.ytDlpStatus; color: "#9dadc7"; wrapMode: Text.WordWrap }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            text: qsTr("Check now")
                            enabled: !root.backend.updateManager.checking
                            onClicked: root.backend.updateManager.checkForUpdates()
                        }
                        AppButton {
                            text: qsTr("Update yt-dlp")
                            enabled: !root.backend.updateManager.checking
                            onClicked: root.backend.updateManager.updateYtDlp()
                        }
                        AppButton {
                            primary: true
                            text: qsTr("Install app update")
                            visible: root.backend.updateManager.applicationUpdateAvailable
                            onClicked: root.backend.updateManager.installApplicationUpdate()
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                Label {
                    Layout.fillWidth: true
                    text: root.backend.statusMessage
                    color: root.backend.statusIsError ? "#ff8c98" : "#9dadc7"
                    wrapMode: Text.WordWrap
                }
                AppButton {
                    danger: true
                    text: qsTr("Stop")
                    enabled: root.backend.botRunning
                    onClicked: root.backend.stopBot()
                }
                AppButton {
                    primary: true
                    text: root.backend.botRunning ? qsTr("Save and restart") : qsTr("Save and start")
                    onClicked: root.backend.saveAndStart()
                }
            }
        }
    }
}
