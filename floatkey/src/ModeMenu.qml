import QtQuick

// Where the keyboard goes, iPad style: hold the docked keyboard's hide key and
// slide onto a placement, or tap the floating keyboard's keyboard button (or
// the voice orb's grabbers). The last row hides it.
Item {
    id: menu

    required property QtObject style
    property string current
    property bool opened: false
    property int highlighted: -1

    signal chosen(string mode)

    readonly property var entries: [
        {mode: "docked", label: qsTr("Docked")},
        {mode: "floating", label: qsTr("Floating")},
        {mode: "orb", label: qsTr("Voice orb")},
        {mode: "hide", label: qsTr("Hide")},
    ]
    readonly property real rowHeight: 48

    width: 240
    height: entries.length * rowHeight + 12
    z: 100
    visible: opacity > 0
    opacity: opened ? 1 : 0
    scale: opened ? 1 : 0.94
    Behavior on opacity { NumberAnimation { duration: 120 } }
    Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    // `anchor` is the button or key it opens from, in the parent's coordinates.
    function openAt(anchor) {
        x = Math.max(8, Math.min(anchor.x + anchor.width / 2 - width / 2, parent.width - width - 8))
        const above = anchor.y - height - 8
        y = above >= 8 ? above : Math.min(anchor.y + anchor.height + 8, parent.height - height - 8)
        transformOrigin = above >= 8 ? Item.Bottom : Item.Top
        highlighted = -1
        opened = true
    }

    function close() {
        opened = false
        highlighted = -1
    }

    function indexAt(point) {
        if (point.x < 0 || point.x > width || point.y < 6 || point.y >= height - 6)
            return -1
        return Math.floor((point.y - 6) / rowHeight)
    }

    function highlightAt(point) {
        highlighted = indexAt(point)
    }

    function releaseAt(point) {
        const index = indexAt(point)
        if (index < 0)
            return false
        chosen(entries[index].mode)
        close()
        return true
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 3
        anchors.bottomMargin: -5
        radius: menu.style.boardRadius
        color: menu.style.shadow
    }
    Rectangle {
        anchors.fill: parent
        radius: menu.style.boardRadius
        color: menu.style.board
        border.width: 1
        border.color: menu.style.edge
    }

    Column {
        y: 6
        Repeater {
            model: menu.entries
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool selected: modelData.mode === menu.current
                width: menu.width
                height: menu.rowHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    radius: menu.style.keyRadius + 2
                    color: row.index === menu.highlighted ? menu.style.glow : "transparent"
                }

                // A little screen showing where this placement puts the keyboard.
                Rectangle {
                    visible: row.modelData.mode !== "hide"
                    x: 18
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 22
                    radius: 3
                    color: "transparent"
                    border.width: 1.5
                    border.color: menu.style.faint

                    Rectangle {
                        readonly property var box: ({
                            docked: [2.5, 14, 27, 5.5],
                            floating: [15, 7, 12, 7],
                            orb: [10, 14, 12, 4.5],
                            hide: [0, 0, 0, 0],
                        })[row.modelData.mode]
                        x: box[0]
                        y: box[1]
                        width: box[2]
                        height: box[3]
                        radius: 1.5
                        color: row.selected ? menu.style.accent : menu.style.label
                    }
                }

                // The orb's pill gets its orb.
                Rectangle {
                    visible: row.modelData.mode === "orb"
                    x: 18 + 16 - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 5
                    width: 7
                    height: 7
                    radius: 3.5
                    color: row.selected ? menu.style.accent : menu.style.board
                    border.width: 1.5
                    border.color: row.selected ? menu.style.accent : menu.style.label
                }
                KeyIcon {
                    visible: row.modelData.mode === "hide"
                    x: 22
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 24
                    name: "hide"
                    color: menu.style.label
                }

                Text {
                    x: 64
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.label
                    color: menu.style.label
                    font.family: menu.style.font
                    font.pixelSize: 16
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.selected
                    text: "✓"
                    color: menu.style.accent
                    font.family: menu.style.font
                    font.pixelSize: 17
                }
            }
        }
    }

    // A MouseArea rather than a TapHandler, so the press stops here instead
    // of also reaching the dismiss area underneath.
    MouseArea {
        anchors.fill: parent
        enabled: menu.opened
        onPressed: (mouse) => menu.highlightAt(Qt.point(mouse.x, mouse.y))
        onPositionChanged: (mouse) => menu.highlightAt(Qt.point(mouse.x, mouse.y))
        onReleased: (mouse) => {
            if (!menu.releaseAt(Qt.point(mouse.x, mouse.y)))
                menu.highlighted = -1
        }
    }
}
