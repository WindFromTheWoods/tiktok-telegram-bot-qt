pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var backend
    TabBar { id: viewTabs; anchors.top: parent.top; width: parent.width
        TabButton { text: qsTr("Calendar") }
        TabButton { text: qsTr("Recurring slots") }
    }
    PublicationCalendar { anchors.fill: parent; anchors.topMargin: 52; visible: viewTabs.currentIndex === 0; backend: root.backend }

    function dayName(day) {
        const names = [qsTr("Every day"), qsTr("Monday"), qsTr("Tuesday"),
                       qsTr("Wednesday"), qsTr("Thursday"), qsTr("Friday"),
                       qsTr("Saturday"), qsTr("Sunday")]
        return names[Math.max(0, Math.min(7, Number(day)))]
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 52
        visible: viewTabs.currentIndex === 1
        spacing: 18

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            Label {
                text: qsTr("Publication schedule")
                color: "#eef4ff"
                font.pixelSize: 26
                font.bold: true
            }

            Label {
                text: qsTr("Create daily or weekly publication slots for each channel")
                color: "#9dadc7"
                font.pixelSize: 13
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: addLayout.implicitHeight + 36
            radius: 15
            color: "#111c2f"
            border.color: "#263751"

            ColumnLayout {
                id: addLayout
                anchors.fill: parent
                anchors.margins: 18
                spacing: 12

                Label {
                    text: qsTr("Add schedule slot")
                    color: "#eef4ff"
                    font.pixelSize: 17
                    font.bold: true
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ComboBox {
                        id: channelBox
                        Layout.fillWidth: true
                        model: root.backend.channels
                        textRole: "name"
                        valueRole: "id"
                    }

                    ComboBox {
                        id: dayBox
                        Layout.preferredWidth: 170
                        model: [qsTr("Every day"), qsTr("Monday"), qsTr("Tuesday"),
                                qsTr("Wednesday"), qsTr("Thursday"), qsTr("Friday"),
                                qsTr("Saturday"), qsTr("Sunday")]
                    }

                    TextField {
                        id: timeField
                        Layout.preferredWidth: 120
                        text: "18:00"
                        placeholderText: "HH:mm"
                        inputMask: "99:99"
                    }

                    AppButton {
                        primary: true
                        text: qsTr("Add slot")
                        enabled: channelBox.currentIndex >= 0
                        onClicked: root.backend.addScheduleSlot(
                                       Number(channelBox.currentValue),
                                       dayBox.currentIndex, timeField.text)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("If a channel has no slots, the fallback interval from Settings is used. Slots use the channel time zone configured in Settings; empty means system time.")
                    color: "#71829f"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                text: qsTr("Configured slots")
                color: "#eef4ff"
                font.pixelSize: 17
                font.bold: true
            }

            Label {
                text: qsTr("%1 slots").arg(root.backend.scheduleSlots.length)
                color: "#9dadc7"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 15
            color: "#111c2f"
            border.color: "#263751"

            Label {
                anchors.centerIn: parent
                text: qsTr("No calendar slots yet. The fixed interval is active.")
                color: "#9dadc7"
                visible: root.backend.scheduleSlots.length === 0
            }

            ListView {
                anchors.fill: parent
                anchors.margins: 14
                model: root.backend.scheduleSlots
                spacing: 9
                clip: true
                visible: count > 0
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: slotRow
                    required property var modelData
                    width: ListView.view.width
                    height: 66
                    radius: 11
                    color: "#0e192b"
                    border.color: "#263751"

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            radius: 11
                            color: "#1d3154"
                            Label {
                                anchors.centerIn: parent
                                text: "◷"
                                color: "#7fa4ff"
                                font.pixelSize: 18
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Label {
                                text: root.dayName(slotRow.modelData.dayOfWeek) + "  " + slotRow.modelData.time
                                color: "#eef4ff"
                                font.bold: true
                            }
                            Label {
                                text: slotRow.modelData.channelName
                                color: "#9dadc7"
                            }
                        }

                        AppButton {
                            compact: true
                            danger: true
                            text: qsTr("Remove")
                            onClicked: root.backend.removeScheduleSlot(slotRow.modelData.id)
                        }
                    }
                }
            }
        }
    }
}
