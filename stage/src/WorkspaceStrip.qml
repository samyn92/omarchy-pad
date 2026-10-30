import QtQuick
import QtQuick.Effects

// The workspaces a lifted window can be put on, along the top while two
// fingers carry it: each with its number and the icons of its apps, the one
// on screen marked, and a last one for a new, empty workspace. The tile under
// the fingers (`lit`) stands out; tileAt and tileBox are in the coordinates
// of the strip's parent.
Item {
    id: strip

    required property QtObject style
    property var tiles: []          // from stage's grab plan (Stage::workspaceTiles)
    property int lit: -1
    property color accent: style.accent
    // Main's, for the icon of a window's app.
    property var iconSource: function (appClass) { return "" }

    readonly property real tileWidth: 112
    readonly property real tileHeight: 76
    readonly property real gap: 10
    readonly property real pad: 10

    width: tiles.length * tileWidth + Math.max(0, tiles.length - 1) * gap + 2 * pad
    height: tileHeight + 2 * pad

    // Generous, as a finger carrying a window doesn't aim: anything above the
    // strip's bottom counts, and the gaps belong to the tiles beside them.
    function tileAt(x, y) {
        if (!visible || opacity < 0.5 || y > strip.y + strip.height + 16)
            return -1
        const along = x - strip.x - pad + gap / 2
        const i = Math.floor(along / (tileWidth + gap))
        return i >= 0 && i < tiles.length ? i : -1
    }

    function tileBox(i) {
        return Qt.rect(strip.x + pad + i * (tileWidth + gap), strip.y + pad, tileWidth, tileHeight)
    }

    RectangularShadow {
        anchors.fill: parent
        radius: strip.style.radius
        blur: 30
        offset.y: 6
        color: strip.style.shadow
    }

    Rectangle {
        anchors.fill: parent
        radius: strip.style.radius
        color: strip.style.panel
        border.width: 1
        border.color: strip.style.edge
    }

    Row {
        x: strip.pad
        y: strip.pad
        spacing: strip.gap

        Repeater {
            model: strip.tiles
            delegate: Rectangle {
                id: tile
                required property var modelData
                required property int index
                readonly property bool lit: strip.lit === index
                width: strip.tileWidth
                height: strip.tileHeight
                radius: strip.style.radius - 4
                color: strip.style.tile
                border.width: lit ? 2 : modelData.current ? 1 : 0
                border.color: lit ? strip.accent : strip.style.label
                scale: lit ? 1.1 : 1
                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: strip.accent
                    opacity: tile.lit ? 0.22 : 0
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                }

                Text {
                    x: 9
                    y: 6
                    visible: !tile.modelData.empty
                    text: tile.modelData.id
                    color: strip.style.label
                    font.family: strip.style.font
                    font.pixelSize: 13
                    font.bold: true
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    spacing: 4
                    visible: !tile.modelData.empty
                    Repeater {
                        model: (tile.modelData.apps || []).slice(0, 3)
                        delegate: Image {
                            required property string modelData
                            width: 28
                            height: 28
                            sourceSize: Qt.size(64, 64)
                            source: strip.iconSource(modelData)
                            asynchronous: true
                        }
                    }
                }

                // A new workspace.
                Text {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -6
                    visible: tile.modelData.empty === true
                    text: "+"
                    color: strip.style.label
                    font.family: strip.style.font
                    font.pixelSize: 30
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 7
                    visible: tile.modelData.empty === true
                    text: "New"
                    color: strip.style.faint
                    font.family: strip.style.font
                    font.pixelSize: 12
                }
            }
        }
    }
}
