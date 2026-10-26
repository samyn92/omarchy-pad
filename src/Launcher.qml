import QtQuick

// The apps, as a grid of icons that fills whatever room the gesture made.
Item {
    id: launcher

    required property QtObject style
    property bool interactive: true
    // Inside a window frame there is no panel of its own to draw.
    property bool plain: false

    signal picked(string appId)

    readonly property real cell: Math.max(92, Math.min(124, width / Math.max(1, Math.round(width / 112))))

    Rectangle {
        visible: !launcher.plain
        anchors.fill: parent
        radius: launcher.style.radius
        color: launcher.style.panel
        border.width: 1
        border.color: launcher.style.edge
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: 14
        clip: true
        interactive: launcher.interactive
        boundsBehavior: Flickable.StopAtBounds
        cellWidth: Math.floor(width / Math.max(1, Math.floor(width / launcher.cell)))
        cellHeight: launcher.cell
        model: apps

        delegate: Item {
            id: tile
            required property string name
            required property string icon
            required property string appId
            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                radius: launcher.style.radius - 4
                color: launcher.style.tile
                opacity: tap.pressed ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 90 } }
            }
            Image {
                id: glyph
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.12
                width: Math.round(parent.height * 0.48)
                height: width
                sourceSize: Qt.size(128, 128)
                source: "image://icon/" + tile.icon
                asynchronous: true
                scale: tap.pressed ? 0.9 : 1
                Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }
            Text {
                anchors.top: glyph.bottom
                anchors.topMargin: 6
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 8
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: tile.name
                color: launcher.style.label
                font.family: launcher.style.font
                font.pixelSize: 13
            }
            TapHandler {
                id: tap
                enabled: launcher.interactive
                onTapped: launcher.picked(tile.appId)
            }
        }
    }
}
