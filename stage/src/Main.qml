import QtQuick
import QtQuick.Effects

// One transparent overlay over the whole screen. At rest it only takes
// touches in thin strips along the right, bottom and top edges (the top one
// only without Omarchy Pad's bar, which hands those pulls over itself). A finger that
// lands there drags the window beside that edge smaller, or the scratchpad
// console down, and the room it makes becomes the launcher. Pictures of the
// windows move under the finger; Hyprland only lays out the real ones once
// the finger has decided. Two fingers pulling a window down (stage reads
// those from the touch screen itself) move a picture of it the same way, and
// let go far enough it slides off and the window closes. Three fingers
// sideways slide the workspace aside for the next one, pictures again. Two
// fingers held still on a window lift it: it goes beside another window, or
// onto a workspace from the strip that comes down along the top.
Window {
    id: win

    title: "stage"
    color: "transparent"
    visible: false

    Style { id: style }

    // How far in from the edge a finger has to land to start a gesture.
    readonly property real strip: 10

    // What the gesture acts on (from stage.beginEdge) and how far it has got.
    property var plan: null
    property string phase: "idle"   // idle, holding, tracking, settling, room, launching, leaving, revealing, dismiss
    property real pull: 0
    property real fraction: 1       // the share the shrinking window keeps once settled
    property real veil: 1
    property point start
    property var samples: []
    property var ready: ({})        // snapshot ids that have arrived
    property string launchingIcon: ""
    property bool gone: false       // the window pulled away has closed

    readonly property string edge: plan ? plan.edge : ""
    // A window pulled down by a hand: its picture, once it's there (until
    // then, the real one). The real one is hidden once the picture is drawn
    // in its place, so what is behind it shows.
    readonly property bool closing: edge === "close"
    readonly property bool showing: (closing || grabbing) && ready[plan.snapshot] === true
    // Three fingers sliding the workspaces: `pull` is how far the next one
    // has come in, once this workspace's pictures are there (the next one's
    // show their app until theirs arrive).
    readonly property bool sliding: edge === "slide"
    readonly property bool slideShowing: sliding && allReady(plan.current)
    readonly property real slideOffset: sliding ? (plan.direction === "left" ? -pull : pull) : 0
    readonly property rect closingOuter: closing || grabbing ? Qt.rect(plan.x - border, plan.y - border, plan.w + 2 * border, plan.h + 2 * border)
                                                 : Qt.rect(0, 0, 0, 0)
    readonly property bool sideways: edge === "right"
    readonly property bool splitting: (edge === "right" || edge === "bottom") && plan !== null && !plan.empty
    readonly property bool fresh: (edge === "right" || edge === "bottom") && plan !== null && plan.empty === true
    readonly property rect target: splitting ? Qt.rect(plan.x, plan.y, plan.w, plan.h) : Qt.rect(0, 0, 0, 0)
    readonly property rect usable: plan && plan.usable ? Qt.rect(plan.usable.x, plan.usable.y, plan.usable.w, plan.usable.h)
                                                       : Qt.rect(0, 0, width, height)
    readonly property rect box: plan && plan.console ? Qt.rect(plan.console.x, plan.console.y, plan.console.w, plan.console.h)
                                                     : Qt.rect(0, 0, 0, 0)
    // Hyprland's own measures, so what a gesture draws is what Hyprland
    // will lay out: the border, the gap between windows and around them.
    readonly property var look: stage.look
    readonly property real border: look && look.border !== undefined ? look.border : 2
    readonly property real gapIn: look && look.gapsIn !== undefined ? look.gapsIn : 5
    readonly property real gapOut: look && look.gapsOut !== undefined ? look.gapsOut : 10

    // Dwindle's node for the window being split: the window with its border
    // and half a gap on each side. The finger moves the boundary between
    // the two halves-to-be; each window keeps its border and half a gap
    // from it, exactly as dwindle will once the app is open.
    readonly property real nodeStart: sideways ? target.x - border - gapIn : target.y - border - gapIn
    readonly property real span: (sideways ? target.width : target.height) + 2 * (border + gapIn)
    readonly property real boundary: nodeStart + span - pull
    readonly property rect keptOuter: sideways
        ? Qt.rect(target.x - border, target.y - border, Math.max(0, boundary - gapIn - target.x + border), target.height + 2 * border)
        : Qt.rect(target.x - border, target.y - border, target.width + 2 * border, Math.max(0, boundary - gapIn - target.y + border))
    readonly property rect roomOuter: fresh
        ? Qt.rect(usable.x + gapOut, usable.y + gapOut, usable.width - 2 * gapOut, usable.height - 2 * gapOut)
        : sideways
        ? Qt.rect(boundary + gapIn, target.y - border, Math.max(0, target.x + target.width + border - boundary - gapIn), target.height + 2 * border)
        : Qt.rect(target.x - border, boundary + gapIn, target.width + 2 * border, Math.max(0, target.y + target.height + border - boundary - gapIn))
    // What the window gives up, gap included: wallpaper, and the new window on it.
    readonly property rect given: sideways
        ? Qt.rect(keptOuter.x + keptOuter.width, keptOuter.y, target.x + target.width + border - keptOuter.x - keptOuter.width, keptOuter.height)
        : Qt.rect(keptOuter.x, keptOuter.y + keptOuter.height, keptOuter.width, target.y + target.height + border - keptOuter.y - keptOuter.height)

    // How far the console has come down, 0 to 1.
    readonly property real drop: edge === "top" && box.height > 0 ? Math.min(1, pull / box.height) : 0

    // At rest only the edges (and, while the console is open, the dimmed
    // screen around it) take touches; during a gesture everything does.
    readonly property var inputRects: {
        if (phase !== "idle")
            return [Qt.rect(0, 0, width, height)]
        const rects = [Qt.rect(width - strip, 0, strip, height), Qt.rect(0, height - strip, width, strip)]
        // Omarchy Pad's bar hands over pulls from the top itself, by thirds;
        // a strip of stage's own over it would take some of them first.
        if (!stage.barConnected)
            rects.push(Qt.rect(0, 0, width, strip - 2))
        if (stage.consoleOpen) {
            const c = stage.consoleRect
            rects.push(Qt.rect(0, c.y + c.height, width, height - c.y - c.height))
            rects.push(Qt.rect(0, c.y, c.x, c.height))
            rects.push(Qt.rect(c.x + c.width, c.y, width - c.x - c.width, c.height))
        }
        // A grip along every line between windows, so holding it never reaches
        // the apps (a browser would open its menu first). They stop short of the
        // screen's edges, which have gestures of their own.
        for (const d of stage.dividers) {
            const from = Math.max(d.from, 20), to = Math.min(d.to, (d.vertical ? height : width) - 20)
            if (to > from)
                rects.push(d.vertical ? Qt.rect(d.at - grip / 2, from, grip, to - from)
                                      : Qt.rect(from, d.at - grip / 2, to - from, grip))
        }
        return rects
    }
    onInputRectsChanged: stage.setInputRegion(inputRects)

    function iconFor(appClass) {
        return apps.icon(appClass) || (appClass || "").toLowerCase()
    }

    // An icon by name (or path), from the icon theme in use: the theme is in
    // the URL, so one Omarchy's theme switches to loads anew.
    function iconUrl(name) {
        return name ? "image://icon/" + name + "?" + apps.iconTheme : ""
    }

    function allReady(windows) {
        for (let i = 0; i < windows.length; ++i)
            if (ready[windows[i].snapshot] !== true)
                return false
        return true
    }

    function inside(r, x, y) {
        return x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height
    }

    function edgeAt(x, y) {
        // With the console down, the bottom edge sends it back up: a console
        // whose windows fill the screen leaves no dimmed screen to tap.
        if (stage.consoleOpen && y >= height - strip * 1.5)
            return "dismiss"
        if (x >= width - strip * 1.5)
            return "right"
        if (y >= height - strip * 1.5)
            return "bottom"
        if (stage.consoleOpen && !inside(stage.consoleRect, x, y))
            return "dismiss"
        if (y <= strip * 1.5 && !stage.barConnected)
            return "top"
        return ""
    }

    // ---- the finger ----

    function pressed(p) {
        // A hand's fingers are the hand's, wherever they land.
        if (stage.hand)
            return
        if (phase === "room") {
            // A tap beside the launcher puts things back.
            if (!inside(Qt.rect(room.x, room.y, room.width, room.height), p.x, p.y)
                    && !inside(Qt.rect(dropped.x, dropped.y, dropped.width, dropped.height), p.x, p.y))
                cancel()
            return
        }
        if (phase !== "idle")
            return
        const held = dividerAt(p.x, p.y)
        if (held && edgeAt(p.x, p.y) === "") {
            beginHold(held, p)
            return
        }
        const at = edgeAt(p.x, p.y)
        if (at === "")
            return
        start = Qt.point(p.x, p.y)
        samples = []
        if (at === "dismiss") {
            phase = "dismiss"
            return
        }
        plan = stage.beginEdge(at, p.x, p.y)
        ready = ({})
        pull = 0
        fraction = 1
        veil = 1
        phase = "tracking"
    }

    function distance(p) {
        return edge === "right" ? start.x - p.x : edge === "bottom" ? start.y - p.y : p.y - start.y
    }

    // Past what's allowed the picture still gives a little, the way iOS
    // lets you feel the end of a list.
    function rubber(d) {
        return d <= 0 ? 0 : 36 * (1 - Math.exp(-d / 140))
    }

    function moved(p) {
        // A push up on the dimmed screen or the bottom edge takes the console
        // up with the finger.
        if (phase === "dismiss") {
            if (start.y - p.y > 12)
                beginLift()
            return
        }
        if (phase === "holding") {
            // Moving away before it's grabbed was a scroll or a slip, not a hold.
            if (Math.hypot(p.x - start.x, p.y - start.y) > 10)
                letGo()
            return
        }
        if (phase !== "tracking" || closing)
            return
        const d = dividing ? (dividerVertical ? p.x - start.x : p.y - start.y) : distance(p)
        const now = Date.now()
        samples.push({d: d, t: now})
        while (samples.length > 2 && now - samples[0].t > 90)
            samples.shift()
        if (dividing) {
            pull = dividerPull(d)
            return
        }
        if (!plan.allowed) {
            pull = rubber(d)
        } else if (splitting) {
            const most = span * 0.75
            pull = d <= most ? Math.max(0, d) : most + rubber(d - most)
        } else if (lifting) {
            pull = d <= 0 ? Math.max(0, box.height + d) : box.height + rubber(d)
        } else if (edge === "top") {
            pull = d <= box.height ? Math.max(0, d) : box.height + rubber(d - box.height)
        } else {
            pull = Math.max(0, d)
        }
    }

    // How fast the finger was going as it lifted, in px/s, toward the room.
    function speed() {
        if (samples.length < 2)
            return 0
        const a = samples[0], b = samples[samples.length - 1]
        return (b.d - a.d) / Math.max(1, b.t - a.t) * 1000
    }

    function released(p) {
        if (phase === "dismiss") {
            phase = "idle"
            // A tap on the dimmed screen sends the console back.
            if (Math.hypot(p.x - start.x, p.y - start.y) < 14)
                stage.closeConsole()
            return
        }
        if (phase === "holding") {
            letGo()
            return
        }
        if (phase !== "tracking" || closing)
            return
        const v = speed()
        if (dividing) {
            releaseDivider(v)
            return
        }
        if (!plan.allowed) {
            settle(0, finish)
            return
        }
        if (lifting) {
            // Up a third of the way, or flicked up, and it's gone; less, and it
            // comes back down.
            const up = box.height - pull
            if (up > box.height * 0.3 || v < -700) {
                if (!liftHidden)
                    stage.closeConsole()
                settle(0, finish)
            } else {
                settle(box.height, function () {
                    if (!liftHidden)
                        return finish()
                    phase = "leaving"
                    stage.openConsole()
                    leaveTimer.start()
                })
            }
            return
        }
        if (edge === "top") {
            settleTop(pull > box.height * 0.35 || v > 700)
            return
        }
        if (fresh) {
            if (pull > 48 || v > 500)
                settle(220, function () { phase = "room" })
            else
                settle(0, finish)
            return
        }
        const size = span
        if (pull < 56 && v < 450) {
            settle(0, finish)
            return
        }
        // Let go somewhere: the window keeps the share the finger left it,
        // from a quarter (as far as it goes before it only gives a little) to
        // four fifths (the room an app needs at least); a short, quick flick,
        // which has no place to keep, makes even halves.
        const kept = (size - pull) / size
        let choice = Math.max(0.25, Math.min(0.8, kept))
        if (v > 900 && pull < size * 0.25)
            choice = 1 / 2
        fraction = choice
        settle(size * (1 - choice), function () { phase = "room" })
    }

    function cancel() {
        settle(0, finish)
    }

    // Down it comes (the console opens, or an empty one offers apps), or back up.
    function settleTop(open) {
        if (!open) {
            settle(0, finish)
            return
        }
        settle(box.height, function () {
            if (plan.empty) {
                phase = "room"
            } else {
                phase = "leaving"
                stage.openConsole()
                leaveTimer.start()
            }
        })
    }

    // ---- the console from the bar, and back up ----

    property bool fromTop: false     // the bar is sending this pull
    property bool liftHidden: false  // the real console has gone; its pictures are it now
    readonly property bool lifting: plan !== null && plan.lift === true
    readonly property bool liftReady: lifting && plan.windows.every(w => ready[w.snapshot] === true)
    onLiftReadyChanged: {
        // Pictures in, real console out, in the same frame.
        if (liftReady && !liftHidden) {
            liftHidden = true
            stage.hideConsole()
        }
    }

    function beginLift() {
        plan = stage.beginLift()
        ready = ({})
        samples = []
        veil = 1
        liftHidden = false
        phase = "tracking"
        pull = box.height
    }

    function topPulled(p, at) {
        if (phase !== "idle")
            return
        plan = p
        start = Qt.point(p.fromX, p.fromY)
        ready = ({})
        pull = 0
        fraction = 1
        veil = 1
        samples = []
        fromTop = true
        phase = "tracking"
        moved(at)
    }

    function finish() {
        // Called off (a second finger): the windows go back where they were,
        // as they stay there when it is pictures that moved.
        if (dividing && liveMoved && !liveSettled)
            settleLive(divider.at)
        hideOnNextFrame = false
        if (hidden !== "" && !gone) {
            // The window comes back first, under its picture, and the picture
            // goes once Hyprland has drawn it, so neither is ever missing.
            stage.showWindow(hidden)
            hidden = ""
            phase = "revealing"
            revealTimer.start()
            return
        }
        hidden = ""
        phase = "idle"
        plan = null
        pull = 0
        fraction = 1
        veil = 1
        launchingIcon = ""
        gone = false
        holdProgress = 0
        burst = 0
        ripple = 0
        landing = false
        landTile = -1
        goingBack = false
        closing_up = false
        carriedOpacity = 1
        dropZone = null
        dropTile = -1
        fromTop = false
        liftHidden = false
    }

    // ---- holding the line between two windows ----

    // How long a finger holds a divider before it's grabbed, and how wide the
    // grip along it is: the gap itself is about 14 px, and a fingertip finds
    // it more easily reaching a little into the windows on both sides.
    readonly property int holdMs: 200
    readonly property real grip: 36
    property real holdProgress: 0   // the light gathering while it's held, 0 to 1
    property real burst: 0          // the flash as it's grabbed, up to 1 and back
    property real ripple: 0         // the ring going out from the finger then, 0 to 1
    property real fingerAlong: 0    // where along the line the finger is
    readonly property bool dividing: edge === "divider"
    readonly property var divider: dividing ? plan.divider : null
    readonly property bool dividerVertical: divider ? divider.vertical : true
    readonly property real dividerLine: divider ? divider.at + pull : 0
    // The theme's accent, the colour Omarchy marks what is picked with.
    readonly property color glowColor: theme.accent || (look && look.active && look.active.colors ? look.active.colors[0] : "#7aa2f7")

    function dividerAt(x, y) {
        for (const d of stage.dividers) {
            const across = d.vertical ? x : y, along = d.vertical ? y : x
            if (Math.abs(across - d.at) <= grip / 2 && along >= d.from && along <= d.to)
                return d
        }
        return null
    }

    function beginHold(d, p) {
        liveMoved = false
        liveSettled = false
        plan = stage.beginDivider(d)
        start = Qt.point(p.x, p.y)
        fingerAlong = d.vertical ? p.y : p.x
        pull = 0
        veil = 1
        samples = []
        holdProgress = 0
        burst = 0
        ripple = 0
        phase = "holding"
        holding.restart()
    }

    // Lifted, moved away or joined by another finger before it was grabbed:
    // the light fades and nothing happens.
    function letGo() {
        holding.stop()
        phase = "settling"
        fading.restart()
    }

    function grab() {
        phase = "tracking"
        samples = []
        flash.restart()
    }

    // Resizing live (stage.liveResize), the windows themselves follow the line
    // as it moves, the finger's and the settle's after it.
    readonly property bool liveDividing: dividing && stage.liveResize && (phase === "tracking" || phase === "settling")
    onPullChanged: {
        if (!liveDividing)
            return
        liveMoved = true
        stage.moveDivider(plan, divider.at + pull)
    }
    // Settled live (stage.settleDivider) by a release; one called off another
    // way (a second finger) goes back where it started (see finish).
    property bool liveMoved: false
    property bool liveSettled: false
    function settleLive(line) {
        liveSettled = true
        stage.settleDivider(plan, line)
    }

    // The line follows the finger across the room it splits, and only gives a
    // little past its ends.
    function dividerPull(d) {
        const lo = divider.start, hi = divider.end, at = divider.at + d
        if (at < lo)
            return lo - rubber(lo - at) - divider.at
        if (at > hi)
            return hi + rubber(at - hi) - divider.at
        return d
    }

    // Let go: on the nearest of a third, a half and two thirds (a flick goes
    // on to the next one), or, pushed right to an end with a single window
    // there, that window leaves the split for a workspace of its own, the way
    // iPadOS does.
    function releaseDivider(v) {
        const d = divider, room = d.end - d.start
        const f = (d.at + pull - d.start) / room
        const alone = side => side.length === 1 && !plan.inConsole
        const away = function (windows) {
            if (stage.liveResize)
                settleLive(-1)
            stage.pushOff(windows.map(w => w.address))
            leaveDivider()
        }
        if (f < 0.12 && alone(plan.before)) {
            settle(d.start - d.at, function () { away(plan.before) })
            return
        }
        if (f > 0.88 && alone(plan.after)) {
            settle(d.end - d.at, function () { away(plan.after) })
            return
        }
        const stops = [1 / 3, 1 / 2, 2 / 3]
        let choice = stops.reduce((a, b) => Math.abs(b - f) < Math.abs(a - f) ? b : a)
        if (Math.abs(v) > 900) {
            const ahead = stops.filter(c => v > 0 ? c > f + 0.02 : c < f - 0.02)
            if (ahead.length)
                choice = v > 0 ? ahead[0] : ahead[ahead.length - 1]
        }
        settle(d.start + choice * room - d.at, function () {
            if (stage.liveResize)
                settleLive(d.start + choice * room)
            else
                stage.resizeDivider(d, choice)
            leaveDivider()
        })
    }

    // The pictures stay until Hyprland has the windows where they are.
    function leaveDivider() {
        phase = "leaving"
        leaveTimer.start()
    }

    // A window beside the line, with the line where the finger has it.
    function paneBox(w, before) {
        const half = gapIn + border
        if (dividerVertical)
            return before ? Qt.rect(w.x, w.y, dividerLine - half - w.x, w.h)
                          : Qt.rect(dividerLine + half, w.y, w.x + w.w - dividerLine - half, w.h)
        return before ? Qt.rect(w.x, w.y, w.w, dividerLine - half - w.y)
                      : Qt.rect(w.x, dividerLine + half, w.w, w.y + w.h - dividerLine - half)
    }

    // All the windows on both sides, borders included.
    function dividedRoom() {
        let x0 = width, y0 = height, x1 = 0, y1 = 0
        for (const w of plan.before.concat(plan.after)) {
            x0 = Math.min(x0, w.x - border)
            y0 = Math.min(y0, w.y - border)
            x1 = Math.max(x1, w.x + w.w + border)
            y1 = Math.max(y1, w.y + w.h + border)
        }
        return Qt.rect(x0, y0, x1 - x0, y1 - y0)
    }

    SequentialAnimation {
        id: holding
        NumberAnimation { target: win; property: "holdProgress"; from: 0; to: 1; duration: win.holdMs; easing.type: Easing.InOutSine }
        ScriptAction { script: win.grab() }
    }

    SequentialAnimation {
        id: fading
        NumberAnimation { target: win; property: "holdProgress"; to: 0; duration: 220; easing.type: Easing.OutCubic }
        ScriptAction { script: win.finish() }
    }

    ParallelAnimation {
        id: flash
        SequentialAnimation {
            NumberAnimation { target: win; property: "burst"; from: 0; to: 1; duration: 90; easing.type: Easing.OutCubic }
            NumberAnimation { target: win; property: "burst"; to: 0; duration: 480; easing.type: Easing.OutQuad }
        }
        NumberAnimation { target: win; property: "ripple"; from: 0; to: 1; duration: 520; easing.type: Easing.OutCubic }
    }

    // ---- three fingers sliding the workspaces ----

    function handSlid(p, at, ms) {
        if (phase !== "idle")
            return
        plan = p
        start = Qt.point(p.fromX, p.fromY)
        veil = 1
        pull = 0
        samples = []
        phase = "tracking"
        handSlideMoved(at, ms)
    }

    function handSlideMoved(at, ms) {
        if (phase !== "tracking" || !sliding)
            return
        const d = plan.direction === "left" ? start.x - at.x : at.x - start.x
        samples.push({d: d, t: ms})
        while (samples.length > 2 && ms - samples[0].t > 90)
            samples.shift()
        // Past the last workspace, and back past where it started, it only gives a little.
        if (d < 0)
            pull = -rubber(-d)
        else if (!plan.next)
            pull = rubber(d)
        else
            pull = d <= width ? d : width + rubber(d - width)
    }

    // A quarter of the way, or a flick, and the next workspace takes the
    // screen; Hyprland switches to it once the pictures are where it is.
    function handSlideReleased(at, ms) {
        if (phase !== "tracking" || !sliding)
            return
        handSlideMoved(at, ms)
        if (!plan.next || (pull < width / 4 && speed() < 500)) {
            settle(0, finish)
            return
        }
        settle(width, function () {
            phase = "leaving"
            stage.switchWorkspace(plan.next)
            leaveTimer.start()
        })
    }

    // ---- a hand pulling a window down ----

    // The window hidden under its picture, and whether to hide it once the
    // picture is on screen.
    property string hidden: ""
    property bool hideOnNextFrame: false
    readonly property bool pictureInPlace: showing && closingPicture.status === Image.Ready
    onPictureInPlaceChanged: if (pictureInPlace && hidden === "") hideOnNextFrame = true
    onFrameSwapped: {
        if (!hideOnNextFrame || !pictureInPlace)
            return
        hideOnNextFrame = false
        hidden = plan.address
        stage.hideWindow(hidden)
    }

    Timer {
        id: revealTimer
        interval: 50
        onTriggered: win.finish()
    }

    function handPulled(p, at, ms) {
        if (phase !== "idle")
            return
        plan = p
        start = Qt.point(p.fromX, p.fromY)
        gone = false
        veil = 1
        pull = 0
        samples = []
        phase = "tracking"
        handMoved(at, ms)
    }

    function handMoved(at, ms) {
        if (phase !== "tracking" || !closing)
            return
        const d = at.y - start.y
        samples.push({d: d, t: ms})
        while (samples.length > 2 && ms - samples[0].t > 90)
            samples.shift()
        pull = d >= 0 ? d : -rubber(-d)
    }

    // Far enough, or quick enough, and the window goes: the picture slides
    // off, then the window is asked to close and the others move in. Not
    // sooner: closing makes Hyprland lay the others out again and their apps
    // draw at their new sizes, which costs the slide frames. A window that
    // won't close (it asks about unsaved work) shows again.
    function handReleased(at, ms) {
        if (phase !== "tracking" || !closing)
            return
        handMoved(at, ms)
        if (pull < 100 && speed() < 500) {
            settle(0, finish)
            return
        }
        if (!showing) {
            stage.closeWindow(plan.address)
            finish()
            return
        }
        settle(height - closingOuter.y + 40, function () {
            phase = "leaving"
            stage.closeWindow(plan.address)
            leaveTimer.start()
        })
    }

    // ---- two fingers carrying a window ----

    readonly property bool grabbing: edge === "grab"
    property point finger           // where the hand has the window
    property bool landing: false    // put down: the picture goes to lx, ly, lw, lh
    property real lx: 0
    property real ly: 0
    property real lw: 0
    property real lh: 0
    property real carriedOpacity: 1
    property int landTile: -1       // the strip's tile it was put down on
    // Where it would go: half of a window ({address, side, x, y, w, h}, the
    // half's own window as dwindle will lay it out), or a tile of the strip.
    property var dropZone: null
    property int dropTile: -1
    // How big the picture is while carried: lifted, smaller over half a
    // window so that half shows, and over the strip a thumbnail hanging
    // below the fingers (`hang`), clear of the tiles.
    readonly property bool overStrip: grabbing && !landing && dropTile >= 0
    property real lift: grabbing && !landing ? (overStrip ? Math.min(0.3, 160 / Math.max(1, closingOuter.width))
                                                          : dropZone ? 0.5 : 0.85) : 1
    Behavior on lift { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    property real hang: overStrip ? 1 : 0
    Behavior on hang { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    // The picture, as big as `lift` has it, with the point that was under the
    // fingers staying under them (or hanging below them).
    readonly property rect carried: {
        if (!grabbing)
            return Qt.rect(0, 0, 0, 0)
        if (landing)
            return Qt.rect(lx, ly, lw, lh)
        const o = closingOuter, w = o.width * lift, h = o.height * lift
        const ox = (start.x - o.x) * lift * (1 - hang) + w / 2 * hang
        const oy = (start.y - o.y) * lift * (1 - hang) - 56 * hang
        return Qt.rect(finger.x - ox, finger.y - oy, w, h)
    }

    function handGrabbed(p, at) {
        if (phase !== "idle")
            return
        plan = p
        start = at
        finger = at
        gone = false
        veil = 1
        landing = false
        landTile = -1
        carriedOpacity = 1
        dropZone = null
        dropTile = -1
        phase = "tracking"
    }

    function handCarried(at) {
        if (phase !== "tracking" || !grabbing)
            return
        finger = at
        dropTile = workspaces.tileAt(at.x, at.y)
        dropZone = dropTile >= 0 ? null : zoneAt(at)
    }

    // The half of a window the hand is over: its left or right third gives
    // that side, the middle the top or bottom half, whichever it is in.
    // Over the windows as they show: closed up where the window left, while
    // its workspace is pictures (see `board`).
    function zoneAt(p) {
        for (const o of plan.others) {
            const w = board ? closedUp[o.address] : o
            if (!inside(Qt.rect(w.x, w.y, w.w, w.h), p.x, p.y))
                continue
            const fx = (p.x - w.x) / w.w, fy = (p.y - w.y) / w.h
            const side = fx < 0.3 ? "left" : fx > 0.7 ? "right" : fy < 0.5 ? "top" : "bottom"
            return Object.assign({address: o.address, side: side}, halfOf(w, side))
        }
        return null
    }

    // Dwindle splits a window's node (the window, its border and half a gap
    // all round) in two, and each half's window keeps its own border and gap.
    function halfOf(w, side) {
        const g = gapIn + border
        if (side === "left" || side === "right") {
            const node = w.w + 2 * g
            return {x: side === "left" ? w.x : w.x + node / 2, y: w.y, w: node / 2 - 2 * g, h: w.h}
        }
        const node = w.h + 2 * g
        return {x: w.x, y: side === "top" ? w.y : w.y + node / 2, w: w.w, h: node / 2 - 2 * g}
    }

    // Let go: onto a workspace (the picture goes into its tile), beside a
    // window (into the half it gets), or anywhere else back where it was.
    function handPutDown(at) {
        if (phase !== "tracking" || !grabbing)
            return
        handCarried(at)
        const r = carried
        lx = r.x
        ly = r.y
        lw = r.width
        lh = r.height
        landing = true
        const tile = dropTile >= 0 ? plan.workspaces[dropTile] : null
        if (tile && tile.id !== plan.workspaceId) {
            stage.sendToWorkspace(plan.address, tile.empty ? 0 : tile.id)
            landTile = dropTile
            const box = workspaces.tileBox(dropTile), h = box.height * 0.6, w = h * lw / Math.max(1, lh)
            putDown(Qt.rect(box.x + (box.width - w) / 2, box.y + (box.height - h) / 2, w, h), 0)
            dropZone = null
        } else if (dropZone) {
            stage.placeWindow(plan.address, dropZone.address, dropZone.side)
            putDown(Qt.rect(dropZone.x - border, dropZone.y - border, dropZone.w + 2 * border, dropZone.h + 2 * border), 1)
        } else {
            // The others open its place up again as it goes back.
            goingBack = true
            putDown(closingOuter, 1)
        }
        dropTile = -1
    }

    function putDown(to, fade) {
        phase = "settling"
        landed.to = to
        landed.fade = fade
        landed.restart()
    }

    // The window was laid out (or sent away) as the hand let go, unseen and
    // without animating; it shows again as its picture gets there.
    SequentialAnimation {
        id: landed
        property rect to
        property real fade: 1
        ParallelAnimation {
            // The others' springs settle a little after it lands.
            PauseAnimation { duration: win.board ? 450 : 300 }
            NumberAnimation { target: win; property: "lx"; to: landed.to.x; duration: 300; easing.type: Easing.OutCubic }
            NumberAnimation { target: win; property: "ly"; to: landed.to.y; duration: 300; easing.type: Easing.OutCubic }
            NumberAnimation { target: win; property: "lw"; to: landed.to.width; duration: 300; easing.type: Easing.OutCubic }
            NumberAnimation { target: win; property: "lh"; to: landed.to.height; duration: 300; easing.type: Easing.OutCubic }
            NumberAnimation { target: win; property: "carriedOpacity"; to: landed.fade; duration: 300; easing.type: Easing.InQuad }
        }
        ScriptAction { script: win.finish() }
    }

    // ---- the others making room ----

    // While a window is carried, its workspace is pictures over the
    // wallpaper (once they are all in), so the other windows can move: they
    // close up the place it left, as Hyprland will, a window it comes over
    // moves aside for it, springing, and they open its place again if it goes
    // back. The console's windows stay put, over the dimmed screen.
    readonly property bool board: grabbing && showing && plan.others.length > 0
                                  && !String(plan.workspace).startsWith("special:") && allReady(plan.others)
    property bool goingBack: false
    // Set a frame after the pictures are up, so they move there rather than
    // start there.
    property bool closing_up: false
    onBoardChanged: if (board) closeUpSoon.restart()
    Timer {
        id: closeUpSoon
        interval: 16
        onTriggered: win.closing_up = win.board
    }

    readonly property var closedUp: grabbing ? closeUp(plan.others, plan) : ({})
    // Where each of the others is drawn: {address: {x, y, w, h}}.
    readonly property var arranged: {
        const out = {}
        if (!grabbing)
            return out
        for (const w of plan.others)
            out[w.address] = closing_up && !goingBack ? closedUp[w.address] : {x: w.x, y: w.y, w: w.w, h: w.h}
        if (closing_up && !goingBack && dropZone) {
            const opposite = {left: "right", right: "left", top: "bottom", bottom: "top"}
            out[dropZone.address] = halfOf(closedUp[dropZone.address], opposite[dropZone.side])
        }
        return out
    }

    // The windows once `gone` has left: dwindle gives its place to its
    // sibling, a window or windows filling the side of it they share, which
    // grow across it keeping their proportions. Of the sides that have one,
    // the one with fewest windows is taken (dwindle's last split).
    function closeUp(windows, gone) {
        const g = gapIn + border, slack = 3
        const node = w => ({x: w.x - g, y: w.y - g, w: w.w + 2 * g, h: w.h + 2 * g})
        const near = (a, b) => Math.abs(a - b) <= slack
        const G = node(gone)
        const out = {}
        for (const w of windows)
            out[w.address] = {x: w.x, y: w.y, w: w.w, h: w.h}
        let best = null
        for (const side of ["left", "right", "top", "bottom"]) {
            const across = side === "left" || side === "right"
            // The windows on that side, within its span.
            const band = windows.filter(w => {
                const n = node(w)
                return across ? n.y >= G.y - slack && n.y + n.h <= G.y + G.h + slack
                                && (side === "left" ? n.x + n.w <= G.x + slack : n.x >= G.x + G.w - slack)
                              : n.x >= G.x - slack && n.x + n.w <= G.x + G.w + slack
                                && (side === "top" ? n.y + n.h <= G.y + slack : n.y >= G.y + G.h - slack)
            })
            // The nearest of them that fill a box against the whole side.
            const reach = w => {
                const n = node(w)
                return side === "left" ? G.x - n.x : side === "right" ? n.x + n.w - G.x - G.w
                     : side === "top" ? G.y - n.y : n.y + n.h - G.y - G.h
            }
            const depths = band.map(reach).sort((a, b) => a - b)
            for (const depth of depths) {
                const group = band.filter(w => reach(w) <= depth + slack)
                let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity, area = 0
                for (const w of group) {
                    const n = node(w)
                    x0 = Math.min(x0, n.x)
                    y0 = Math.min(y0, n.y)
                    x1 = Math.max(x1, n.x + n.w)
                    y1 = Math.max(y1, n.y + n.h)
                    area += n.w * n.h
                }
                const box = (x1 - x0) * (y1 - y0)
                const fits = across ? near(y0, G.y) && near(y1, G.y + G.h) && (side === "left" ? near(x1, G.x) : near(x0, G.x + G.w))
                                    : near(x0, G.x) && near(x1, G.x + G.w) && (side === "top" ? near(y1, G.y) : near(y0, G.y + G.h))
                if (fits && Math.abs(area - box) <= box * 0.01) {
                    if (!best || group.length < best.group.length)
                        best = {side: side, group: group, box: {x: x0, y: y0, w: x1 - x0, h: y1 - y0}}
                    break
                }
            }
        }
        if (!best)
            return out
        const b = best.box, across = best.side === "left" || best.side === "right"
        const k = across ? (b.w + G.w) / b.w : (b.h + G.h) / b.h
        const from = best.side === "left" ? b.x : best.side === "right" ? G.x : best.side === "top" ? b.y : G.y
        for (const w of best.group) {
            const n = node(w)
            out[w.address] = across ? {x: from + (n.x - b.x) * k + g, y: w.y, w: n.w * k - 2 * g, h: w.h}
                                    : {x: w.x, y: from + (n.y - b.y) * k + g, w: w.w, h: n.h * k - 2 * g}
        }
        return out
    }

    function launchApp(appId) {
        launchingIcon = apps.icon(appId)
        phase = "launching"
        stage.launch(plan, appId, fraction)
    }

    property var afterSettle: null
    function settle(to, done) {
        phase = "settling"
        afterSettle = done
        settler.to = to
        settler.restart()
    }

    SequentialAnimation {
        id: settler
        property real to: 0
        NumberAnimation {
            target: win
            property: "pull"
            to: settler.to
            duration: 320
            // A little overshoot when it lands somewhere, none on the way back,
            // nor where a workspace lines up with the screen.
            easing.type: (settler.to > 0 || win.dividing) && !win.sliding ? Easing.OutBack : Easing.OutCubic
            easing.overshoot: 0.7
        }
        ScriptAction {
            script: {
                const done = win.afterSettle
                win.afterSettle = null
                if (done)
                    done()
            }
        }
    }

    SequentialAnimation {
        id: fadeOut
        PauseAnimation { duration: 120 }
        NumberAnimation { target: win; property: "veil"; to: 0; duration: 200; easing.type: Easing.OutCubic }
        ScriptAction { script: win.finish() }
    }

    // The console is down once Hyprland says so, or after a moment anyway.
    Timer {
        id: leaveTimer
        interval: 800
        onTriggered: if (win.phase === "leaving") fadeOut.start()
    }

    Connections {
        target: stage
        function onSnapshotReady(id, ok) {
            if (!ok)
                return
            const r = Object.assign({}, win.ready)
            r[id] = true
            win.ready = r
        }
        function onPlaced(ok) {
            if (win.phase === "launching")
                fadeOut.start()
        }
        function onConsoleOpenChanged() {
            if (win.phase === "leaving" && stage.consoleOpen) {
                leaveTimer.stop()
                fadeOut.start()
            }
        }
        // A hand puts down whatever one finger at an edge had started.
        function onHandChanged() {
            if (stage.hand && win.phase === "holding")
                win.letGo()
            else if (stage.hand && (win.phase === "tracking" || win.phase === "dismiss") && !win.closing && !win.grabbing)
                win.finish()
        }
        function onTopPulled(plan, at) { win.topPulled(plan, at) }
        function onTopMoved(at) {
            if (win.fromTop)
                win.moved(at)
        }
        function onTopReleased(at, open) {
            if (!win.fromTop || win.phase !== "tracking")
                return
            win.moved(at)
            win.settleTop(open && win.plan.allowed)
        }
        // Hyprland has laid the windows out where the pictures were.
        function onDividersChanged() {
            if (win.phase === "leaving" && win.dividing) {
                leaveTimer.stop()
                fadeOut.start()
            }
        }
        function onPullStarted(plan, at, ms) { win.handPulled(plan, at, ms) }
        function onPullMoved(at, ms) { win.handMoved(at, ms) }
        function onPullEnded(at, ms) { win.handReleased(at, ms) }
        function onPullCanceled() {
            if (win.phase === "tracking" && win.closing)
                win.cancel()
        }
        function onGrabStarted(plan, at) { win.handGrabbed(plan, at) }
        function onGrabMoved(at) { win.handCarried(at) }
        function onGrabEnded(at) { win.handPutDown(at) }
        function onPinched(direction) { clipToast.show(direction === "in" ? "copy" : "paste") }
        function onCopied() { clipToast.show("copied") }
        function onSwiped(direction) {
            if (win.phase === "idle")
                stage.swipe(direction)
        }
        function onSlideStarted(plan, at, ms) { win.handSlid(plan, at, ms) }
        function onSlideMoved(at, ms) { win.handSlideMoved(at, ms) }
        function onSlideEnded(at, ms) { win.handSlideReleased(at, ms) }
        function onSlideCanceled() {
            if (win.phase === "tracking" && win.sliding)
                win.cancel()
        }
        function onWorkspaceShown(name) {
            if (win.phase === "leaving" && win.sliding) {
                leaveTimer.stop()
                win.finish()
            }
        }
        function onWindowClosed(address) {
            if (!win.closing || address !== win.plan.address)
                return
            win.gone = true
            if (win.phase === "leaving") {
                leaveTimer.stop()
                win.finish()
            }
        }
    }

    // ---- what the gesture shows ----

    Item {
        id: scene
        anchors.fill: parent
        opacity: win.veil
        visible: win.phase !== "idle" && win.phase !== "dismiss"

        // The real window stays where it is; what it gives up is covered by
        // the wallpaper (as the gap will show it) with the new window on it,
        // so it looks cut off where the finger is, border and all.
        Item {
            visible: win.splitting
            x: win.given.x
            y: win.given.y
            width: Math.max(0, win.given.width)
            height: Math.max(0, win.given.height)
            clip: true
            Image {
                x: -parent.x
                y: -parent.y
                width: win.width
                height: win.height
                source: wallpaper.source
                sourceSize: wallpaper.sourceSize
                fillMode: Image.PreserveAspectCrop
            }
        }

        // The border on the edge the finger is pulling, in the window's own
        // colors (blue while it has focus).
        WindowFrame {
            visible: win.splitting
            x: win.sideways ? win.keptOuter.x + win.keptOuter.width - width : win.keptOuter.x
            y: win.sideways ? win.keptOuter.y : win.keptOuter.y + win.keptOuter.height - height
            width: win.sideways ? win.border : win.keptOuter.width
            height: win.sideways ? win.keptOuter.height : win.border
            border: 0
            fill: "transparent"
            colors: !win.look ? null : win.plan && win.plan.active ? win.look.active : win.look.inactive
        }

        // The room the finger made: a new window, launcher inside.
        WindowFrame {
            id: room
            visible: win.splitting || win.fresh
            x: win.roomOuter.x
            y: win.roomOuter.y
            width: win.roomOuter.width
            height: win.roomOuter.height
            border: win.border
            rounding: win.look && win.look.rounding !== undefined ? win.look.rounding : 0
            colors: win.look ? win.look.active : null
            fill: style.background
            opacity: win.fresh ? Math.min(1, win.pull / 160) : Math.min(1, win.pull / 50)
            scale: win.fresh ? 0.96 + 0.04 * Math.min(1, win.pull / 160) : 1

            Launcher {
                anchors.fill: parent
                style: style
                plain: true
                interactive: win.phase === "room"
                opacity: win.phase === "launching" ? 0 : Math.min(1, win.pull / 140)
                Behavior on opacity { NumberAnimation { duration: 150 } }
                onPicked: (appId) => win.launchApp(appId)
            }
        }

        // The console, pulled down from the top over a darkening screen.
        Rectangle {
            visible: win.edge === "top" && (!win.lifting || win.liftHidden)
            anchors.fill: parent
            color: "black"
            opacity: 0.6 * win.drop
        }

        Item {
            id: dropped
            visible: win.edge === "top" && (!win.lifting || win.liftHidden)
            x: win.box.x
            width: win.box.width
            height: win.box.height
            y: win.box.y - (win.box.y + win.box.height) * (1 - win.drop)
               + (win.pull > win.box.height ? win.pull - win.box.height : 0)

            Repeater {
                model: win.edge === "top" && win.plan.windows ? win.plan.windows : []
                delegate: Item {
                    required property var modelData
                    x: modelData.x - win.box.x
                    y: modelData.y - win.box.y
                    width: modelData.w
                    height: modelData.h

                    Rectangle {
                        anchors.fill: parent
                        color: style.panel
                        Image {
                            anchors.centerIn: parent
                            width: 80
                            height: 80
                            sourceSize: Qt.size(160, 160)
                            source: win.iconUrl(win.iconFor(parent.parent.modelData.appClass))
                            asynchronous: true
                        }
                    }
                    Picture {
                        anchors.fill: parent
                        visible: shown
                        shown: win.ready[parent.modelData.snapshot] === true
                        snapshot: parent.modelData.snapshot
                        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                    }
                }
            }

            Launcher {
                anchors.fill: parent
                visible: win.edge === "top" && win.plan.empty === true
                style: style
                interactive: win.phase === "room"
                opacity: win.phase === "launching" ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: 150 } }
                onPicked: (appId) => win.launchApp(appId)
            }
        }

        // The carried window's workspace, as pictures that make room for it.
        Item {
            visible: win.board
            x: win.usable.x
            y: win.usable.y
            width: win.usable.width
            height: win.usable.height
            clip: true
            Image {
                x: -parent.x
                y: -parent.y
                width: win.width
                height: win.height
                source: wallpaper.source
                sourceSize: wallpaper.sourceSize
                fillMode: Image.PreserveAspectCrop
            }
        }
        Repeater {
            model: win.board ? win.plan.others : []
            delegate: WindowFrame {
                id: other
                required property var modelData
                readonly property var to: win.arranged[modelData.address] || modelData
                x: to.x - win.border
                y: to.y - win.border
                width: to.w + 2 * win.border
                height: to.h + 2 * win.border
                // A spring, so a window moving aside overshoots a little and
                // settles, and changes its mind smoothly as the finger does.
                Behavior on x { SpringAnimation { spring: 3.5; damping: 0.3; epsilon: 0.25 } }
                Behavior on y { SpringAnimation { spring: 3.5; damping: 0.3; epsilon: 0.25 } }
                Behavior on width { SpringAnimation { spring: 3.5; damping: 0.3; epsilon: 0.25 } }
                Behavior on height { SpringAnimation { spring: 3.5; damping: 0.3; epsilon: 0.25 } }
                border: win.border
                rounding: win.look && win.look.rounding !== undefined ? win.look.rounding : 0
                colors: win.look ? win.look.inactive : null
                fill: style.panel
                // Its own size, cut off or with room around it as the frame
                // changes, the way Hyprland shows a window being resized.
                Picture {
                    width: other.modelData.w
                    height: other.modelData.h
                    shown: win.ready[other.modelData.snapshot] === true
                    snapshot: other.modelData.snapshot
                    sourceSize: Qt.size(other.modelData.w * Screen.devicePixelRatio, other.modelData.h * Screen.devicePixelRatio)
                }
            }
        }

        // Where a carried window would go: the half of the window under it.
        Rectangle {
            id: zone
            property rect box: Qt.rect(0, 0, 0, 0)
            readonly property var at: win.dropZone
            onAtChanged: if (at) box = Qt.rect(at.x - win.border, at.y - win.border, at.w + 2 * win.border, at.h + 2 * win.border)
            visible: win.grabbing && opacity > 0
            opacity: win.grabbing && win.dropZone && !win.landing ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 140 } }
            x: box.x
            y: box.y
            width: box.width
            height: box.height
            Behavior on x { enabled: zone.opacity > 0; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            Behavior on y { enabled: zone.opacity > 0; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            Behavior on width { enabled: zone.opacity > 0; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            Behavior on height { enabled: zone.opacity > 0; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
            radius: win.look && win.look.rounding ? win.look.rounding + win.border : 0
            color: Qt.rgba(win.glowColor.r, win.glowColor.g, win.glowColor.b, 0.25)
            border.width: 3
            border.color: win.glowColor
        }

        WorkspaceStrip {
            id: workspaces
            objectName: "workspaceStrip"
            style: style
            accent: win.glowColor
            iconSource: appClass => win.iconUrl(win.iconFor(appClass))
            tiles: win.grabbing ? win.plan.workspaces : []
            lit: win.landing ? win.landTile : win.dropTile
            readonly property bool shown: win.grabbing && (!win.landing || win.landTile >= 0) && win.phase !== "revealing"
            x: (win.width - width) / 2
            y: shown ? win.usable.y + 14 : win.usable.y - height - 20
            opacity: shown ? 1 : 0
            visible: opacity > 0
            Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }

        // A window carried casts a shadow, as lifted off the screen.
        RectangularShadow {
            visible: win.grabbing && win.showing
            x: win.carried.x
            y: win.carried.y + 18
            width: win.carried.width
            height: win.carried.height
            radius: win.look && win.look.rounding ? win.look.rounding + win.border : 0
            blur: 48
            color: Qt.rgba(0, 0, 0, 0.45 * win.carriedOpacity * (win.landing ? 0.5 : 1))
        }

        // The window itself, following the fingers down and a little smaller
        // the farther it goes; or carried where they take it.
        WindowFrame {
            visible: win.showing
            // The picture is opaque; a see-through window's shows as the window
            // does, as one: frame and picture together, not each on its own.
            opacity: (win.plan && win.plan.opacity !== undefined ? win.plan.opacity : 1) * (win.grabbing ? win.carriedOpacity : 1)
            layer.enabled: opacity < 1
            x: win.grabbing ? win.carried.x : win.closingOuter.x
            y: win.grabbing ? win.carried.y : win.closingOuter.y + win.pull
            width: win.grabbing ? win.carried.width : win.closingOuter.width
            height: win.grabbing ? win.carried.height : win.closingOuter.height
            transformOrigin: Item.Top
            scale: win.grabbing ? 1 : 1 - 0.08 * Math.min(1, Math.max(0, win.pull) / Math.max(1, win.closingOuter.height))
            border: win.border
            rounding: win.look && win.look.rounding !== undefined ? win.look.rounding : 0
            colors: win.look ? win.look.active : null
            Picture {
                id: closingPicture
                anchors.fill: parent
                shown: win.showing
                snapshot: win.showing ? win.plan.snapshot : -1
                sourceSize: Qt.size(win.closingOuter.width * Screen.devicePixelRatio, win.closingOuter.height * Screen.devicePixelRatio)
            }
        }

        // Sliding workspaces: the wallpaper stays, as it does when Hyprland
        // switches, and the windows of both workspaces move over it.
        Item {
            visible: win.slideShowing
            x: win.usable.x
            y: win.usable.y
            width: win.usable.width
            height: win.usable.height
            clip: true
            Image {
                x: -parent.x
                y: -parent.y
                width: win.width
                height: win.height
                source: wallpaper.source
                sourceSize: wallpaper.sourceSize
                fillMode: Image.PreserveAspectCrop
            }
        }
        Workspace {
            visible: win.slideShowing
            x: win.slideOffset
            windows: win.sliding ? win.plan.current : []
        }
        Workspace {
            visible: win.slideShowing
            x: win.slideOffset + (win.sliding && win.plan.direction === "left" ? win.width : -win.width)
            windows: win.sliding && win.plan.nextWindows ? win.plan.nextWindows : []
        }

        // A divider grabbed: the windows on both sides become frosted pictures
        // over the wallpaper and follow the line (a picture cut or stretched
        // never shows through the frost; the app's icon says which is which).
        // Resizing live, the windows themselves do, and none of this shows.
        Item {
            visible: win.dividing && win.phase !== "holding" && !stage.liveResize
            readonly property rect room: win.dividing ? win.dividedRoom() : Qt.rect(0, 0, 0, 0)
            x: room.x
            y: room.y
            width: room.width
            height: room.height
            clip: true
            Image {
                x: -parent.x
                y: -parent.y
                width: win.width
                height: win.height
                source: wallpaper.source
                sourceSize: wallpaper.sourceSize
                fillMode: Image.PreserveAspectCrop
            }
        }
        Repeater {
            model: win.dividing && win.phase !== "holding" && !stage.liveResize ? win.plan.before : []
            delegate: Pane { before: true }
        }
        Repeater {
            model: win.dividing && win.phase !== "holding" && !stage.liveResize ? win.plan.after : []
            delegate: Pane { before: false }
        }

        // The light: the line grows out from the finger while it's held, a bloom
        // gathers under it and the windows' frames light up; then a flash and a
        // ring as it's grabbed, and the line stays lit while it moves.
        Item {
            id: glow
            visible: win.dividing
            anchors.fill: parent
            readonly property real across: win.dividerLine
            readonly property real along: win.fingerAlong
            readonly property real from: win.divider ? win.divider.from : 0
            readonly property real to: win.divider ? win.divider.to : 0

            // How strongly it shines: there from the touch, building to the grab.
            readonly property real shine: Math.min(1, 0.35 + win.holdProgress)

            Item {
                id: light
                anchors.fill: parent
                visible: false
                // The line, from the finger out along the divider.
                Rectangle {
                    readonly property real reach: 30 + (glow.to - glow.from) * win.holdProgress
                    readonly property real lo: Math.max(glow.from, glow.along - reach)
                    readonly property real hi: Math.min(glow.to, glow.along + reach)
                    x: win.dividerVertical ? glow.across - 4 : lo
                    y: win.dividerVertical ? lo : glow.across - 4
                    width: win.dividerVertical ? 8 : hi - lo
                    height: win.dividerVertical ? hi - lo : 8
                    radius: 4
                    color: win.glowColor
                    // A white-hot middle.
                    Rectangle {
                        anchors.centerIn: parent
                        width: win.dividerVertical ? 3 : parent.width
                        height: win.dividerVertical ? parent.height : 3
                        radius: 1.5
                        color: "white"
                        opacity: 0.85
                    }
                }
                // The bloom under the finger, hot in the middle.
                Rectangle {
                    readonly property real size: 36 + 60 * win.holdProgress + 160 * win.burst
                    x: (win.dividerVertical ? glow.across : glow.along) - size / 2
                    y: (win.dividerVertical ? glow.along : glow.across) - size / 2
                    width: size
                    height: size
                    radius: size / 2
                    color: win.glowColor
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * 0.4
                        height: width
                        radius: width / 2
                        color: "white"
                        opacity: 0.7 + 0.3 * win.burst
                    }
                }
                // Both windows' frames light up, brighter as the hold goes on.
                Repeater {
                    model: win.dividing ? win.plan.before.map(w => ({w: w, before: true}))
                                              .concat(win.plan.after.map(w => ({w: w, before: false}))) : []
                    delegate: Rectangle {
                        required property var modelData
                        readonly property rect box: win.paneBox(modelData.w, modelData.before)
                        x: box.x - win.border - 1
                        y: box.y - win.border - 1
                        width: box.width + 2 * win.border + 2
                        height: box.height + 2 * win.border + 2
                        color: "transparent"
                        border.width: 3 + 3 * win.burst
                        border.color: win.glowColor
                        opacity: 0.3 + 0.7 * Math.max(win.burst, win.holdProgress * win.holdProgress)
                    }
                }
            }
            // The halo: every shape above, haloed in the accent.
            MultiEffect {
                anchors.fill: light
                source: light
                blurMax: 64
                shadowEnabled: true
                shadowColor: win.glowColor
                shadowBlur: 1.0
                shadowScale: 1.04
                shadowOpacity: 1
                shadowHorizontalOffset: 0
                shadowVerticalOffset: 0
                brightness: 0.15 + 0.45 * win.burst
                opacity: glow.shine
            }
            Rectangle {
                readonly property real size: 40 + 340 * win.ripple
                visible: win.ripple > 0 && win.ripple < 1
                x: (win.dividerVertical ? glow.across : glow.along) - size / 2
                y: (win.dividerVertical ? glow.along : glow.across) - size / 2
                width: size
                height: size
                radius: size / 2
                color: "transparent"
                border.width: 3
                border.color: win.glowColor
                opacity: 1 - win.ripple
            }
        }

        // The app on its way, where it will appear.
        Image {
            readonly property Item host: win.edge === "top" ? dropped : room
            visible: win.phase === "launching" && win.launchingIcon !== ""
            width: 112
            height: 112
            x: host.x + host.width / 2 - width / 2
            y: host.y + host.height / 2 - height / 2
            sourceSize: Qt.size(224, 224)
            source: win.iconUrl(win.launchingIcon)
            SequentialAnimation on scale {
                running: win.phase === "launching"
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.9; duration: 520; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.9; to: 1; duration: 520; easing.type: Easing.InOutSine }
            }
        }
    }

    // A window beside a grabbed divider, frosted, with its app's icon.
    component Pane: WindowFrame {
        id: pane
        required property var modelData
        property bool before
        readonly property rect box: win.paneBox(modelData, before)
        x: box.x - win.border
        y: box.y - win.border
        width: Math.max(0, box.width) + 2 * win.border
        height: Math.max(0, box.height) + 2 * win.border
        border: win.border
        rounding: win.look && win.look.rounding !== undefined ? win.look.rounding : 0
        colors: win.look ? win.look.active : null
        fill: style.panel
        Picture {
            id: shot
            anchors.fill: parent
            visible: false
            fillMode: Image.PreserveAspectCrop
            shown: win.ready[pane.modelData.snapshot] === true
            snapshot: pane.modelData.snapshot
            sourceSize: Qt.size(pane.modelData.w * Screen.devicePixelRatio, pane.modelData.h * Screen.devicePixelRatio)
        }
        MultiEffect {
            anchors.fill: shot
            source: shot
            visible: shot.status === Image.Ready
            blurEnabled: true
            blurMax: 40
            blur: 0.75
            brightness: -0.06
        }
        Image {
            anchors.centerIn: parent
            width: 96
            height: 96
            sourceSize: Qt.size(192, 192)
            source: win.iconUrl(win.iconFor(pane.modelData.appClass))
            asynchronous: true
        }
    }

    // A workspace's windows as pictures in their frames, where Hyprland has them.
    component Workspace: Item {
        property var windows: []
        width: win.width
        height: win.height
        Repeater {
            model: parent.windows
            delegate: WindowFrame {
                id: frame
                required property var modelData
                x: modelData.x - win.border
                y: modelData.y - win.border
                width: modelData.w + 2 * win.border
                height: modelData.h + 2 * win.border
                border: win.border
                rounding: win.look && win.look.rounding !== undefined ? win.look.rounding : 0
                colors: !win.look ? null : modelData.active ? win.look.active : win.look.inactive
                fill: style.panel
                Image {
                    anchors.centerIn: parent
                    visible: picture.status !== Image.Ready
                    width: 80
                    height: 80
                    sourceSize: Qt.size(160, 160)
                    source: win.iconUrl(win.iconFor(frame.modelData.appClass))
                    asynchronous: true
                }
                Picture {
                    id: picture
                    anchors.fill: parent
                    shown: win.ready[frame.modelData.snapshot] === true
                    snapshot: frame.modelData.snapshot
                    sourceSize: Qt.size(frame.modelData.w * Screen.devicePixelRatio, frame.modelData.h * Screen.devicePixelRatio)
                }
            }
        }
    }

    // A finger without a hand, for `stage simulate`: drags from one point to
    // another over `ms` and keeps holding, so the screen can be looked at
    // mid-gesture, then lets go or picks an app.
    function simulate(action, x0, y0, x1, y1, ms) {
        if (action === "drag") {
            fakeFinger.from = Qt.point(x0, y0)
            fakeFinger.to = Qt.point(x1, y1)
            fakeFinger.steps = Math.max(1, Math.round(ms / fakeFinger.interval))
            fakeFinger.step = 0
            pressed({x: x0, y: y0})
            fakeFinger.restart()
        } else if (action === "press") {
            // Just a finger landing, to hold a divider for real time.
            fakeFinger.stop()
            fakeFinger.at = Qt.point(x0, y0)
            pressed({x: x0, y: y0})
        } else if (action === "move") {
            fakeFinger.from = fakeFinger.at
            fakeFinger.to = Qt.point(x0, y0)
            fakeFinger.steps = Math.max(1, Math.round((x1 > 0 ? x1 : 300) / fakeFinger.interval))
            fakeFinger.step = 0
            fakeFinger.restart()
        } else if (action === "release") {
            fakeFinger.stop()
            released({x: fakeFinger.at.x, y: fakeFinger.at.y})
        } else if (action === "pick" && phase === "room") {
            launchApp(x0)
        } else if (action === "cancel") {
            fakeFinger.stop()
            cancel()
        }
        return phase
    }

    Timer {
        id: fakeFinger
        property point from
        property point to
        property point at
        property int steps: 1
        property int step: 0
        interval: 16
        repeat: true
        onTriggered: {
            step += 1
            const t = Math.min(1, step / steps)
            at = Qt.point(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t)
            win.moved({x: at.x, y: at.y})
            if (t >= 1)
                stop()
        }
    }

    // The wallpaper as Omarchy shows it, decoded once (WebP is slow), for the
    // gaps a gesture opens up.
    Image {
        id: wallpaper
        visible: false
        source: theme.wallpaper
        sourceSize: Qt.size(win.width * Screen.devicePixelRatio, win.height * Screen.devicePixelRatio)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }

    // Every app's icon, loaded once at the size the launcher asks for, so the
    // first gesture doesn't show a launcher of names waiting for pictures.
    Repeater {
        model: apps
        delegate: Image {
            required property string icon
            visible: false
            sourceSize: Qt.size(128, 128)
            source: win.iconUrl(icon)
            asynchronous: true
        }
    }

    MultiPointTouchArea {
        anchors.fill: parent
        z: -1
        maximumTouchPoints: 1
        mouseEnabled: true
        onPressed: (touchPoints) => win.pressed(touchPoints[0])
        onUpdated: (touchPoints) => win.moved(touchPoints[0])
        onReleased: (touchPoints) => win.released(touchPoints[0])
        onCanceled: (touchPoints) => { if (win.phase === "tracking" && !win.closing) win.cancel() }
    }

    // What the clipboard just did, under the bar.
    ClipToast {
        id: clipToast
        style: style
        z: 1000
        anchors.horizontalCenter: parent.horizontalCenter
        y: 48
    }
}
