import QtQuick

// The clipboard page: what was copied, as cards that scroll sideways (pinned
// ones first), from Omarchy's own clipboard history. Tap a card to paste it
// into the focused field; hold it to pin, copy or delete it. A bar along the
// bottom goes back to the letters.
Item {
    id: pane

    required property QtObject style
    property bool compact: false

    signal letters()
    signal erase()

    readonly property int rows: compact ? 2 : 3
    readonly property real barHeight: Math.round(height * (compact ? 0.2 : 0.18))
    // The card whose actions are showing, by key.
    property string menuKey: ""

    onVisibleChanged: {
        menuKey = ""
        if (visible) {
            clipboard.reload()
            grid.positionViewAtBeginning()
        }
    }

    // A tap outside the open actions closes them.
    MouseArea {
        anchors.fill: parent
        enabled: pane.menuKey !== ""
        onClicked: pane.menuKey = ""
    }

    GridView {
        id: grid
        objectName: "clips"
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.bottomMargin: pane.barHeight
        flow: GridView.FlowTopToBottom
        cellHeight: Math.floor(height / pane.rows)
        cellWidth: Math.round(cellHeight * (pane.compact ? 1.9 : 2.3))
        model: clipboard.entries
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: pane.menuKey === ""

        delegate: Item {
            id: card
            required property var modelData
            readonly property var entry: modelData
            readonly property bool image: entry.type === "image"
            readonly property bool menu: pane.menuKey === entry.key
            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                id: face
                anchors.fill: parent
                anchors.margins: 4
                radius: pane.style.keyRadius + 2
                color: tap.pressed ? pane.style.keyDown : pane.style.key
                border.width: card.entry.pinned ? 1.5 : 0
                border.color: pane.style.alpha(pane.style.accent, 0.7)
                scale: tap.pressed ? 0.96 : 1
                Behavior on scale { NumberAnimation { duration: 90 } }
                clip: true

                Text {
                    visible: !card.image
                    anchors.fill: parent
                    anchors.margins: 10
                    text: card.image ? "" : card.entry.text
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    maximumLineCount: Math.max(1, Math.floor(height / (font.pixelSize * 1.35)))
                    verticalAlignment: Text.AlignTop
                    color: pane.style.label
                    font.family: pane.style.font
                    font.pixelSize: pane.compact ? 12 : 14
                }

                Image {
                    visible: card.image
                    anchors.fill: parent
                    anchors.margins: 3
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 320
                    source: card.image ? "file://" + card.entry.path : ""
                }

                KeyIcon {
                    visible: card.entry.pinned
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 5
                    width: 16
                    height: 16
                    name: "pin"
                    color: pane.style.accent
                }
            }

            MouseArea {
                id: tap
                anchors.fill: parent
                enabled: pane.menuKey === ""
                pressAndHoldInterval: 420
                property bool held: false
                onPressed: held = false
                onPressAndHold: {
                    held = true
                    pane.menuKey = card.entry.key
                }
                onClicked: {
                    if (held)
                        return
                    clipboard.paste(card.entry)
                    pane.letters()
                }
            }

            // Held: what can be done with it, over the card.
            Rectangle {
                visible: card.menu
                anchors.fill: face
                radius: face.radius
                color: pane.style.alpha(pane.style.board, 0.92)
                border.width: 1.5
                border.color: pane.style.accent

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Repeater {
                        model: [
                            {action: "pin", icon: "pin", label: card.entry.pinned ? qsTr("Unpin") : qsTr("Pin")},
                            {action: "copy", icon: "copy", label: qsTr("Copy")},
                            {action: "delete", icon: "close", label: qsTr("Delete")},
                        ]
                        Item {
                            required property var modelData
                            width: Math.min(64, (face.width - 24) / 3)
                            height: face.height - 16
                            Rectangle {
                                anchors.fill: parent
                                radius: pane.style.keyRadius
                                color: act.pressed ? pane.style.keyDown : pane.style.special
                            }
                            Column {
                                anchors.centerIn: parent
                                spacing: 3
                                KeyIcon {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 18
                                    height: 18
                                    name: modelData.icon
                                    color: pane.style.label
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: modelData.label
                                    color: pane.style.label
                                    font.family: pane.style.font
                                    font.pixelSize: 11
                                }
                            }
                            MouseArea {
                                id: act
                                anchors.fill: parent
                                onClicked: {
                                    const entry = card.entry
                                    pane.menuKey = ""
                                    if (modelData.action === "pin")
                                        clipboard.pin(entry, !entry.pinned)
                                    else if (modelData.action === "copy")
                                        clipboard.copy(entry)
                                    else
                                        clipboard.remove(entry)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        visible: clipboard.entries.length === 0
        anchors.centerIn: grid
        text: qsTr("What you copy shows up here")
        color: pane.style.faint
        font.family: pane.style.font
        font.pixelSize: 15
    }

    // ABC, the edit keys, delete.
    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: pane.barHeight

        component BarKey: Item {
            id: key
            property string icon
            property string label
            signal triggered()
            width: Math.round(height * 1.3)
            height: parent.height
            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                radius: pane.style.keyRadius
                color: keyArea.pressed ? pane.style.keyDown : pane.style.special
            }
            KeyIcon {
                visible: key.icon !== ""
                anchors.centerIn: parent
                width: Math.round(parent.height * 0.45)
                height: width
                name: key.icon
                color: pane.style.label
            }
            Text {
                visible: key.icon === ""
                anchors.centerIn: parent
                text: key.label
                color: pane.style.label
                font.family: pane.style.font
                font.pixelSize: Math.round(parent.height * 0.32)
            }
            MouseArea {
                id: keyArea
                anchors.fill: parent
                onClicked: key.triggered()
            }
        }

        BarKey {
            objectName: "clipLetters"
            anchors.left: parent.left
            anchors.leftMargin: 3
            label: "ABC"
            width: Math.round(height * 1.6)
            onTriggered: pane.letters()
        }
        // The docked keyboard has its edit bar above; the small one gets copy
        // and paste here.
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 3
            height: parent.height
            BarKey { visible: pane.compact; icon: "copy"; onTriggered: controller.edit("copy") }
            BarKey { visible: pane.compact; icon: "paste"; onTriggered: controller.edit("paste") }
            Item { width: Math.round(parent.height * 0.5); height: 1 }
            BarKey { icon: "backspace"; onTriggered: pane.erase() }
        }
    }
}
