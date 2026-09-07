pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    required property var backend
    function loadChannel() {
        if (!zone || !caption) return
        const item = backend.channels.find(item => item.id === channel.currentValue)
        zone.currentIndex = item ? backend.availableTimeZones.indexOf(item.timeZoneId) : -1
        zone.editText = item ? item.timeZoneId : ""
        caption.text = item ? item.captionTemplate : ""
    }
    Component.onCompleted: loadChannel()
    implicitHeight: content.implicitHeight + 36
    color: "#111c2f"; border.color: "#263751"; radius: 15
    ColumnLayout {
        id: content; anchors.fill: parent; anchors.margins: 18; spacing: 10
        Label { text: qsTr("Channel defaults, storage and diagnostics"); font.pixelSize: 18; font.bold: true }
        ComboBox { id: channel; Layout.fillWidth: true; model: root.backend.channels; textRole: "name"; valueRole: "id"; onCurrentValueChanged: root.loadChannel() }
        RowLayout {
            Label { text: qsTr("Time zone (empty = system)") }
            ComboBox { id: zone; Layout.fillWidth: true; model: root.backend.availableTimeZones; editable: true }
        }
        TextField { id: caption; Layout.fillWidth: true; placeholderText: qsTr("Caption template: {title} — @{author} {url}") }
        AppButton { text: qsTr("Save channel defaults"); enabled: channel.count>0; onClicked: { root.backend.setChannelTimeZone(channel.currentValue,zone.editText); if(!root.backend.statusIsError) root.backend.setChannelCaptionTemplate(channel.currentValue,caption.text) } }
        GridLayout { columns: 2; Layout.fillWidth: true
            Label { text: qsTr("Cache limit (MiB)") }
            SpinBox { from: 256; to: 1048576; stepSize:256; editable:true; value:root.backend.cacheLimitMiB; onValueModified:root.backend.cacheLimitMiB=value }
            Label { text: qsTr("Keep free on disk (MiB)") }
            SpinBox { from:128; to:1048576; stepSize:128; editable:true; value:root.backend.minFreeDiskMiB; onValueModified:root.backend.minFreeDiskMiB=value }
            Label { text: qsTr("Completed media retention (days)") }
            SpinBox { from:0; to:365; editable:true; value:root.backend.cacheRetentionDays; onValueModified:root.backend.cacheRetentionDays=value }
        }
        RowLayout {
            AppButton { text:qsTr("Clean expired cache"); onClicked:root.backend.cleanCache() }
            AppButton { text:qsTr("Run diagnostics"); onClicked:root.backend.runDiagnostics() }
        }
        Repeater { model:root.backend.diagnostics
            delegate: Label { required property var modelData; Layout.fillWidth:true; text:modelData.name+" · "+modelData.detail; color:modelData.state==="error" ? "#ff9baa" : modelData.state==="ok" ? "#7ee2b8" : "#9dadc7"; wrapMode:Text.WordWrap }
        }
    }
}
