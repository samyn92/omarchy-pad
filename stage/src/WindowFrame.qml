import QtQuick

// A window the way Hyprland draws one: its content inside a border in the
// active or inactive colors, with Hyprland's rounding. The border is the
// frame's background and the content covers all but the ring of it.
Item {
    id: frame

    // From stage.look: {colors: ["#aarrggbb", ...], angle}.
    property var colors: ({colors: ["#ff888888"], angle: 0})
    property real border: 2
    property real rounding: 0
    property color fill: "black"
    default property alias content: inside.data

    readonly property var stops: colors && colors.colors ? colors.colors : ["#ff888888"]

    Rectangle {
        anchors.fill: parent
        radius: frame.rounding > 0 ? frame.rounding + frame.border : 0
        color: frame.stops[0]
        // Hyprland turns a gradient by any angle; across or down is close
        // enough for a frame this thin.
        gradient: frame.stops.length > 1 ? twoTone : undefined
        Gradient {
            id: twoTone
            orientation: (frame.colors.angle % 180) >= 45 && (frame.colors.angle % 180) < 135
                         ? Gradient.Vertical : Gradient.Horizontal
            GradientStop { position: 0; color: frame.stops[0] }
            GradientStop { position: 1; color: frame.stops[frame.stops.length - 1] }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: frame.border
        radius: frame.rounding
        color: frame.fill
    }

    Item {
        id: inside
        anchors.fill: parent
        anchors.margins: frame.border
        clip: true
    }
}
