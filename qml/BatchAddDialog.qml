pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property var backend
    property var preview: []
    title: qsTr("Add TikTok links")
    modal: true
    width: Math.min(720, parent ? parent.width - 32 : 720)
    height: Math.min(680, parent ? parent.height - 32 : 680)
    anchors.centerIn: parent
    standardButtons: Dialog.Close
    background: Rectangle { color: "#111c2f"; radius: 14; border.color: "#496388" }
    contentItem: ColumnLayout {
        spacing: 10
        Label { text: qsTr("Paste links, one per line. Nothing is added until you confirm."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: 110
            TextArea { id: links; placeholderText: qsTr("https://www.tiktok.com/@author/video/…"); wrapMode: TextEdit.Wrap; Accessible.name: qsTr("TikTok links"); onTextChanged: root.preview = [] }
        }
        ComboBox { id: channel; Layout.fillWidth: true; model: root.backend.channels; textRole: "name"; valueRole: "id"; Accessible.name: qsTr("Destination channel"); onCurrentValueChanged: root.preview = [] }
        TextField { id: date; Layout.fillWidth: true; placeholderText: qsTr("Optional first time: 2026-10-01T18:00 (channel time zone)"); Accessible.name: qsTr("First publication time") }
        CheckBox { id: draft; checked: true; text: qsTr("Download as drafts — require approval before publication") }
        CheckBox { id: repeat; text: qsTr("Allow deliberate repeats in this channel"); onCheckedChanged: root.preview = [] }
        RowLayout {
            AppButton { text: qsTr("Check links"); enabled: channel.count > 0 && links.text.trim().length > 0; onClicked: root.preview = root.backend.previewUrls(links.text, channel.currentValue, repeat.checked) }
            AppButton { text: qsTr("Add valid links"); primary: true; enabled: root.preview.some(row => row.valid); onClicked: {
                const result = root.backend.addVideos(links.text, channel.currentValue, date.text, draft.checked, repeat.checked)
                feedback.text = result.message + (result.errors.length ? "\n" + result.errors.join("\n") : "")
                if (result.added > 0) { links.clear(); root.preview = [] }
            } }
        }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: root.preview; spacing: 8
            delegate: Label { required property var modelData; width: ListView.view.width; text: (modelData.valid ? "✓ " : "! ") + (modelData.url || "") + "\n" + modelData.message; color: modelData.valid ? "#7ee2b8" : "#ff9baa"; wrapMode: Text.WrapAnywhere; font.pixelSize: 12 }
        }
        Label { id: feedback; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#9dadc7" }
    }
}
