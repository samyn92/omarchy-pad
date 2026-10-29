import QtQuick
import QtQuick.Shapes

// A small pill at the top of the screen saying what the clipboard just did:
// "Copied" whenever anything is copied, or the copy or paste a three-finger
// pinch asked for. It takes no touches and goes by itself.
Item {
    id: toast

    required property QtObject style
    // "copied", "copy" or "paste".
    property string kind: ""
    property bool shown: false

    function show(next) {
        kind = next
        shown = true
        linger.restart()
    }

    readonly property var looks: ({
        copied: {icon: "M5 12.5 L10 17.5 L19 7", label: qsTr("Copied")},
        copy: {icon: "M9 8.5 H19.5 V20.5 H9 Z M5 16 V3.5 H15.5", label: qsTr("Copy")},
        paste: {icon: "M9 4.5 H5.5 V21 H18.5 V4.5 H15 M9 3 H15 V6.5 H9 Z M12 10.5 V17 M9 14 L12 17 L15 14",
                label: qsTr("Paste")},
    })
    readonly property var look: looks[kind] || looks.copied

    width: row.implicitWidth + 36
    height: 42
    visible: opacity > 0
    opacity: shown ? 1 : 0
    scale: shown ? 1 : 0.9
    Behavior on opacity { NumberAnimation { duration: 180 } }
    Behavior on scale { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }

    Timer {
        id: linger
        interval: 1300
        onTriggered: toast.shown = false
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 3
        anchors.bottomMargin: -4
        radius: height / 2
        color: toast.style.shadow
        opacity: 0.6
    }
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: toast.style.panel
        border.width: 1
        border.color: toast.style.edge
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 10

        Item {
            width: 20
            height: 20
            anchors.verticalCenter: parent.verticalCenter
            Shape {
                anchors.centerIn: parent
                width: 24
                height: 24
                scale: 20 / 24
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: toast.style.accent
                    strokeWidth: 2
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg { path: toast.look.icon }
                }
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: toast.look.label
            color: toast.style.label
            font.family: toast.style.font
            font.pixelSize: 16
        }
    }
}
