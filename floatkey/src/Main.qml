import QtQuick
import "layouts.js" as Layouts

// One transparent overlay over the whole output. The keyboard is drawn inside
// it, so it follows the finger and changes shape without the compositor
// resizing anything; the controller tells the compositor that only the
// keyboard's rectangle (or, while the placement menu is open, the whole
// screen) takes touches.
Window {
    id: win

    title: "floatkey"
    color: "transparent"
    visible: controller.visible || reveal > 0

    Style { id: style }

    readonly property bool compact: controller.mode !== "docked"
    readonly property real margin: 12
    readonly property int dockHeight: Math.round(0.3 * Math.min(width, height) * controller.dockedScale)
    readonly property int compactWidth: Math.round(Math.min(400 * controller.compactScale,
                                                            Math.max(240, width - 2 * margin)))
    readonly property int compactHeight: Math.round(compactWidth * 0.56) + panel.handleHeight

    // 0 hidden, 1 shown; the docked keyboard slides up, the floating one fades in.
    property real reveal: controller.visible ? 1 : 0
    Behavior on reveal { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    property bool dragging: false
    property point dragOrigin
    // Where letting go now would put the keyboard: "", "docked", "left" or "right".
    property string snap: ""

    Binding {
        target: controller
        property: "dockedHeight"
        value: win.dockHeight
    }

    // The orb follows a dictation started from Omarchy's key too, while the
    // keyboard is up.
    Binding {
        target: dictation
        property: "watching"
        value: controller.visible
    }

    readonly property rect inputRect: !controller.visible ? Qt.rect(0, 0, 1, 1)
        : menu.opened ? Qt.rect(0, 0, width, height)
        : Qt.rect(panel.x, panel.y, panel.width, panel.height)
    onInputRectChanged: controller.setInputRegion(inputRect)

    function clamp(value, low, high) {
        return Math.max(low, Math.min(value, high))
    }

    function rectFor(mode) {
        if (mode === "docked")
            return Qt.rect(0, height - dockHeight, width, dockHeight)
        const w = compactWidth, h = compactHeight
        if (mode === "left")
            return Qt.rect(margin, height - h - margin, w, h)
        if (mode === "right")
            return Qt.rect(width - w - margin, height - h - margin, w, h)
        const p = controller.floatingPosition
        return Qt.rect(clamp(p.x * width - w / 2, margin, width - w - margin),
                       clamp(p.y * height - h / 2, margin, height - h - margin), w, h)
    }

    function place(animated) {
        if (dragging)
            return
        const r = rectFor(controller.mode)
        settle.stop()
        if (animated && visible) {
            settle.goal = r
            settle.start()
        } else {
            panel.x = r.x
            panel.y = r.y
            panel.width = r.width
            panel.height = r.height
        }
    }

    function beginDrag() {
        settle.stop()
        menu.close()
        dragOrigin = Qt.point(panel.x, panel.y)
        dragging = true
    }

    function dragTo(dx, dy) {
        const w = panel.width, h = panel.height, bottom = height - h
        const x = clamp(dragOrigin.x + dx, 0, width - w)
        const wantedY = dragOrigin.y + dy
        // It may go a little past the bottom edge: that's how it docks.
        const y = clamp(wantedY, 0, bottom + 40)
        panel.x = x
        panel.y = y
        const low = height - (Math.min(y, bottom) + h) < 64
        const next = low && x < 64 ? "left"
                   : low && width - (x + w) < 64 ? "right"
                   : wantedY > bottom + 32 ? "docked" : ""
        if (next !== snap) {
            snap = next
            if (next !== "")
                ghost.area = rectFor(next)
        }
    }

    function endDrag() {
        if (!dragging)
            return
        dragging = false
        const target = snap
        snap = ""
        if (target === "")
            controller.floatingPosition = Qt.point((panel.x + panel.width / 2) / width,
                                                   (Math.min(panel.y, height - panel.height) + panel.height / 2) / height)
        controller.mode = target === "" ? "floating" : target
        place(true)
    }

    ParallelAnimation {
        id: settle
        property rect goal
        NumberAnimation { target: panel; property: "x"; to: settle.goal.x; duration: 280; easing.type: Easing.OutCubic }
        NumberAnimation { target: panel; property: "y"; to: settle.goal.y; duration: 280; easing.type: Easing.OutCubic }
        NumberAnimation { target: panel; property: "width"; to: settle.goal.width; duration: 280; easing.type: Easing.OutCubic }
        NumberAnimation { target: panel; property: "height"; to: settle.goal.height; duration: 280; easing.type: Easing.OutCubic }
    }

    Connections {
        target: controller
        function onModeChanged() { win.place(true) }
        function onFloatingPositionChanged() { win.place(true) }
        function onVisibleChanged() {
            menu.close()
            if (controller.visible)
                win.place(false)
        }
    }
    onWidthChanged: place(false)
    onHeightChanged: place(false)
    onDockHeightChanged: place(false)
    onCompactWidthChanged: place(false)
    Component.onCompleted: {
        virtualKeyboard.preload(Layouts.characters(controller.layouts))
        place(false)
        controller.setInputRegion(inputRect)
    }

    // Shows where the keyboard will land while it is dragged into a corner or
    // down onto the bottom edge.
    Rectangle {
        id: ghost
        property rect area
        visible: win.dragging && win.snap !== ""
        x: area.x
        y: area.y
        width: area.width
        height: area.height
        radius: win.snap === "docked" ? 0 : style.boardRadius
        color: style.glow
        border.width: 2
        border.color: style.accent
    }

    KeyboardPanel {
        id: panel
        objectName: "panel"
        style: style
        compact: win.compact
        layoutCode: controller.layouts[controller.layoutIndex] || "us"
        menu: menu
        opacity: win.compact ? win.reveal : 1
        scale: win.compact ? 0.94 + 0.06 * win.reveal : 1
        transform: Translate { y: win.compact ? 0 : (1 - win.reveal) * panel.height }

        onDragStarted: win.beginDrag()
        onDragMoved: (dx, dy) => win.dragTo(dx, dy)
        onDragFinished: win.endDrag()
        onMenuRequested: (anchor) => menu.openAt(panel.mapToItem(null, anchor.x, anchor.y, anchor.width, anchor.height))
        onPinched: (outward) => {
            if (!win.compact && !outward)
                controller.mode = "floating"
            else if (win.compact && outward)
                controller.mode = "docked"
        }
    }

    // While the menu is open a tap anywhere else closes it.
    MouseArea {
        anchors.fill: parent
        enabled: menu.opened
        visible: enabled
        onPressed: menu.close()
    }

    ModeMenu {
        id: menu
        objectName: "menu"
        style: style
        current: controller.mode
        onChosen: (mode) => controller.mode = mode
    }
}
