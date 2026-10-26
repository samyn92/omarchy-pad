import QtQuick

// The keyboard's body. Docked it spans the bottom of the screen; floating or
// in a corner it is a small board with a grabber strip underneath that drags
// it around, like the iPad's floating keyboard.
Item {
    id: panel

    required property QtObject style
    property bool compact: false
    property string layoutCode: "us"
    property Item menu: null
    readonly property int handleHeight: 28
    readonly property alias keys: view

    signal dragStarted()
    signal dragMoved(real dx, real dy)
    signal dragFinished()
    // `anchor` is in the panel's coordinates.
    signal menuRequested(rect anchor)
    signal pinched(bool outward)

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
        radius: panel.compact ? panel.style.boardRadius : 0
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

    KeyboardView {
        id: view
        objectName: "keys"
        anchors.fill: parent
        anchors.bottomMargin: panel.compact ? panel.handleHeight - 4 : 0
        style: panel.style
        compact: panel.compact
        layoutCode: panel.layoutCode
        menu: panel.menu
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
        height: panel.handleHeight

        Rectangle {
            anchors.centerIn: parent
            width: 48
            height: 5
            radius: 2.5
            color: drag.active ? panel.style.accent : panel.style.faint
        }

        DragHandler {
            id: drag
            target: null
            dragThreshold: 2
            onActiveChanged: active ? panel.dragStarted() : panel.dragFinished()
            onActiveTranslationChanged: if (active) panel.dragMoved(activeTranslation.x, activeTranslation.y)
        }

        Item {
            id: placements
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

        Item {
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
