import QtQuick

// The row of accented characters a long press brings up over a key. The
// finger slides along it and lifts on the one it wants.
Item {
    id: popup

    required property QtObject style
    property var options: []
    property int selected: 0
    property real cell: 40
    property real cellHeight: 48

    visible: false
    z: 20
    width: options.length * cell + 8
    height: cellHeight + 8

    // `cap` is the key's rectangle in the parent's coordinates.
    function open(choices, cap, fingerX) {
        options = choices
        cell = Math.round(Math.max(30, Math.min(cap.width, cap.height * 0.95)))
        cellHeight = Math.round(cap.height)
        // Over the key, kept on screen.
        const left = cap.x + cap.width / 2 - cell / 2 - 4
        const top = cap.y - height - 6
        const corner = parent.mapToItem(null, left, top)
        const shift = Math.max(4 - corner.x, Math.min(0, Window.width - 4 - (corner.x + width)))
        x = left + shift
        y = corner.y >= 4 ? top : cap.y + cap.height + 6
        visible = true
        pick(fingerX)
    }

    function pick(fingerX) {
        selected = Math.max(0, Math.min(options.length - 1, Math.floor((fingerX - x - 4) / cell)))
    }

    function current() {
        return visible && options.length > 0 ? options[selected] : ""
    }

    function close() {
        visible = false
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.bottomMargin: -2
        radius: popup.style.keyRadius + 4
        color: popup.style.shadow
    }
    Rectangle {
        anchors.fill: parent
        radius: popup.style.keyRadius + 4
        color: popup.style.key
        border.width: 1
        border.color: popup.style.edge
    }

    Row {
        x: 4
        y: 4
        Repeater {
            model: popup.options
            delegate: Rectangle {
                required property string modelData
                required property int index
                width: popup.cell
                height: popup.cellHeight
                radius: popup.style.keyRadius
                color: index === popup.selected ? popup.style.accent : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: parent.modelData
                    color: parent.index === popup.selected ? popup.style.accentText : popup.style.label
                    font.family: popup.style.font
                    font.pixelSize: Math.round(popup.cellHeight * 0.5)
                }
            }
        }
    }
}
