import QtQuick
import "layouts.js" as Layouts

// floatkey's docked keyboard for the lock screen's password field. A locked
// session shows no window but the lock's own, so floatkey cannot come up over
// it; this draws the same keys inside the lock and types into the field
// directly. Only what a password needs is here: the letters, numbers and
// symbols with shift, caps lock and a flick down for the upper character,
// delete, return, and the space bar.
Item {
    id: kb

    property color background: "#101315"
    property color foreground: "#cacccc"
    property color accent: "#cacccc"
    property real radius: 8
    // The layouts to cycle through with the globe key, floatkey's first.
    property var layoutCodes: ["us"]
    // What XKB says each layout's keys type (`floatkey layouts`).
    property var layoutKeys: ({})
    property int layoutIndex: 0
    readonly property string layoutCode: layoutCodes[Math.min(layoutIndex, layoutCodes.length - 1)] || "us"

    signal typed(string text)
    signal erased()
    signal submitted()
    signal hideRequested()
    // Any finger down, so the lock can wake the displays.
    signal touched()

    // floatkey's Style.qml, mixed from the lock's theme colors.
    QtObject {
        id: keyStyle
        readonly property bool dark: kb.background.hslLightness <= 0.5
        readonly property color black: Qt.rgba(0, 0, 0, 1)
        readonly property color white: Qt.rgba(1, 1, 1, 1)

        function mix(a, b, t) {
            return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t)
        }
        function alpha(c, a) {
            return Qt.rgba(c.r, c.g, c.b, a)
        }

        readonly property color board: dark ? mix(kb.background, black, 0.3) : mix(kb.background, black, 0.1)
        readonly property color edge: dark ? alpha(mix(kb.background, kb.foreground, 0.35), 0.6) : alpha(black, 0.12)
        readonly property color key: dark ? mix(kb.background, kb.foreground, 0.27) : mix(kb.background, white, 0.9)
        readonly property color keyDown: dark ? mix(kb.background, kb.foreground, 0.12) : mix(kb.background, black, 0.2)
        readonly property color special: dark ? mix(kb.background, kb.foreground, 0.12) : mix(kb.background, black, 0.17)
        readonly property color specialDown: key
        readonly property color label: dark ? mix(kb.foreground, white, 0.5) : mix(kb.foreground, black, 0.4)
        readonly property color faint: alpha(label, 0.5)
        readonly property color shadow: dark ? alpha(black, 0.55) : alpha(black, 0.28)
        readonly property color accentText: dark ? kb.background : white

        readonly property string font: "Adwaita Sans"
        readonly property real keyRadius: Math.max(6, Math.min(kb.radius, 10))
    }

    // floatkey's full layout without its bottom row of modifiers, arrows and
    // the mic, which have nothing to do in a password.
    function lockRows(code) {
        const rows = Layouts.typingRows(Layouts.layout(code, kb.layoutKeys).full.letters.slice(0, 4))
        const globe = kb.layoutCodes.length > 1 ? {k: "globe", w: 1.25} : {gap: 1.25}
        return rows.concat([[globe, {gap: 2.75}, {k: "space", w: 7}, {gap: 2.75}, {k: "hide", w: 1.25}]])
    }

    readonly property var model: Layouts.keys(lockRows(layoutCode))

    readonly property real padX: 6
    readonly property real padTop: 8
    readonly property real padBottom: 6
    readonly property real unitW: (width - 2 * padX) / model.units
    readonly property real rowH: (height - padTop - padBottom) / model.rows
    readonly property real gapX: Math.round(Math.max(6, unitW * 0.08))
    readonly property real gapY: Math.round(Math.max(6, rowH * 0.13))

    // 0 off, 1 for the next character, 2 locked.
    property int shiftState: 0
    property real lastShiftTap: 0
    property var downKeys: ({})
    property int flickIndex: -1
    // Fingers by touch point id, and the order they went down in: characters
    // are typed in that order whichever finger lifts first.
    property var touches: ({})
    property var order: []

    onLayoutCodeChanged: reset()
    onEnabledChanged: if (!enabled) reset()

    function reset() {
        touches = ({})
        order = []
        downKeys = ({})
        flickIndex = -1
        repeat.stop()
    }

    // ---- geometry ------------------------------------------------------

    function keyRect(k) {
        return Qt.rect(padX + k.ux * unitW, padTop + k.row * rowH, k.w * unitW, k.h * rowH)
    }

    // The visible key inside its slot: for the L-shaped return of European
    // keyboards its top row, and footRect() the rest.
    function capRect(k) {
        const r = keyRect(Layouts.parts(k)[0])
        return Qt.rect(r.x + gapX / 2, r.y + gapY / 2, r.width - gapX, r.height - gapY)
    }

    function footRect(k) {
        const r = keyRect(Layouts.parts(k)[1])
        return Qt.rect(r.x + gapX / 2, r.y + gapY / 2, r.width - gapX, r.height - gapY)
    }

    // The key under a point, or the nearest one, as on an iPad.
    function keyAt(x, y) {
        const keys = model.keys
        let best = -1, bestDistance = Infinity
        for (let i = 0; i < keys.length; ++i) {
            for (const part of Layouts.parts(keys[i])) {
                const r = keyRect(part)
                const dx = x < r.x ? r.x - x : x > r.x + r.width ? x - r.x - r.width : 0
                const dy = y < r.y ? r.y - y : y > r.y + r.height ? y - r.y - r.height : 0
                const d = dx * dx + dy * dy
                if (d < bestDistance) {
                    best = i
                    bestDistance = d
                }
            }
            if (bestDistance === 0)
                break
        }
        return best
    }

    function setDown(index, on) {
        if (index < 0)
            return
        const down = Object.assign({}, downKeys)
        if (on)
            down[index] = true
        else
            delete down[index]
        downKeys = down
    }

    // ---- labels --------------------------------------------------------

    function charFor(k, flick) {
        if (k.s !== undefined)
            return flick || shiftState === 1 ? k.s : k.c
        return shiftState > 0 && Layouts.isLetter(k.c) ? Layouts.upper(k.c) : k.c
    }

    function labelFor(k) {
        switch (k.kind) {
        case "char": return charFor(k, false)
        case "space": return kb.layoutCodes.length > 1 ? Layouts.name(layoutCode) : ""
        case "return": return "return"
        case "tab": return "tab"
        case "caps": return "caps lock"
        case "shift": return "shift"
        case "backspace": return "delete"
        }
        return ""
    }

    function iconFor(k) {
        switch (k.kind) {
        case "globe": return "globe"
        case "hide": return "hide"
        }
        return ""
    }

    // ---- typing --------------------------------------------------------

    function typeKey(k, flick) {
        kb.typed(charFor(k, flick))
        if (shiftState === 1)
            shiftState = 0
    }

    // Shift, caps lock and delete act as they go down; the rest as they lift.
    function pressNow(k) {
        switch (k.kind) {
        case "shift": {
            const now = Date.now()
            shiftState = shiftState === 0 ? 1 : shiftState === 1 && now - lastShiftTap < 350 ? 2 : 0
            lastShiftTap = now
            return true
        }
        case "caps":
            shiftState = shiftState === 2 ? 0 : 2
            return true
        case "backspace":
            kb.erased()
            return true
        }
        return false
    }

    function release(k, flick) {
        switch (k.kind) {
        case "char": typeKey(k, flick); break
        case "space": kb.typed(" "); break
        case "return": kb.submitted(); break
        case "globe": layoutIndex = (layoutIndex + 1) % layoutCodes.length; break
        case "hide": kb.hideRequested(); break
        }
    }

    function begin(p) {
        kb.touched()
        const index = keyAt(p.x, p.y)
        if (index < 0)
            return
        const k = model.keys[index]
        const now = pressNow(k)
        const t = {key: index, startY: p.y, flick: false, done: false, cancelled: false, acted: now}
        const all = Object.assign({}, touches)
        all[p.pointId] = t
        touches = all
        if (!now)
            order = order.concat([p.pointId])
        setDown(index, true)
        if (k.kind === "backspace") {
            repeat.pointId = p.pointId
            repeat.count = 0
            repeat.interval = 450
            repeat.restart()
        }
    }

    function move(p) {
        const t = touches[p.pointId]
        if (!t || t.acted)
            return
        const k = model.keys[t.key]
        // A flick down on a two-character key types its upper one.
        if (k.kind === "char" && k.s !== undefined && p.y - t.startY > rowH * 0.35) {
            t.flick = true
            flickIndex = t.key
            return
        }
        const index = keyAt(p.x, p.y)
        if (index !== t.key && !t.flick) {
            setDown(t.key, false)
            t.key = index
            setDown(index, true)
        }
    }

    function end(p, cancelled) {
        const t = touches[p.pointId]
        if (!t)
            return
        if (repeat.pointId === p.pointId)
            repeat.stop()
        setDown(t.key, false)
        if (flickIndex === t.key)
            flickIndex = -1
        t.done = true
        t.cancelled = cancelled
        const all = Object.assign({}, touches)
        if (t.acted)
            delete all[p.pointId]
        touches = all
        flush()
    }

    function flush() {
        const queue = order.slice()
        const all = Object.assign({}, touches)
        while (queue.length > 0 && all[queue[0]] && all[queue[0]].done) {
            const id = queue.shift()
            const t = all[id]
            delete all[id]
            if (!t.cancelled)
                release(model.keys[t.key], t.flick)
        }
        order = queue
        touches = all
    }

    Timer {
        id: repeat
        property int pointId: -1
        property int count: 0
        repeat: true
        onTriggered: {
            if (!kb.touches[pointId]) {
                stop()
                return
            }
            kb.erased()
            ++count
            interval = count > 15 ? 35 : 70
        }
    }

    // ---- drawing -------------------------------------------------------

    Rectangle {
        anchors.fill: parent
        color: keyStyle.board

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: keyStyle.edge
        }
    }

    Repeater {
        model: kb.model.keys

        delegate: Item {
            id: cell

            required property var modelData
            required property int index

            readonly property var k: modelData
            readonly property bool down: kb.downKeys[index] === true
            readonly property bool letterLike: k.kind === "char" || k.kind === "space"
            readonly property bool on: (k.kind === "shift" && kb.shiftState > 0) || (k.kind === "caps" && kb.shiftState === 2)
            readonly property bool inert: k.kind === "tab"
            readonly property bool dual: k.s !== undefined
            readonly property string icon: kb.iconFor(k)
            readonly property bool cornerLabel: ["tab", "caps", "shift", "backspace", "return"].indexOf(k.kind) >= 0
            readonly property bool leftSide: k.ux + k.w / 2 < kb.model.units / 2
            readonly property color ink: on ? keyStyle.accentText : inert ? keyStyle.faint : keyStyle.label
            readonly property color fill: on ? kb.accent
                : down && !inert ? (letterLike ? keyStyle.keyDown : keyStyle.specialDown)
                : letterLike ? keyStyle.key : keyStyle.special

            x: kb.padX + k.ux * kb.unitW
            y: kb.padTop + k.row * kb.rowH
            width: k.w * kb.unitW
            height: k.h * kb.rowH

            Rectangle {
                x: face.x
                y: face.y + 1.5
                width: face.width
                height: face.height
                radius: face.radius
                color: keyStyle.shadow
            }
            Rectangle {
                x: foot.x
                y: foot.y + 1.5
                width: foot.width
                height: foot.height
                radius: foot.radius
                color: keyStyle.shadow
                visible: foot.visible
            }

            Rectangle {
                id: face
                readonly property rect cap: kb.capRect(cell.k)
                x: cap.x - cell.x
                y: cap.y - cell.y
                width: cap.width
                height: cap.height
                radius: keyStyle.keyRadius
                color: cell.fill
            }

            // The lower row of an L-shaped return.
            Rectangle {
                id: foot
                readonly property rect cap: cell.k.foot ? kb.footRect(cell.k) : Qt.rect(0, 0, 0, 0)
                visible: !!cell.k.foot
                x: cap.x - cell.x
                y: cap.y - cell.y
                width: cap.width
                height: cap.height
                radius: keyStyle.keyRadius
                color: cell.fill
            }

            Text {
                visible: !cell.dual && cell.icon === ""
                anchors.fill: cell.k.foot ? foot : face
                anchors.leftMargin: cell.cornerLabel ? 10 : 3
                anchors.rightMargin: cell.cornerLabel ? 10 : 3
                anchors.bottomMargin: cell.cornerLabel ? 7 : 0
                text: kb.labelFor(cell.k)
                color: cell.k.kind === "space" ? keyStyle.faint : cell.ink
                font.family: keyStyle.font
                font.pixelSize: Math.max(10, Math.round(face.height * (cell.k.kind === "char" ? 0.4 : 0.27)))
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 9
                horizontalAlignment: !cell.cornerLabel ? Text.AlignHCenter
                                   : cell.leftSide ? Text.AlignLeft : Text.AlignRight
                verticalAlignment: cell.cornerLabel ? Text.AlignBottom : Text.AlignVCenter
            }

            // Two-character keys: the shifted one above, the other below; a
            // flick in progress pulls the upper one to the middle.
            Text {
                visible: cell.dual
                x: face.x
                width: face.width
                y: face.y + face.height * (kb.flickIndex === cell.index ? 0.3 : 0.1)
                text: cell.k.s !== undefined ? cell.k.s : ""
                horizontalAlignment: Text.AlignHCenter
                color: kb.shiftState === 1 || kb.flickIndex === cell.index ? keyStyle.label : keyStyle.faint
                font.family: keyStyle.font
                font.pixelSize: Math.round(face.height * (kb.flickIndex === cell.index ? 0.4 : 0.27))
            }
            Text {
                visible: cell.dual && kb.flickIndex !== cell.index
                x: face.x
                width: face.width
                y: face.y + face.height * 0.48
                text: cell.k.c || ""
                horizontalAlignment: Text.AlignHCenter
                color: kb.shiftState === 1 ? keyStyle.faint : keyStyle.label
                font.family: keyStyle.font
                font.pixelSize: Math.round(face.height * 0.3)
            }

            KeyIcon {
                visible: cell.icon !== ""
                anchors.centerIn: face
                width: Math.round(Math.min(face.width, face.height) * 0.44)
                height: width
                name: cell.icon
                color: cell.ink
            }

            // The caps lock light.
            Rectangle {
                visible: cell.k.kind === "caps"
                x: face.x + 9
                y: face.y + 9
                width: 7
                height: 7
                radius: 3.5
                color: kb.shiftState === 2 ? keyStyle.accentText : keyStyle.faint
            }
        }
    }

    MultiPointTouchArea {
        anchors.fill: parent
        mouseEnabled: true
        maximumTouchPoints: 10
        onPressed: (touchPoints) => { for (const p of touchPoints) kb.begin(p) }
        onUpdated: (touchPoints) => { for (const p of touchPoints) kb.move(p) }
        onReleased: (touchPoints) => { for (const p of touchPoints) kb.end(p, false) }
        onCanceled: (touchPoints) => { for (const p of touchPoints) kb.end(p, true) }
    }
}
