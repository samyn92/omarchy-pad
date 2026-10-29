import QtQuick

// The keyboard's body. Docked it spans the bottom of the screen; floating it
// is a small board with a grabber strip underneath that drags
// it around, like the iPad's floating keyboard, with the voice orb in its
// middle. As the orb alone it is only that strip: a pill with the orb between
// two grabbers, and a tap on a grabber brings up the placements.
Item {
    id: panel

    required property QtObject style
    property bool compact: false
    property bool orbOnly: false
    property string layoutCode: "us"
    property Item menu: null
    readonly property int handleHeight: 46
    // The docked keyboard's edit bar, above its keys.
    readonly property int editBarHeight: 44
    readonly property alias keys: view

    signal dragStarted()
    signal dragMoved(real dx, real dy)
    signal dragFinished()
    // `anchor` is in the panel's coordinates.
    signal menuRequested(rect anchor)
    signal pinched(bool outward)

    // Behind the floating board; over the docked one's top edge.
    VoiceGlow {
        z: panel.compact ? -1 : 1
        anchors.fill: board
        style: panel.style
        compact: panel.compact
        radius: board.radius
    }

    // A soft shadow under the floating board.
    Rectangle {
        visible: panel.compact
        anchors.fill: board
        anchors.topMargin: 3
        anchors.bottomMargin: -6
        anchors.leftMargin: -1
        anchors.rightMargin: -1
        radius: board.radius + 2
        color: panel.style.shadow
        opacity: 0.6
    }

    Rectangle {
        id: board
        anchors.fill: parent
        radius: panel.orbOnly ? height / 2 : panel.compact ? panel.style.boardRadius : 0
        color: panel.style.board
        border.width: panel.compact ? 1 : 0
        border.color: panel.style.edge

        // A hairline along the top of the docked keyboard.
        Rectangle {
            visible: !panel.compact
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: panel.style.edge
        }
    }

    EditBar {
        objectName: "editBar"
        visible: !panel.compact
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 2
        height: panel.editBarHeight
        style: panel.style
        keys: view
    }

    KeyboardView {
        id: view
        objectName: "keys"
        anchors.fill: parent
        anchors.topMargin: panel.compact ? 0 : panel.editBarHeight
        anchors.bottomMargin: panel.compact ? panel.handleHeight - 4 : 0
        style: panel.style
        compact: panel.compact
        layoutCode: panel.layoutCode
        menu: panel.menu
        enabled: !panel.orbOnly
        visible: opacity > 0
        opacity: panel.orbOnly ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 160 } }
        onPinched: (outward) => panel.pinched(outward)
        onMenuRequested: (anchor) => panel.menuRequested(Qt.rect(anchor.x + view.x, anchor.y + view.y,
                                                                 anchor.width, anchor.height))
    }

    // The grabber strip: drag it to move the keyboard; the buttons on either
    // side pick a placement and hide the keyboard.
    Item {
        id: handle
        objectName: "handle"
        visible: panel.compact
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: panel.orbOnly ? panel.height : panel.handleHeight

        // The grabber, split around the orb when there is one.
        Repeater {
            model: dictation.available ? [-1, 1] : [0]
            Rectangle {
                required property int modelData
                readonly property bool makesRoom: modelData === -1 ? cancel.visible : modelData === 1 && (timer.visible || send.visible)
                anchors.verticalCenter: parent.verticalCenter
                x: handle.width / 2 + modelData * (orb.width / 2 + (panel.orbOnly ? 20 : 14) + width / 2) - width / 2
                width: modelData === 0 ? 48 : panel.orbOnly ? 36 : 28
                height: panel.orbOnly ? 6 : 5
                radius: height / 2
                color: drag.active ? panel.style.accent : panel.style.faint
                opacity: makesRoom ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: 160 } }
            }
        }

        DragHandler {
            id: drag
            target: null
            dragThreshold: 2
            onActiveChanged: active ? panel.dragStarted() : panel.dragFinished()
            onActiveTranslationChanged: if (active) panel.dragMoved(activeTranslation.x, activeTranslation.y)
        }

        TapHandler {
            enabled: panel.orbOnly
            onTapped: panel.menuRequested(Qt.rect(0, handle.y, handle.width, handle.height))
        }

        Item {
            id: placements
            visible: !panel.orbOnly
            anchors.left: parent.left
            anchors.leftMargin: 6
            width: 46
            height: parent.height
            KeyIcon {
                anchors.centerIn: parent
                width: 20
                height: 20
                name: "keyboard"
                color: placementsTap.pressed ? panel.style.accent : panel.style.faint
            }
            TapHandler {
                id: placementsTap
                onTapped: panel.menuRequested(Qt.rect(placements.x, handle.y, placements.width, handle.height))
            }
        }

        // The clipboard page, beside the placements.
        Item {
            objectName: "floatingClipboard"
            visible: !panel.orbOnly
            anchors.left: placements.right
            width: 40
            height: parent.height
            KeyIcon {
                anchors.centerIn: parent
                width: 20
                height: 20
                name: "clipboard"
                color: clipTap.pressed || view.page === "clipboard" ? panel.style.accent : panel.style.faint
            }
            TapHandler {
                id: clipTap
                onTapped: view.goTo(view.page === "clipboard" ? "letters" : "clipboard")
            }
        }

        Item {
            visible: !panel.orbOnly
            anchors.right: parent.right
            anchors.rightMargin: 6
            width: 46
            height: parent.height
            KeyIcon {
                anchors.centerIn: parent
                width: 20
                height: 20
                name: "hide"
                color: hideTap.pressed ? panel.style.accent : panel.style.faint
            }
            TapHandler {
                id: hideTap
                onTapped: controller.hide()
            }
        }

        VoiceOrb {
            id: orb
            objectName: "orb"
            visible: dictation.available
            anchors.centerIn: parent
            width: panel.orbOnly ? 56 : 36
            height: width
            style: panel.style
        }

        // While it listens: let go of the words on the left, the time on the right.
        Item {
            id: cancel
            objectName: "cancelVoice"
            visible: opacity > 0
            opacity: dictation.state === "listening" ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: orb.left
            anchors.rightMargin: 8
            width: 36
            height: parent.height
            KeyIcon {
                anchors.centerIn: parent
                width: 18
                height: 18
                name: "close"
                stroke: 2
                color: cancelArea.pressed ? panel.style.accent : panel.style.label
            }
            MouseArea {
                id: cancelArea
                anchors.fill: parent
                preventStealing: true
                onClicked: dictation.cancel()
            }
        }
        Text {
            id: timer
            visible: opacity > 0
            opacity: dictation.state === "listening" ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: orb.right
            anchors.leftMargin: 12
            readonly property int whole: Math.floor(dictation.seconds)
            text: Math.floor(whole / 60) + ":" + String(whole % 60).padStart(2, "0")
            color: panel.style.label
            font.family: panel.style.font
            font.pixelSize: panel.orbOnly ? 15 : 13
            font.features: {"tnum": 1}
        }

        // Once the words went in: return, for a few seconds, to send them.
        Item {
            id: send
            objectName: "sendVoice"
            property bool offered: false
            property real remaining: 0
            visible: opacity > 0
            enabled: offered
            opacity: offered ? 1 : 0
            scale: offered ? (sendArea.pressed ? 0.88 : 1) : 0.7
            Behavior on opacity { NumberAnimation { duration: 180 } }
            Behavior on scale { SpringAnimation { spring: 6; damping: 0.3; epsilon: 0.002 } }
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: orb.right
            anchors.leftMargin: panel.orbOnly ? 14 : 10
            width: panel.orbOnly ? 56 : 46
            height: panel.orbOnly ? 36 : 28

            function offer() {
                offered = true
                drain.restart()
            }

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: sendArea.pressed ? panel.style.mix(panel.style.accent, panel.style.black, 0.2) : panel.style.accent
            }
            // How long it stays: a light band that drains away to the left.
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.width * send.remaining
                radius: height / 2
                color: panel.style.alpha(panel.style.white, 0.22)
            }
            KeyIcon {
                anchors.centerIn: parent
                width: parent.height * 0.62
                height: width
                name: "return"
                stroke: 2
                color: panel.style.accentText
            }

            NumberAnimation {
                id: drain
                target: send
                property: "remaining"
                from: 1
                to: 0
                duration: 4000
                onFinished: send.offered = false
            }

            MouseArea {
                id: sendArea
                anchors.fill: parent
                anchors.margins: -6
                preventStealing: true
                onClicked: {
                    virtualKeyboard.tapKey("Return", 0)
                    drain.stop()
                    send.offered = false
                }
            }

            Connections {
                target: dictation
                function onStateChanged() {
                    if (dictation.state === "done")
                        send.offer()
                    else if (dictation.state !== "idle") {
                        drain.stop()
                        send.offered = false
                    }
                }
            }
        }
    }

    // Say plainly when typing can't work, instead of keys that do nothing.
    Rectangle {
        visible: !virtualKeyboard.ready
        anchors.centerIn: view
        width: Math.min(view.width - 24, problem.implicitWidth + 32)
        height: problem.implicitHeight + 20
        radius: panel.style.keyRadius + 4
        color: panel.style.board
        border.width: 1
        border.color: panel.style.accent
        Text {
            id: problem
            anchors.centerIn: parent
            width: Math.min(implicitWidth, view.width - 56)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Typing is unavailable: %1").arg(virtualKeyboard.error)
            color: panel.style.label
            font.family: panel.style.font
            font.pixelSize: 15
        }
    }
}
