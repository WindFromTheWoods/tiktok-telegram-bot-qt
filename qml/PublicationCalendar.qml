pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property var backend
    property date pivot: new Date()
    property bool monthMode: false
    property bool dragging: false
    function step(direction) {
        const date = new Date(pivot)
        if (monthMode) { date.setDate(1); date.setMonth(date.getMonth() + direction) }
        else date.setDate(date.getDate() + 7 * direction)
        pivot = date
    }
    function iso(day) { return Qt.formatDate(day, "yyyy-MM-dd") }
    function days() {
        const start = new Date(pivot.getFullYear(), pivot.getMonth(), monthMode ? 1 : pivot.getDate(), 12)
        start.setDate(start.getDate() - (start.getDay() + 6) % 7)
        const result = []
        for (let i=0; i<(monthMode ? 42 : 7); ++i) { const d = new Date(start); d.setDate(d.getDate()+i); result.push(d) }
        return result
    }
    function posts(day) { return backend.scheduledVideos.concat(backend.sentVideos).filter(p => p.scheduledDate === iso(day) && (channel.currentIndex === 0 || p.channelId === channel.currentValue)) }
    function freeSlots(day) {
        if (channel.currentIndex === 0) return ""
        const weekday = day.getDay() === 0 ? 7 : day.getDay()
        const occupied = posts(day).map(p => p.scheduledTime)
        const free = backend.scheduleSlots.filter(s => s.channelId === channel.currentValue && (s.dayOfWeek === 0 || s.dayOfWeek === weekday) && !occupied.includes(s.time)).map(s => s.time)
        return free.length ? qsTr("Free: %1").arg(free.join(", ")) : ""
    }
    VideoEditorDialog { id: editor; parent: Overlay.overlay; backend: root.backend }
    ColumnLayout {
        anchors.fill: parent; spacing: 10
        RowLayout {
            AppButton { text: "‹"; compact: true; Accessible.name: qsTr("Previous period"); onClicked: root.step(-1) }
            Label { text: Qt.formatDate(root.pivot, "MMMM yyyy"); font.pixelSize: 20; Layout.fillWidth: true }
            AppButton { text: qsTr("Today"); compact: true; onClicked: root.pivot = new Date() }
            AppButton { text: "›"; compact: true; Accessible.name: qsTr("Next period"); onClicked: root.step(1) }
            ComboBox { model: [qsTr("Week"),qsTr("Month")]; onCurrentIndexChanged: root.monthMode = currentIndex === 1 }
        }
        ComboBox { id: channel; Layout.fillWidth: true; model: [{id:0,name:qsTr("All channels (each in its own time zone)")}].concat(root.backend.channels); textRole: "name"; valueRole: "id" }
        Label { text: qsTr("Drag a post to another date; its channel-local time is preserved. Open a post to review."); color: "#9dadc7"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
            GridLayout {
                width: parent.width; columns: 7; columnSpacing: 5; rowSpacing: 5
                Repeater {
                    model: root.days()
                    delegate: Rectangle {
                        id: cell; required property var modelData
                        Layout.fillWidth: true; Layout.minimumWidth: 65; Layout.preferredHeight: root.monthMode ? 190 : 390
                        radius: 9; color: drop.containsDrag ? "#263d66" : "#111c2f"; border.color: root.iso(cell.modelData) === root.iso(new Date()) ? "#7199ff" : "#263751"
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 7; spacing: 5
                            Label { text: Qt.formatDate(cell.modelData,"ddd d"); font.bold: true; color: "#eef4ff" }
                            Label { Layout.fillWidth: true; text: root.freeSlots(cell.modelData); visible: text.length>0; color: "#7ee2b8"; font.pixelSize: 10; wrapMode: Text.WordWrap }
                            ListView {
                                id: posts; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: root.posts(cell.modelData); spacing: 5
                                delegate: Rectangle {
                                    id: chip; required property var modelData; width: posts.width; height: 76; radius: 6
                                    color: modelData.approved ? "#203454" : "#382e20"; border.color: "#496388"
                                    Label { anchors.fill: parent; anchors.margins: 5; text: chip.modelData.scheduledTime + " #" + chip.modelData.id + "\n" + chip.modelData.title; wrapMode: Text.WordWrap; elide: Text.ElideRight; font.pixelSize: 11; maximumLineCount: 4 }
                                    MouseArea {
                                        id: mouse; anchors.fill: parent
                                        property point startPoint
                                        onPressed: function(event) { startPoint=Qt.point(event.x,event.y); const p=mapToItem(root,event.x,event.y); ghost.x=p.x; ghost.y=p.y; ghost.videoId=chip.modelData.id; ghost.label=chip.modelData.title }
                                        onPositionChanged: function(event) { if(pressed) { if (Math.abs(event.x-startPoint.x)+Math.abs(event.y-startPoint.y)>10 && chip.modelData.status !== "sent") root.dragging=true; const p=mapToItem(root,event.x,event.y); ghost.x=p.x; ghost.y=p.y } }
                                        onReleased: { if(root.dragging) ghost.Drag.drop(); root.dragging=false }
                                        onCanceled: root.dragging=false
                                        onDoubleClicked: { root.dragging=false; editor.entry=chip.modelData; editor.open() }
                                    }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("Post %1, %2").arg(modelData.id).arg(modelData.title)
                                    activeFocusOnTab: true
                                    Keys.onReturnPressed: { editor.entry=chip.modelData; editor.open() }
                                }
                            }
                        }
                        DropArea { id: drop; anchors.fill: parent; onDropped: function(event) { if(event.source === ghost && ghost.videoId) { root.backend.moveVideoToDate(ghost.videoId,root.iso(cell.modelData)); event.acceptProposedAction() } } }
                    }
                }
            }
        }
        Label { Layout.fillWidth: true; text: root.backend.statusMessage; color: root.backend.statusIsError ? "#ff9baa" : "#9dadc7"; wrapMode: Text.WordWrap }
    }
    Rectangle { id: ghost; property double videoId: 0; property string label: ""; width: 150; height: 48; radius: 7; color: "#3e67d5"; visible: root.dragging; z: 100
        Drag.active: root.dragging; Drag.source: ghost; Drag.hotSpot.x: 5; Drag.hotSpot.y: 5
        Label { anchors.fill: parent; anchors.margins: 5; text: ghost.label; elide: Text.ElideRight }
    }
}
