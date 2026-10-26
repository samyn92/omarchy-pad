import QtQuick
import "emoji.js" as Emoji

// The emoji page: a grid that scrolls sideways through the categories, with
// a bar along the bottom to jump between them, as on an iPad.
Item {
    id: pane

    required property QtObject style
    property bool compact: false
    property bool showGlobe: false

    signal picked(string emoji)
    signal erase()
    signal letters()
    signal nextLayout()

    // Rebuilt only when the page opens, so the grid doesn't jump while the
    // recent emoji change under it.
    property var content: Emoji.build([])
    onVisibleChanged: {
        if (!visible)
            return
        content = Emoji.build(controller.recentEmoji)
        grid.positionViewAtBeginning()
    }

    readonly property int rows: compact ? 4 : 5
    readonly property real barHeight: Math.round(height * (compact ? 0.2 : 0.18))
    readonly property int section: {
        const first = grid.indexAt(grid.contentX + 4, grid.contentY + 4)
        let current = 0
        for (let i = 0; i < content.sections.length; ++i)
            if (content.sections[i].start <= first)
                current = i
        return current
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.bottomMargin: pane.barHeight
        flow: GridView.FlowTopToBottom
        cellHeight: Math.floor(height / pane.rows)
        cellWidth: cellHeight
        model: pane.content.items
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        delegate: Item {
            required property string modelData
            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                radius: pane.style.keyRadius + 2
                color: tap.pressed ? pane.style.special : "transparent"
            }
            Text {
                anchors.centerIn: parent
                text: parent.modelData
                font.family: pane.style.emojiFont
                font.pixelSize: Math.round(grid.cellHeight * 0.62)
            }
            TapHandler {
                id: tap
                onTapped: pane.picked(parent.modelData)
            }
        }
    }

    Item {
        id: bar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: pane.barHeight

        Row {
            id: leftKeys
            x: 6
            height: parent.height
            spacing: 6

            Rectangle {
                width: Math.round(pane.barHeight * 1.5)
                height: parent.height - 10
                anchors.verticalCenter: parent.verticalCenter
                radius: pane.style.keyRadius
                color: abc.pressed ? pane.style.key : pane.style.special
                Text {
                    anchors.centerIn: parent
                    text: "ABC"
                    color: pane.style.label
                    font.family: pane.style.font
                    font.pixelSize: Math.round(parent.height * 0.4)
                }
                TapHandler {
                    id: abc
                    onTapped: pane.letters()
                }
            }

            Rectangle {
                visible: pane.showGlobe
                width: Math.round(pane.barHeight * 1.1)
                height: parent.height - 10
                anchors.verticalCenter: parent.verticalCenter
                radius: pane.style.keyRadius
                color: globe.pressed ? pane.style.key : pane.style.special
                KeyIcon {
                    anchors.centerIn: parent
                    width: Math.round(parent.height * 0.6)
                    height: width
                    name: "globe"
                    color: pane.style.label
                }
                TapHandler {
                    id: globe
                    onTapped: pane.nextLayout()
                }
            }
        }

        Row {
            id: sections
            anchors.left: leftKeys.right
            anchors.right: eraser.left
            anchors.leftMargin: 4
            anchors.rightMargin: 4
            height: parent.height

            Repeater {
                model: pane.content.sections
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: sections.width / pane.content.sections.length
                    height: sections.height

                    Rectangle {
                        anchors.centerIn: parent
                        width: Math.min(parent.width, parent.height) - 2
                        height: width
                        radius: width / 2
                        color: pane.style.special
                        visible: index === pane.section
                    }
                    Text {
                        anchors.centerIn: parent
                        text: modelData.icon
                        opacity: index === pane.section ? 1 : 0.55
                        font.family: pane.style.emojiFont
                        font.pixelSize: Math.round(Math.min(parent.width * 0.78, parent.height * 0.5))
                    }
                    TapHandler {
                        onTapped: grid.positionViewAtIndex(modelData.start, GridView.Beginning)
                    }
                }
            }
        }

        Rectangle {
            id: eraser
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: Math.round(pane.barHeight * 1.5)
            height: parent.height - 10
            radius: pane.style.keyRadius
            color: erasing.pressed ? pane.style.key : pane.style.special

            KeyIcon {
                anchors.centerIn: parent
                width: Math.round(parent.height * 0.62)
                height: width
                name: "backspace"
                color: pane.style.label
            }
            MouseArea {
                id: erasing
                anchors.fill: parent
                onPressed: {
                    pane.erase()
                    repeat.interval = 450
                    repeat.start()
                }
                onReleased: repeat.stop()
                onCanceled: repeat.stop()
            }
            Timer {
                id: repeat
                repeat: true
                onTriggered: {
                    pane.erase()
                    interval = 70
                }
            }
        }
    }
}
