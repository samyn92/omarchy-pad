import QtQuick

// The apps in rows, one kind of app a row (shelves.rows), each scrolling
// sideways across whatever room the gesture made, the rows up and down.
//
// Hold an app to arrange them: the icons wiggle, and the app held follows the
// finger into another row, between two apps, or onto "New row" at the end; a
// row scrolls along when it is dragged to its edge, the rows when it is
// dragged to the top or bottom. The arrows beside a row's name move the row.
// Done, or the launcher going away, ends it.
Item {
    id: launcher

    required property QtObject style
    property bool interactive: true
    // Inside a window frame there is no panel of its own to draw.
    property bool plain: false

    signal picked(string appId)

    readonly property real cell: 92
    readonly property real iconSize: 52
    readonly property real tileHeight: iconSize + 34

    property bool editing: false
    onInteractiveChanged: if (!interactive) stopEditing()

    // The app the finger holds, where the finger is (in the launcher's
    // coordinates), and where the app would go: a row and the place in it,
    // or -2 for a row of its own.
    property string dragId: ""
    property string dragIcon: ""
    property point dragAt
    property int dropRow: -1
    property int dropIndex: -1
    property rect dropMark: Qt.rect(0, 0, 0, 0)

    function stopEditing() {
        editing = false
        dragId = ""
        dropRow = -1
    }

    function iconUrl(name) {
        return name ? "image://icon/" + name + "?" + apps.iconKey : ""
    }

    // Where an app let go at `p` would go, and where to show it.
    function aim(p) {
        dropRow = -1
        dropIndex = -1
        const n = newRowTarget.mapFromItem(launcher, p.x, p.y)
        if (newRowTarget.visible && n.x >= 0 && n.y >= 0 && n.x <= newRowTarget.width && n.y <= newRowTarget.height) {
            dropRow = -2
            return
        }
        for (let r = 0; r < rowRepeater.count; ++r) {
            const row = rowRepeater.itemAt(r)
            const lane = row.lane
            const q = lane.mapFromItem(launcher, p.x, p.y)
            if (q.y < -row.spacing || q.y > lane.height + row.spacing)
                continue
            const x = q.x + lane.contentX
            let index = lane.count
            for (let i = 0; i < lane.count; ++i) {
                const tile = lane.itemAtIndex(i)
                if (tile && x < tile.x + tile.width / 2) {
                    index = i
                    break
                }
            }
            dropRow = r
            dropIndex = index
            const before = index < lane.count ? lane.itemAtIndex(index) : lane.itemAtIndex(lane.count - 1)
            if (before) {
                const at = lane.mapToItem(launcher, (index < lane.count ? before.x : before.x + before.width) - lane.contentX, 0)
                dropMark = Qt.rect(at.x - 2, at.y + 6, 4, lane.height - 12)
            }
            return
        }
    }

    function drop() {
        if (dragId !== "") {
            if (dropRow === -2)
                shelves.newRow(dragId)
            else if (dropRow >= 0)
                shelves.moveApp(dragId, dropRow, dropIndex)
        }
        dragId = ""
        dropRow = -1
    }

    // Scrolling along under a dragged app held near an edge.
    Timer {
        interval: 16
        repeat: true
        running: launcher.dragId !== ""
        onTriggered: {
            const p = launcher.dragAt, edge = 48, step = 10
            const v = scroller.mapFromItem(launcher, p.x, p.y)
            if (v.y < edge)
                scroller.contentY = Math.max(0, scroller.contentY - step)
            else if (v.y > scroller.height - edge)
                scroller.contentY = Math.min(Math.max(0, scroller.contentHeight - scroller.height), scroller.contentY + step)
            if (launcher.dropRow >= 0) {
                const lane = rowRepeater.itemAt(launcher.dropRow).lane
                const q = lane.mapFromItem(launcher, p.x, p.y)
                if (q.x < edge)
                    lane.contentX = Math.max(0, lane.contentX - step)
                else if (q.x > lane.width - edge)
                    lane.contentX = Math.min(Math.max(0, lane.contentWidth - lane.width), lane.contentX + step)
            }
            launcher.aim(p)
        }
    }

    Rectangle {
        visible: !launcher.plain
        anchors.fill: parent
        radius: launcher.style.radius
        color: launcher.style.panel
        border.width: 1
        border.color: launcher.style.edge
    }

    // Arranging: what it does, and the way out.
    Item {
        id: arranging
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 14 }
        height: launcher.editing ? done.height : 0
        visible: launcher.editing

        Text {
            anchors.left: parent.left
            anchors.right: done.left
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: "Drag apps between rows, or into a new one"
            color: launcher.style.label
            opacity: 0.7
            font.family: launcher.style.font
            font.pixelSize: 13
        }

        Rectangle {
            id: done
            anchors.right: parent.right
            width: doneLabel.implicitWidth + 28
            height: 34
            radius: launcher.style.radius - 4
            color: launcher.style.tile

            Text {
                id: doneLabel
                anchors.centerIn: parent
                text: "Done"
                color: launcher.style.label
                font.family: launcher.style.font
                font.pixelSize: 14
                font.bold: true
            }
            TapHandler { onTapped: launcher.stopEditing() }
        }
    }

    Flickable {
        id: scroller
        anchors { top: arranging.bottom; left: parent.left; right: parent.right; bottom: parent.bottom; margins: 14 }
        anchors.topMargin: launcher.editing ? 10 : 14
        clip: true
        contentHeight: column.height
        interactive: launcher.interactive && launcher.dragId === ""
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: scroller.width
            spacing: 12

            Repeater {
                id: rowRepeater
                model: shelves.rows

                delegate: Column {
                    id: row
                    required property var modelData
                    required property int index
                    property alias lane: lane
                    width: column.width
                    spacing: 4

                    Row {
                        spacing: 8
                        height: 22

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData.name.toUpperCase()
                            color: launcher.style.label
                            opacity: 0.6
                            font.family: launcher.style.font
                            font.pixelSize: 12
                            font.bold: true
                            font.letterSpacing: 1
                        }
                        // Up and down the list, while arranging.
                        Repeater {
                            model: launcher.editing ? [-1, 1] : []
                            delegate: Text {
                                required property int modelData
                                anchors.verticalCenter: parent.verticalCenter
                                visible: row.index + modelData >= 0 && row.index + modelData < rowRepeater.count
                                text: modelData < 0 ? "↑" : "↓"
                                color: launcher.style.label
                                font.family: launcher.style.font
                                font.pixelSize: 16
                                TapHandler {
                                    margin: 10
                                    onTapped: shelves.moveRow(row.index, row.index + parent.modelData)
                                }
                            }
                        }
                    }

                    ListView {
                        id: lane
                        width: parent.width
                        height: launcher.tileHeight
                        orientation: ListView.Horizontal
                        spacing: 2
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: launcher.interactive && launcher.dragId === ""
                        // Every tile made, so a drop can be aimed past the ones in view.
                        cacheBuffer: 4000
                        model: row.modelData.apps
                        delegate: AppTile {}
                    }
                }
            }

            Rectangle {
                id: newRowTarget
                visible: launcher.editing
                width: column.width
                height: 56
                radius: launcher.style.radius - 4
                color: launcher.dropRow === -2 ? launcher.style.tile : "transparent"
                border.width: 1
                border.color: launcher.style.edge

                Text {
                    anchors.centerIn: parent
                    text: "+  New row"
                    color: launcher.style.label
                    opacity: 0.7
                    font.family: launcher.style.font
                    font.pixelSize: 13
                }
            }
        }
    }

    // Where the app held would go, and the app itself under the finger.
    Rectangle {
        visible: launcher.dragId !== "" && launcher.dropRow >= 0
        x: launcher.dropMark.x
        y: launcher.dropMark.y
        width: launcher.dropMark.width
        height: launcher.dropMark.height
        radius: 2
        color: launcher.style.label
    }
    Image {
        visible: launcher.dragId !== ""
        x: launcher.dragAt.x - width / 2
        y: launcher.dragAt.y - height / 2 - 24
        width: launcher.iconSize * 1.15
        height: width
        sourceSize: Qt.size(128, 128)
        source: launcher.iconUrl(launcher.dragIcon)
        opacity: 0.9
    }

    component AppTile: Item {
        id: tile
        required property var modelData
        // What tests and the drop find a tile by.
        readonly property string appId: modelData.appId
        width: launcher.cell
        height: launcher.tileHeight
        opacity: launcher.dragId === appId ? 0.25 : 1

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: launcher.style.radius - 4
            color: launcher.style.tile
            opacity: area.pressed && !launcher.editing ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 90 } }
        }
        Image {
            id: glyph
            anchors.horizontalCenter: parent.horizontalCenter
            y: 6
            width: launcher.iconSize
            height: width
            sourceSize: Qt.size(128, 128)
            source: launcher.iconUrl(tile.modelData.icon)
            asynchronous: true
            scale: area.pressed && !launcher.editing ? 0.9 : 1
            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

            // Arranging, the icons wiggle, each a little out of step.
            SequentialAnimation on rotation {
                running: launcher.editing
                loops: Animation.Infinite
                alwaysRunToEnd: true
                PauseAnimation { duration: (tile.appId.length * 37) % 120 }
                NumberAnimation { from: -2.5; to: 2.5; duration: 140; easing.type: Easing.InOutSine }
                NumberAnimation { from: 2.5; to: -2.5; duration: 140; easing.type: Easing.InOutSine }
            }
        }
        Text {
            anchors.top: glyph.bottom
            anchors.topMargin: 6
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 8
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: tile.modelData.name
            color: launcher.style.label
            font.family: launcher.style.font
            font.pixelSize: 13
        }

        // A tap opens the app; a hold takes it up to arrange (sooner while
        // arranging already). Once it is up the rows can't take the finger to
        // scroll.
        MouseArea {
            id: area
            anchors.fill: parent
            enabled: launcher.interactive
            pressAndHoldInterval: launcher.editing ? 150 : 450
            preventStealing: launcher.dragId === tile.appId
            onClicked: if (!launcher.editing) launcher.picked(tile.appId)
            onPressAndHold: function (mouse) {
                launcher.editing = true
                launcher.dragId = tile.appId
                launcher.dragIcon = tile.modelData.icon
                launcher.dragAt = mapToItem(launcher, mouse.x, mouse.y)
                launcher.aim(launcher.dragAt)
            }
            onPositionChanged: function (mouse) {
                if (launcher.dragId !== tile.appId)
                    return
                launcher.dragAt = mapToItem(launcher, mouse.x, mouse.y)
                launcher.aim(launcher.dragAt)
            }
            onReleased: if (launcher.dragId === tile.appId) launcher.drop()
            onCanceled: if (launcher.dragId === tile.appId) launcher.dragId = ""
        }
    }
}
