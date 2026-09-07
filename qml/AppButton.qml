import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool primary: false
    property bool danger: false
    property bool compact: false

    implicitWidth: Math.max(compact ? 76 : 104, label.implicitWidth + (compact ? 22 : 34))
    implicitHeight: compact ? 34 : 40
    leftPadding: compact ? 11 : 17
    rightPadding: compact ? 11 : 17
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: control.text
    Accessible.role: Accessible.Button

    contentItem: Text {
        id: label
        text: control.text
        color: !control.enabled ? "#66758f"
                                : (control.danger ? "#ff9baa" : "#ffffff")
        font.family: control.font.family
        font.pixelSize: control.compact ? 12 : 13
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: control.compact ? 7 : 9
        color: !control.enabled
               ? "#151f31"
               : (control.primary
                  ? (control.down ? "#3e67d5"
                                  : (control.hovered ? "#7199ff" : "#5b8cff"))
                  : (control.danger
                     ? (control.down ? "#44202d"
                                     : (control.hovered ? "#382231" : "#291c29"))
                     : (control.down ? "#243a5b"
                                     : (control.hovered ? "#253958" : "#1a2942"))))
        border.width: control.activeFocus ? 2 : 1
        border.color: !control.enabled
                      ? "#27344b"
                      : (control.primary ? "#8baaff"
                                         : (control.danger ? "#a94f68" : "#496388"))
    }
}
