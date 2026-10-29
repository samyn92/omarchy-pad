import QtQuick

// The strip along the top of the docked keyboard, like an iPad's shortcut
// bar: undo and redo, cut, copy and paste on the left, what was just copied
// in the middle to paste it with one tap, and the clipboard page on the right.
Item {
    id: bar

    required property QtObject style
    // The keys, to open the clipboard page on.
    property Item keys: null

    readonly property bool clipboardOpen: keys !== null && keys.page === "clipboard"
    readonly property real button: Math.round(height * 1.35)

    component Action: Item {
        id: action
        property string icon
        property string label
        property bool on: false
        signal triggered()
        width: bar.button
        height: bar.height

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: bar.style.keyRadius
            color: action.on ? bar.style.accent
                 : area.pressed ? bar.style.special : "transparent"
            scale: area.pressed ? 0.92 : 1
            Behavior on scale { NumberAnimation { duration: 90 } }
        }
        KeyIcon {
            anchors.centerIn: parent
            width: Math.round(bar.height * 0.5)
            height: width
            name: action.icon
            color: action.on ? bar.style.accentText : bar.style.label
        }
        MouseArea {
            id: area
            anchors.fill: parent
            onClicked: action.triggered()
        }
        Accessible.role: Accessible.Button
        Accessible.name: action.label
    }

    Row {
        id: left
        anchors.left: parent.left
        anchors.leftMargin: 6
        height: parent.height

        Action { objectName: "undo"; icon: "undo"; label: qsTr("Undo"); onTriggered: controller.edit("undo") }
        Action { objectName: "redo"; icon: "redo"; label: qsTr("Redo"); onTriggered: controller.edit("redo") }
        Item { width: 10; height: 1 }
        Action { objectName: "cut"; icon: "cut"; label: qsTr("Cut"); onTriggered: controller.edit("cut") }
        Action { objectName: "copy"; icon: "copy"; label: qsTr("Copy"); onTriggered: controller.edit("copy") }
        Action { objectName: "paste"; icon: "paste"; label: qsTr("Paste"); onTriggered: controller.edit("paste") }
    }

    // What was just copied: tap to paste it.
    Item {
        id: chip
        objectName: "recentClip"
        readonly property var entry: clipboard.recent
        readonly property bool shown: entry !== undefined && entry.key !== undefined
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Math.max(left.width, right.width) - 40, Math.max(160, content.implicitWidth + 28))
        height: parent.height - 10
        visible: opacity > 0
        opacity: shown ? 1 : 0
        scale: chipArea.pressed ? 0.95 : 1
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Behavior on scale { NumberAnimation { duration: 90 } }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: chipArea.pressed ? bar.style.keyDown : bar.style.key
            border.width: 1
            border.color: bar.style.alpha(bar.style.accent, 0.5)
        }
        Row {
            id: content
            anchors.centerIn: parent
            spacing: 8
            KeyIcon {
                anchors.verticalCenter: parent.verticalCenter
                width: chip.height * 0.55
                height: width
                name: "paste"
                color: bar.style.accent
            }
            Image {
                visible: chip.shown && chip.entry.type === "image"
                anchors.verticalCenter: parent.verticalCenter
                height: chip.height - 8
                width: visible ? height * 1.5 : 0
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.height: 96
                source: visible ? "file://" + chip.entry.path : ""
            }
            Text {
                visible: chip.shown && chip.entry.type !== "image"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, chip.width - 60)
                elide: Text.ElideRight
                text: chip.shown && chip.entry.type !== "image" ? chip.entry.text.replace(/\s+/g, " ").trim() : ""
                color: bar.style.label
                font.family: bar.style.font
                font.pixelSize: Math.round(chip.height * 0.42)
            }
        }
        MouseArea {
            id: chipArea
            anchors.fill: parent
            enabled: chip.shown
            onClicked: clipboard.paste(chip.entry)
        }
    }

    Row {
        id: right
        anchors.right: parent.right
        anchors.rightMargin: 6
        height: parent.height

        Action {
            objectName: "clipboardButton"
            icon: "clipboard"
            label: qsTr("Clipboard")
            on: bar.clipboardOpen
            onTriggered: if (bar.keys) bar.keys.goTo(bar.clipboardOpen ? "letters" : "clipboard")
        }
    }
}
