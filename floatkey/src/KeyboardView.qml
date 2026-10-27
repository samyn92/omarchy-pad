import QtQuick
import "layouts.js" as Layouts

// The keys of one page and everything a finger can do on them. A single touch
// area hit-tests every finger itself, so fast two-thumb typing lands in the
// order the keys went down, sliding to the right key before lifting works,
// and holding a key brings up its accents, the space bar's trackpad or the
// hide key's placement menu. The mic key is the voice orb: tap to speak, or
// hold it while talking.
Item {
    id: view

    required property QtObject style
    property bool compact: false
    property string layoutCode: "us"
    property Item menu: null

    signal pinched(bool outward)
    signal menuRequested(rect anchor)

    readonly property var layoutData: Layouts.layout(layoutCode)
    property string page: "letters"
    readonly property var model: Layouts.keys(page === "emoji" ? []
                                              : compact ? layoutData.compact[page] : layoutData.full.letters)

    readonly property real padX: compact ? 3 : 6
    readonly property real padTop: compact ? 6 : 8
    readonly property real padBottom: compact ? 2 : 6
    readonly property real unitW: (width - 2 * padX) / model.units
    readonly property real rowH: (height - padTop - padBottom) / model.rows
    readonly property real gapX: Math.round(compact ? Math.max(4, unitW * 0.14) : Math.max(6, unitW * 0.08))
    readonly property real gapY: Math.round(compact ? Math.max(6, rowH * 0.22) : Math.max(6, rowH * 0.13))

    // Shift and the modifiers: 0 off, 1 for the next key, 2 locked.
    property int shiftState: 0
    property int ctrlState: 0
    property int altState: 0
    property int superState: 0
    property int shiftHolds: 0
    property bool trackpad: false
    property var downKeys: ({})
    property int previewIndex: -1
    property int flickIndex: -1

    // Fingers on the keys by touch point id, and the ones whose key still has
    // to be typed, in the order they went down.
    property var touches: ({})
    property var order: []
    property var pinch: null
    property var lastTap: ({})
    property real lastShiftTap: 0
    property real lastSpace: 0
    property string history: ""

    onCompactChanged: reset()
    onLayoutCodeChanged: if (page !== "emoji") goTo("letters")

    function reset() {
        cancelAll()
        touches = ({})
        shiftHolds = 0
        goTo("letters")
    }

    function goTo(next) {
        page = next
        downKeys = ({})
        previewIndex = -1
        flickIndex = -1
    }

    // ---- geometry ------------------------------------------------------

    function keyRect(k) {
        return Qt.rect(padX + k.ux * unitW, padTop + k.row * rowH, k.w * unitW, k.h * rowH)
    }

    // The visible key inside its slot. Stacked halves line up with their
    // neighbours' top and bottom edges and keep half a gap between them.
    function capRect(k) {
        const r = keyRect(k)
        if (k.h >= 1)
            return Qt.rect(r.x + gapX / 2, r.y + gapY / 2, r.width - gapX, r.height - gapY)
        const top = k.row % 1 < 0.001
        return Qt.rect(r.x + gapX / 2, r.y + (top ? gapY / 2 : gapY / 4), r.width - gapX, r.height - 0.75 * gapY)
    }

    // The key under a point, or the nearest one: gaps and the board's edges
    // belong to the closest key, the way they do on an iPad.
    function keyAt(x, y) {
        const keys = model.keys
        let best = -1, bestDistance = Infinity
        for (let i = 0; i < keys.length; ++i) {
            const r = keyRect(keys[i])
            const dx = x < r.x ? r.x - x : x > r.x + r.width ? x - r.x - r.width : 0
            const dy = y < r.y ? r.y - y : y > r.y + r.height ? y - r.y - r.height : 0
            const d = dx * dx + dy * dy
            if (d < bestDistance) {
                best = i
                bestDistance = d
                if (d === 0)
                    break
            }
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
            return flick || shiftState === 1 || shiftHolds > 0 ? k.s : k.c
        return shiftState > 0 && Layouts.isLetter(k.c) ? Layouts.upper(k.c) : k.c
    }

    function heldModifiers() {
        const held = []
        if (ctrlState)
            held.push("ctrl")
        if (altState)
            held.push("alt")
        if (superState)
            held.push("super")
        return held
    }

    function labelFor(k) {
        switch (k.kind) {
        case "char": return charFor(k, false)
        case "space": {
            const held = heldModifiers()
            if (held.length > 0)
                return held.join(" + ") + " +"
            if (controller.layouts.length > 1)
                return Layouts.name(layoutCode)
            return compact ? "space" : ""
        }
        case "return": return "return"
        case "tab": return "tab"
        case "caps": return "caps lock"
        case "shift": return compact ? "" : "shift"
        case "backspace": return compact ? "" : "delete"
        case "ctrl": return "ctrl"
        case "alt": return "alt"
        case "super": return "super"
        case "esc": return "esc"
        case "numbers": return "123"
        case "symbols": return "#+="
        case "letters": return "ABC"
        }
        return ""
    }

    function iconFor(k) {
        switch (k.kind) {
        case "shift": return compact ? (shiftState === 2 ? "caps" : "shift") : ""
        case "backspace": return compact ? "backspace" : ""
        case "globe": return controller.layouts.length > 1 ? "globe" : "emoji"
        case "hide": return "hide"
        case "left": case "right": case "up": case "down": return k.kind
        }
        return ""
    }

    function isOn(k) {
        switch (k.kind) {
        case "shift": return shiftState > 0
        case "ctrl": return ctrlState > 0
        case "alt": return altState > 0
        case "super": return superState > 0
        }
        return false
    }

    function isLocked(k) {
        switch (k.kind) {
        case "ctrl": return ctrlState === 2
        case "alt": return altState === 2
        case "super": return superState === 2
        }
        return false
    }

    // ---- typing --------------------------------------------------------

    function modifierFlags(withShift) {
        return (ctrlState ? 1 : 0) | (altState ? 2 : 0) | (superState ? 4 : 0) | (withShift && shiftState ? 8 : 0)
    }

    // One-shot shift and modifiers are spent by the key they modified; a
    // shift key that is still held stays on until it lifts.
    function spent() {
        if (shiftHolds > 0) {
            for (const id in touches)
                if (touches[id].mode === "shift")
                    touches[id].used = true
        } else if (shiftState === 1) {
            shiftState = 0
        }
        if (ctrlState === 1)
            ctrlState = 0
        if (altState === 1)
            altState = 0
        if (superState === 1)
            superState = 0
    }

    function type(text) {
        const plain = !ctrlState && !altState && !superState
        virtualKeyboard.typeText(text, modifierFlags(false))
        spent()
        history = plain ? (history + text).slice(-3) : ""
        if (plain && controller.autoCapitalize && shiftState === 0 && /[.!?] $/.test(history))
            shiftState = 1
    }

    function press(name) {
        virtualKeyboard.tapKey(name, modifierFlags(true))
        spent()
        history = name === "BackSpace" ? history.slice(0, -1) : ""
    }

    function isWordChar(ch) {
        return Layouts.isLetter(ch) || (ch >= "0" && ch <= "9")
    }

    // Two quick spaces after a word make a full stop, as on an iPad.
    function space() {
        const now = Date.now()
        const plain = !ctrlState && !altState && !superState
        if (plain && controller.doubleSpacePeriod && now - lastSpace < 600 && history.length >= 2
                && history[history.length - 1] === " " && isWordChar(history[history.length - 2])) {
            virtualKeyboard.tapKey("BackSpace", 0)
            history = history.slice(0, -1)
            type(". ")
            lastSpace = 0
        } else {
            type(" ")
            lastSpace = now
        }
        if (page === "numbers" || page === "symbols")
            goTo("letters")
    }

    function globe() {
        if (controller.layoutIndex < controller.layouts.length - 1)
            controller.layoutIndex = controller.layoutIndex + 1
        else
            goTo("emoji")
    }

    function commit(t) {
        const k = t.key
        switch (k.kind) {
        case "char":
            type(t.flick ? k.s : charFor(k, false))
            break
        case "space":
            space()
            break
        case "return":
            press("Return")
            if (controller.autoCapitalize && shiftState === 0)
                shiftState = 1
            if (page === "numbers" || page === "symbols")
                goTo("letters")
            break
        case "tab": press("Tab"); break
        // Escape lets go of a dictation, as Superwhisper's own shortcut does.
        case "esc":
            if (dictation.state === "listening")
                dictation.cancel()
            else
                press("Escape")
            break
        case "mic": dictation.toggle(); break
        case "globe": globe(); break
        case "hide": controller.hide(); break
        }
    }

    // Keys that act when pressed, and again and again while held.
    function act(k) {
        switch (k.kind) {
        case "backspace": press("BackSpace"); break
        case "left": press("Left"); break
        case "right": press("Right"); break
        case "up": press("Up"); break
        case "down": press("Down"); break
        }
    }

    function pressShift() {
        const now = Date.now()
        if (shiftState === 0)
            shiftState = 1
        else if (shiftState === 1)
            shiftState = now - lastShiftTap < 350 ? 2 : 0
        else
            shiftState = 0
        lastShiftTap = now
        ++shiftHolds
    }

    // Tap for the next key, double-tap to lock, like shift.
    function pressModifier(kind) {
        const now = Date.now(), name = kind + "State", state = view[name]
        view[name] = state === 0 ? 1 : state === 1 && now - (lastTap[kind] || 0) < 350 ? 2 : 0
        lastTap[kind] = now
        // A modifier is nearly always for a letter.
        if (page === "symbols" && view[name] > 0)
            goTo("letters")
    }

    // ---- fingers -------------------------------------------------------

    function removeFromOrder(id) {
        const i = order.indexOf(id)
        if (i >= 0)
            order.splice(i, 1)
    }

    // Types every waiting key that went down before (and including) `upTo`,
    // in order, so a quick second thumb never overtakes the first.
    function flush(upTo) {
        while (order.length > 0) {
            const id = order.shift()
            const t = touches[id]
            if (t && t.mode === "tap") {
                t.mode = "done"
                commit(t)
            }
            if (id === upTo)
                return
        }
    }

    function begin(p) {
        if (trackpad || accents.visible)
            return
        const index = keyAt(p.x, p.y)
        if (index < 0)
            return
        const k = model.keys[index]
        const t = {id: p.pointId, index: index, key: k, x0: p.x, y0: p.y, x: p.x, y: p.y,
                   mode: "tap", flick: false, used: false}
        touches[p.pointId] = t
        setDown(index, true)

        const ids = Object.keys(touches)
        pinch = ids.length === 2 ? {a: ids[0], b: ids[1], d0: distance(touches[ids[0]], touches[ids[1]])} : null

        switch (k.kind) {
        case "backspace": case "left": case "right": case "up": case "down":
            flush()
            t.mode = "repeat"
            act(k)
            repeater.begin(p.pointId)
            break
        case "shift":
            flush()
            t.mode = "shift"
            pressShift()
            break
        case "caps":
            flush()
            t.mode = "held"
            shiftState = shiftState === 2 ? 0 : 2
            break
        case "ctrl": case "alt": case "super":
            flush()
            t.mode = "held"
            pressModifier(k.kind)
            break
        case "numbers": case "symbols": case "letters":
            flush()
            t.mode = "page"
            t.from = page
            t.index = -1
            goTo(k.kind)
            break
        default:
            // Characters, space, return, tab, esc, globe, mic and hide act
            // when the finger lifts.
            order.push(p.pointId)
            if (compact && k.kind === "char")
                previewIndex = index
            hold.arm(p.pointId)
        }
    }

    function move(p) {
        const t = touches[p.pointId]
        if (!t)
            return
        t.x = p.x
        t.y = p.y
        if (pinchMoved())
            return
        switch (t.mode) {
        case "tap": slide(t); break
        case "accents": accents.pick(t.x); break
        case "trackpad": steer(t); break
        case "menu":
            if (menu)
                menu.highlightAt(view.mapToItem(menu, t.x, t.y))
            break
        }
    }

    function slide(t) {
        const k = t.key
        if (k.kind === "space" && Math.abs(t.x - t.x0) > Math.max(20, unitW * 0.5)) {
            startTrackpad(t)
            return
        }
        // A downward flick on a two-character key types the upper one.
        if (k.kind === "char" && !compact && k.s !== undefined) {
            const dy = t.y - t.y0
            const flick = dy > rowH * 0.3 && dy > Math.abs(t.x - t.x0)
            if (flick !== t.flick) {
                t.flick = flick
                flickIndex = flick ? t.index : -1
            }
            if (flick)
                return
        }
        const index = keyAt(t.x, t.y)
        if (index === t.index)
            return
        const next = model.keys[index]
        if (k.kind === "char" && next.kind === "char") {
            // The key under the finger when it lifts is the one typed.
            setDown(t.index, false)
            setDown(index, true)
            t.index = index
            t.key = next
            if (compact)
                previewIndex = index
            if (hold.pointId === t.id)
                hold.arm(t.id)
            return
        }
        // Sliding well off any other key lets it go.
        const r = keyRect(k)
        if (t.x < r.x - gapX || t.x > r.x + r.width + gapX || t.y < r.y - gapY || t.y > r.y + r.height + gapY)
            drop(t)
    }

    function drop(t) {
        setDown(t.index, false)
        if (previewIndex === t.index)
            previewIndex = -1
        t.index = -1
        t.mode = "dropped"
        removeFromOrder(t.id)
        if (hold.pointId === t.id)
            hold.stop()
    }

    function end(p, cancelled) {
        const t = touches[p.pointId]
        if (!t)
            return
        t.x = p.x
        t.y = p.y
        if (cancelled && t.mode !== "shift" && t.mode !== "talk")
            t.mode = "dropped"
        switch (t.mode) {
        case "tap":
            flush(t.id)
            break
        case "accents": {
            const choice = accents.current()
            accents.close()
            if (choice)
                type(choice)
            break
        }
        case "trackpad":
            trackpad = false
            break
        case "menu":
            if (menu)
                menu.releaseAt(view.mapToItem(menu, t.x, t.y))
            break
        case "shift":
            shiftHolds = Math.max(0, shiftHolds - 1)
            if (t.used && shiftState === 1)
                shiftState = 0
            break
        case "repeat":
            repeater.end(t.id)
            break
        case "talk":
            dictation.stop()
            break
        case "page":
            slideFromPage(t)
            break
        }
        setDown(t.index, false)
        if (previewIndex === t.index)
            previewIndex = -1
        if (flickIndex === t.index)
            flickIndex = -1
        if (hold.pointId === t.id)
            hold.stop()
        removeFromOrder(t.id)
        delete touches[p.pointId]
        if (pinch && (pinch.a == t.id || pinch.b == t.id))
            pinch = null
    }

    // Press 123, slide to a key and lift: that key is typed and the letters
    // come back.
    function slideFromPage(t) {
        if (Math.hypot(t.x - t.x0, t.y - t.y0) < unitW * 0.6)
            return
        const k = model.keys[keyAt(t.x, t.y)]
        if (k && k.kind === "char") {
            type(charFor(k, false))
            goTo(t.from)
        }
    }

    function held(id) {
        const t = touches[id]
        if (!t || t.mode !== "tap" || t.flick)
            return
        const k = t.key
        if (k.kind === "char") {
            const options = Layouts.alternates(layoutCode, k, shiftState > 0)
            if (options.length === 0)
                return
            removeFromOrder(id)
            t.mode = "accents"
            previewIndex = -1
            accents.open(options, capRect(k), t.x)
        } else if (k.kind === "space") {
            startTrackpad(t)
        } else if (k.kind === "hide") {
            removeFromOrder(id)
            t.mode = "menu"
            setDown(t.index, false)
            menuRequested(capRect(k))
        } else if (k.kind === "mic" && (dictation.state === "idle" || dictation.state === "done")) {
            // Hold to talk, let go to type it.
            removeFromOrder(id)
            t.mode = "talk"
            dictation.start()
        }
    }

    // Holding (or sliding along) the space bar turns the keyboard into a
    // trackpad that moves the text cursor; with shift on it selects.
    function startTrackpad(t) {
        removeFromOrder(t.id)
        if (hold.pointId === t.id)
            hold.stop()
        setDown(t.index, false)
        t.mode = "trackpad"
        t.ax = t.x
        t.ay = t.y
        trackpad = true
        previewIndex = -1
    }

    function steer(t) {
        const stepX = compact ? 10 : 14, stepY = compact ? 20 : 28
        const flags = (shiftState || shiftHolds ? 8 : 0) | modifierFlags(false)
        while (t.x - t.ax >= stepX) { virtualKeyboard.tapKey("Right", flags); t.ax += stepX }
        while (t.ax - t.x >= stepX) { virtualKeyboard.tapKey("Left", flags); t.ax -= stepX }
        while (t.y - t.ay >= stepY) { virtualKeyboard.tapKey("Down", flags); t.ay += stepY }
        while (t.ay - t.y >= stepY) { virtualKeyboard.tapKey("Up", flags); t.ay -= stepY }
    }

    function distance(a, b) {
        return Math.hypot(a.x - b.x, a.y - b.y)
    }

    // Two fingers pinching in on the docked keyboard float it; spreading
    // them on the floating one docks it again.
    function pinchMoved() {
        if (!pinch)
            return false
        const a = touches[pinch.a], b = touches[pinch.b]
        if (!a || !b) {
            pinch = null
            return false
        }
        const d = distance(a, b)
        if (Math.abs(d - pinch.d0) < Math.max(60, pinch.d0 * 0.3))
            return false
        const outward = d > pinch.d0
        pinch = null
        cancelAll()
        pinched(outward)
        return true
    }

    function cancelAll() {
        for (const id in touches) {
            if (touches[id].mode === "shift")
                shiftHolds = Math.max(0, shiftHolds - 1)
            else if (touches[id].mode === "talk")
                dictation.stop()
            touches[id].mode = "dropped"
            touches[id].index = -1
        }
        order = []
        downKeys = ({})
        previewIndex = -1
        flickIndex = -1
        hold.stop()
        repeater.stop()
        trackpad = false
        accents.close()
    }

    Timer {
        id: hold
        property int pointId: -1
        interval: 420
        function arm(id) {
            pointId = id
            restart()
        }
        onTriggered: view.held(pointId)
    }

    Timer {
        id: repeater
        property int pointId: -1
        property int count: 0
        interval: 450
        repeat: true
        function begin(id) {
            pointId = id
            count = 0
            interval = 450
            restart()
        }
        function end(id) {
            if (id === pointId) {
                stop()
                pointId = -1
            }
        }
        onTriggered: {
            const t = view.touches[pointId]
            if (!t || t.mode !== "repeat") {
                stop()
                return
            }
            view.act(t.key)
            ++count
            interval = count > 15 ? 35 : 70
        }
    }

    // ---- drawing -------------------------------------------------------

    Repeater {
        model: view.model.keys

        delegate: Item {
            id: cell

            required property var modelData
            required property int index

            readonly property var k: modelData
            readonly property bool down: view.downKeys[index] === true
            readonly property bool letterLike: k.kind === "char" || k.kind === "space"
            readonly property bool on: view.isOn(k)
            readonly property bool dual: !view.compact && k.s !== undefined
            readonly property string icon: view.iconFor(k)
            // The full keyboard writes its edge keys' names into their outer
            // corner, like a 13" iPad.
            readonly property bool cornerLabel: !view.compact
                && ["tab", "caps", "shift", "backspace", "return"].indexOf(k.kind) >= 0
            readonly property bool leftSide: k.ux + k.w / 2 < view.model.units / 2
            readonly property color ink: on ? view.style.accentText : view.style.label

            x: view.padX + k.ux * view.unitW
            y: view.padTop + k.row * view.rowH
            width: k.w * view.unitW
            height: k.h * view.rowH

            Rectangle {
                x: face.x
                y: face.y + 1.5
                width: face.width
                height: face.height
                radius: face.radius
                color: view.style.shadow
                visible: !view.trackpad
            }

            Rectangle {
                id: face
                readonly property rect cap: view.capRect(cell.k)
                x: cap.x - cell.x
                y: cap.y - cell.y
                width: cap.width
                height: cap.height
                radius: view.style.keyRadius
                opacity: view.trackpad ? 0.4 : 1
                // The compact keys show a balloon instead of darkening.
                color: cell.on ? view.style.accent
                     : cell.down && !(view.compact && cell.k.kind === "char")
                       ? (cell.letterLike ? view.style.keyDown : view.style.specialDown)
                     : cell.letterLike ? view.style.key : view.style.special
            }

            Text {
                visible: !cell.dual && cell.icon === "" && !view.trackpad
                anchors.fill: face
                anchors.leftMargin: cell.cornerLabel ? 10 : 3
                anchors.rightMargin: cell.cornerLabel ? 10 : 3
                anchors.bottomMargin: cell.cornerLabel ? 7 : 0
                text: view.labelFor(cell.k)
                color: cell.k.kind === "space" ? view.style.faint : cell.ink
                font.family: view.style.font
                font.pixelSize: Math.max(10, Math.round(face.height * (cell.k.kind === "char"
                    ? (view.compact ? 0.52 : 0.4) : (view.compact ? 0.34 : 0.27))))
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 9
                horizontalAlignment: !cell.cornerLabel ? Text.AlignHCenter
                                   : cell.leftSide ? Text.AlignLeft : Text.AlignRight
                verticalAlignment: cell.cornerLabel ? Text.AlignBottom : Text.AlignVCenter
            }

            // Two-character keys: the shifted one above, the other below; a
            // flick in progress pulls the upper one to the middle.
            Text {
                visible: cell.dual && !view.trackpad
                x: face.x
                width: face.width
                y: face.y + face.height * (view.flickIndex === cell.index ? 0.3 : 0.1)
                text: cell.k.s !== undefined ? cell.k.s : ""
                horizontalAlignment: Text.AlignHCenter
                color: view.shiftState === 1 || view.shiftHolds > 0 || view.flickIndex === cell.index
                       ? view.style.label : view.style.faint
                font.family: view.style.font
                font.pixelSize: Math.round(face.height * (view.flickIndex === cell.index ? 0.4 : 0.27))
            }
            Text {
                visible: cell.dual && !view.trackpad && view.flickIndex !== cell.index
                x: face.x
                width: face.width
                y: face.y + face.height * 0.48
                text: cell.k.c || ""
                horizontalAlignment: Text.AlignHCenter
                color: view.shiftState === 1 || view.shiftHolds > 0 ? view.style.faint : view.style.label
                font.family: view.style.font
                font.pixelSize: Math.round(face.height * 0.3)
            }

            VoiceOrb {
                visible: cell.k.kind === "mic"
                x: face.x
                y: face.y
                width: face.width
                height: face.height
                radius: face.radius
                style: view.style
                interactive: false
                down: cell.down && dictation.state !== "listening"
                idleColor: face.color
                iconColor: dictation.available ? cell.ink : view.style.faint
                iconSize: Math.min(face.width, face.height) * 0.5
                opacity: view.trackpad ? 0.4 : 1
            }

            KeyIcon {
                visible: cell.icon !== "" && !view.trackpad
                anchors.centerIn: face
                width: Math.round(Math.min(face.width, face.height) * (view.compact ? 0.56 : cell.k.h < 1 ? 0.7 : 0.44))
                height: width
                name: cell.icon
                color: cell.ink
                filled: cell.on && cell.k.kind === "shift"
            }

            // The caps lock light, and a bar under a locked modifier.
            Rectangle {
                visible: cell.k.kind === "caps" && !view.trackpad
                x: face.x + 9
                y: face.y + 9
                width: 7
                height: 7
                radius: 3.5
                color: view.shiftState === 2 ? view.style.accent : view.style.faint
            }
            Rectangle {
                visible: view.isLocked(cell.k)
                anchors.horizontalCenter: face.horizontalCenter
                y: face.y + face.height - 6
                width: face.width * 0.4
                height: 2
                radius: 1
                color: cell.ink
            }
        }
    }

    // The balloon a compact key grows while it is held, iPhone style: a wide
    // head with the letter, on a stem that covers the key itself.
    Item {
        id: balloon
        readonly property var k: view.previewIndex >= 0 && view.previewIndex < view.model.keys.length
                                 ? view.model.keys[view.previewIndex] : null
        readonly property rect cap: k ? view.capRect(k) : Qt.rect(0, 0, 0, 0)
        readonly property real headWidth: Math.round(cap.width * 1.6)
        readonly property real headHeight: Math.round(cap.height * 1.2)
        // How far the head rises clear of the key, so the finger never hides it.
        readonly property real lift: Math.round(cap.height * 0.3)
        visible: view.compact && k !== null && !view.trackpad && !accents.visible
        z: 10
        x: cap.x + cap.width / 2 - headWidth / 2
        y: cap.y - headHeight - lift
        width: headWidth
        height: headHeight + lift + cap.height

        Rectangle {
            y: 1.5
            width: balloon.headWidth
            height: balloon.headHeight
            radius: view.style.keyRadius + 5
            color: view.style.shadow
        }
        Rectangle {
            x: (balloon.headWidth - balloon.cap.width) / 2
            y: balloon.headHeight / 2 + 1.5
            width: balloon.cap.width
            height: balloon.height - balloon.headHeight / 2
            radius: view.style.keyRadius
            color: view.style.shadow
        }
        Rectangle {
            width: balloon.headWidth
            height: balloon.headHeight
            radius: view.style.keyRadius + 5
            color: view.style.key
        }
        Rectangle {
            x: (balloon.headWidth - balloon.cap.width) / 2
            y: balloon.headHeight / 2
            width: balloon.cap.width
            height: balloon.height - balloon.headHeight / 2
            radius: view.style.keyRadius
            color: view.style.key
        }
        Text {
            width: balloon.headWidth
            height: balloon.headHeight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: balloon.k ? view.charFor(balloon.k, false) : ""
            color: view.style.label
            font.family: view.style.font
            font.pixelSize: Math.round(balloon.cap.height * 0.8)
        }
    }

    AccentPopup {
        id: accents
        style: view.style
    }

    EmojiPane {
        anchors.fill: parent
        visible: view.page === "emoji"
        style: view.style
        compact: view.compact
        showGlobe: controller.layouts.length > 1
        onPicked: (emoji) => {
            virtualKeyboard.typeText(emoji, 0)
            controller.noteEmoji(emoji)
        }
        onErase: virtualKeyboard.tapKey("BackSpace", 0)
        onLetters: view.goTo("letters")
        onNextLayout: {
            controller.layoutIndex = 0
            view.goTo("letters")
        }
    }

    MultiPointTouchArea {
        anchors.fill: parent
        enabled: view.page !== "emoji"
        mouseEnabled: true
        maximumTouchPoints: 10
        onPressed: (touchPoints) => { for (const p of touchPoints) view.begin(p) }
        onUpdated: (touchPoints) => { for (const p of touchPoints) view.move(p) }
        onReleased: (touchPoints) => { for (const p of touchPoints) view.end(p, false) }
        onCanceled: (touchPoints) => { for (const p of touchPoints) view.end(p, true) }
    }
}
