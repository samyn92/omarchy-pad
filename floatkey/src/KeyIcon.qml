import QtQuick
import QtQuick.Shapes

// Line icons for the special keys, drawn on a 24×24 grid so they take the
// label color and stay sharp at any key size.
Item {
    id: icon

    property string name
    property color color
    property bool filled: false
    property real stroke: 1.7

    implicitWidth: 24
    implicitHeight: 24

    readonly property var paths: ({
        shift: "M12 3.5 L20.5 12 H15.5 V19.5 H8.5 V12 H3.5 Z",
        caps: "M12 3 L20.5 11 H15.5 V15.5 H8.5 V11 H3.5 Z M8.5 19 H15.5 V21 H8.5 Z",
        backspace: "M8.5 5 H20.5 V19 H8.5 L2.5 12 Z M11.5 9 L16.5 15 M16.5 9 L11.5 15",
        "return": "M19.5 5 V13 H5.5 M9.5 9 L5.5 13 L9.5 17",
        tab: "M3 12 H17.5 M13 7.5 L17.5 12 L13 16.5 M21 6.5 V17.5",
        globe: "M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12 Z M3 12 H21 M12 3 C8.4 6.6 8.4 17.4 12 21 M12 3 C15.6 6.6 15.6 17.4 12 21 M4.6 7.5 H19.4 M4.6 16.5 H19.4",
        emoji: "M21 12 A9 9 0 1 1 3 12 A9 9 0 1 1 21 12 Z M7.8 14 C9.4 17.2 14.6 17.2 16.2 14 M9 9 V10.5 M15 9 V10.5",
        hide: "M3 3.5 H21 V14.5 H3 Z M6.5 7 H7.5 M10.5 7 H11.5 M14.5 7 H15.5 M8 11 H16 M8.5 18 L12 21 L15.5 18",
        keyboard: "M3 6 H21 V18 H3 Z M6.5 9.5 H7.5 M10.5 9.5 H11.5 M14.5 9.5 H15.5 M8 14 H16",
        left: "M14.5 5.5 L8 12 L14.5 18.5",
        right: "M9.5 5.5 L16 12 L9.5 18.5",
        up: "M5.5 15 L12 8.5 L18.5 15",
        down: "M5.5 9 L12 15.5 L18.5 9",
        mic: "M9 6.5 A3 3 0 0 1 15 6.5 V11.5 A3 3 0 0 1 9 11.5 Z M5.5 11 A6.5 6.5 0 0 0 18.5 11 M12 17.5 V20.5",
        close: "M6.5 6.5 L17.5 17.5 M17.5 6.5 L6.5 17.5",
        check: "M5 12.5 L10 17.5 L19 7",
        undo: "M8.5 13.5 L4 9 L8.5 4.5 M4 9 H14.5 A5.5 5.5 0 0 1 14.5 20 H10",
        redo: "M15.5 13.5 L20 9 L15.5 4.5 M20 9 H9.5 A5.5 5.5 0 0 0 9.5 20 H14",
        cut: "M9 17.5 A2.5 2.5 0 1 1 4 17.5 A2.5 2.5 0 1 1 9 17.5 Z M20 17.5 A2.5 2.5 0 1 1 15 17.5 A2.5 2.5 0 1 1 20 17.5 Z M8.2 15.6 L17.5 3.5 M15.8 15.6 L6.5 3.5",
        copy: "M9 8.5 H19.5 V20.5 H9 Z M5 16 V3.5 H15.5",
        paste: "M9 4.5 H5.5 V21 H18.5 V4.5 H15 M9 3 H15 V6.5 H9 Z M12 10.5 V17 M9 14 L12 17 L15 14",
        clipboard: "M9 4.5 H5.5 V21 H18.5 V4.5 H15 M9 3 H15 V6.5 H9 Z M9 11 H15 M9 14.5 H15 M9 18 H13",
        pin: "M9 3.5 H15 M10 3.5 V10 L7 14 H17 L14 10 V3.5 M12 14 V21",
    })

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: Math.min(icon.width, icon.height) / 24
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: icon.color
            strokeWidth: icon.stroke
            fillColor: icon.filled ? icon.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}
