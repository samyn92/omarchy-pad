import QtQuick

// One transparent overlay over the whole screen. At rest it only takes
// touches in thin strips along the right, bottom and top edges. A finger that
// lands there drags the window beside that edge smaller, or the scratchpad
// console down, and the room it makes becomes the launcher. Pictures of the
// windows move under the finger; Hyprland only lays out the real ones once
// the finger has decided. Two fingers pulling a window down (stage reads
// those from the touch screen itself) move a picture of it the same way, and
// let go far enough it slides off and the window closes. Three fingers
// sideways slide the workspace aside for the next one, pictures again.
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
    property string phase: "idle"   // idle, tracking, settling, room, launching, leaving, dismiss
    property real pull: 0
    property real fraction: 1       // the share the shrinking window keeps once settled
    property real veil: 1
    property point start
    property var samples: []
    property var ready: ({})        // snapshot ids that have arrived
    property string launchingIcon: ""
    property bool gone: false       // the window pulled away has closed

    readonly property string edge: plan ? plan.edge : ""
    // A window pulled down by a hand: its picture, over the wallpaper it
    // leaves behind, once the picture is there (until then, the real one).
    readonly property bool closing: edge === "close"
    readonly property bool showing: closing && plan.animated === true && ready[plan.snapshot] === true
    // Three fingers sliding the workspaces: `pull` is how far the next one
    // has come in, once this workspace's pictures are there (the next one's
    // show their app until theirs arrive).
    readonly property bool sliding: edge === "slide"
    readonly property bool slideShowing: sliding && allReady(plan.current)
    readonly property real slideOffset: sliding ? (plan.direction === "left" ? -pull : pull) : 0
    readonly property rect closingOuter: closing ? Qt.rect(plan.x - border, plan.y - border, plan.w + 2 * border, plan.h + 2 * border)
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
        const rects = [Qt.rect(width - strip, 0, strip, height), Qt.rect(0, height - strip, width, strip),
                       Qt.rect(0, 0, width, strip - 2)]
        if (stage.consoleOpen) {
            const c = stage.consoleRect
            rects.push(Qt.rect(0, c.y + c.height, width, height - c.y - c.height))
            rects.push(Qt.rect(0, c.y, c.x, c.height))
            rects.push(Qt.rect(c.x + c.width, c.y, width - c.x - c.width, c.height))
        }
        return rects
    }
    onInputRectsChanged: stage.setInputRegion(inputRects)

    function iconFor(appClass) {
        return apps.icon(appClass) || (appClass || "").toLowerCase()
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
        if (y <= strip * 1.5)
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
        if (phase !== "tracking" || closing)
            return
        const d = distance(p)
        const now = Date.now()
        samples.push({d: d, t: now})
        while (samples.length > 2 && now - samples[0].t > 90)
            samples.shift()
        if (!plan.allowed) {
            pull = rubber(d)
        } else if (splitting) {
            const most = span * 0.75
            pull = d <= most ? Math.max(0, d) : most + rubber(d - most)
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
            // A tap, or a push upward, on the dimmed screen sends the console back.
            if (Math.hypot(p.x - start.x, p.y - start.y) < 14 || start.y - p.y > 40)
                stage.closeConsole()
            return
        }
        if (phase !== "tracking" || closing)
            return
        const v = speed()
        if (!plan.allowed) {
            settle(0, finish)
            return
        }
        if (edge === "top") {
            if (pull > box.height * 0.35 || v > 700) {
                settle(box.height, function () {
                    if (plan.empty) {
                        phase = "room"
                    } else {
                        phase = "leaving"
                        stage.openConsole()
                        leaveTimer.start()
                    }
                })
            } else {
                settle(0, finish)
            }
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
        // Let go somewhere: the nearest of a third, a half and two thirds;
        // a short, quick flick makes even halves.
        const kept = (size - pull) / size
        let choice = [2 / 3, 1 / 2, 1 / 3].reduce((a, b) => Math.abs(b - kept) < Math.abs(a - kept) ? b : a)
        if (v > 900 && pull < size * 0.25)
            choice = 1 / 2
        fraction = choice
        settle(size * (1 - choice), function () { phase = "room" })
    }

    function cancel() {
        settle(0, finish)
    }

    function finish() {
        phase = "idle"
        plan = null
        pull = 0
        fraction = 1
        veil = 1
        launchingIcon = ""
        gone = false
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

    // Far enough, or quick enough, and the window goes: it is asked to close
    // as the picture slides off, and the wallpaper under it gives way to the
    // others once it has. A window that won't close (it asks about unsaved
    // work) shows again.
    function handReleased(at, ms) {
        if (phase !== "tracking" || !closing)
            return
        handMoved(at, ms)
        if (pull < 100 && speed() < 500) {
            settle(0, finish)
            return
        }
        stage.closeWindow(plan.address)
        if (!showing) {
            finish()
            return
        }
        settle(height - closingOuter.y + 40, function () {
            if (gone) {
                finish()
            } else {
                phase = "leaving"
                leaveTimer.start()
            }
        })
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
            easing.type: settler.to > 0 && !win.sliding ? Easing.OutBack : Easing.OutCubic
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
            if (stage.hand && (win.phase === "tracking" || win.phase === "dismiss") && !win.closing)
                win.finish()
        }
        function onPullStarted(plan, at, ms) { win.handPulled(plan, at, ms) }
        function onPullMoved(at, ms) { win.handMoved(at, ms) }
        function onPullEnded(at, ms) { win.handReleased(at, ms) }
        function onPullCanceled() {
            if (win.phase === "tracking" && win.closing)
                win.cancel()
        }
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
            visible: win.edge === "top"
            anchors.fill: parent
            color: "black"
            opacity: 0.6 * win.drop
        }

        Item {
            id: dropped
            visible: win.edge === "top"
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
                            source: "image://icon/" + win.iconFor(parent.parent.modelData.appClass)
                            asynchronous: true
                        }
                    }
                    Image {
                        anchors.fill: parent
                        visible: win.ready[parent.modelData.snapshot] === true
                        source: visible ? "image://snapshot/" + parent.modelData.snapshot : ""
                        cache: false
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

        // Where the window pulled down was: wallpaper, the way the gaps show
        // it, until Hyprland has closed it and moves the others in.
        Item {
            visible: win.showing && !win.gone
            x: win.closingOuter.x
            y: win.closingOuter.y
            width: win.closingOuter.width
            height: win.closingOuter.height
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

        // The window itself, following the fingers down and a little smaller
        // the farther it goes.
        WindowFrame {
            visible: win.showing
            x: win.closingOuter.x
            y: win.closingOuter.y + win.pull
            width: win.closingOuter.width
            height: win.closingOuter.height
            transformOrigin: Item.Top
            scale: 1 - 0.08 * Math.min(1, Math.max(0, win.pull) / Math.max(1, win.closingOuter.height))
            border: win.border
            rounding: win.look && win.look.rounding !== undefined ? win.look.rounding : 0
            colors: win.look ? win.look.active : null
            Image {
                anchors.fill: parent
                source: win.showing ? "image://snapshot/" + win.plan.snapshot : ""
                sourceSize: Qt.size(win.closingOuter.width * Screen.devicePixelRatio, win.closingOuter.height * Screen.devicePixelRatio)
                asynchronous: true
                cache: false
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

        // The app on its way, where it will appear.
        Image {
            readonly property Item host: win.edge === "top" ? dropped : room
            visible: win.phase === "launching" && win.launchingIcon !== ""
            width: 112
            height: 112
            x: host.x + host.width / 2 - width / 2
            y: host.y + host.height / 2 - height / 2
            sourceSize: Qt.size(224, 224)
            source: win.launchingIcon !== "" ? "image://icon/" + win.launchingIcon : ""
            SequentialAnimation on scale {
                running: win.phase === "launching"
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.9; duration: 520; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.9; to: 1; duration: 520; easing.type: Easing.InOutSine }
            }
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
                    source: "image://icon/" + win.iconFor(frame.modelData.appClass)
                    asynchronous: true
                }
                // Asked for only once the capture is in (the provider has
                // nothing before), off the GUI thread, at the size it shows.
                Image {
                    id: picture
                    anchors.fill: parent
                    source: win.ready[frame.modelData.snapshot] === true ? "image://snapshot/" + frame.modelData.snapshot : ""
                    sourceSize: Qt.size(frame.modelData.w * Screen.devicePixelRatio, frame.modelData.h * Screen.devicePixelRatio)
                    asynchronous: true
                    cache: false
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
            source: "image://icon/" + icon
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
}
