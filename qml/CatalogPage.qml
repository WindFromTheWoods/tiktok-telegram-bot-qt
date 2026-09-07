pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var backend
    property string testDialog: ""
    Component.onCompleted: Qt.callLater(function() {
        if (root.testDialog === "batch") add.open()
        if (root.testDialog === "editor" && root.backend.scheduledVideos.length) {
            editor.entry = root.backend.scheduledVideos[0]
            editor.open()
        }
    })
    property var selectedIds: []
    property double now: Date.now()
    property var rows: {
        const source = tabs.currentIndex === 0 ? backend.scheduledVideos : tabs.currentIndex === 1 ? backend.failedVideos : backend.sentVideos
        const query = search.text.toLowerCase().trim()
        return source.filter(row => (!query || [row.title,row.author,row.url,String(row.id)].join(" ").toLowerCase().includes(query))
            && (channel.currentIndex === 0 || row.channelId === channel.currentValue)
            && (!fromDate.text || row.scheduledDate >= fromDate.text)
            && (!toDate.text || row.scheduledDate <= toDate.text)
            && (state.currentIndex === 0 || (state.currentIndex === 1 ? !row.approved : row.status === state.currentText)))
    }
    function select(id, checked) { selectedIds = checked ? selectedIds.concat([id]) : selectedIds.filter(v => v !== id) }
    function apply(action, value) { const result = backend.bulkVideoAction(selectedIds, action, value || ""); feedback.text = result.message + (result.errors.length ? "\n" + result.errors.join("\n") : ""); selectedIds = [] }
    Timer { interval: 1000; running: root.visible; repeat: true; onTriggered: root.now = Date.now() }
    BatchAddDialog { id: add; parent: Overlay.overlay; backend: root.backend }
    VideoEditorDialog { id: editor; parent: Overlay.overlay; backend: root.backend }
    Dialog { id: bulk; parent: Overlay.overlay; anchors.centerIn: parent; modal: true; title: qsTr("Apply to selected videos"); standardButtons: Dialog.Ok | Dialog.Cancel
        property string operation: "schedule"
        contentItem: ColumnLayout {
            Label { text: bulk.operation === "channel" ? qsTr("Destination channel") : qsTr("Future date/time in each channel's time zone") }
            ComboBox { id: destination; visible: bulk.operation === "channel"; model: root.backend.channels; textRole: "name"; valueRole: "id" }
            TextField { id: bulkDate; visible: bulk.operation === "schedule"; placeholderText: "2026-10-01T18:00" }
        }
        onAccepted: root.apply(operation, operation === "channel" ? String(destination.currentValue) : bulkDate.text)
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 10
        RowLayout {
            Label { text: qsTr("Video catalog"); font.pixelSize: 26; font.bold: true; Layout.fillWidth: true }
            AppButton { text: qsTr("Add links"); primary: true; onClicked: add.open() }
        }
        RowLayout {
            TextField { id: search; Layout.fillWidth: true; placeholderText: qsTr("Search title, author, URL or ID"); Accessible.name: qsTr("Search catalog") }
            ComboBox { id: channel; model: [{id:0,name:qsTr("All channels")}].concat(root.backend.channels); textRole: "name"; valueRole: "id"; Layout.preferredWidth: 190 }
            ComboBox { id: state; model: [qsTr("All states"),qsTr("Draft"),"queued","downloading","ready","uploading","server_scheduled","delivery_unknown","failed","sent"]; Layout.preferredWidth: 150 }
        }
        RowLayout {
            TextField { id: fromDate; placeholderText: qsTr("From YYYY-MM-DD"); Layout.fillWidth: true; Accessible.name: qsTr("From date") }
            TextField { id: toDate; placeholderText: qsTr("To YYYY-MM-DD"); Layout.fillWidth: true; Accessible.name: qsTr("To date") }
            Label { text: qsTr("%1 matching").arg(root.rows.length); color: "#9dadc7" }
        }
        TabBar { id: tabs; Layout.fillWidth: true; onCurrentIndexChanged: root.selectedIds = []
            TabButton { text: qsTr("Queue (%1)").arg(root.backend.scheduledVideos.length) }
            TabButton { text: qsTr("Needs attention (%1)").arg(root.backend.failedVideos.length) }
            TabButton { text: qsTr("Published (%1)").arg(root.backend.sentVideos.length) }
        }
        Flow { Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height; spacing: 5
            CheckBox { text: qsTr("Select visible"); checked: root.rows.length > 0 && root.rows.every(row => root.selectedIds.includes(row.id)); onClicked: root.selectedIds = checked ? root.rows.map(row => row.id) : [] }
            AppButton { compact: true; text: qsTr("Approve"); enabled: root.selectedIds.length > 0; onClicked: root.apply("approve") }
            AppButton { compact: true; text: qsTr("Hold as draft"); enabled: root.selectedIds.length > 0; onClicked: root.apply("draft") }
            AppButton { compact: true; text: qsTr("Retry"); enabled: root.selectedIds.length > 0; onClicked: root.apply("retry") }
            AppButton { compact: true; text: qsTr("Channel"); enabled: root.selectedIds.length > 0; onClicked: { bulk.operation = "channel"; bulk.open() } }
            AppButton { compact: true; text: qsTr("Schedule"); enabled: root.selectedIds.length > 0; onClicked: { bulk.operation = "schedule"; bulk.open() } }
            AppButton { compact: true; text: qsTr("Cancel selected"); danger: true; enabled: root.selectedIds.length > 0; onClicked: root.apply("cancel") }
        }
        Label { id: feedback; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#9dadc7"; visible: text.length > 0 }
        ListView {
            id: list; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 12; model: root.rows
            ScrollBar.vertical: ScrollBar {}
            delegate: ColumnLayout {
                id: row; required property var modelData; width: list.width - 12; spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    CheckBox { checked: root.selectedIds.includes(row.modelData.id); text: "#" + row.modelData.id; onClicked: root.select(row.modelData.id, checked) }
                    Label { text: row.modelData.approved ? qsTr("Approved") : qsTr("Draft — waiting for approval"); color: row.modelData.approved ? "#7ee2b8" : "#f1bf62"; Layout.fillWidth: true }
                    Label { visible: !!row.modelData.nextRetryAtIso; text: qsTr("Retry in %1s").arg(Math.max(0, Math.ceil((Date.parse(row.modelData.nextRetryAtIso) - root.now)/1000))); color: "#f1bf62" }
                    AppButton { compact: true; text: qsTr("Edit / history"); onClicked: { editor.entry = row.modelData; editor.open() } }
                }
                VideoCard { Layout.fillWidth: true; backend: root.backend; entry: row.modelData; mode: tabs.currentIndex === 0 ? "active" : tabs.currentIndex === 1 ? "failed" : "sent" }
            }
            Label { anchors.centerIn: parent; visible: list.count === 0; text: qsTr("No matching videos. Add links or adjust your filters."); color: "#9dadc7" }
        }
    }
}
