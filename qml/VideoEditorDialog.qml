pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    required property var backend
    property var entry: ({})
    property var events: []
    title: qsTr("Post #%1").arg(entry.id || "")
    modal: true
    width: Math.min(720, parent ? parent.width - 32 : 720)
    height: Math.min(700, parent ? parent.height - 32 : 700)
    anchors.centerIn: parent
    standardButtons: Dialog.Close
    background: Rectangle { color: "#111c2f"; radius: 14; border.color: "#496388" }
    onOpened: { caption.text = entry.caption || ""; events = backend.videoEvents(entry.id) }
    Connections { target: root.backend; function onDataModelsChanged() {
        const all = root.backend.scheduledVideos.concat(root.backend.failedVideos, root.backend.sentVideos)
        const updated = all.find(row => row.id === root.entry.id)
        if (updated) root.entry = updated
        if (root.visible) root.events = root.backend.videoEvents(root.entry.id)
    } }
    contentItem: ColumnLayout {
        spacing: 10
        Label { Layout.fillWidth: true; text: root.entry.title || qsTr("TikTok video"); font.pixelSize: 18; wrapMode: Text.WordWrap }
        Label { Layout.fillWidth: true; text: (root.entry.channelName || "") + " • " + (root.entry.approved ? qsTr("Approved") : qsTr("Draft")) + " • " + (root.entry.status || ""); color: "#9dadc7"; wrapMode: Text.WordWrap }
        RowLayout {
            AppButton { text: qsTr("Preview video"); enabled: !!root.entry.localFileAvailable; onClicked: root.backend.openVideoFile(root.entry.id) }
            AppButton { text: qsTr("Use channel template"); onClicked: {
                const channel = root.backend.channels.find(c => c.id === root.entry.channelId)
                caption.text = channel ? channel.captionTemplate : ""
            } }
        }
        Label { text: qsTr("Caption · placeholders: {title}, {author}, {url}"); color: "#9dadc7" }
        ScrollView { Layout.fillWidth: true; Layout.preferredHeight: 120
            TextArea {
                id: caption
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("Write a caption or apply the channel template")
                Accessible.name: qsTr("Post caption")
                enabled: ["queued","downloading","ready","failed"].includes(root.entry.status)
                background: Rectangle {
                    color: "#0d1728"
                    radius: 8
                    border.color: caption.activeFocus ? "#7199ff" : "#496388"
                }
            }
        }
        RowLayout {
            Label { text: caption.length + " / 1024"; color: caption.length > 1024 ? "#ff9baa" : "#9dadc7" }
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Save caption"); enabled: caption.enabled && caption.length <= 1024; onClicked: root.backend.setVideoCaption(root.entry.id, caption.text) }
            AppButton { text: qsTr("Approve"); primary: true; enabled: caption.enabled && !root.entry.approved && caption.length <= 1024; onClicked: { root.backend.setVideoCaption(root.entry.id, caption.text); if (!root.backend.statusIsError) root.backend.approveVideo(root.entry.id) } }
            AppButton { text: qsTr("Hold as draft"); enabled: caption.enabled && root.entry.approved; onClicked: root.backend.setVideoDraft(root.entry.id, true) }
        }
        Label { Layout.fillWidth: true; visible: !!root.entry.error; text: (root.entry.errorCategory || "") + ": " + (root.entry.error || ""); wrapMode: Text.WordWrap; color: "#ff9baa" }
        RowLayout { visible: root.entry.status === "delivery_unknown"
            AppButton { text: qsTr("I verified: published"); onClicked: root.backend.resolveDeliveryUnknown(root.entry.id, true) }
            AppButton { text: qsTr("I verified: not delivered"); onClicked: root.backend.resolveDeliveryUnknown(root.entry.id, false) }
        }
        AppButton { visible: root.entry.errorCategory === "duplicate"; text: qsTr("Permit this repeat"); onClicked: root.backend.allowVideoRepeat(root.entry.id) }
        Label { text: qsTr("Persistent event history"); font.bold: true }
        ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: root.events; spacing: 8
            delegate: Label { required property var modelData; width: ListView.view.width; text: (modelData.time || "") + " • " + (modelData.category || modelData.level || "") + (modelData.message ? "\n" + modelData.message : ""); color: "#9dadc7"; wrapMode: Text.WordWrap; font.pixelSize: 12 }
        }
        Label { Layout.fillWidth: true; text: root.backend.statusMessage; color: root.backend.statusIsError ? "#ff9baa" : "#9dadc7"; wrapMode: Text.WordWrap }
    }
}
